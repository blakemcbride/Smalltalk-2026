"
Anthropic's Messages API: Claude, asked as any LLM here is asked.

    claude := Anthropic apiKey: (Smalltalk environmentAt: 'ANTHROPIC_API_KEY') model: 'claude-opus-5'.
    claude send: 'Why is the sky blue?'
    claude send: 'Write a limerick' do: [:piece | Transcript show: piece]
    claude models                                  #('claude-opus-5' ...)

Kiss's org.kissweb.llm.Anthropic -- send, send with an image, stream,
temperature or top-p but not both, max_tokens -- on the LLM class's shape,
which adds the conversation, the tools and the model list.  The wire is the
API's own: `x-api-key' and `anthropic-version: 2023-06-01' on every
request, the system prompt in the request's `system' field, the reply's
text in content blocks of type `text', its tool calls in blocks of type
`tool_use' answered with blocks of type `tool_result', and a streamed reply
as server-sent events whose `content_block_delta' carries the text.

max_tokens is required by the API and is 8192 unless maxTokens: says
otherwise.  effort: -- `low', `medium', `high', `xhigh', `max' -- is how
much thinking the current models do; it goes in output_config, and is not
sent unless set.  fallbacks: names models the service should try when the
first declines a request (stop_reason `refusal'), which some models do:
it sends the beta header the feature wants and an array under `fallbacks'.
A refusal that nobody falls back from is an empty answer with
lastStopReason `refusal'; the reply is in lastResponse, with the reason.
"
Class {
	#name : 'Anthropic',
	#superclass : 'LLM',
	#instVars : [ 'version', 'effort', 'fallbacks' ],
	#category : 'LLM'
}

{ #category : 'defaults' }
Anthropic class >> defaultUrl [
	^'https://api.anthropic.com'
]

{ #category : 'initialization' }
Anthropic >> initialize [
	super initialize.
	version := '2023-06-01'
]

{ #category : 'configuration' }
Anthropic >> version: aString [
	"The anthropic-version header, to pin an API contract."
	version := aString
]

{ #category : 'configuration' }
Anthropic >> effort: aString [
	"How hard the model thinks: low, medium, high, xhigh or max; nil sends nothing."
	effort := aString
]

{ #category : 'configuration' }
Anthropic >> fallbacks: anArrayOfModelNames [
	"Models to fall back to when this one declines; nil sends nothing."
	fallbacks := anArrayOfModelNames
]

{ #category : 'accessing' }
Anthropic >> version [
	^version
]

{ #category : 'accessing' }
Anthropic >> effort [
	^effort
]

{ #category : 'accessing' }
Anthropic >> fallbacks [
	^fallbacks
]

{ #category : 'asking' }
Anthropic >> models [
	^((self getJson: url, '/v1/models') arrayAt: 'data') asArray collect: [:each | each stringAt: 'id']
]

{ #category : 'the wire' }
Anthropic >> chatUrl [
	^url, '/v1/messages'
]

{ #category : 'the wire' }
Anthropic >> headers [
	(apiKey isNil or: [apiKey isEmpty]) ifTrue: [
		^LLMError new signal: 'no API key: Anthropic apiKey: (Smalltalk environmentAt: ''ANTHROPIC_API_KEY'') model: ...'].
	^(fallbacks isNil or: [fallbacks isEmpty])
		ifTrue: [Array with: 'x-api-key' -> apiKey with: 'anthropic-version' -> version]
		ifFalse: [Array with: 'x-api-key' -> apiKey with: 'anthropic-version' -> version
					with: 'anthropic-beta' -> 'server-side-fallback-2026-06-01']
]

{ #category : 'the wire' }
Anthropic >> userMessage: aString image: aPathOrNil [
	| content source |
	content := JSONArray new.
	content add: (JSONObject new at: 'type' put: 'text'; at: 'text' put: aString; yourself).
	aPathOrNil notNil ifTrue: [
		source := JSONObject new
			at: 'type' put: 'base64';
			at: 'media_type' put: (self imageMediaType: aPathOrNil);
			at: 'data' put: (self imageBase64: aPathOrNil);
			yourself.
		content add: (JSONObject new at: 'type' put: 'image'; at: 'source' put: source; yourself)].
	^JSONObject new at: 'role' put: 'user'; at: 'content' put: content; yourself
]

{ #category : 'the wire' }
Anthropic >> requestBodyFor: messages tools: toolsOrNil stream: aBoolean [
	| body |
	body := JSONObject new.
	body at: 'model' put: model;
		at: 'max_tokens' put: (maxTokens ifNil: [8192]);
		at: 'messages' put: (JSONArray withAll: messages).
	systemPrompt notNil ifTrue: [body at: 'system' put: systemPrompt].
	temperature notNil ifTrue: [body at: 'temperature' put: temperature].
	topP notNil ifTrue: [body at: 'top_p' put: topP].
	effort notNil ifTrue: [body at: 'output_config' put: (JSONObject new at: 'effort' put: effort; yourself)].
	(toolsOrNil notNil and: [toolsOrNil notEmpty]) ifTrue: [
		body at: 'tools' put: (JSONArray withAll: (toolsOrNil collect: [:each | self toolWireFor: each]))].
	(fallbacks notNil and: [fallbacks notEmpty]) ifTrue: [
		body at: 'fallbacks' put: (JSONArray withAll: (fallbacks collect: [:each | JSONObject new at: 'model' put: each; yourself]))].
	aBoolean ifTrue: [body at: 'stream' put: true].
	^body
]

{ #category : 'the wire' }
Anthropic >> toolWireFor: anLLMTool [
	^JSONObject new
		at: 'name' put: anLLMTool name;
		at: 'description' put: anLLMTool description;
		at: 'input_schema' put: anLLMTool parameters;
		yourself
]

{ #category : 'the wire' }
Anthropic >> textOf: aReply [
	| out |
	out := WriteStream on: (String new: 256).
	(aReply arrayAt: 'content') do: [:block |
		(self string: 'type' of: block) = 'text' ifTrue: [out nextPutAll: (block stringAt: 'text')]].
	^out contents
]

{ #category : 'the wire' }
Anthropic >> toolCallsOf: aReply [
	| calls |
	calls := OrderedCollection new.
	(aReply arrayAt: 'content') do: [:block |
		(self string: 'type' of: block) = 'tool_use' ifTrue: [
			calls add: (LLMToolCall id: (block stringAt: 'id') name: (block stringAt: 'name') arguments: (block objectAt: 'input'))]].
	^calls
]

{ #category : 'the wire' }
Anthropic >> assistantMessageOf: aReply [
	^JSONObject new at: 'role' put: 'assistant'; at: 'content' put: (aReply arrayAt: 'content'); yourself
]

{ #category : 'the wire' }
Anthropic >> toolResultMessagesFor: calls results: strings [
	"One user message carrying every result, as the API wants them."
	| content |
	content := JSONArray new.
	calls doWithIndex: [:call :i |
		content add: (JSONObject new
			at: 'type' put: 'tool_result';
			at: 'tool_use_id' put: call id;
			at: 'content' put: (strings at: i);
			yourself)].
	^Array with: (JSONObject new at: 'role' put: 'user'; at: 'content' put: content; yourself)
]

{ #category : 'the wire' }
Anthropic >> stopReasonOf: aReply [
	^self string: 'stop_reason' of: aReply
]

{ #category : 'the wire' }
Anthropic >> streamedTextOf: aLine [
	"Server-sent events: `event: name' lines, which say nothing the data
	 does not, and `data: {json}' lines.  The text is in
	 content_block_delta's text_delta; message_delta carries the stop
	 reason; message_stop is the end; error is an error."
	| payload event type delta |
	(aLine startsWith: 'data: ') ifFalse: [^''].
	payload := (aLine copyFrom: 7 to: aLine size) trimmed.
	(payload startsWith: '{') ifFalse: [^''].
	event := JSONObject fromString: payload.
	type := (self string: 'type' of: event) ifNil: [''].
	type = 'error' ifTrue: [
		^(LLMError new) body: payload; signal: 'Anthropic: ', (event objectAt: 'error') asJsonString].
	type = 'content_block_delta' ifTrue: [
		delta := event objectAt: 'delta'.
		^(self string: 'type' of: delta) = 'text_delta'
			ifTrue: [(self string: 'text' of: delta) ifNil: ['']]
			ifFalse: ['']].
	type = 'message_delta' ifTrue: [
		lastStopReason := self string: 'stop_reason' of: (event objectAt: 'delta').
		^''].
	type = 'message_stop' ifTrue: [^nil].
	^''
]
