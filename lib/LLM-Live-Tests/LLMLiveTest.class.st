"
The LLM classes against the real services, with real keys: what
lib/LLM-Tests checks on a canned wire, checked on the live one -- a
model list, an answer, an answer as it comes, a conversation that
remembers, a tool the model must call, an embedding.

    ANTHROPIC_API_KEY=... OPENAI_API_KEY=... OPENROUTER_API_KEY=... \
        ./st80 -bootstrap -profile profiles/llm-live.profile -tests

A test whose key is not in the environment FAILS, saying which key: this
profile is run on purpose, and a suite that passed while asking nobody
would be the profile teaching its reader to ignore it.  The Ollama test
wants an Ollama on localhost:11434 with a chat model, and fails the same
way when there is none.  The models are the services' current small ones,
and LLM_LIVE_ANTHROPIC_MODEL, LLM_LIVE_OPENAI_MODEL and
LLM_LIVE_OPENROUTER_MODEL in the environment choose others.
"
Class {
	#name : 'LLMLiveTest',
	#superclass : 'TestCase',
	#category : 'LLM-Live-Tests'
}

{ #category : 'private' }
LLMLiveTest >> key: aName [
	| key |
	key := Smalltalk environmentAt: aName.
	(key isNil or: [key isEmpty]) ifTrue: [^self error: aName, ' is not set in the environment'].
	^key
]

{ #category : 'private' }
LLMLiveTest >> model: aName or: aDefault [
	^(Smalltalk environmentAt: aName) ifNil: [aDefault]
]

{ #category : 'private' }
LLMLiveTest >> exercise: anLLM [
	"What every chat model here must do."
	| answer pieces talk tool |
	answer := anLLM send: 'Answer with the single word: pong'.
	self assert: (answer asLowercase includesSubstring: 'pong').
	"Streamed: the pieces, joined, are the answer.  Not what the answer
	 says -- a local model on this machine counted in Polish."
	pieces := OrderedCollection new.
	answer := anLLM send: 'Count from one to three, as words.' do: [:piece | pieces add: piece].
	self assert: pieces notEmpty.
	self assert: answer notEmpty.
	self assert: (pieces inject: '' into: [:acc :each | acc, each]) equals: answer.
	talk := anLLM conversation.
	talk ask: 'My name is Ada Lovelace. Remember it.'.
	self assert: ((talk ask: 'What is my surname? Answer with the surname only.') asLowercase includesSubstring: 'lovelace').
	self assert: talk messages size equals: 4.
	"A small local model sends the number as a string as often as not --
	 llama3.2 did -- so the tool takes either, as a tool should."
	tool := LLMTool name: 'square' description: 'The square of a number'
		parameters: (JSONObject fromString: '{"type":"object","properties":{"n":{"type":"number"}},"required":["n"]}')
		do: [:args | | n | n := args at: 'n'. n isString ifTrue: [n := n asNumber]. (n * n) printString].
	talk := anLLM conversation.
	talk tools: (Array with: tool).
	answer := talk ask: 'Use the square tool to square 12, then answer with the number alone.'.
	self assert: (answer includesSubstring: '144').
	self assert: talk messages size >= 4
]

{ #category : 'tests' }
LLMLiveTest >> testAnthropic [
	| claude |
	claude := Anthropic apiKey: (self key: 'ANTHROPIC_API_KEY')
		model: (self model: 'LLM_LIVE_ANTHROPIC_MODEL' or: 'claude-opus-5').
	self assert: (claude models anySatisfy: [:each | each startsWith: 'claude']).
	claude maxTokens: 400.
	self exercise: claude.
	self assert: claude lastStopReason equals: 'end_turn'
]

{ #category : 'tests' }
LLMLiveTest >> testOpenAI [
	| gpt vector |
	gpt := OpenAI apiKey: (self key: 'OPENAI_API_KEY')
		model: (self model: 'LLM_LIVE_OPENAI_MODEL' or: 'gpt-4o-mini').
	self assert: (gpt models anySatisfy: [:each | each startsWith: 'gpt']).
	self exercise: gpt.
	vector := (OpenAI apiKey: gpt apiKey model: 'text-embedding-3-small') embed: 'a sentence'.
	self assert: vector size equals: 1536.
	self assert: ((vector at: 1) isKindOf: Float)
]

{ #category : 'tests' }
LLMLiveTest >> testOpenRouter [
	| router |
	router := OpenRouter apiKey: (self key: 'OPENROUTER_API_KEY')
		model: (self model: 'LLM_LIVE_OPENROUTER_MODEL' or: 'openai/gpt-4o-mini').
	router siteName: 'Smalltalk-2026 tests'.
	self assert: router models notEmpty.
	self exercise: router
]

{ #category : 'tests' }
LLMLiveTest >> testOllama [
	| ollama models chat embed vector |
	ollama := Ollama new.
	ollama isUp ifFalse: [^self error: 'Ollama is not running on localhost:11434'].
	models := ollama models.
	self assert: models notEmpty.
	"A llama when there is one -- it calls tools and answers in English --
	 else the first model that is not an embedding model."
	chat := (Smalltalk environmentAt: 'LLM_LIVE_OLLAMA_MODEL') ifNil: [
		models detect: [:each | each startsWith: 'llama'] ifNone: [
			models detect: [:each | (each includesSubstring: 'embed') not] ifNone: [models first]]].
	ollama model: chat.
	self exercise: ollama.
	embed := models detect: [:each | each includesSubstring: 'embed'] ifNone: [nil].
	embed notNil ifTrue: [
		vector := (Ollama new model: embed; yourself) embed: 'a sentence'.
		self assert: vector size > 100]
]
