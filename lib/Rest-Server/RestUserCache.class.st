"
The logged-in users, by uuid, and the process that forgets the idle ones.

Kiss keeps this in static fields of UserCache; here it is an object the server
owns, with one Mutex over the table -- reads too -- and a purge process the
server starts and stops.  A user idle for longer than inactiveSeconds is
removed on the next purge, and the application's logout handler, if it set
one, is told.

AND THERE IS A CEILING ON HOW MANY.  Kiss has none, and neither did this:
sessions were removed only by a logout or by the purge that runs once a
minute, so a client logging in over and over -- which on a server with no
authentication required is one request with no credentials at all -- grew the
table by one entry a call for as long as it liked, and the entries it made
were young enough that no purge would take them.  maxUsers stops that at ten
thousand.  Reaching it first sweeps the idle ones, which is the purge brought
forward rather than a new rule; only a table still full after that refuses,
and it refuses the new login rather than evicting somebody else's session,
because evicting is how a flood logs the real users out.
"
Class {
	#name : 'RestUserCache',
	#superclass : 'Object',
	#instVars : [ 'table', 'lock', 'inactiveSeconds', 'logoutHandler', 'purging', 'maxUsers' ],
	#category : 'Rest-Server'
}

{ #category : 'initialization' }
RestUserCache >> initialize [
	table := Dictionary new.
	lock := Mutex new.
	inactiveSeconds := 3600.
	maxUsers := 10000.
	purging := false
]

{ #category : 'configuration' }
RestUserCache >> inactiveSeconds: anInteger [
	inactiveSeconds := anInteger
]

{ #category : 'configuration' }
RestUserCache >> inactiveSeconds [
	^inactiveSeconds
]

{ #category : 'configuration' }
RestUserCache >> maxUsers: anInteger [
	"How many sessions may be held at once.  See the class comment."
	maxUsers := anInteger
]

{ #category : 'configuration' }
RestUserCache >> maxUsers [
	^maxUsers
]

{ #category : 'configuration' }
RestUserCache >> logoutHandler: aBlockOrNil [
	"Evaluated with the RestUserData of every user this forgets."
	logoutHandler := aBlockOrNil
]

{ #category : 'users' }
RestUserCache >> newUser: aName password: aPassword userId: anId [
	"Log a user in: answer their RestUserData, with a fresh uuid.

	 Raises a RestLogError when the table is full -- see the class comment.
	 The idle sweep is tried first and outside the lock's second look, so
	 the common case is one critical section and no sweep at all."
	| user |
	user := RestUserData username: aName password: aPassword userId: anId.
	(lock critical: [table size >= maxUsers]) ifTrue: [
		self purgeOld.
		(lock critical: [table size >= maxUsers]) ifTrue: [
			RestLog error: 'the session table is full at ', maxUsers printString,
				': refusing a login'.
			^RestLogError new signal: 'Too many sessions are open; try again later.']].
	lock critical: [table at: user uuid put: user].
	^user
]

{ #category : 'users' }
RestUserCache >> findUser: aUuid [
	"The user with that uuid, or nil -- for a nil or empty uuid too."
	(aUuid isNil or: [aUuid isEmpty]) ifTrue: [^nil].
	^lock critical: [table at: aUuid ifAbsent: [nil]]
]

{ #category : 'users' }
RestUserCache >> removeUser: aUuid [
	"Log the user out.  Answer the RestUserData, or nil if there was none."
	| user |
	user := lock critical: [table removeKey: aUuid ifAbsent: [nil]].
	(user notNil and: [logoutHandler notNil]) ifTrue: [logoutHandler value: user].
	^user
]

{ #category : 'users' }
RestUserCache >> size [
	^lock critical: [table size]
]

{ #category : 'purging' }
RestUserCache >> purgeOld [
	"Forget every user idle for longer than inactiveSeconds.  The handler
	 runs outside the lock: it is the application's code."
	| gone |
	gone := lock critical: [ | old |
		old := table values select: [:u | u secondsSinceAccess > inactiveSeconds].
		old do: [:u | table removeKey: u uuid ifAbsent: [nil]].
		old].
	logoutHandler notNil ifTrue: [gone do: [:u | logoutHandler value: u]].
	^gone size
]

{ #category : 'purging' }
RestUserCache >> startPurging [
	"A process that purges once a minute until stopPurging."
	purging ifTrue: [^self].
	purging := true.
	[[purging] whileTrue: [
		(Delay forSeconds: 60) wait.
		purging ifTrue: [[self purgeOld] on: Error do: [:e | RestLog error: 'purge: ', e messageText]]]] fork
]

{ #category : 'purging' }
RestUserCache >> stopPurging [
	purging := false
]
