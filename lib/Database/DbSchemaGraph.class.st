"
The schema as a graph: tables are nodes, foreign keys are edges.

Given a set of tables that a query needs, this answers the joins that connect
them -- which is the thing DbQueryBuilder exists to spare a caller from
writing by hand, and the reason that class can take a list of columns and
produce a correct five-table join.

HOW THE PATH IS CHOSEN.  Connecting N tables with the fewest joins is the
Steiner tree problem, which is NP-hard, so this does what everyone does: take
one table as the root, then repeatedly attach whichever remaining table has
the shortest breadth-first path to anything already attached.  That is a
standard approximation and it is not always the true minimum.  It is
DETERMINISTIC, which matters more than optimal here -- the same query must
produce the same SQL every time it is built, or two runs of one report join
differently and nobody can tell which was right.

WHERE THE EDGES COME FROM.  Either the database's own catalogue, through
fromConnection:, or declared by hand with from:column:to:column:, or both.
The hybrid case is the useful one: a schema whose real foreign keys are
declared and whose historical ones are not can have the missing edges added
without giving up the ones the database knows.

READ ONCE, KEPT.  Walking a large schema's metadata is many round trips, so
a connection holds its graph -- see DbConnection>>schemaGraph -- and an
application that wants to skip even that first walk can build the graph, write
it out, and hand it back with schemaGraph:.
"
Class {
	#name : 'DbSchemaGraph',
	#superclass : 'Object',
	#instVars : [ 'adjacency', 'lock' ],
	#category : 'Database'
}

{ #category : 'instance creation' }
DbSchemaGraph class >> new [
	^super new initialize
]

{ #category : 'instance creation' }
DbSchemaGraph class >> fromConnection: aConnection [
	"Every foreign key in the schema, from the database's own catalogue.

	 Walked table by table because that is the only question SQLForeignKeys
	 answers: it takes a table and tells you the keys at one end of it.  The
	 tables come from SQLTables, filtered to real ones -- a view has no
	 foreign keys and asking about every system catalogue table is a great
	 many round trips for nothing."

	| graph |
	graph := self new.
	(aConnection tableNames) do:
		[:table |
		(aConnection importedKeysOf: table) do:
			[:each | graph addRow: each]].
	^graph
]

{ #category : 'initialization' }
DbSchemaGraph >> initialize [
	adjacency := Dictionary new.
	lock := Mutex new
]

{ #category : 'building' }
DbSchemaGraph >> addRow: aRecord [
	"One row of SQLForeignKeys.

	 A composite key arrives as SEVERAL rows -- one per column pair, told
	 apart by KEY_SEQ -- so rows are merged into the edge they belong to
	 rather than each becoming an edge of its own.  Missing that is how a
	 two-column key becomes two one-column keys, each of which is wrong and
	 both of which look plausible."

	| fromTable toTable fromColumn toColumn |
	fromTable := (aRecord at: 'FKTABLE_NAME') asLowercase.
	toTable := (aRecord at: 'PKTABLE_NAME') asLowercase.
	fromColumn := aRecord at: 'FKCOLUMN_NAME'.
	toColumn := aRecord at: 'PKCOLUMN_NAME'.
	"The look and the merge are ONE critical section.  As two, the second
	 row of a composite key can find no edge because the first row's edge has
	 not been added yet, and a two-column key becomes two one-column keys --
	 which is the same wrong answer this method was written to avoid, arrived
	 at from the other direction."
	lock critical: [
		| existing |
		existing := self basicEdgeBetween: fromTable and: toTable.
		existing isNil
			ifTrue:
				[self basicAddForeignKey: (DbForeignKey
					from: fromTable columns: (Array with: fromColumn)
					to: toTable columns: (Array with: toColumn))]
			ifFalse:
				[self basicReplace: existing with: (DbForeignKey
					from: fromTable
					columns: existing fromColumns , (Array with: fromColumn)
					to: toTable
					columns: existing toColumns , (Array with: toColumn))]]
]

{ #category : 'building' }
DbSchemaGraph >> from: aTable column: aColumn to: anotherTable column: anotherColumn [
	^self addForeignKey: (DbForeignKey from: aTable column: aColumn
		to: anotherTable column: anotherColumn)
]

{ #category : 'building' }
DbSchemaGraph >> from: aTable columns: someColumns to: anotherTable columns: otherColumns [
	^self addForeignKey: (DbForeignKey from: aTable columns: someColumns
		to: anotherTable columns: otherColumns)
]

{ #category : 'building' }
DbSchemaGraph >> addForeignKey: anEdge [
	"Recorded at BOTH ends, because a path may be walked in either
	 direction.  A key from employee to department is as much a way of
	 reaching employee from department as the reverse, and a graph that only
	 recorded the declaring end would find half the joins that exist.

	 Both ends under one lock, so that no reader ever sees an edge recorded
	 at one end and not the other -- which is a graph in which the join
	 exists in one direction and not the other, for as long as the window
	 lasts."

	^lock critical: [self basicAddForeignKey: anEdge]
]

{ #category : 'private' }
DbSchemaGraph >> basicAddForeignKey: anEdge [
	"Send only with the lock held."

	(self basicEdgesFrom: anEdge fromTable) add: anEdge.
	anEdge fromTable = anEdge toTable ifFalse:
		[(self basicEdgesFrom: anEdge toTable) add: anEdge].
	^anEdge
]

{ #category : 'private' }
DbSchemaGraph >> basicReplace: anEdge with: aNewEdge [
	"Send only with the lock held."

	(self basicEdgesFrom: anEdge fromTable) remove: anEdge ifAbsent: [nil].
	(self basicEdgesFrom: anEdge toTable) remove: anEdge ifAbsent: [nil].
	self basicAddForeignKey: aNewEdge
]

{ #category : 'private' }
DbSchemaGraph >> basicEdgesFrom: aTableName [
	"The edge list for that table, made if it is not there.

	 Send only with the lock held -- and note that this READS by adding,
	 which is why it cannot be a public accessor: a caller asking an empty
	 graph what it has changes it."

	^adjacency at: aTableName asLowercase
		ifAbsent: [adjacency at: aTableName asLowercase put: OrderedCollection new]
]

{ #category : 'private' }
DbSchemaGraph >> edgeBetween: aTable and: anotherTable [
	^lock critical: [self basicEdgeBetween: aTable and: anotherTable]
]

{ #category : 'private' }
DbSchemaGraph >> basicEdgeBetween: aTable and: anotherTable [
	"Send only with the lock held."

	(adjacency at: aTable asLowercase ifAbsent: [^nil]) do:
		[:each |
		(each fromTable = aTable asLowercase
			and: [each toTable = anotherTable asLowercase])
				ifTrue: [^each]].
	^nil
]

{ #category : 'accessing' }
DbSchemaGraph >> tableNames [
	^lock critical: [adjacency keys]
]

{ #category : 'accessing' }
DbSchemaGraph >> includesTable: aTableName [
	^lock critical: [adjacency includesKey: aTableName asLowercase]
]

{ #category : 'accessing' }
DbSchemaGraph >> foreignKeysOf: aTableName [
	"An Array, and therefore a copy: a caller holding my own edge list could
	 read it while another process is adding to it."

	^lock critical: [
		(adjacency at: aTableName asLowercase
			ifAbsent: [OrderedCollection new]) asArray]
]

{ #category : 'accessing' }
DbSchemaGraph >> isEmpty [
	^lock critical: [adjacency isEmpty]
]

{ #category : 'pathfinding' }
DbSchemaGraph >> joinPathFor: tableNames root: rootTableOrNil [
	"The edges that connect every named table, starting from the root.

	 Answers an empty collection for one table, which is not a failure --
	 a query against one table needs no joins at all.  Raises when two
	 tables have no path between them, because the alternative is a
	 CROSS JOIN nobody asked for and a result set the size of the product."

	| tables root connected joinEdges remaining best path |
	tables := tableNames collect: [:each | each asLowercase].
	tables isEmpty ifTrue: [^OrderedCollection new].
	tables size = 1 ifTrue: [^OrderedCollection new].
	root := (rootTableOrNil notNil
			and: [tables includes: rootTableOrNil asLowercase])
		ifTrue: [rootTableOrNil asLowercase]
		ifFalse: [tables asArray first].

	connected := OrderedCollection with: root.
	joinEdges := OrderedCollection new.
	remaining := (tables reject: [:each | each = root]) asOrderedCollection.

	"The whole search under one lock, and not a lock per lookup.

	 A join path is only an answer if the graph did not change while it was
	 being found: half a path through the schema as it was and half through
	 the schema as it became is a set of joins that connects nothing in
	 particular, and it would be produced without any error.  The search is
	 bounded by the size of the schema and runs no block of the caller's, so
	 holding the lock across it costs a query that is being built at the same
	 moment on another core a short wait and nothing else.

	 Everything inside sends basicShortestPathFrom:to:, never the public one:
	 Mutex is not re-entrant and detects re-entry by raising, so a send back
	 through the guarded door would report a deadlock rather than deadlock --
	 which is better, and is still a bug."

	^lock critical: [
		[remaining isEmpty] whileFalse:
			[best := nil.
			 remaining do:
				[:target |
				path := self basicShortestPathFrom: connected to: target.
				(path notNil and: [best isNil or: [path size < best size]])
					ifTrue: [best := path]].
			 best isNil ifTrue:
				[^DbError signal: 'no foreign key path connects ',
					remaining asArray printString, ' to ',
					connected asArray printString].
			 best do:
				[:edge |
				(joinEdges includes: edge) ifFalse: [joinEdges add: edge].
				(connected includes: edge fromTable)
					ifFalse: [connected add: edge fromTable].
				(connected includes: edge toTable)
					ifFalse: [connected add: edge toTable]].
			 remaining := (remaining reject: [:each | connected includes: each])
				asOrderedCollection].
		joinEdges]
]

{ #category : 'pathfinding' }
DbSchemaGraph >> shortestPathFrom: sourceTables to: targetTable [
	"The guarded door.  joinPathFor:root: does not come through it -- it
	 already holds the lock -- and nothing else in this package sends this at
	 all; it is here because a caller with a graph in hand may reasonably
	 ask."

	^lock critical: [self basicShortestPathFrom: sourceTables to: targetTable]
]

{ #category : 'private' }
DbSchemaGraph >> basicShortestPathFrom: sourceTables to: targetTable [
	"Breadth first from every source at once, answering the edges to the
	 target, or nil if it cannot be reached.

	 From every source AT ONCE rather than one search per source: the
	 question is how far the target is from the tree already built, and N
	 separate searches would answer how far it is from each table
	 separately and then have to compare them.  Seeding the queue with all
	 of them makes the first arrival the right answer by construction."

	| parentEdge parentTable queue current neighbour path node |
	(sourceTables includes: targetTable) ifTrue: [^OrderedCollection new].
	parentEdge := Dictionary new.
	parentTable := Dictionary new.
	queue := OrderedCollection new.
	sourceTables do:
		[:each |
		queue addLast: each.
		parentTable at: each put: nil].
	[queue isEmpty] whileFalse:
		[current := queue removeFirst.
		 (adjacency at: current ifAbsent: [#()]) do:
			[:edge |
			neighbour := edge otherTable: current.
			(parentTable includesKey: neighbour) ifFalse:
				[parentTable at: neighbour put: current.
				 parentEdge at: neighbour put: edge.
				 neighbour = targetTable ifTrue:
					[path := OrderedCollection new.
					 node := targetTable.
					 [parentEdge includesKey: node] whileTrue:
						[path addFirst: (parentEdge at: node).
						 node := parentTable at: node].
					 ^path].
				 queue addLast: neighbour]]].
	^nil
]

{ #category : 'printing' }
DbSchemaGraph >> printOn: aStream [
	| tables |
	tables := lock critical: [adjacency size].
	aStream nextPutAll: 'a DbSchemaGraph('; print: tables;
		nextPutAll: ' tables)'
]

{ #category : 'storing' }
DbSchemaGraph >> storeOn: aStream [
	"One line per edge, so that a schema read once can be kept.

	 A plain text format rather than an object one: the file is meant to be
	 readable, editable and diffable, because the case it exists for is an
	 application that reads the schema once and then wants to add the
	 foreign keys the database was never told about.

	 Lines end with nl and NOT with Character cr.  In this image cr is 13,
	 because that is what it was on the Alto and the 1983 source means it;
	 nl is 10, which is what a Unix file wants.  Writing one and reading the
	 other makes the whole file a single line, and a graph read back that
	 way has one malformed edge in it instead of all of them."

	| written tables edgesOf |
	written := Set new.
	"A snapshot first, so that the file is the graph as it was at one instant
	 rather than a walk through one that is still being added to."
	lock critical: [
		tables := adjacency keys asSortedCollection.
		edgesOf := Dictionary new.
		tables do: [:each | edgesOf at: each put: (adjacency at: each) asArray]].
	tables do:
		[:table |
		(edgesOf at: table) do:
			[:edge |
			(written includes: edge) ifFalse:
				[written add: edge.
				 aStream nextPutAll: edge fromTable; nextPut: $:.
				 edge fromColumns doWithIndex:
					[:each :i | i > 1 ifTrue: [aStream nextPut: $,].
						aStream nextPutAll: each].
				 aStream nextPutAll: '->'; nextPutAll: edge toTable; nextPut: $:.
				 edge toColumns doWithIndex:
					[:each :i | i > 1 ifTrue: [aStream nextPut: $,].
						aStream nextPutAll: each].
				 aStream nl]]]
]

{ #category : 'storing' }
DbSchemaGraph class >> readFrom: aStream [
	"Read back what storeOn: wrote."

	| graph line |
	graph := self new.
	[aStream atEnd] whileFalse:
		[line := aStream upTo: Character nl.
		 line isEmpty ifFalse: [graph addLine: line]].
	^graph
]

{ #category : 'private' }
DbSchemaGraph >> addLine: aLine [
	| arrow left right |
	arrow := aLine indexOfSubCollection: '->' startingAt: 1.
	arrow = 0 ifTrue: [^DbError signal: 'not a schema line: ', aLine].
	left := aLine copyFrom: 1 to: arrow - 1.
	right := aLine copyFrom: arrow + 2 to: aLine size.
	^self
		from: (self tableOf: left) columns: (self columnsOf: left)
		to: (self tableOf: right) columns: (self columnsOf: right)
]

{ #category : 'private' }
DbSchemaGraph >> tableOf: aString [
	^aString copyFrom: 1 to: (aString indexOf: $:) - 1
]

{ #category : 'private' }
DbSchemaGraph >> columnsOf: aString [
	| text names stream |
	text := aString copyFrom: (aString indexOf: $:) + 1 to: aString size.
	names := OrderedCollection new.
	stream := WriteStream on: (String new: 16).
	text do:
		[:each |
		each = $,
			ifTrue: [names add: stream contents.
				 stream := WriteStream on: (String new: 16)]
			ifFalse: [stream nextPut: each]].
	stream contents isEmpty ifFalse: [names add: stream contents].
	^names asArray
]
