"
The application's login, as the Rest-Server tests want it: one user, ada,
whose password is secret, and every session good until it is logged out.
"
Class {
	#name : 'Login',
	#superclass : 'Object',
	#category : 'backend'
}

{ #category : 'login' }
Login class >> login: username password: password outjson: outjson request: request [
	(username = 'ada' and: [password = 'secret']) ifFalse: [^nil].
	outjson at: 'greeting' put: 'hello ', username.
	^request server userCache newUser: username password: password userId: 1
]

{ #category : 'login' }
Login class >> checkLogin: aUser request: request [
	^true
]
