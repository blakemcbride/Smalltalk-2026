"
A document this system could not read as JSON, or an object it could not
write as JSON.

One class and not a hierarchy, for the same reason DbError is one: a caller
handling a bad document wants to say `that was not JSON' and carry on, not
to tell a bad escape from a bad number.  What it does want is to be told
WHERE, so every message the parser raises carries the character position --
a document that goes wrong at character 4096 of 40000 is one somebody can
open and look at, and `invalid JSON' is not.

The position is the stream's, counted from the beginning of the text the
parser was given, and it names the character just consumed rather than the
one about to be.  That is the character the message is about: by the time
anything is known to be wrong, the offending character has been read.
"
Class {
	#name : 'JSONError',
	#superclass : 'Error',
	#category : 'Json'
}

{ #category : 'accessing' }
JSONError >> description [
	^'A JSON error has occurred'
]
