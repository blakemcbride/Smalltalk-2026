"
The part of the LLM classes that is not any service's: a conversation
that remembers, a tool that runs, a tool nobody gave, a loop that will
not end, and what cannot be asked.
"
Class {
	#name : 'LLMConversationTest',
	#superclass : 'LLMTestCase',
	#category : 'LLM-Tests'
}

{ #category : 'private' }
LLMConversationTest >> reply: aText [
	^self chunked: '{"model":"m","message":{"role":"assistant","content":"', aText, '"},"done":true,"done_reason":"stop"}'
]

{ #category : 'tests' }
LLMConversationTest >> testAConversationRemembers [
	| talk messages |
	self serving: (Array with: (self reply: 'Hello, Ada.') with: (self reply: 'Your name is Ada.'))
		do: [:url |
			talk := (Ollama new url: url; model: 'm'; yourself) conversation.
			self assert: (talk ask: 'My name is Ada.') equals: 'Hello, Ada.'.
			self assert: (talk ask: 'What is my name?') equals: 'Your name is Ada.'].
	messages := (self bodyAt: 2) arrayAt: 'messages'.
	self assert: messages size equals: 3.
	self assert: (messages objectAt: 1) asJsonString equals: '{"role":"user","content":"My name is Ada."}'.
	self assert: (messages objectAt: 2) asJsonString equals: '{"role":"assistant","content":"Hello, Ada."}'.
	self assert: (messages objectAt: 3) asJsonString equals: '{"role":"user","content":"What is my name?"}'.
	self assert: talk messages size equals: 4.
	talk clear.
	self assert: talk messages isEmpty
]

{ #category : 'tests' }
LLMConversationTest >> testAToolRuns [
	| tool args |
	args := JSONObject new at: 'city' put: 'Paris'; yourself.
	tool := LLMTool name: 't' description: 'd' do: [:a | 'text'].
	self assert: (tool run: args) equals: 'text'.
	tool := LLMTool name: 't' description: 'd' do: [:a | a].
	self assert: (tool run: args) equals: '{"city":"Paris"}'.
	tool := LLMTool name: 't' description: 'd' do: [:a | JSONArray withAll: #(1 2)].
	self assert: (tool run: args) equals: '[1,2]'.
	tool := LLMTool name: 't' description: 'd' do: [:a | 42].
	self assert: (tool run: args) equals: '42'.
	tool := LLMTool name: 't' description: 'd' do: [:a | nil].
	self assert: (tool run: args) equals: ''.
	self assert: ((LLMTool missing: 'nothing') run: args) equals: 'no such tool: nothing'.
	self assert: (tool parameters stringAt: 'type') equals: 'object'
]

{ #category : 'tests' }
LLMConversationTest >> testAToolNobodyGaveIsToldSo [
	| answer |
	answer := self serving: (Array
			with: (self chunked: '{"model":"m","message":{"role":"assistant","content":"","tool_calls":[{"function":{"name":"launch","arguments":{"target":"x"}}}]},"done":true}')
			with: (self reply: 'I see there is no such tool.'))
		do: [:url | | talk |
			talk := (Ollama new url: url; model: 'm'; yourself) conversation.
			talk tools: (Array with: (LLMTool name: 'time' description: 'the time' do: [:a | '12:00'])).
			talk ask: 'Launch x.'].
	self assert: answer equals: 'I see there is no such tool.'.
	self assert: ((((self bodyAt: 2) arrayAt: 'messages') objectAt: 3) stringAt: 'content') equals: 'no such tool: launch'
]

{ #category : 'tests' }
LLMConversationTest >> testALoopThatWillNotEndIsAnError [
	| replies error |
	replies := (1 to: 22) collect: [:i |
		self chunked: '{"model":"m","message":{"role":"assistant","content":"","tool_calls":[{"function":{"name":"again","arguments":{}}}]},"done":true}'].
	error := self serving: replies do: [:url | | talk |
		talk := (Ollama new url: url; model: 'm'; yourself) conversation.
		talk tools: (Array with: (LLMTool name: 'again' description: 'once more' do: [:a | 'ok'])).
		[talk ask: 'Go.'. nil] on: LLMError do: [:e | e]].
	self assert: error notNil.
	self assert: (error messageText includesSubstring: 'twenty').
	self assert: self requestCount equals: 21
]

{ #category : 'tests' }
LLMConversationTest >> testWithToolsTheAnswerComesWhole [
	"A block and tools: the answer is handed over once, at the end."
	| pieces answer |
	pieces := OrderedCollection new.
	answer := self serving: (self reply: 'All at once.') do: [:url | | talk |
		talk := (Ollama new url: url; model: 'm'; yourself) conversation.
		talk tools: (Array with: (LLMTool name: 't' description: 'd' do: [:a | ''])).
		talk ask: 'Say it.' do: [:piece | pieces add: piece]].
	self assert: answer equals: 'All at once.'.
	self assert: pieces asArray equals: #('All at once.').
	self deny: ((self bodyAt: 1) booleanAt: 'stream')
]
