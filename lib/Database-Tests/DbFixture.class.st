"
The schema graph test_parallel_db hands to every worker.

One graph, shared, because a per-worker graph would test nothing.  A
DbSchemaGraph is the one object in lib/Database that is genuinely shared
between processes: a connection reads it once, and an application that has
paid for that read is invited by DbConnection>>schemaGraph: to hand the same
graph to every connection it opens.  Every other object in the package
belongs to one connection and a connection belongs to one process.

The table names are built here, once, on one thread, and handed out
ready-made, for the reason JSONFixture gives: a worker building a String on
its way into a critical section puts the allocator in the middle of a test
whose subject is the lock.

Class variables in a class initializer, never lazily -- see
ConcurrencyFixture, which explains why `^Foo ifNil: [Foo := ...]' is the one
pattern this system will not have.  DbConnection>>schemaGraph had that shape
until this work; two processes each read a whole schema and one of the two
graphs was dropped, along with everything that had captured it.
"
Class {
	#name : 'DbFixture',
	#superclass : 'Object',
	#classVars : [ 'SharedGraph', 'FromNames', 'ToNames' ],
	#category : 'Database-Tests'
}

{ #category : 'initialization' }
DbFixture class >> initialize [
	SharedGraph := DbSchemaGraph new.
	FromNames := (1 to: 10) collect: [:i | 'from', i printString].
	ToNames := (1 to: 10) collect: [:i | 'to', i printString]
]

{ #category : 'accessing' }
DbFixture class >> graph [ ^SharedGraph ]

{ #category : 'accessing' }
DbFixture class >> pairCount [ ^FromNames size ]

{ #category : 'accessing' }
DbFixture class >> fromTableAt: anIndex [ ^FromNames at: anIndex ]

{ #category : 'accessing' }
DbFixture class >> toTableAt: anIndex [ ^ToNames at: anIndex ]

{ #category : 'accessing' }
DbFixture class >> edgeCount [
	"Every edge the graph holds, counted from the `from' side only.

	 From one side only because addForeignKey: records each edge at BOTH
	 ends: counting both would count every edge twice and the arithmetic
	 this test rests on would stop having one right answer."

	^FromNames inject: 0 into: [:sum :each |
		sum + (SharedGraph foreignKeysOf: each) size]
]
