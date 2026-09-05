"
A local Ollama server: models on this machine, asked as any LLM here is
asked, with no key.

    ollama := Ollama new.                           http://localhost:11434
    ollama isUp                                     true or false, never an error
    ollama models                                   #('llama3.2:latest' ...)
    ollama model: 'llama3.2:latest'; send: 'Why is the sky blue?'
    ollama send: 'Tell me a story' do: [:piece | Transcript show: piece]
    (ollama model: 'nomic-embed-text'; yourself) embed: 'a sentence'
    Ollama toHtml: aReply                           what the demo's screen shows

Kiss's org.kissweb.llm.Ollama -- isOllamaUp, getAvailableModels, send,
chat with a history, streaming, tools, embeddings, toHtml -- on the LLM
class's shape.  Every question goes to /api/chat with a messages array,
where Kiss's send went to /api/generate with a bare prompt: the chat
endpoint applies the model's own template to the roles, which is what
Kiss's own comment on its chat method recommends, and it is the endpoint
that takes tools.  A streamed reply is one JSON document a line, each with
a piece of the text and the last with `done: true'; Ollama does not speak
server-sent events.  The tools are OpenAI's shape and the tool calls come
back with their arguments already as JSON, not as a string.

temperature:, topP: and maxTokens: go under `options', as num_predict
for the last.  A model can take minutes on a slow machine; the wait is
timeoutSeconds (five minutes) and runs out as a TimedOut.

toHtml: is Kiss's four substitutions, in order: `\boxed{x}' becomes x,
every backslash goes, a newline becomes <br>, and **bold** becomes <b>.
It does not escape HTML, because Kiss's does not and the screen sets the
result as HTML; a reply that contains markup is shown as markup.
"
Class {
	#name : 'Ollama',
	#superclass : 'LLM',
	#category : 'LLM'
}

{ #category : 'defaults' }
Ollama class >> defaultUrl [
	^'http://localhost:11434'
]

{ #category : 'configuration' }
Ollama >> url: aString [
	"The server, as `http://host:port'; a trailing slash or /api is dropped."
	| s |
	super url: aString.
	s := url.
	(s size > 4 and: [(s copyFrom: s size - 3 to: s size) = '/api'])
		ifTrue: [s := s copyFrom: 1 to: s size - 4].
	url := s
]

{ #category : 'asking' }
Ollama >> isUp [
	"Whether the server answers at all.  Never an error: not there is false."
	^[(self client get: url, '/api/version') isSuccess] on: Error do: [:e | false]
]

{ #category : 'asking' }
Ollama >> models [
	"The names of the models the server has, as an Array of Strings."
	^((self getJson: url, '/api/tags') arrayAt: 'models') asArray collect: [:each | each stringAt: 'model']
]

{ #category : 'asking' }
Ollama >> embed: aString [
	| body reply |
	self checkConfigured.
	body := JSONObject new at: 'model' put: model; at: 'input' put: aString; yourself.
	reply := self postJson: body to: url, '/api/embed'.
	lastResponse := reply.
	^self floatsOf: (self embeddingIn: reply)
]

{ #category : 'private' }
Ollama >> embeddingIn: aReply [
	"embeddings[0], or an LLMError naming what is missing rather than a
	 MessageNotUnderstood.  See LLM>>malformed:because:."
	| list |
	list := aReply arrayAt: 'embeddings' ifAbsent: [nil].
	(list isNil or: [list isEmpty])
		ifTrue: [^self malformed: aReply because: 'it has no embeddings'].
	^list arrayAt: 1
]

{ #category : 'the wire' }
Ollama >> chatUrl [
	^url, '/api/chat'
]

{ #category : 'the wire' }
Ollama >> userMessage: aString image: aPathOrNil [
	| message |
	message := JSONObject new at: 'role' put: 'user'; at: 'content' put: aString; yourself.
	aPathOrNil notNil ifTrue: [
		message at: 'images' put: (JSONArray with: (self imageBase64: aPathOrNil))].
	^message
]

{ #category : 'the wire' }
Ollama >> requestBodyFor: messages tools: toolsOrNil stream: aBoolean [
	| body all options |
	body := JSONObject new.
	body at: 'model' put: model.
	all := JSONArray new.
	systemPrompt notNil ifTrue: [
		all add: (JSONObject new at: 'role' put: 'system'; at: 'content' put: systemPrompt; yourself)].
	all addAll: messages.
	body at: 'messages' put: all; at: 'stream' put: aBoolean.
	options := JSONObject new.
	temperature notNil ifTrue: [options at: 'temperature' put: temperature].
	topP notNil ifTrue: [options at: 'top_p' put: topP].
	maxTokens notNil ifTrue: [options at: 'num_predict' put: maxTokens].
	options notEmpty ifTrue: [body at: 'options' put: options].
	(toolsOrNil notNil and: [toolsOrNil notEmpty]) ifTrue: [
		body at: 'tools' put: (JSONArray withAll: (toolsOrNil collect: [:each | self toolWireFor: each]))].
	^body
]

{ #category : 'the wire' }
Ollama >> toolWireFor: anLLMTool [
	| function |
	function := JSONObject new
		at: 'name' put: anLLMTool name;
		at: 'description' put: anLLMTool description;
		at: 'parameters' put: anLLMTool parameters;
		yourself.
	^JSONObject new at: 'type' put: 'function'; at: 'function' put: function; yourself
]

{ #category : 'the wire' }
Ollama >> textOf: aReply [
	| message |
	message := aReply at: 'message' ifAbsent: [nil].
	message isNil ifTrue: [^''].
	^(self string: 'content' of: message) ifNil: ['']
]

{ #category : 'the wire' }
Ollama >> toolCallsOf: aReply [
	| calls message list function arguments |
	calls := OrderedCollection new.
	message := aReply at: 'message' ifAbsent: [nil].
	message isNil ifTrue: [^calls].
	list := message at: 'tool_calls' ifAbsent: [nil].
	list isNil ifTrue: [^calls].
	list do: [:each |
		function := each objectAt: 'function'.
		arguments := function at: 'arguments' ifAbsent: [JSONObject new].
		arguments isString ifTrue: [arguments := JSONObject fromString: arguments].
		calls add: (LLMToolCall id: (function stringAt: 'name') name: (function stringAt: 'name') arguments: arguments)].
	^calls
]

{ #category : 'the wire' }
Ollama >> assistantMessageOf: aReply [
	^(aReply objectAt: 'message' ifAbsent: [nil])
		ifNil: [self malformed: aReply because: 'it has no message']
]

{ #category : 'the wire' }
Ollama >> toolResultMessagesFor: calls results: strings [
	| out |
	out := OrderedCollection new.
	calls doWithIndex: [:call :i |
		out add: (JSONObject new
			at: 'role' put: 'tool';
			at: 'tool_name' put: call name;
			at: 'content' put: (strings at: i);
			yourself)].
	^out
]

{ #category : 'the wire' }
Ollama >> stopReasonOf: aReply [
	^self string: 'done_reason' of: aReply
]

{ #category : 'the wire' }
Ollama >> streamedTextOf: aLine [
	"One JSON document a line; a blank or a non-JSON keep-alive line is
	 nothing; `error' is an error; the piece is message.content."
	| document |
	(aLine trimmed startsWith: '{') ifFalse: [^''].
	document := JSONObject fromString: aLine.
	(document includesKey: 'error') ifTrue: [
		^(LLMError new) body: aLine; signal: 'Ollama: ', (document at: 'error') printString].
	(document at: 'done' ifAbsent: [false]) == true ifTrue: [
		lastStopReason := self string: 'done_reason' of: document].
	^self textOf: document
]

{ #category : 'text' }
Ollama class >> toHtml: aString [
	"Kiss's Ollama.toHtml: boxed answers unboxed, backslashes gone, newlines
	 as <br>, **bold** as <b>."
	| s |
	aString isNil ifTrue: [^''].
	s := self unbox: aString.
	s := s reject: [:c | c = $\].
	s := s copyReplaceAll: (String with: (Character value: 10)) with: '<br>'.
	^self bold: s
]

{ #category : 'private' }
Ollama class >> find: aSubstring in: aString from: anIndex [
	"The index of aSubstring in aString at or after anIndex, or 0."
	anIndex to: aString size - aSubstring size + 1 do: [:i |
		((aString copyFrom: i to: i + aSubstring size - 1) = aSubstring) ifTrue: [^i]].
	^0
]

{ #category : 'private' }
Ollama class >> unbox: aString [
	"\boxed{x} becomes x, everywhere."
	| s start close |
	s := aString.
	[start := self find: '\boxed{' in: s from: 1.
	 start > 0] whileTrue: [
		close := self find: '}' in: s from: start + 7.
		close = 0 ifTrue: [^s].
		s := (s copyFrom: 1 to: start - 1), (s copyFrom: start + 7 to: close - 1), (s copyFrom: close + 1 to: s size)].
	^s
]

{ #category : 'private' }
Ollama class >> bold: aString [
	"**x** becomes <b>x</b>, pair by pair; an unmatched ** stays."
	| out i open close |
	out := WriteStream on: (String new: aString size + 16).
	i := 1.
	[open := self find: '**' in: aString from: i.
	 open > 0 and: [(close := self find: '**' in: aString from: open + 2) > 0]] whileTrue: [
		out nextPutAll: (aString copyFrom: i to: open - 1);
			nextPutAll: '<b>';
			nextPutAll: (aString copyFrom: open + 2 to: close - 1);
			nextPutAll: '</b>'.
		i := close + 2].
	out nextPutAll: (aString copyFrom: i to: aString size).
	^out contents
]
