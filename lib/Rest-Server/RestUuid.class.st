"
A version-4 UUID as a String, from the system's entropy source: what a session
is named by, and what a client cannot guess.
"
Class {
	#name : 'RestUuid',
	#superclass : 'Object',
	#category : 'Rest-Server'
}

{ #category : 'instance creation' }
RestUuid class >> new [
	"Sixteen random bytes, the version and variant bits set, as
	 xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx."
	| bytes out hex |
	bytes := Socket randomBytes: 16.
	bytes at: 7 put: (Character value: (((bytes at: 7) asciiValue bitAnd: 16r0F) bitOr: 16r40)).
	bytes at: 9 put: (Character value: (((bytes at: 9) asciiValue bitAnd: 16r3F) bitOr: 16r80)).
	hex := '0123456789abcdef'.
	out := WriteStream on: (String new: 36).
	1 to: 16 do: [:i | | v |
		(i = 5 or: [i = 7 or: [i = 9 or: [i = 11]]]) ifTrue: [out nextPut: $-].
		v := (bytes at: i) asciiValue.
		out nextPut: (hex at: (v bitShift: -4) + 1); nextPut: (hex at: (v bitAnd: 15) + 1)].
	^out contents
]
