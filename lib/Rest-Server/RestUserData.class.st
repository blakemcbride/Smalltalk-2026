"
One logged-in user: who they are, the uuid the front end sends with every
request, when they were last heard from, and whatever the application wants
to keep beside them.

Shared between every request that user makes, which on this system means
between workers; the last-access time is one word and a torn read is
impossible, but the application's own data is a Dictionary and takes a Mutex
for every access.
"
Class {
	#name : 'RestUserData',
	#superclass : 'Object',
	#instVars : [ 'username', 'password', 'userId', 'uuid', 'lastAccess', 'data', 'lock' ],
	#category : 'Rest-Server'
}

{ #category : 'instance creation' }
RestUserData class >> username: aName password: aPassword userId: anId [
	^self new setUsername: aName password: aPassword userId: anId
]

{ #category : 'private' }
RestUserData >> setUsername: aName password: aPassword userId: anId [
	username := aName.
	password := aPassword.
	userId := anId.
	uuid := RestUuid new.
	lastAccess := Time totalSeconds.
	data := Dictionary new.
	lock := Mutex new
]

{ #category : 'accessing' }
RestUserData >> username [
	^username
]

{ #category : 'accessing' }
RestUserData >> password [
	^password
]

{ #category : 'accessing' }
RestUserData >> userId [
	^userId
]

{ #category : 'accessing' }
RestUserData >> uuid [
	^uuid
]

{ #category : 'accessing' }
RestUserData >> lastAccess [
	"Seconds on Time totalSeconds' scale."
	^lastAccess
]

{ #category : 'accessing' }
RestUserData >> touch [
	lastAccess := Time totalSeconds
]

{ #category : 'accessing' }
RestUserData >> secondsSinceAccess [
	^Time totalSeconds - lastAccess
]

{ #category : 'data' }
RestUserData >> at: aKey [
	"What the application kept under aKey, or nil."
	^lock critical: [data at: aKey ifAbsent: [nil]]
]

{ #category : 'data' }
RestUserData >> at: aKey put: aValue [
	^lock critical: [data at: aKey put: aValue]
]

{ #category : 'data' }
RestUserData >> removeKey: aKey [
	^lock critical: [data removeKey: aKey ifAbsent: [nil]]
]

{ #category : 'printing' }
RestUserData >> printOn: aStream [
	aStream nextPutAll: 'RestUserData '; nextPutAll: username printString
]
