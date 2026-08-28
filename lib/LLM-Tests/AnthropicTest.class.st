"
Anthropic's wire, without Anthropic: the headers and the body a request
carries, the content blocks a reply is read from, an event stream cut into
pieces, a tool round trip, and what the service's refusals become.
"
Class {
	#name : 'AnthropicTest',
	#superclass : 'LLMTestCase',
	#category : 'LLM-Tests'
}

{ #category : 'private' }
AnthropicTest >> claudeAt: aUrl [
	^(Anthropic apiKey: 'sk-test' model: 'claude-test') url: aUrl; yourself
]

{ #category : 'tests' }
AnthropicTest >> testTheWire [
	| answer body |
	answer := self serving: (self chunked: '{"id":"msg_1","type":"message","role":"assistant","content":[{"type":"text","text":"Hello."}],"stop_reason":"end_turn","usage":{"input_tokens":9,"output_tokens":3}}')
		do: [:url | (self claudeAt: url) systemPrompt: 'Be brief'; temperature: 0.3; maxTokens: 100; send: 'hi'].
	self assert: answer equals: 'Hello.'.
	self assert: (self lineAt: 1) equals: 'POST /v1/messages HTTP/1.1'.
	self assert: (self header: 'x-api-key' at: 1) equals: 'sk-test'.
	self assert: (self header: 'anthropic-version' at: 1) equals: '2023-06-01'.
	self assert: (self header: 'anthropic-beta' at: 1) isNil.
	self assert: (self header: 'content-type' at: 1) equals: 'application/json'.
	body := self bodyAt: 1.
	self assert: (body stringAt: 'model') equals: 'claude-test'.
	self assert: (body integerAt: 'max_tokens') equals: 100.
	self assert: (body stringAt: 'system') equals: 'Be brief'.
	"3/10 and not 0.3: the wire carries the decimal 0.3, the reader gives it
	 back as the exact 3/10, and a Fraction is equal to a Float only when the
	 Float is exactly that value (Bugs3 B36) -- which 0.3 is not."
	self assert: (body numberAt: 'temperature') equals: 3/10.
	self deny: (body includesKey: 'top_p').
	self deny: (body includesKey: 'stream').
	self deny: (body includesKey: 'tools').
	self assert: ((body arrayAt: 'messages') objectAt: 1) asJsonString equals: '{"role":"user","content":[{"type":"text","text":"hi"}]}'
]

{ #category : 'tests' }
AnthropicTest >> testWhatIsNotSetIsNotSent [
	| claude body |
	claude := Anthropic apiKey: 'k' model: 'm'.
	body := claude requestBodyFor: (OrderedCollection with: (claude userMessage: 'q' image: nil)) tools: nil stream: false.
	self assert: (body integerAt: 'max_tokens') equals: 8192.
	self deny: (body includesKey: 'system').
	self deny: (body includesKey: 'temperature').
	self deny: (body includesKey: 'top_p').
	self deny: (body includesKey: 'output_config').
	self deny: (body includesKey: 'fallbacks').
	"top-p forgets temperature, and the other way, because the API refuses both"
	claude temperature: 0.2; topP: 0.9.
	body := claude requestBodyFor: OrderedCollection new tools: nil stream: true.
	self assert: (body numberAt: 'top_p') equals: 0.9.
	self deny: (body includesKey: 'temperature').
	self assert: (body booleanAt: 'stream').
	claude temperature: 0.5.
	body := claude requestBodyFor: OrderedCollection new tools: nil stream: false.
	self deny: (body includesKey: 'top_p').
	claude effort: 'high'; fallbacks: #('claude-opus-4-8').
	body := claude requestBodyFor: OrderedCollection new tools: nil stream: false.
	self assert: ((body objectAt: 'output_config') stringAt: 'effort') equals: 'high'.
	self assert: (((body arrayAt: 'fallbacks') objectAt: 1) stringAt: 'model') equals: 'claude-opus-4-8'.
	self assert: (claude headers detect: [:h | h key = 'anthropic-beta'] ifNone: [nil]) notNil
]

{ #category : 'tests' }
AnthropicTest >> testAStreamedAnswer [
	| pieces answer claude |
	pieces := OrderedCollection new.
	answer := self serving: (self events: #(
			'{"type":"message_start","message":{"id":"msg_1","type":"message","role":"assistant","content":[],"stop_reason":null}}'
			'{"type":"content_block_start","index":0,"content_block":{"type":"text","text":""}}'
			'{"type":"content_block_delta","index":0,"delta":{"type":"text_delta","text":"The sky "}}'
			'{"type":"content_block_delta","index":0,"delta":{"type":"text_delta","text":"is blue."}}'
			'{"type":"content_block_stop","index":0}'
			'{"type":"message_delta","delta":{"stop_reason":"end_turn"},"usage":{"output_tokens":5}}'
			'{"type":"message_stop"}'))
		do: [:url | claude := self claudeAt: url. claude send: 'Why?' do: [:piece | pieces add: piece]].
	self assert: answer equals: 'The sky is blue.'.
	self assert: pieces asArray equals: #('The sky ' 'is blue.').
	self assert: ((self bodyAt: 1) asJsonString includesSubstring: '"stream":true').
	self assert: claude lastStopReason equals: 'end_turn'.
	self assert: claude lastResponse isNil
]

{ #category : 'tests' }
AnthropicTest >> testAToolRoundTrip [
	| tool talk answer second |
	tool := LLMTool name: 'weather' description: 'The weather in a city'
		parameters: (JSONObject fromString: '{"type":"object","properties":{"city":{"type":"string"}},"required":["city"]}')
		do: [:args | 'raining in ', (args stringAt: 'city')].
	answer := self serving: (Array
			with: (self chunked: '{"id":"msg_1","type":"message","role":"assistant","content":[{"type":"text","text":"Let me look."},{"type":"tool_use","id":"toolu_1","name":"weather","input":{"city":"Paris"}}],"stop_reason":"tool_use"}')
			with: (self chunked: '{"id":"msg_2","type":"message","role":"assistant","content":[{"type":"text","text":"It is raining in Paris."}],"stop_reason":"end_turn"}'))
		do: [:url |
			talk := (self claudeAt: url) conversation.
			talk tools: (Array with: tool).
			talk ask: 'Is it raining in Paris?'].
	self assert: answer equals: 'It is raining in Paris.'.
	self assert: self requestCount equals: 2.
	self assert: (((self bodyAt: 1) arrayAt: 'tools') objectAt: 1) asJsonString
		equals: '{"name":"weather","description":"The weather in a city","input_schema":{"type":"object","properties":{"city":{"type":"string"}},"required":["city"]}}'.
	second := (self bodyAt: 2) arrayAt: 'messages'.
	self assert: second size equals: 3.
	self assert: ((second objectAt: 2) stringAt: 'role') equals: 'assistant'.
	self assert: ((((second objectAt: 2) arrayAt: 'content') objectAt: 2) asJsonString includesSubstring: '"tool_use"').
	self assert: (second objectAt: 3) asJsonString
		equals: '{"role":"user","content":[{"type":"tool_result","tool_use_id":"toolu_1","content":"raining in Paris"}]}'.
	"the conversation remembers all of it, ready for the next question"
	self assert: talk messages size equals: 4.
	self assert: talk llm lastStopReason equals: 'end_turn'
]

{ #category : 'tests' }
AnthropicTest >> testAnErrorReplyIsAnLLMError [
	| error |
	error := self serving: (self status: 401 body: '{"type":"error","error":{"type":"authentication_error","message":"invalid x-api-key"}}')
		do: [:url | [(self claudeAt: url) send: 'hi'. nil] on: LLMError do: [:e | e]].
	self assert: error notNil.
	self assert: error status equals: 401.
	self assert: (error body includesSubstring: 'invalid x-api-key').
	self assert: (error messageText includesSubstring: 'Anthropic answered 401').
	"and in a stream, where an error is a plain body and not events"
	error := self serving: (self status: 529 body: '{"type":"error","error":{"type":"overloaded_error","message":"Overloaded"}}')
		do: [:url | [(self claudeAt: url) send: 'hi' do: [:p | ]. nil] on: LLMError do: [:e | e]].
	self assert: error status equals: 529.
	"or an event, when it comes after the stream began"
	error := self serving: (self events: #('{"type":"message_start","message":{"content":[]}}' '{"type":"error","error":{"type":"overloaded_error","message":"Overloaded"}}'))
		do: [:url | [(self claudeAt: url) send: 'hi' do: [:p | ]. nil] on: LLMError do: [:e | e]].
	self assert: (error messageText includesSubstring: 'Overloaded')
]

{ #category : 'tests' }
AnthropicTest >> testARefusalIsAnEmptyAnswerThatSaysWhy [
	| claude answer |
	answer := self serving: (self chunked: '{"id":"msg_1","type":"message","role":"assistant","content":[],"stop_reason":"refusal","stop_details":{"type":"refusal","category":"cyber"}}')
		do: [:url | claude := self claudeAt: url. claude send: 'no'].
	self assert: answer equals: ''.
	self assert: claude lastStopReason equals: 'refusal'.
	self assert: ((claude lastResponse objectAt: 'stop_details') stringAt: 'category') equals: 'cyber'
]

{ #category : 'tests' }
AnthropicTest >> testModels [
	| models |
	models := self serving: (self chunked: '{"data":[{"id":"claude-opus-5","type":"model"},{"id":"claude-sonnet-5","type":"model"}],"has_more":false}')
		do: [:url | (self claudeAt: url) models].
	self assert: models equals: #('claude-opus-5' 'claude-sonnet-5').
	self assert: (self lineAt: 1) equals: 'GET /v1/models HTTP/1.1'
]

{ #category : 'tests' }
AnthropicTest >> testWhatCannotBeAsked [
	self should: [Anthropic new model: 'm'; send: 'hi'] raise: LLMError.
	self should: [Anthropic new apiKey: 'k'; send: 'hi'] raise: LLMError.
	self should: [(Anthropic apiKey: 'k' model: 'm') embed: 'x'] raise: LLMError.
	self should: [(Anthropic apiKey: 'k' model: 'm') url: 'http://127.0.0.1:1'; send: 'hi'] raise: NetError
]

{ #category : 'tests' }
AnthropicTest >> testAPictureGoesInline [
	"A PNG from the tree, read as bytes and sent base64 with its type."
	| claude message image data |
	claude := Anthropic apiKey: 'k' model: 'm'.
	message := claude userMessage: 'What is this?' image: 'manual/figures/desktop-menu.png'.
	image := (message arrayAt: 'content') objectAt: 2.
	self assert: (image stringAt: 'type') equals: 'image'.
	self assert: ((image objectAt: 'source') stringAt: 'media_type') equals: 'image/png'.
	data := Base64 decode: ((image objectAt: 'source') stringAt: 'data').
	self assert: (data at: 1) asInteger equals: 137.
	self assert: (data copyFrom: 2 to: 4) equals: 'PNG'.
	self assert: data size > 500
]
