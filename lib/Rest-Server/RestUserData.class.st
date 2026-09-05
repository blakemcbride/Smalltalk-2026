"
One logged-in user: who they are, the uuid the front end sends with every
request, when they were last heard from, and whatever the application wants
to keep beside them.

Shared between every request that user makes, which on this system means
between workers; the last-access time is one word and a torn read is
impossible, but the application's own data is a Dictionary and takes a Mutex
for every access.

AND ONE REQUEST AT A TIME PER SESSION.  critical: is a SECOND lock, held by
the dispatcher for the whole of a request that carries this session's uuid,
so that two requests bearing one _uuid run one after the other instead of at
once on two workers.  Bugs4 found that they did not: `session at: #cart' read,
changed and written back by two requests in parallel is the ordinary
read-modify-write race, and every application that keeps anything in a
session writes that shape.  The data lock alone cannot fix it -- it makes each
access atomic and the SEQUENCE of accesses is what has to be.

The two locks are separate and neither is reentrant, which is why they must
be: a service running inside critical: reaches at: and at:put:, and one
Mutex would deadlock the first time it did.  The order is always this lock
first, then the data lock, and nothing takes them the other way round.
"
Class {
	#name : 'RestUserData',
	#superclass : 'Object',
	#instVars : [ 'username', 'password', 'userId', 'uuid', 'lastAccess', 'data', 'lock', 'requestLock' ],
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
	lock := Mutex new.
	requestLock := Mutex new
]

{ #category : 'data' }
RestUserData >> critical: aBlock [
	"aBlock with this session held: one request at a time for this uuid.
	 See the class comment for why this is not the data lock."
	^requestLock critical: aBlock
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
