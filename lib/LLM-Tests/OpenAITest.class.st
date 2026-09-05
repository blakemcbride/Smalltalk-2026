"
OpenAI's wire, without OpenAI: the bearer header, the system message
first, choices[0].message, a stream ending in [DONE], a tool call whose
arguments are a JSON string, and an embedding.
"
Class {
	#name : 'OpenAITest',
	#superclass : 'LLMTestCase',
	#category : 'LLM-Tests'
}

{ #category : 'private' }
OpenAITest >> gptAt: aUrl [
	^(OpenAI apiKey: 'sk-test' model: 'gpt-test') url: aUrl; yourself
]

{ #category : 'tests' }
OpenAITest >> testTheWire [
	| gpt answer body messages |
	answer := self serving: (self chunked: '{"id":"chatcmpl-1","object":"chat.completion","model":"gpt-test","choices":[{"index":0,"message":{"role":"assistant","content":"Hello."},"finish_reason":"stop"}],"usage":{"prompt_tokens":9,"completion_tokens":2}}')
		do: [:url | gpt := self gptAt: url. gpt systemPrompt: 'Be brief'; reasoningEffort: 'low'; maxTokens: 50; send: 'hi'].
	self assert: answer equals: 'Hello.'.
	self assert: gpt lastStopReason equals: 'stop'.
	self assert: (self lineAt: 1) equals: 'POST /v1/chat/completions HTTP/1.1'.
	self assert: (self header: 'authorization' at: 1) equals: 'Bearer sk-test'.
	body := self bodyAt: 1.
	self assert: (body stringAt: 'model') equals: 'gpt-test'.
	self assert: (body stringAt: 'reasoning_effort') equals: 'low'.
	self assert: (body integerAt: 'max_completion_tokens') equals: 50.
	self deny: (body includesKey: 'temperature').
	self deny: (body includesKey: 'stream').
	messages := body arrayAt: 'messages'.
	self assert: (messages objectAt: 1) asJsonString equals: '{"role":"system","content":"Be brief"}'.
	self assert: (messages objectAt: 2) asJsonString equals: '{"role":"user","content":[{"type":"text","text":"hi"}]}'
]

{ #category : 'tests' }
OpenAITest >> testAStreamedAnswer [
	| pieces answer |
	pieces := OrderedCollection new.
	answer := self serving: (self events: #(
			'{"id":"chatcmpl-1","object":"chat.completion.chunk","choices":[{"index":0,"delta":{"role":"assistant","content":""},"finish_reason":null}]}'
			'{"id":"chatcmpl-1","object":"chat.completion.chunk","choices":[{"index":0,"delta":{"content":"The sky "},"finish_reason":null}]}'
			'{"id":"chatcmpl-1","object":"chat.completion.chunk","choices":[{"index":0,"delta":{"content":"is blue."},"finish_reason":null}]}'
			'{"id":"chatcmpl-1","object":"chat.completion.chunk","choices":[{"index":0,"delta":{},"finish_reason":"stop"}]}'
			'{"id":"chatcmpl-1","object":"chat.completion.chunk","choices":[],"usage":{"prompt_tokens":9,"completion_tokens":5}}'
			'[DONE]'))
		do: [:url | (self gptAt: url) send: 'Why?' do: [:piece | pieces add: piece]].
	self assert: answer equals: 'The sky is blue.'.
	self assert: pieces asArray equals: #('The sky ' 'is blue.').
	self assert: ((self bodyAt: 1) booleanAt: 'stream')
]

{ #category : 'tests' }
OpenAITest >> testAToolRoundTrip [
	| tool talk answer second |
	tool := LLMTool name: 'add' description: 'Add two numbers'
		parameters: (JSONObject fromString: '{"type":"object","properties":{"a":{"type":"number"},"b":{"type":"number"}}}')
		do: [:args | ((args numberAt: 'a') + (args numberAt: 'b')) printString].
	answer := self serving: (Array
			with: (self chunked: '{"choices":[{"index":0,"message":{"role":"assistant","content":null,"tool_calls":[{"id":"call_1","type":"function","function":{"name":"add","arguments":"{\"a\": 2, \"b\": 3}"}}]},"finish_reason":"tool_calls"}]}')
			with: (self chunked: '{"choices":[{"index":0,"message":{"role":"assistant","content":"2 + 3 is 5."},"finish_reason":"stop"}]}'))
		do: [:url |
			talk := (self gptAt: url) conversation.
			talk addTool: tool.
			talk ask: 'What is 2 + 3?'].
	self assert: answer equals: '2 + 3 is 5.'.
	self assert: (((self bodyAt: 1) arrayAt: 'tools') objectAt: 1) asJsonString
		equals: '{"type":"function","function":{"name":"add","description":"Add two numbers","parameters":{"type":"object","properties":{"a":{"type":"number"},"b":{"type":"number"}}}}}'.
	second := (self bodyAt: 2) arrayAt: 'messages'.
	self assert: second size equals: 3.
	self assert: ((((second objectAt: 2) arrayAt: 'tool_calls') objectAt: 1) asJsonString includesSubstring: '"call_1"').
	self assert: (second objectAt: 3) asJsonString equals: '{"role":"tool","tool_call_id":"call_1","content":"5"}'
]

{ #category : 'tests' }
OpenAITest >> testAnEmbedding [
	| vector |
	vector := self serving: (self chunked: '{"object":"list","data":[{"object":"embedding","index":0,"embedding":[0.25,-0.5,1e-3]}],"model":"text-embedding-test"}')
		do: [:url | (self gptAt: url) model: 'text-embedding-test'; embed: 'a sentence'].
	self assert: vector size equals: 3.
	self assert: (vector at: 1) equals: 0.25.
	self assert: (vector at: 2) equals: -0.5.
	self assert: ((vector at: 1) isKindOf: Float).
	self assert: (self lineAt: 1) equals: 'POST /v1/embeddings HTTP/1.1'.
	self assert: ((self bodyAt: 1) stringAt: 'input') equals: 'a sentence'
]

{ #category : 'tests' }
OpenAITest >> testModelsAndErrors [
	| models error |
	models := self serving: (self chunked: '{"object":"list","data":[{"id":"gpt-4o","object":"model"},{"id":"o3","object":"model"}]}')
		do: [:url | (self gptAt: url) models].
	self assert: models equals: #('gpt-4o' 'o3').
	self assert: (self lineAt: 1) equals: 'GET /v1/models HTTP/1.1'.
	error := self serving: (self status: 429 body: '{"error":{"message":"Rate limit reached","type":"rate_limit_error"}}')
		do: [:url | [(self gptAt: url) send: 'hi'. nil] on: LLMError do: [:e | e]].
	self assert: error status equals: 429.
	self assert: (error messageText includesSubstring: 'OpenAI answered 429').
	error := self serving: (self events: #('{"error":{"message":"The server had an error","type":"server_error"}}'))
		do: [:url | [(self gptAt: url) send: 'hi' do: [:p | ]. nil] on: LLMError do: [:e | e]].
	self assert: (error messageText includesSubstring: 'server_error').
	self should: [OpenAI new model: 'm'; send: 'hi'] raise: LLMError
]

{ #category : 'tests' }
OpenAITest >> testAPictureIsADataUrl [
	| gpt message image |
	gpt := OpenAI apiKey: 'k' model: 'm'.
	gpt imageDetail: 'high'.
	message := gpt userMessage: 'What is this?' image: 'manual/figures/desktop-menu.png'.
	image := ((message arrayAt: 'content') objectAt: 2) objectAt: 'image_url'.
	self assert: ((image stringAt: 'url') startsWith: 'data:image/png;base64,iVBORw0KGgo').
	self assert: (image stringAt: 'detail') equals: 'high'
]

{ #category : 'tests' }
OpenAITest >> testTheSpaceAfterDataIsOptionalAndAMalformedSuccessIsAnLLMError [
	"Bugs4, both halves, on this service's shape.  `data:{...}' with no
	 space was dropped, so a stream of them answered nothing at all; and a
	 200 without choices reached `nil objectAt:' and became a
	 MessageNotUnderstood rather than an LLMError naming the service."
	| gpt |
	gpt := OpenAI apiKey: 'k' model: 'm'.
	self assert: (gpt streamedTextOf: 'data:{"choices":[{"delta":{"content":"hi"}}]}')
		equals: 'hi'.
	self assert: (gpt streamedTextOf: 'data: {"choices":[{"delta":{"content":"hi"}}]}')
		equals: 'hi'.
	self assert: (gpt streamedTextOf: 'data:[DONE]') isNil.
	self assert: (gpt streamedTextOf: 'data: [DONE]') isNil.
	self assert: (gpt streamedTextOf: ': OPENROUTER PROCESSING') equals: ''.
	self should: [gpt textOf: JSONObject new] raise: LLMError.
	self should: [gpt textOf: (JSONObject fromString: '{"choices":[]}')] raise: LLMError.
	self should: [gpt textOf: (JSONObject fromString: '{"choices":[{}]}')] raise: LLMError.
	self should: [gpt embeddingIn: JSONObject new] raise: LLMError.
	self should: [gpt embeddingIn: (JSONObject fromString: '{"data":[{}]}')] raise: LLMError.
	"stopReasonOf: is asked first, in complete:, and answers nil rather
	 than raising about a field nobody asked for."
	self assert: (gpt stopReasonOf: JSONObject new) isNil
]
