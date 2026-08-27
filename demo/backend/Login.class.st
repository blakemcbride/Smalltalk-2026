"
The demo's login: a user is a row in `users' whose password matches, and
a session is checked against that row again every two minutes.

This is Kiss's Login.groovy, with the same three tiers a stored password
can be in: the current form, PBKDF2 (PasswordHash isHashed:), which is how
Users adds one; a 64-character SHA-256 hex from before salting; and plain
text from before hashing.  The old forms are still accepted so that an
account made under them keeps working until its password is next changed,
which is exactly what Kiss does.

login:... answers a RestUserData from the server's user cache, or nil for
any reason at all -- no such user, inactive, wrong password -- and the
dispatcher answers `Invalid login' in every case, because which of those
it was is not the caller's business.  checkLogin:request: is asked every
two minutes whether the session is still welcome, which is how a user
disabled in the Users screen is logged out without a restart.

Both run inside a transaction on request db, taken from the pool by the
dispatcher and given back after.
"
Class {
	#name : 'Login',
	#superclass : 'Object',
	#category : 'Web-Demo'
}

{ #category : 'login' }
Login class >> login: username password: password outjson: outjson request: request [
	"Validate a user's name and password; answer their RestUserData, or nil."
	| rec stored |
	request db isNil ifTrue: [^nil].
	rec := request db
		fetchOne: 'select user_id, user_password from users where user_name = ? and user_active = ''Y'''
		with: (Array with: username).
	rec isNil ifTrue: [^nil].
	stored := rec stringAt: 'user_password'.
	stored isNil ifTrue: [^nil].
	(self password: password matches: stored) ifFalse: [^nil].
	"ud at: 'abc' put: 5      -- anything to keep beside the user on the back end"
	"outjson at: 'user_type' put: 'xxx'      -- anything to send back to the front end"
	^request server userCache newUser: username password: password userId: (rec integerAt: 'user_id')
]

{ #category : 'login' }
Login class >> checkLogin: aUser request: request [
	"Whether the user is still allowed in: asked about every two minutes,
	 so that a login disabled while the user is in is logged out."
	| rec stored |
	rec := request db
		fetchOne: 'select user_password from users where user_name = ? and user_active = ''Y'''
		with: (Array with: aUser username).
	rec isNil ifTrue: [^false].
	stored := rec stringAt: 'user_password'.
	stored isNil ifTrue: [^false].
	^self password: aUser password matches: stored
]

{ #category : 'private' }
Login class >> password: entered matches: stored [
	"The three tiers: PBKDF2, a legacy SHA-256 hex, legacy plain text."
	(PasswordHash isHashed: stored)
		ifTrue: [^PasswordHash verify: entered against: stored].
	stored size = 64
		ifTrue: [^stored asLowercase = (Crypto hex: (Crypto sha256: entered))].
	^stored = entered
]
