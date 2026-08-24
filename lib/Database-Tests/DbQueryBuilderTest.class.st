"
The SQL generator, asserted against the text it must produce.

Against the TEXT, deliberately, and not against what a database makes of it.
The generator's job is to write one particular string and put the parameters
in one particular order; a test that ran the query and checked the rows would
pass for a query that was accidentally right and would need a database to say
so.  What a database makes of the text is DbConnectionTest's question.
"
Class {
	#name : 'DbQueryBuilderTest',
	#superclass : 'TestCase',
	#category : 'Database-Tests'
}

{ #category : 'private' }
DbQueryBuilderTest >> graph [
	^DbSchemaGraph new
		from: 'employee' column: 'department_id' to: 'department' column: 'department_id';
		from: 'employee' column: 'employee_id' to: 'assignment' column: 'employee_id';
		from: 'assignment' column: 'project_id' to: 'project' column: 'project_id';
		yourself
]

{ #category : 'private' }
DbQueryBuilderTest >> on: aTable [
	^DbQueryBuilder onGraph: self graph table: aTable
]

{ #category : 'tests' }
DbQueryBuilderTest >> testBareSelect [
	self assert: ((self on: 'employee') build) equals: 'SELECT * FROM employee'
]

{ #category : 'tests' }
DbQueryBuilderTest >> testNamedColumnsNeedNoJoin [
	self assert: ((self on: 'employee')
		select: 'employee.first_name';
		select: 'employee.last_name';
		build)
	equals: 'SELECT employee.first_name, employee.last_name FROM employee'
]

{ #category : 'tests' }
DbQueryBuilderTest >> testJoinIsFoundFromAColumn [
	"Naming a column of a second table is the whole of what the caller does;
	 the join is the builder's answer to it."

	self assert: ((self on: 'employee')
		select: 'employee.last_name';
		select: 'department.name';
		build)
	equals: 'SELECT employee.last_name, department.name FROM employee JOIN department ON employee.department_id = department.department_id'
]

{ #category : 'tests' }
DbQueryBuilderTest >> testJoinIsFoundThroughAnUnnamedTable [
	"assignment appears in the SQL and nowhere in the query that asked for
	 it, which is the point of the graph."

	self assert: ((self on: 'employee')
		select: 'employee.last_name';
		where: 'project.project_id = ?' with: #(17);
		build)
	equals: 'SELECT employee.last_name FROM employee JOIN assignment ON employee.employee_id = assignment.employee_id JOIN project ON assignment.project_id = project.project_id WHERE project.project_id = ?'
]

{ #category : 'tests' }
DbQueryBuilderTest >> testParametersComeOutInTextOrder [
	"The order the question marks are WRITTEN, which is not the order the
	 methods were called in: the WHERE is written before the HAVING however
	 they were added."

	| q |
	q := (self on: 'employee')
		select: 'employee.department_id';
		having: 'COUNT(*) > ?' with: #(5);
		where: 'employee.salary > ?' with: #(100);
		groupBy: 'employee.department_id';
		yourself.
	self assert: q parameters equals: #(100 5)
]

{ #category : 'tests' }
DbQueryBuilderTest >> testOrGrouping [
	"`a AND (b OR c)' is not `a AND b OR c', and a flat list of conditions
	 could only produce the second."

	| q |
	q := (self on: 'employee')
		where: 'employee.active = ?' with: #(1);
		startOr;
			where: 'employee.last_name = ?' with: #('Hopper');
			where: 'employee.last_name = ?' with: #('Lovelace');
		endGroup;
		yourself.
	self assert: q build equals:
		'SELECT * FROM employee WHERE employee.active = ? AND (employee.last_name = ? OR employee.last_name = ?)'.
	self assert: q parameters equals: #(1 'Hopper' 'Lovelace')
]

{ #category : 'tests' }
DbQueryBuilderTest >> testUnmatchedEndGroupRaises [
	self should: [(self on: 'employee') endGroup] raise: DbError
]

{ #category : 'tests' }
DbQueryBuilderTest >> testInListBindsEveryValue [
	| q |
	q := (self on: 'employee')
		where: 'employee.employee_id' in: #(1 2 3);
		yourself.
	self assert: q build equals:
		'SELECT * FROM employee WHERE employee.employee_id IN (?, ?, ?)'.
	self assert: q parameters equals: #(1 2 3)
]

{ #category : 'tests' }
DbQueryBuilderTest >> testEmptyInListIsNotASyntaxError [
	"`IN ()' is a syntax error in every database, so an empty filter must
	 become a condition rather than a broken query.  IN nothing matches
	 nothing; NOT IN nothing matches everything."

	self assert: ((self on: 'employee') where: 'employee.employee_id' in: #(); build)
		equals: 'SELECT * FROM employee WHERE 1 = 0'.
	self assert: ((self on: 'employee') where: 'employee.employee_id' notIn: #(); build)
		equals: 'SELECT * FROM employee WHERE 1 = 1'
]

{ #category : 'tests' }
DbQueryBuilderTest >> testLeftJoin [
	self assert: ((self on: 'department')
		select: 'department.name';
		selectCount: 'employee.employee_id' as: 'headcount';
		leftJoin: 'employee';
		groupBy: 'department.name';
		build)
	equals: 'SELECT department.name, COUNT(employee.employee_id) AS headcount FROM department LEFT JOIN employee ON employee.department_id = department.department_id GROUP BY department.name'
]

{ #category : 'tests' }
DbQueryBuilderTest >> testAggregateArgumentIsNotASubquery [
	"COUNT(employee.employee_id) is parenthesised and is not a subquery.  A
	 scanner that skipped every parenthesised group would never see the
	 table, never join it, and answer a count over a cross product -- a
	 query that runs, returns numbers, and is wrong."

	self assert: ((self on: 'department')
		selectCount: 'employee.employee_id' as: 'headcount';
		build)
	equals: 'SELECT COUNT(employee.employee_id) AS headcount FROM department JOIN employee ON employee.department_id = department.department_id'
]

{ #category : 'tests' }
DbQueryBuilderTest >> testDistinctAndOrder [
	self assert: ((self on: 'employee')
		distinct;
		select: 'employee.last_name';
		orderByDescending: 'employee.last_name';
		build)
	equals: 'SELECT DISTINCT employee.last_name FROM employee ORDER BY employee.last_name DESC'
]

{ #category : 'tests' }
DbQueryBuilderTest >> testStringLiteralIsNotATable [
	"A condition comparing against the literal 'a.b' must not join a table
	 called a.  This is the case a regular expression over the text gets
	 wrong and a scan that knows about quotes gets right."

	self assert: ((self on: 'employee')
		where: 'employee.code = ''dept.name''' with: #();
		build)
	equals: 'SELECT * FROM employee WHERE employee.code = ''dept.name'''
]

{ #category : 'tests' }
DbQueryBuilderTest >> testSubqueryTablesAreNotJoined [
	"A subquery has its own FROM.  Joining its tables in the outer query as
	 well would multiply the rows."

	self assert: ((self on: 'employee')
		where: 'EXISTS (SELECT 1 FROM project WHERE project.owner = employee.employee_id)'
			with: #();
		build)
	equals: 'SELECT * FROM employee WHERE EXISTS (SELECT 1 FROM project WHERE project.owner = employee.employee_id)'
]

{ #category : 'tests' }
DbQueryBuilderTest >> testAlias [
	"The ON condition names the table by its ALIAS, because that is the only
	 name it has inside this query.  Writing the real name there is a
	 reference to a table the FROM clause never introduced."

	self assert: ((self on: 'employee')
		table: 'department' as: 'd';
		select: 'employee.last_name';
		select: 'd.name';
		build)
	equals: 'SELECT employee.last_name, d.name FROM employee JOIN department d ON employee.department_id = d.department_id'
]

{ #category : 'tests' }
DbQueryBuilderTest >> testExplicitJoin [
	self assert: ((self on: 'employee')
		select: 'employee.last_name';
		join: 'employee' column: 'manager_id' to: 'employee' column: 'employee_id';
		build)
	equals: 'SELECT employee.last_name FROM employee JOIN employee ON employee.manager_id = employee.employee_id'
]

{ #category : 'tests' }
DbQueryBuilderTest >> testLimitWithoutAConnection [
	"With no connection there is nothing to ask what dialect this is, so the
	 common spelling is used.  Recorded as a test because it is a choice
	 rather than an oversight."

	self assert: ((self on: 'employee') limit: 10; build)
		equals: 'SELECT * FROM employee LIMIT 10'
]

{ #category : 'tests' }
DbQueryBuilderTest >> testCommonTableExpression [
	self assert: ((self on: 'employee')
		with: 'recent' as: 'SELECT * FROM assignment WHERE started > 2020';
		select: 'employee.last_name';
		build)
	equals: 'WITH recent AS (SELECT * FROM assignment WHERE started > 2020) SELECT employee.last_name FROM employee'
]

{ #category : 'tests' }
DbQueryBuilderTest >> testNoConnectionRefusesToRun [
	self should: [(self on: 'employee') fetchAll] raise: DbError
]
