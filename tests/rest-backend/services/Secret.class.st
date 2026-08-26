Class {
	#name : 'Secret',
	#superclass : 'RestService',
	#category : 'services'
}

{ #category : 'services' }
Secret >> secret [
	self requireLogin.
	outjson at: 'user' put: self userData username.
	outjson at: 'userId' put: self userData userId
]

{ #category : 'services' }
Secret >> remember [
	self userData at: 'note' put: (injson at: 'note').
	outjson at: 'kept' put: true
]

{ #category : 'services' }
Secret >> recall [
	outjson at: 'note' put: (self userData at: 'note')
]
