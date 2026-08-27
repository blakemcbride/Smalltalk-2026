"
The application's startup, as the Rest-Server tests want it.

init: records the server it was given and whether it was listening yet
(it must not have been), or raises when the environment's `initMode' is
`fail'.  init2: records what it was given -- nil when the server has no
database -- and with a database makes the table `marks' if it is missing
and inserts one row; when `init2Mode' is `reset' it empties the table
instead, and when it is `fail' it inserts the row and then raises, so that
the row's absence afterwards proves the transaction was rolled back and
the server's not listening proves a failed start is a stopped server.
"
Class {
	#name : 'Init',
	#superclass : 'Object',
	#category : 'backend'
}

{ #category : 'startup' }
Init class >> init: aServer [
	(aServer environmentAt: 'initMode') = 'fail'
		ifTrue: [^self error: 'init: refused, as asked'].
	Smalltalk at: #RestInitSeen put: (Array with: aServer with: aServer isRunning).
	Smalltalk at: #RestInit2Seen put: #notYet
]

{ #category : 'startup' }
Init class >> init2: aConnectionOrNil [
	| mode |
	Smalltalk at: #RestInit2Seen put: aConnectionOrNil.
	aConnectionOrNil isNil ifTrue: [^self].
	mode := (Smalltalk at: #RestInitSeen) first environmentAt: 'init2Mode'.
	(aConnectionOrNil tableExists: 'marks')
		ifFalse: [aConnectionOrNil executeImmediate: 'CREATE TABLE marks (id INTEGER)'].
	mode = 'reset' ifTrue: [^aConnectionOrNil executeImmediate: 'DELETE FROM marks'].
	aConnectionOrNil execute: 'INSERT INTO marks VALUES (?)' with: (Array with: 1).
	mode = 'fail' ifTrue: [^self error: 'init2: refused after the insert, as asked']
]
