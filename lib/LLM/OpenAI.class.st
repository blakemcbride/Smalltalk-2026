"
OpenAI's chat completions and embeddings APIs.

    gpt := OpenAI apiKey: (Smalltalk environmentAt: 'OPENAI_API_KEY') model: 'gpt-4o'.
    gpt send: 'Why is the sky blue?'
    gpt send: 'Write a limerick' do: [:piece | Transcript show: piece]
    (OpenAI apiKey: key model: 'text-embedding-3-small') embed: 'a sentence'
    gpt models

Kiss's org.kissweb.llm.OpenAI on the LLM class's shape: send, send with an
image, stream, embeddings, reasoning effort for the models that take it,
image detail.  The wire is the chat completions endpoint: `Authorization:
Bearer', the system prompt as the first message, the reply's text at
choices[0].message.content, its tool calls under tool_calls with their
arguments as a JSON *string*, which is parsed here, answered by messages
of role `tool'; a streamed reply is server-sent events whose
choices[0].delta.content is the text and whose last line is `data:
[DONE]'.

reasoningEffort: -- low, medium, high -- is sent as reasoning_effort when
set, for the o-series and later models that reason; Kiss decides this by a
flag on the constructor, and here it is the setting itself.  imageDetail:
is `low', `high' or `auto' for a picture.  maxTokens: is sent as
max_completion_tokens, and not unless set.

Not here: the Responses endpoint, which serves some models the chat
completions endpoint does not and which Kiss falls back to on a 404
naming it; such a model answers here with that 404 and its sentence.
"
Class {
	#name : 'OpenAI',
	#superclass : 'LLM',
	#instVars : [ 'reasoningEffort', 'imageDetail' ],
	#category : 'LLM'
}

{ #category : 'defaults' }
OpenAI class >> defaultUrl [
	^'https://api.openai.com'
]

{ #category : 'configuration' }
OpenAI >> reasoningEffort: aString [
	"low, medium or high, for a model that reasons; nil sends nothing."
	reasoningEffort := aString
]

{ #category : 'configuration' }
OpenAI >> imageDetail: aString [
	"low, high or auto, for a picture sent with a prompt; nil sends nothing."
	imageDetail := aString
]

{ #category : 'accessing' }
OpenAI >> reasoningEffort [
	^reasoningEffort
]

{ #category : 'accessing' }
OpenAI >> imageDetail [
	^imageDetail
]

{ #category : 'asking' }
OpenAI >> models [
	^((self getJson: self modelsUrl) arrayAt: 'data') asArray collect: [:each | each stringAt: 'id']
]

{ #category : 'asking' }
OpenAI >> embed: aString [
	| body reply |
	self checkConfigured.
	body := JSONObject new at: 'input' put: aString; at: 'model' put: model; yourself.
	reply := self postJson: body to: self embeddingsUrl.
	lastResponse := reply.
	^self floatsOf: (((reply arrayAt: 'data') objectAt: 1) arrayAt: 'embedding')
]

{ #category : 'the wire' }
OpenAI >> chatUrl [
	^url, '/v1/chat/completions'
]

{ #category : 'the wire' }
OpenAI >> embeddingsUrl [
	^url, '/v1/embeddings'
]

{ #category : 'the wire' }
OpenAI >> modelsUrl [
	^url, '/v1/models'
]

{ #category : 'the wire' }
OpenAI >> headers [
	(apiKey isNil or: [apiKey isEmpty]) ifTrue: [
		^LLMError new signal: 'no API key: ', self class name, ' apiKey: (Smalltalk environmentAt: ''OPENAI_API_KEY'') model: ...'].
	^Array with: 'Authorization' -> ('Bearer ', apiKey)
]

{ #category : 'the wire' }
OpenAI >> userMessage: aString image: aPathOrNil [
	| content image |
	content := JSONArray new.
	content add: (JSONObject new at: 'type' put: 'text'; at: 'text' put: aString; yourself).
	aPathOrNil notNil ifTrue: [
		image := JSONObject new
			at: 'url' put: 'data:', (self imageMediaType: aPathOrNil), ';base64,', (self imageBase64: aPathOrNil);
			yourself.
		imageDetail notNil ifTrue: [image at: 'detail' put: imageDetail].
		content add: (JSONObject new at: 'type' put: 'image_url'; at: 'image_url' put: image; yourself)].
	^JSONObject new at: 'role' put: 'user'; at: 'content' put: content; yourself
]

{ #category : 'the wire' }
OpenAI >> requestBodyFor: messages tools: toolsOrNil stream: aBoolean [
	| body all |
	body := JSONObject new.
	body at: 'model' put: model.
	all := JSONArray new.
	systemPrompt notNil ifTrue: [
		all add: (JSONObject new at: 'role' put: 'system'; at: 'content' put: systemPrompt; yourself)].
	all addAll: messages.
	body at: 'messages' put: all.
	reasoningEffort notNil ifTrue: [body at: 'reasoning_effort' put: reasoningEffort].
	temperature notNil ifTrue: [body at: 'temperature' put: temperature].
	topP notNil ifTrue: [body at: 'top_p' put: topP].
	maxTokens notNil ifTrue: [body at: 'max_completion_tokens' put: maxTokens].
	(toolsOrNil notNil and: [toolsOrNil notEmpty]) ifTrue: [
		body at: 'tools' put: (JSONArray withAll: (toolsOrNil collect: [:each | self toolWireFor: each]))].
	aBoolean ifTrue: [body at: 'stream' put: true].
	^body
]

{ #category : 'the wire' }
OpenAI >> toolWireFor: anLLMTool [
	| function |
	function := JSONObject new
		at: 'name' put: anLLMTool name;
		at: 'description' put: anLLMTool description;
		at: 'parameters' put: anLLMTool parameters;
		yourself.
	^JSONObject new at: 'type' put: 'function'; at: 'function' put: function; yourself
]

{ #category : 'the wire' }
OpenAI >> messageOf: aReply [
	^((aReply arrayAt: 'choices') objectAt: 1) objectAt: 'message'
]

{ #category : 'the wire' }
OpenAI >> textOf: aReply [
	^(self string: 'content' of: (self messageOf: aReply)) ifNil: ['']
]

{ #category : 'the wire' }
OpenAI >> toolCallsOf: aReply [
	| calls list function text |
	calls := OrderedCollection new.
	list := (self messageOf: aReply) at: 'tool_calls' ifAbsent: [nil].
	list isNil ifTrue: [^calls].
	list do: [:each |
		function := each objectAt: 'function'.
		text := (self string: 'arguments' of: function) ifNil: [''].
		calls add: (LLMToolCall id: (each stringAt: 'id') name: (function stringAt: 'name')
			arguments: (text trimmed isEmpty ifTrue: [JSONObject new] ifFalse: [JSONObject fromString: text]))].
	^calls
]

{ #category : 'the wire' }
OpenAI >> assistantMessageOf: aReply [
	^self messageOf: aReply
]

{ #category : 'the wire' }
OpenAI >> toolResultMessagesFor: calls results: strings [
	| out |
	out := OrderedCollection new.
	calls doWithIndex: [:call :i |
		out add: (JSONObject new
			at: 'role' put: 'tool';
			at: 'tool_call_id' put: call id;
			at: 'content' put: (strings at: i);
			yourself)].
	^out
]

{ #category : 'the wire' }
OpenAI >> stopReasonOf: aReply [
	^self string: 'finish_reason' of: ((aReply arrayAt: 'choices') objectAt: 1)
]

{ #category : 'the wire' }
OpenAI >> streamedTextOf: aLine [
	"`data: {chunk}' lines and a last `data: [DONE]'; a comment line --
	 OpenRouter sends `: OPENROUTER PROCESSING' to keep the connection
	 warm -- carries nothing.  A chunk with no choices is the trailing
	 usage; one with `error' is an error."
	| payload chunk choices delta |
	(aLine startsWith: 'data: ') ifFalse: [^''].
	payload := (aLine copyFrom: 7 to: aLine size) trimmed.
	payload = '[DONE]' ifTrue: [^nil].
	(payload startsWith: '{') ifFalse: [^''].
	chunk := JSONObject fromString: payload.
	(chunk includesKey: 'error') ifTrue: [
		^(LLMError new) body: payload; signal: self class name, ': ', (chunk at: 'error') asJsonString].
	choices := chunk at: 'choices' ifAbsent: [nil].
	(choices isNil or: [choices isEmpty]) ifTrue: [^''].
	delta := (choices objectAt: 1) at: 'delta' ifAbsent: [nil].
	delta isNil ifTrue: [^''].
	lastStopReason := (self string: 'finish_reason' of: (choices objectAt: 1)) ifNil: [lastStopReason].
	^(self string: 'content' of: delta) ifNil: ['']
]
