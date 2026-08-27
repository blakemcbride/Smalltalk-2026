"
One request from a model to run a tool: the call's id, which the answer
must carry back; the tool's name; and the arguments as a JSONObject.
"
Class {
	#name : 'LLMToolCall',
	#superclass : 'Object',
	#instVars : [ 'id', 'name', 'arguments' ],
	#category : 'LLM'
}

{ #category : 'instance creation' }
LLMToolCall class >> id: anId name: aName arguments: aJSONObject [
	^self new setId: anId name: aName arguments: aJSONObject
]

{ #category : 'private' }
LLMToolCall >> setId: anId name: aName arguments: aJSONObject [
	id := anId.
	name := aName.
	arguments := aJSONObject
]

{ #category : 'accessing' }
LLMToolCall >> id [
	^id
]

{ #category : 'accessing' }
LLMToolCall >> name [
	^name
]

{ #category : 'accessing' }
LLMToolCall >> arguments [
	^arguments
]

{ #category : 'printing' }
LLMToolCall >> printOn: aStream [
	aStream nextPutAll: 'LLMToolCall '; nextPutAll: name; space; nextPutAll: arguments asJsonString
]
