"
A conversation with a model: what was asked and what was answered, sent
whole with every question so that the model remembers -- the service does
not.

    talk := llm conversation.
    talk ask: 'My name is Ada.'.
    talk ask: 'What is my name?'.
    talk ask: 'Say it again, slowly' do: [:piece | Transcript show: piece].
    talk messages                                   what has been said, in the service's shape
    talk clear

    talk tools: (Array with: aTool with: anotherTool).
    talk ask: 'What time is it in Tokyo?'          the tool runs; the answer is the model's

The messages are kept in the service's own shape -- Anthropic's content
blocks, OpenAI's tool_calls -- because a tool exchange has to be echoed
back exactly as the service wrote it, and because the shape is the
model's business and not this class's.  A conversation belongs to one
model; to talk to another, start another.

Tools are the conversation's, not the model's: the same model answers a
plain question in one conversation and runs tools in another.  With tools
the answer is whole, not streamed; the LLM class comment says why.
"
Class {
	#name : 'LLMConversation',
	#superclass : 'Object',
	#instVars : [ 'llm', 'messages', 'tools' ],
	#category : 'LLM'
}

{ #category : 'instance creation' }
LLMConversation class >> on: anLLM [
	^self new setLLM: anLLM
]

{ #category : 'private' }
LLMConversation >> setLLM: anLLM [
	llm := anLLM.
	messages := OrderedCollection new
]

{ #category : 'accessing' }
LLMConversation >> llm [
	^llm
]

{ #category : 'accessing' }
LLMConversation >> messages [
	"What has been said, in the service's shape, oldest first."
	^messages
]

{ #category : 'accessing' }
LLMConversation >> tools [
	^tools
]

{ #category : 'configuration' }
LLMConversation >> tools: aCollection [
	"The LLMTools the model may call in this conversation; nil for none."
	tools := aCollection
]

{ #category : 'configuration' }
LLMConversation >> addTool: anLLMTool [
	tools := (tools ifNil: [#()]) asOrderedCollection add: anLLMTool; yourself
]

{ #category : 'configuration' }
LLMConversation >> clear [
	"Forget everything said."
	messages := OrderedCollection new
]

{ #category : 'asking' }
LLMConversation >> ask: aPrompt [
	"The model's answer, remembering both."
	^self ask: aPrompt image: nil do: nil
]

{ #category : 'asking' }
LLMConversation >> ask: aPrompt do: aBlock [
	"The answer as it comes, each piece to aBlock."
	^self ask: aPrompt image: nil do: aBlock
]

{ #category : 'asking' }
LLMConversation >> ask: aPrompt image: aPathOrNil [
	^self ask: aPrompt image: aPathOrNil do: nil
]

{ #category : 'asking' }
LLMConversation >> ask: aPrompt image: aPathOrNil do: aBlockOrNil [
	messages add: (llm userMessage: aPrompt image: aPathOrNil).
	^llm answerFor: messages tools: tools do: aBlockOrNil
]
