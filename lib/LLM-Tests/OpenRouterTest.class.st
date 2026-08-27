"
What OpenRouter adds to OpenAI's wire: the attribution headers, the
fallback models, reasoning under its own name, and the comment lines it
sends to keep a stream warm.
"
Class {
	#name : 'OpenRouterTest',
	#superclass : 'LLMTestCase',
	#category : 'LLM-Tests'
}

{ #category : 'tests' }
OpenRouterTest >> testTheAdditions [
	| router answer body pieces |
	pieces := OrderedCollection new.
	answer := self serving: (self events: #(
			': OPENROUTER PROCESSING'
			'{"id":"gen-1","choices":[{"index":0,"delta":{"content":"Hi"},"finish_reason":null}]}'
			': OPENROUTER PROCESSING'
			'{"id":"gen-1","choices":[{"index":0,"delta":{"content":" there."},"finish_reason":"stop"}]}'
			'[DONE]'))
		do: [:url |
			router := (OpenRouter apiKey: 'or-test' model: 'anthropic/claude-sonnet-4') url: url; yourself.
			router fallbackModels: #('openai/gpt-4o' 'meta-llama/llama-3.1-70b-instruct');
				siteUrl: 'https://example.com'; siteName: 'A Test'; reasoningEffort: 'high'.
			router send: 'hi' do: [:piece | pieces add: piece]].
	self assert: answer equals: 'Hi there.'.
	self assert: pieces size equals: 2.
	self assert: (self lineAt: 1) equals: 'POST /v1/chat/completions HTTP/1.1'.
	self assert: (self header: 'authorization' at: 1) equals: 'Bearer or-test'.
	self assert: (self header: 'http-referer' at: 1) equals: 'https://example.com'.
	self assert: (self header: 'x-title' at: 1) equals: 'A Test'.
	body := self bodyAt: 1.
	self assert: (body arrayAt: 'models') asArray equals: #('openai/gpt-4o' 'meta-llama/llama-3.1-70b-instruct').
	self assert: ((body objectAt: 'reasoning') stringAt: 'effort') equals: 'high'.
	self deny: (body includesKey: 'reasoning_effort').
	self assert: OpenRouter new url equals: 'https://openrouter.ai/api'.
	self assert: (OpenRouter apiKey: 'k' model: 'm') headers size equals: 1
]
