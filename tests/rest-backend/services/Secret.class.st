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

{ #category : 'services' }
Secret >> bump [
	"Read the counter, let another process run, and write it back: the
	 read-modify-write every session store is made of.  Without the
	 dispatcher's per-session lock eight of these at once lose every
	 increment but one; with it they count to eight.  Bugs4."
	| count |
	count := (self userData at: 'count') ifNil: [0].
	Processor yield.
	(Delay forMilliseconds: 2) wait.
	self userData at: 'count' put: count + 1.
	outjson at: 'count' put: count + 1
]

{ #category : 'services' }
Secret >> count [
	outjson at: 'count' put: ((self userData at: 'count') ifNil: [0])
]
