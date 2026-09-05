"
A language model, asked through its service's HTTP API: one prompt in and
the answer out, the answer as it comes, a conversation that remembers
itself, tools the model may call, and an embedding of a text.

    llm := Anthropic apiKey: (Smalltalk environmentAt: 'ANTHROPIC_API_KEY') model: 'claude-opus-5'.
    llm send: 'What is the capital of France?'
    llm send: 'Tell me a story' do: [:piece | Transcript show: piece]
    llm send: 'What is in this picture?' image: 'chart.png'

    talk := llm conversation.
    talk ask: 'My name is Ada.'.
    talk ask: 'What is my name?'                        ...Ada...

    talk tools: (Array with: (LLMTool name: 'weather' description: 'The weather in a city'
        parameters: aSchema do: [:args | ...])).
    talk ask: 'Is it raining in Paris?'                 the tool is run; the answer says

    (OpenAI apiKey: key model: 'text-embedding-3-small') embed: 'a sentence'   an Array of Floats

This is the shape of Kiss's org.kissweb.llm -- Anthropic, OpenAI, OpenRouter
and Ollama, each a class with send, stream and embeddings where the service
has them -- with what the four have in common lifted here: the
configuration, the HTTP, the tool loop and the conversation.  A subclass
says what its service's wire looks like, which is the whole of what
differs: the endpoint and the headers, the shape of a message and of a
tool, where the text and the tool calls are in a reply, and how a streamed
reply is cut up.

WHAT IS SENT.  Only what was set.  Kiss sends temperature 0.7 and top-p
0.7 unless told otherwise; this sends neither unless asked, so that a model
runs at its own defaults -- and Anthropic refuses a request naming both, so
setting one forgets the other.  maxTokens is sent when set, and always to
Anthropic, which requires it: 8192 unless told.  A system prompt goes where
the service puts it: Anthropic's own field, a first message for the others.

TOOLS.  A tool is an LLMTool: a name, a description, a JSON schema for its
arguments, and a block.  When a reply asks for tools the block runs, its
answer goes back as the tool's result, and the model is asked again, until
it answers with text -- at most twenty rounds, after which it is a fault
and not a conversation.  Tool rounds are not streamed: a reply that turns
out to be a tool call has no text to stream and the request cannot know
that in advance, so a conversation with tools answers whole and hands its
text to the block in one piece.

WHAT RAISES.  A reply that is not 2xx is an LLMError carrying the status
and the body, which is the service's own explanation -- a bad key is 401
with a sentence.  A 2xx reply that is not the shape the service documents
is an LLMError too, carrying the body: it used to be a
MessageNotUnderstood from wherever the subclass reached into it, which
said nothing about the service (Bugs4).  A service that cannot be reached
is a NetError; one that does not answer the connection at all is a
TimedOut after HttpClient's thirty seconds rather than the operating
system's two minutes; one that takes longer than timeoutSeconds -- five
minutes unless told -- is a TimedOut.  The whole of the last reply, when
it was not streamed, is lastResponse, for what a caller wants that this
does not surface: token counts, the model that actually answered.  lastStopReason is why the model
stopped, in the service's word -- `end_turn', `stop', `refusal' -- for
either kind of call.

ONE CALLER PER INSTANCE.  The ivars are the configuration and the last
reply, and two processes asking through one instance would read each
other's lastResponse.  Make one per process, as with a socket; there is no
class-side state to share.
"
Class {
	#name : 'LLM',
	#superclass : 'Object',
	#instVars : [ 'url', 'model', 'apiKey', 'systemPrompt', 'temperature', 'topP', 'maxTokens', 'timeoutSeconds', 'lastStatus', 'lastResponse', 'lastStopReason' ],
	#category : 'LLM'
}

{ #category : 'instance creation' }
LLM class >> model: aString [
	^self new model: aString; yourself
]

{ #category : 'instance creation' }
LLM class >> apiKey: keyString model: modelString [
	^self new apiKey: keyString; model: modelString; yourself
]

{ #category : 'defaults' }
LLM class >> defaultUrl [
	"The service's base URL: what url: overrides, for a proxy or a test."
	^self subclassResponsibility
]

{ #category : 'initialization' }
LLM >> initialize [
	url := self class defaultUrl.
	timeoutSeconds := 300
]

{ #category : 'configuration' }
LLM >> url: aString [
	"The service's base URL -- `https://api.anthropic.com' -- with any
	 trailing slash dropped.  For a proxy, a gateway, or a test."
	| s |
	s := aString.
	[s notEmpty and: [s last = $/]] whileTrue: [s := s copyFrom: 1 to: s size - 1].
	url := s
]

{ #category : 'configuration' }
LLM >> model: aString [
	model := aString
]

{ #category : 'configuration' }
LLM >> apiKey: aString [
	apiKey := aString
]

{ #category : 'configuration' }
LLM >> systemPrompt: aString [
	"What the model is told before the conversation: who it is, how to
	 answer.  nil, the default, sends nothing."
	systemPrompt := aString
]

{ #category : 'configuration' }
LLM >> temperature: aNumber [
	"0 to 1 (OpenAI allows 2): how far the model strays from its likeliest
	 word.  Forgets topP, since Anthropic refuses both at once."
	temperature := aNumber.
	topP := nil
]

{ #category : 'configuration' }
LLM >> topP: aNumber [
	"Nucleus sampling: the smallest set of words whose likelihoods add up
	 to this.  Forgets temperature."
	topP := aNumber.
	temperature := nil
]

{ #category : 'configuration' }
LLM >> maxTokens: anInteger [
	maxTokens := anInteger
]

{ #category : 'configuration' }
LLM >> timeoutSeconds: anInteger [
	"How long any part of a reply may take to arrive.  A model can think
	 for minutes; the default is five."
	timeoutSeconds := anInteger
]

{ #category : 'accessing' }
LLM >> url [
	^url
]

{ #category : 'accessing' }
LLM >> model [
	^model
]

{ #category : 'accessing' }
LLM >> apiKey [
	^apiKey
]

{ #category : 'accessing' }
LLM >> systemPrompt [
	^systemPrompt
]

{ #category : 'accessing' }
LLM >> temperature [
	^temperature
]

{ #category : 'accessing' }
LLM >> topP [
	^topP
]

{ #category : 'accessing' }
LLM >> maxTokens [
	^maxTokens
]

{ #category : 'accessing' }
LLM >> timeoutSeconds [
	^timeoutSeconds
]

{ #category : 'accessing' }
LLM >> lastStatus [
	"The HTTP status of the last call, or nil before any."
	^lastStatus
]

{ #category : 'accessing' }
LLM >> lastResponse [
	"The whole JSON of the last reply that was not streamed, or nil."
	^lastResponse
]

{ #category : 'accessing' }
LLM >> lastStopReason [
	"Why the model stopped, in the service's own word, or nil."
	^lastStopReason
]

{ #category : 'asking' }
LLM >> send: aPrompt [
	"The model's whole answer to one prompt, as a String."
	^self send: aPrompt image: nil
]

{ #category : 'asking' }
LLM >> send: aPrompt image: aPathOrNil [
	"The answer to a prompt and a picture: a PNG, JPEG, GIF or WebP file,
	 sent inline, base64."
	^self conversation ask: aPrompt image: aPathOrNil
]

{ #category : 'asking' }
LLM >> send: aPrompt do: aBlock [
	"The answer as it comes: each piece of text to aBlock as the service
	 sends it, and the whole answered at the end."
	^self conversation ask: aPrompt image: nil do: aBlock
]

{ #category : 'asking' }
LLM >> send: aPrompt image: aPathOrNil do: aBlock [
	^self conversation ask: aPrompt image: aPathOrNil do: aBlock
]

{ #category : 'asking' }
LLM >> conversation [
	"A conversation with this model, which remembers what was said."
	^LLMConversation on: self
]

{ #category : 'asking' }
LLM >> embed: aString [
	"The embedding of a text -- an Array of Floats, the length the model's
	 -- where the service has an embeddings API."
	^LLMError new signal: self class name, ' has no embeddings API'
]

{ #category : 'asking' }
LLM >> models [
	"The names of the models the service offers, as an Array of Strings."
	^LLMError new signal: self class name, ' does not list its models here'
]

{ #category : 'asking' }
LLM >> isUp [
	"Whether the service answers at all.  Never an error: not there, or a
	 refused key, is false."
	^[self models. true] on: Error do: [:e | false]
]

{ #category : 'the wire' }
LLM >> chatUrl [
	"Where a conversation is posted."
	^self subclassResponsibility
]

{ #category : 'the wire' }
LLM >> headers [
	"The headers every request carries, as an Array of Associations."
	^#()
]

{ #category : 'the wire' }
LLM >> userMessage: aString image: aPathOrNil [
	"A user's message in the service's shape, as a JSONObject."
	^self subclassResponsibility
]

{ #category : 'the wire' }
LLM >> assistantMessage: aString [
	"The model's text as a message to send back in the history.  The
	 three services all take {role, content} with a string."
	^JSONObject new at: 'role' put: 'assistant'; at: 'content' put: aString; yourself
]

{ #category : 'the wire' }
LLM >> requestBodyFor: messages tools: toolsOrNil stream: aBoolean [
	"The request, as a JSONObject: the model, the messages, whatever was
	 set, the tools in the service's shape, and whether to stream."
	^self subclassResponsibility
]

{ #category : 'the wire' }
LLM >> textOf: aReply [
	"The model's text in a whole reply."
	^self subclassResponsibility
]

{ #category : 'the wire' }
LLM >> toolCallsOf: aReply [
	"The tools a whole reply asks for, as LLMToolCalls; empty for none."
	^self subclassResponsibility
]

{ #category : 'the wire' }
LLM >> assistantMessageOf: aReply [
	"The reply's own message, to send back verbatim when its tool calls
	 are answered: the service wants to see what it said."
	^self subclassResponsibility
]

{ #category : 'the wire' }
LLM >> toolResultMessagesFor: calls results: strings [
	"The tools' answers as messages, in the service's shape."
	^self subclassResponsibility
]

{ #category : 'the wire' }
LLM >> toolWireFor: anLLMTool [
	"A tool's definition in the service's shape."
	^self subclassResponsibility
]

{ #category : 'the wire' }
LLM >> sseDataOf: aLine [
	"The payload of a server-sent-event `data' line, or nil for a line that
	 is not one.

	 THE SPACE AFTER THE COLON IS OPTIONAL, which is the whole of this
	 method.  Both services' readers tested `startsWith: ''data: ''' and
	 dropped everything else, so `data:{...}' -- which is what the SSE
	 grammar calls a data line and what a service or a proxy that
	 normalises its output may send -- was silently no text at all: the
	 stream ran to its end and answered nothing, with no error anywhere
	 (Bugs4).  The rule in the specification is that the field name runs to
	 the colon and ONE optional space after it is not part of the value.
	 A line with no colon at all is a field with an empty value, and a line
	 beginning with a colon is a comment -- OpenRouter's keep-alive is
	 `: OPENROUTER PROCESSING' -- and neither is data."

	| colon |
	colon := aLine indexOf: $:.
	colon < 2 ifTrue: [^nil].
	(aLine copyFrom: 1 to: colon - 1) = 'data' ifFalse: [^nil].
	^(colon < aLine size and: [(aLine at: colon + 1) = $ ])
		ifTrue: [aLine copyFrom: colon + 2 to: aLine size]
		ifFalse: [aLine copyFrom: colon + 1 to: aLine size]
]

{ #category : 'the wire' }
LLM >> streamedTextOf: aLine [
	"The text one line of a streamed reply carries: '' for a line with
	 none, nil for the line that ends the stream, and an LLMError for a
	 line that reports one."
	^self subclassResponsibility
]

{ #category : 'the wire' }
LLM >> stopReasonOf: aReply [
	"Why the model stopped, from a whole reply, or nil."
	^nil
]

{ #category : 'the loop' }
LLM >> answerFor: messages tools: toolsOrNil do: aBlockOrNil [
	"The model's text answer to the conversation in messages -- an
	 OrderedCollection of messages in the service's shape, which grows by
	 what the model said and, when it asked for tools, by what they
	 answered.  With a block and no tools the answer is streamed to it;
	 with tools it is handed over whole at the end.  See the class
	 comment for why."
	| rounds reply calls results text |
	self checkConfigured.
	(toolsOrNil isNil or: [toolsOrNil isEmpty]) ifTrue: [
		aBlockOrNil notNil ifTrue: [^self streamFor: messages do: aBlockOrNil]].
	rounds := 0.
	[reply := self complete: messages tools: toolsOrNil.
	 calls := self toolCallsOf: reply.
	 calls isEmpty] whileFalse: [
		(rounds := rounds + 1) > 20 ifTrue: [
			^LLMError new signal: 'the model asked for tools twenty times in a row'].
		messages add: (self assistantMessageOf: reply).
		results := calls collect: [:each | (self toolNamed: each name in: toolsOrNil) run: each arguments].
		messages addAll: (self toolResultMessagesFor: calls results: results)].
	text := self textOf: reply.
	messages add: (self assistantMessage: text).
	(aBlockOrNil notNil and: [text notEmpty]) ifTrue: [aBlockOrNil value: text].
	^text
]

{ #category : 'the loop' }
LLM >> checkConfigured [
	(model isNil or: [model isEmpty]) ifTrue: [^LLMError new signal: 'Model not set']
]

{ #category : 'the loop' }
LLM >> toolNamed: aName in: tools [
	"The tool of that name, or one that says there is none: a model that
	 invents a tool is told so, and asked again, rather than failed."
	^tools detect: [:each | each name = aName] ifNone: [LLMTool missing: aName]
]

{ #category : 'the loop' }
LLM >> complete: messages tools: toolsOrNil [
	"One whole reply, kept as lastResponse."
	| reply |
	lastResponse := nil.
	lastStopReason := nil.
	reply := self postJson: (self requestBodyFor: messages tools: toolsOrNil stream: false) to: self chatUrl.
	lastResponse := reply.
	lastStopReason := self stopReasonOf: reply.
	^reply
]

{ #category : 'the loop' }
LLM >> streamFor: messages do: aBlock [
	"The answer as it comes, line by line off the wire, each piece of
	 text to aBlock; the whole is answered and added to messages."
	| out piece |
	lastResponse := nil.
	lastStopReason := nil.
	out := WriteStream on: (String new: 256).
	self postJson: (self requestBodyFor: messages tools: nil stream: true) to: self chatUrl
		linesDo: [:line |
			piece := self streamedTextOf: line.
			(piece notNil and: [piece notEmpty]) ifTrue: [
				out nextPutAll: piece.
				aBlock value: piece]].
	messages add: (self assistantMessage: out contents).
	^out contents
]

{ #category : 'http' }
LLM >> client [
	| client |
	client := HttpClient new readTimeout: timeoutSeconds * 1000; yourself.
	self headers do: [:pair | client headerAt: pair key put: pair value].
	^client
]

{ #category : 'http' }
LLM >> postJson: aJSONObject to: aUrl [
	"The reply's JSON, or an LLMError with the service's own words."
	^self checked: (self client post: aUrl body: aJSONObject asJsonString contentType: 'application/json')
]

{ #category : 'http' }
LLM >> getJson: aUrl [
	^self checked: (self client get: aUrl)
]

{ #category : 'http' }
LLM >> checked: aReply [
	lastStatus := aReply status.
	aReply isSuccess ifFalse: [^self failed: aReply status body: aReply body].
	^aReply json
]

{ #category : 'http' }
LLM >> failed: statusInteger body: aString [
	^(LLMError new) status: statusInteger; body: aString;
		signal: self class name, ' answered ', statusInteger printString, ': ', aString
]

{ #category : 'http' }
LLM >> malformed: aReply because: aReason [
	"A reply this service answered 2xx with, that is not the shape its API
	 documents.

	 EVERY FAILURE HERE IS AN LLMError, and a 200 was the one that was not.
	 The subclasses read a reply by reaching into it -- content, choices,
	 message -- and a document missing one of those, or carrying a null
	 where a value belongs, reached the send after it: Bugs4 found `MNU:
	 do: (sent to nil)' from a reply with no `content', which tells the
	 caller nothing about the service and sends them reading this package
	 instead.  It happens for real: a proxy or a gateway in front of the
	 service answers 200 with its own envelope, and a truncated document
	 can still parse.

	 The body is carried on the exception, whole, for a caller who wants to
	 look; the MESSAGE carries the first two hundred characters of it,
	 because the point of the message is to be read in a log line."
	| text |
	text := [aReply asJsonString] on: Error do: [:e | aReply printString].
	text size > 200 ifTrue: [text := (text copyFrom: 1 to: 200), '...'].
	^(LLMError new) status: lastStatus; body: text;
		signal: self class name, ' answered ',
			(lastStatus ifNil: ['a success']) printString,
			' with a reply this client cannot read (', aReason, '): ', text
]

{ #category : 'http' }
LLM >> postJson: aJSONObject to: aUrl linesDo: aBlock [
	"A streamed reply, each line of its body to aBlock as it arrives.  An
	 error reply is not a stream: it is read whole and raised."
	self client post: aUrl body: aJSONObject asJsonString contentType: 'application/json' do: [:reply |
		lastStatus := reply status.
		reply isSuccess ifFalse: [self failed: reply status body: reply body].
		reply bodyStream linesDo: aBlock]
]

{ #category : 'private' }
LLM >> string: aKey of: aJSONObject [
	"The String at aKey, or nil for none or for a null: a stop reason is
	 null until the model stops."
	| value |
	value := aJSONObject at: aKey ifAbsent: [nil].
	^value isString ifTrue: [value] ifFalse: [nil]
]

{ #category : 'private' }
LLM >> floatsOf: aJSONArray [
	"An embedding as Floats.  The JSON reader answers a decimal as an
	 exact Fraction, which is right for money and wrong for a vector of
	 fifteen hundred of them."
	^aJSONArray asArray collect: [:each | each asFloat]
]

{ #category : 'images' }
LLM >> imageBase64: aPath [
	"The file's bytes, base64."
	^Base64 encode: (self fileContents: aPath)
]

{ #category : 'images' }
LLM >> imageMediaType: aPath [
	"By the extension; JPEG when it is none of the others, as Kiss guesses."
	| lower |
	lower := aPath asLowercase.
	(lower endsWith: '.png') ifTrue: [^'image/png'].
	(lower endsWith: '.gif') ifTrue: [^'image/gif'].
	(lower endsWith: '.webp') ifTrue: [^'image/webp'].
	^'image/jpeg'
]

{ #category : 'images' }
LLM >> fileContents: aPath [
	| stream |
	stream := FileStream oldFileNamed: aPath.
	^[stream contentsOfEntireFile] ensure: [stream close]
]
