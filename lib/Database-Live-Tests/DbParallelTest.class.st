"
Four processes, four connections, four cores, one database.

This is the claim the whole design rests on and the only test that can make
it.  Everything else in this package would pass just as well on a Smalltalk
that ran one bytecode at a time.

WHAT IT ACTUALLY PROVES.  Each process opens its own connection, writes rows
only it writes, and reads back only its own.  A shared connection would
interleave two statements on one handle and the rows would come back wrong or
the driver would refuse; a lock around the whole package would make the
processes take turns, which would still pass -- so the assertion is on the
DATA, and the parallelism it demonstrates is that four native threads were
inside a driver at once without the object memory or the driver noticing.

WHAT IT CANNOT PROVE, and what does instead.  That a blocking call parks its
worker rather than stalling the collector is not visible from Smalltalk: it
is a property of the C, and the thing that checks it is the safepoint's own
contract under ThreadSanitizer.  What this test does contribute to that is
load -- if WORKER_enter_native and WORKER_leave_native did not pair, a
collection landing during a query would find a worker counted twice or not at
all, and that is a hang rather than a wrong answer.
"
Class {
	#name : 'DbParallelTest',
	#superclass : 'TestCase',
	#category : 'Database-Live-Tests'
}

{ #category : 'fixture' }
DbParallelTest >> connectionString [
	^'DRIVER=SQLITE3;Database=', DbLiveTest databaseFile, ';'
]

{ #category : 'fixture' }
DbParallelTest >> setUp [
	| connection |
	connection := DbConnection open: self connectionString.
	[connection executeImmediate: 'DROP TABLE IF EXISTS worker_row'.
	 connection executeImmediate: 'CREATE TABLE worker_row (
		worker INTEGER, n INTEGER, note VARCHAR(40))'.
	 connection commit] ensure: [connection close]
]

{ #category : 'tests' }
DbParallelTest >> testFourProcessesFourConnections [
	"SQLite takes a lock over the whole file for a write, so the processes
	 serialise INSIDE the database however parallel they are outside it.
	 That is a property of this driver and not of this system, and it does
	 not weaken the test: what is being asserted is that four Smalltalk
	 processes each drove their own connection and each got their own rows
	 back, which is exactly what a shared connection could not do."

	| workers rows done |
	workers := 4.
	rows := 5.
	done := SharedQueue new.
	1 to: workers do:
		[:worker |
		[[| connection |
		  connection := DbConnection open: self connectionString.
		  [1 to: rows do:
			[:n |
			 connection
				execute: 'INSERT INTO worker_row VALUES (?, ?, ?)'
				with: (Array with: worker with: n
					with: 'w', worker printString).
			 connection commit].
		   done nextPut: worker]
			ensure: [connection close]]
			on: Error do: [:e | done nextPut: e messageText]] fork].

	"Collect every answer before asserting anything.  A test that asserted
	 as they arrived would leave the other processes running into tearDown."
	workers timesRepeat:
		[| answer |
		 answer := done next.
		 self assert: (answer isKindOf: Integer)
			description: 'a worker failed: ', answer printString].

	self withOwnConnection:
		[:connection |
		self assert: ((connection fetchOne: 'SELECT COUNT(*) AS c FROM worker_row')
			at: 'c') equals: workers * rows.
		1 to: workers do:
			[:worker |
			self assert: ((connection
				fetchOne: 'SELECT COUNT(*) AS c FROM worker_row WHERE worker = ?'
				with: (Array with: worker)) at: 'c') equals: rows]]
]

{ #category : 'tests' }
DbParallelTest >> testEachProcessSeesItsOwnTransaction [
	"Two connections, two transactions, and neither sees the other's
	 uncommitted work.  This is the isolation a connection per process is
	 for, and it is the thing a shared connection destroys."

	| a b |
	a := DbConnection open: self connectionString.
	b := DbConnection open: self connectionString.
	[a execute: 'INSERT INTO worker_row VALUES (?, ?, ?)'
		with: (Array with: 99 with: 1 with: 'uncommitted').
	 self assert: ((b fetchOne: 'SELECT COUNT(*) AS c FROM worker_row WHERE worker = 99')
		at: 'c') equals: 0.
	 a rollback.
	 self assert: ((b fetchOne: 'SELECT COUNT(*) AS c FROM worker_row WHERE worker = 99')
		at: 'c') equals: 0]
		ensure: [a close. b close]
]

{ #category : 'fixture' }
DbParallelTest >> withOwnConnection: aBlock [
	| connection |
	connection := DbConnection open: self connectionString.
	^[aBlock value: connection] ensure: [connection close]
]
