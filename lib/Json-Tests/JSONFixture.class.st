"
The documents test_parallel_json hands to every worker.

Three of them, because the three ways a shared container can be got wrong
are different failures: a JSONObject that gains a name twice or loses one, a
JSONArray that loses an element, and a document read while somebody writes
it.

Class variables created in a class initializer, for the two reasons
ConcurrencyFixture gives and which apply again here.  The compiler resolves
a global to its Association at COMPILE time through the bootstrap's own
table, so a name installed later with `Smalltalk at:put:' is not the name a
method compiled afterwards refers to.  And lazy creation --
`^Foo ifNil: [Foo := ...]' -- is check-then-act: on eight cores two workers
each build one and one is thrown away, which for a shared document means
half the workers filling a document nobody will read.

THE NAMES ARE BUILT HERE, once, on one thread, and handed out ready-made.
Every worker uses the SAME ten, which is what makes the test sharp: without
the lock, two workers reaching `(map includesKey: key) ifFalse: [names
addLast: key]' together for a name neither has yet BOTH add it, and the
object ends up with the same name twice -- a document that writes one
member twice and reads back with a name nobody put there.  Ten names, ten
names afterwards, however many workers there were.
"
Class {
	#name : 'JSONFixture',
	#superclass : 'Object',
	#classVars : [ 'SharedObject', 'SharedArray', 'SharedDocument', 'Names' ],
	#category : 'Json-Tests'
}

{ #category : 'initialization' }
JSONFixture class >> initialize [
	SharedObject := JSONObject new.
	SharedArray := JSONArray new.
	SharedDocument := JSONObject new.
	Names := (1 to: 10) collect: [:i | 'name-', i printString]
]

{ #category : 'accessing' }
JSONFixture class >> object [ ^SharedObject ]

{ #category : 'accessing' }
JSONFixture class >> array [ ^SharedArray ]

{ #category : 'accessing' }
JSONFixture class >> document [ ^SharedDocument ]

{ #category : 'accessing' }
JSONFixture class >> nameCount [ ^Names size ]

{ #category : 'accessing' }
JSONFixture class >> nameAt: anIndex [
	"One of the ten, ready-made.  A worker that built its own would be
	 allocating a String on the way into a critical section, which is a
	 thing to keep out of a test whose subject is the critical section."

	^Names at: anIndex
]
