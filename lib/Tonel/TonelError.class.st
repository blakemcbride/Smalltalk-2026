"
A Tonel file that could not be read, with where in it the trouble was.

The message names the file and the line, because that pair is how nearly every
fault in this project has been found, and a reader that lost it would be a bad
trade -- src/compiler/source.h says the same of the C reader, and this one keeps
the same promise.
"
Class {
	#name : 'TonelError',
	#superclass : 'Error',
	#category : 'Tonel'
}

{ #category : 'accessing' }
TonelError >> description [
	^'A Tonel file could not be read'
]
