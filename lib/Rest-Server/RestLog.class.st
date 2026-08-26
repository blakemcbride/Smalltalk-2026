"
Where a server writes: standard error, one line at a time, with the time.

A -serve image has no Transcript window and nobody looking at one, and its
standard error is what the operator, the service manager or the log
collector reads.  Each line is one send, so lines from many workers do not
interleave -- that is what primitive 248 promises.
"
Class {
	#name : 'RestLog',
	#superclass : 'Object',
	#category : 'Rest-Server'
}

{ #category : 'logging' }
RestLog class >> info: aString [
	self write: 'info' text: aString
]

{ #category : 'logging' }
RestLog class >> warn: aString [
	self write: 'warn' text: aString
]

{ #category : 'logging' }
RestLog class >> error: aString [
	self write: 'ERROR' text: aString
]

{ #category : 'private' }
RestLog class >> write: level text: aString [
	| out |
	out := WriteStream on: (String new: aString size + 40).
	out nextPutAll: Date today printString; space; nextPutAll: Time now printString; space.
	out nextPutAll: level; nextPutAll: ': '; nextPutAll: aString.
	self reportOnStandardError: out contents
]
