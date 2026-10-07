"
Where a server writes: standard error, one line at a time, with the time.

A -serve image has no Transcript window and nobody looking at one, and its
standard error is what the operator, the service manager or the log
collector reads.  Each line is one send, so lines from many workers do not
interleave -- that is what primitive 248 promises.  And each line is one
line: everything outside printable ASCII in the text is written \xHH, since
the text is so often the client's (Bugs5 NET-13).
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
	"The line, with the text escaped HERE and not by whoever built it.
	 Bugs4 NET-6 escaped the two fields RestDispatcher puts in its own
	 line, and every other line -- an application's, the demo's upload
	 line with the client's file name in it -- went out raw, CRs and ANSI
	 escapes and all (Bugs5 NET-13).  See HttpCodec class>>printableForLog:."
	| out text |
	text := aString isString ifTrue: [aString] ifFalse: [aString printString].
	text := HttpCodec printableForLog: text.
	out := WriteStream on: (String new: text size + 40).
	out nextPutAll: Date today printString; space; nextPutAll: Time now printString; space.
	out nextPutAll: level; nextPutAll: ': '; nextPutAll: text.
	self reportOnStandardError: out contents
]
