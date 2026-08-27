"
What a language-model service said no to, or what could not be asked of
it: the HTTP status and the body when there was a reply, since the body is
the service's own sentence about what was wrong, and nil for both when the
fault was here -- no model set, a tool loop that would not end.
"
Class {
	#name : 'LLMError',
	#superclass : 'Error',
	#instVars : [ 'status', 'body' ],
	#category : 'LLM'
}

{ #category : 'accessing' }
LLMError >> status: anInteger [
	status := anInteger
]

{ #category : 'accessing' }
LLMError >> status [
	^status
]

{ #category : 'accessing' }
LLMError >> body: aString [
	body := aString
]

{ #category : 'accessing' }
LLMError >> body [
	^body
]
