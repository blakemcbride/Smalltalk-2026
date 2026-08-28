"
Ollama without an Ollama: a listener of the test's own answering what the
real server answers, chunked as Go's net/http chunks it, so that the
questions are tested on the wire; a server that is not there; a stream of
one document a line; a tool call; an embedding; and Kiss's four
substitutions on text.
"
Class {
	#name : 'OllamaTest',
	#superclass : 'LLMTestCase',
	#category : 'LLM-Tests'
}

{ #category : 'private' }
OllamaTest >> ollamaAt: aUrl [
	^Ollama new url: aUrl; yourself
]

{ #category : 'tests - wire' }
OllamaTest >> testIsUp [
	self assert: (self serving: (self chunked: '{"version":"0.5.1"}') do: [:url | (self ollamaAt: url) isUp]).
	self assert: (self lineAt: 1) equals: 'GET /api/version HTTP/1.1'.
	self deny: (Ollama new url: 'http://127.0.0.1:1'; yourself) isUp
]

{ #category : 'tests - wire' }
OllamaTest >> testModels [
	| models |
	models := self serving: (self chunked: '{"models":[{"name":"llama3.2:latest","model":"llama3.2:latest","size":2019393189},{"name":"phi3:mini","model":"phi3:mini"}]}')
		do: [:url | (self ollamaAt: url) models].
	self assert: models equals: #('llama3.2:latest' 'phi3:mini').
	self assert: (self lineAt: 1) equals: 'GET /api/tags HTTP/1.1'
]

{ #category : 'tests - wire' }
OllamaTest >> testSend [
	| ollama answer body |
	answer := self serving: (self chunked: '{"model":"phi3:mini","message":{"role":"assistant","content":"The sky is blue because **Rayleigh scattering**.\nReally."},"done":true,"done_reason":"stop"}')
		do: [:url | ollama := self ollamaAt: url. ollama model: 'phi3:mini'; systemPrompt: 'Be brief'; temperature: 0.1; maxTokens: 64; send: 'Why is the sky blue?'].
	self assert: answer equals: 'The sky is blue because **Rayleigh scattering**.', self lf, 'Really.'.
	self assert: ollama lastStopReason equals: 'stop'.
	self assert: (self lineAt: 1) equals: 'POST /api/chat HTTP/1.1'.
	body := self bodyAt: 1.
	self assert: (body stringAt: 'model') equals: 'phi3:mini'.
	self deny: (body booleanAt: 'stream').
	self assert: ((body arrayAt: 'messages') objectAt: 1) asJsonString equals: '{"role":"system","content":"Be brief"}'.
	self assert: ((body arrayAt: 'messages') objectAt: 2) asJsonString equals: '{"role":"user","content":"Why is the sky blue?"}'.
	"1/10 and not 0.1: the reader answers the exact decimal, and a Fraction
	 is equal to a Float only when the Float is exactly that value (Bugs3
	 B36) -- which 0.1 is not."
	self assert: ((body objectAt: 'options') numberAt: 'temperature') equals: 1/10.
	self assert: ((body objectAt: 'options') integerAt: 'num_predict') equals: 64.
	self should: [Ollama new send: 'no model'] raise: LLMError
]

{ #category : 'tests - wire' }
OllamaTest >> testAStreamedAnswer [
	| pieces answer |
	pieces := OrderedCollection new.
	answer := self serving: (self lines: #(
			'{"model":"phi3:mini","message":{"role":"assistant","content":"The"},"done":false}'
			''
			'{"model":"phi3:mini","message":{"role":"assistant","content":" sky"},"done":false}'
			'{"model":"phi3:mini","message":{"role":"assistant","content":""},"done":true,"done_reason":"stop"}'))
		do: [:url | (self ollamaAt: url) model: 'phi3:mini'; send: 'Why?' do: [:piece | pieces add: piece]].
	self assert: answer equals: 'The sky'.
	self assert: pieces asArray equals: #('The' ' sky').
	self assert: ((self bodyAt: 1) booleanAt: 'stream').
	self should: [self serving: (self lines: #('{"error":"model not found"}'))
		do: [:url | (self ollamaAt: url) model: 'x'; send: 'q' do: [:p | ]]] raise: LLMError
]

{ #category : 'tests - wire' }
OllamaTest >> testAToolRoundTrip [
	| tool talk answer second |
	tool := LLMTool name: 'time' description: 'The time now' do: [:args | '12:00'].
	answer := self serving: (Array
			with: (self chunked: '{"model":"llama3.2","message":{"role":"assistant","content":"","tool_calls":[{"function":{"name":"time","arguments":{}}}]},"done":true,"done_reason":"stop"}')
			with: (self chunked: '{"model":"llama3.2","message":{"role":"assistant","content":"It is noon."},"done":true,"done_reason":"stop"}'))
		do: [:url |
			talk := (self ollamaAt: url) model: 'llama3.2'; conversation.
			talk tools: (Array with: tool).
			talk ask: 'What time is it?'].
	self assert: answer equals: 'It is noon.'.
	self assert: ((((self bodyAt: 1) arrayAt: 'tools') objectAt: 1) objectAt: 'function') asJsonString
		equals: '{"name":"time","description":"The time now","parameters":{"type":"object","properties":{}}}'.
	second := (self bodyAt: 2) arrayAt: 'messages'.
	self assert: second size equals: 3.
	self assert: (second objectAt: 3) asJsonString equals: '{"role":"tool","tool_name":"time","content":"12:00"}'
]

{ #category : 'tests - wire' }
OllamaTest >> testAnEmbedding [
	| vector |
	vector := self serving: (self chunked: '{"model":"nomic-embed-text","embeddings":[[0.1,0.2,0.3,0.4]]}')
		do: [:url | (self ollamaAt: url) model: 'nomic-embed-text'; embed: 'a sentence'].
	self assert: vector size equals: 4.
	self assert: (vector at: 4) equals: 0.4.
	self assert: (self lineAt: 1) equals: 'POST /api/embed HTTP/1.1'.
	self assert: ((self bodyAt: 1) stringAt: 'input') equals: 'a sentence'
]

{ #category : 'tests - wire' }
OllamaTest >> testTheUrlIsNormalised [
	self assert: (Ollama new url: 'http://box:11434/'; yourself) url equals: 'http://box:11434'.
	self assert: (Ollama new url: 'http://box:11434/api/'; yourself) url equals: 'http://box:11434'.
	self assert: Ollama new url equals: 'http://localhost:11434'
]

{ #category : 'tests - text' }
OllamaTest >> testToHtml [
	| lf |
	lf := String with: (Character value: 10).
	self assert: (Ollama toHtml: 'plain') equals: 'plain'.
	self assert: (Ollama toHtml: 'one', lf, 'two') equals: 'one<br>two'.
	self assert: (Ollama toHtml: 'a **bold** word') equals: 'a <b>bold</b> word'.
	self assert: (Ollama toHtml: '**two** and **more**') equals: '<b>two</b> and <b>more</b>'.
	self assert: (Ollama toHtml: 'odd ** stays') equals: 'odd ** stays'.
	self assert: (Ollama toHtml: 'x = \boxed{42}') equals: 'x = 42'.
	self assert: (Ollama toHtml: '\(a\) and \[b\]') equals: '(a) and [b]'.
	self assert: (Ollama toHtml: nil) equals: ''.
	self assert: (Ollama toHtml: '') equals: ''
]
