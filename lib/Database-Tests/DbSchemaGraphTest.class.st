"
The join graph, asserted from what a join path has to be rather than from
what this implementation happens to produce.

None of these need a database.  The graph is built by hand, which is the
whole point of it being buildable by hand: the pathfinding is the part most
likely to be wrong and the part a database contributes nothing to testing.
"
Class {
	#name : 'DbSchemaGraphTest',
	#superclass : 'TestCase',
	#category : 'Database-Tests'
}

{ #category : 'private' }
DbSchemaGraphTest >> chain [
	"employee -> department, employee -> assignment -> project.

	 Deliberately NOT a star: reaching project from department has to go
	 through two other tables, which is the case a one-hop implementation
	 passes by accident."

	^DbSchemaGraph new
		from: 'employee' column: 'department_id' to: 'department' column: 'department_id';
		from: 'employee' column: 'employee_id' to: 'assignment' column: 'employee_id';
		from: 'assignment' column: 'project_id' to: 'project' column: 'project_id';
		yourself
]

{ #category : 'tests' }
DbSchemaGraphTest >> testOneTableNeedsNoJoins [
	self assert: (self chain joinPathFor: (Array with: 'employee') root: 'employee')
		isEmpty
]

{ #category : 'tests' }
DbSchemaGraphTest >> testAdjacentTables [
	| path |
	path := self chain joinPathFor: #('employee' 'department') root: 'employee'.
	self assert: path size equals: 1.
	self assert: path first fromTable equals: 'employee'.
	self assert: path first toTable equals: 'department'
]

{ #category : 'tests' }
DbSchemaGraphTest >> testPathThroughAnUnnamedTable [
	"employee and project are two hops apart and assignment is neither of
	 them.  The path has to include it, which is the whole reason a graph
	 is better than a list of joins somebody wrote down."

	| path tables |
	path := self chain joinPathFor: #('employee' 'project') root: 'employee'.
	self assert: path size equals: 2.
	tables := Set new.
	path do: [:each | tables add: each fromTable; add: each toTable].
	self assert: (tables includes: 'assignment')
]

{ #category : 'tests' }
DbSchemaGraphTest >> testUnreachableTableRaises [
	| graph |
	graph := self chain.
	graph from: 'planet' column: 'star_id' to: 'star' column: 'star_id'.
	self should: [graph joinPathFor: #('employee' 'star') root: 'employee']
		raise: DbError
]

{ #category : 'tests' }
DbSchemaGraphTest >> testEdgeIsWalkableBothWays [
	"A key declared on employee is as much a way of reaching employee from
	 department as the reverse.  A graph that recorded only the declaring
	 end would find half the joins that exist."

	| path |
	path := self chain joinPathFor: #('department' 'project') root: 'department'.
	self assert: path size equals: 3
]

{ #category : 'tests' }
DbSchemaGraphTest >> testCompositeKey [
	| graph key |
	graph := DbSchemaGraph new
		from: 'order_line' columns: #('order_id' 'product_id')
		to: 'order_product' columns: #('order_id' 'product_id');
		yourself.
	key := (graph joinPathFor: #('order_line' 'order_product') root: 'order_line')
		first.
	self assert: key isComposite.
	self assert: (key onConditionFrom: nil to: nil) equals:
		'order_line.order_id = order_product.order_id AND order_line.product_id = order_product.product_id'
]

{ #category : 'tests' }
DbSchemaGraphTest >> testMismatchedCompositeKeyRefused [
	self should:
		[DbForeignKey from: 'a' columns: #('x' 'y') to: 'b' columns: #('x')]
		raise: DbError
]

{ #category : 'tests' }
DbSchemaGraphTest >> testNamesAreFoldedOnce [
	"A graph half of whose names came from a driver that upper-cases and
	 half from a program that did not would have two nodes for one table."

	| graph |
	graph := DbSchemaGraph new
		from: 'EMPLOYEE' column: 'DEPARTMENT_ID' to: 'Department' column: 'department_id';
		yourself.
	self assert: (graph includesTable: 'employee').
	self assert: (graph includesTable: 'DEPARTMENT').
	self assert: (graph joinPathFor: #('employee' 'department') root: 'employee')
		size equals: 1
]

{ #category : 'tests' }
DbSchemaGraphTest >> testSelfJoin [
	| graph key |
	graph := DbSchemaGraph new
		from: 'employee' column: 'manager_id' to: 'employee' column: 'employee_id';
		yourself.
	key := (graph foreignKeysOf: 'employee') first.
	self assert: (key otherTable: 'employee') equals: 'employee'.
	"Recorded once and not twice: both ends are the same table, and adding
	 it to its own adjacency list twice would walk it twice."
	self assert: (graph foreignKeysOf: 'employee') size equals: 1
]

{ #category : 'tests' }
DbSchemaGraphTest >> testStoreAndReadBack [
	| written read |
	written := WriteStream on: (String new: 100).
	self chain storeOn: written.
	read := DbSchemaGraph readFrom: (ReadStream on: written contents).
	self assert: (read joinPathFor: #('employee' 'project') root: 'employee')
		size equals: 2
]

{ #category : 'tests' }
DbSchemaGraphTest >> testAliasedOnCondition [
	| key |
	key := DbForeignKey from: 'employee' column: 'manager_id'
		to: 'employee' column: 'employee_id'.
	self assert: (key onConditionFrom: 'e' to: 'm') equals:
		'e.manager_id = m.employee_id'
]
