"
The objects test_parallel_lib hands to every worker.

They have to be created at BOOTSTRAP rather than at run time, and the
reason is worth keeping: the compiler resolves a global to its Association
when it compiles, through the bootstrap's own table.  A name installed
later with `Smalltalk at:put:' is in the running image and not in that
table, so a method compiled afterwards still fails with 'undeclared
variable' -- which is what happened, and is why these are class variables
of a class the profile loads.

Initialized eagerly in a class initializer, never lazily on first use.
`^Foo ifNil: [Foo := ...]' is check-then-act: on eight cores two workers
each build one and one of them is thrown away, which for a Mutex means two
workers holding two different locks and believing they hold the same one.
That pattern is forbidden here and doc/ParallelAudit.md says so.
"
Class {
	#name : 'ConcurrencyFixture',
	#superclass : 'Object',
	#classVars : [ 'TestMutex', 'TestCounter', 'TestQueue' ],
	#category : 'Concurrency-Tests'
}

{ #category : 'initialization' }
ConcurrencyFixture class >> initialize [
	TestMutex := Mutex new.
	TestCounter := Array new: 1.
	TestCounter at: 1 put: 0.
	TestQueue := SharedQueue new
]

{ #category : 'accessing' }
ConcurrencyFixture class >> mutex [ ^TestMutex ]

{ #category : 'accessing' }
ConcurrencyFixture class >> counter [ ^TestCounter ]

{ #category : 'accessing' }
ConcurrencyFixture class >> queue [ ^TestQueue ]

{ #category : 'accessing' }
ConcurrencyFixture class >> count [ ^TestCounter at: 1 ]

{ #category : 'accessing' }
ConcurrencyFixture class >> bump [ ^TestCounter at: 1 put: (TestCounter at: 1) + 1 ]
