"
A query written as what you want, not as how to reach it.

	(connection newQueryBuilder: 'employee')
		select: 'employee.first_name';
		select: 'employee.last_name';
		select: 'department.name';
		where: 'project.project_id = ?' with: #(17);
		orderBy: 'employee.last_name';
		fetchAll

Nothing there says how employee reaches department, or how either reaches
project.  DbSchemaGraph does: the builder collects every table named in every
column and condition, asks the graph for the joins that connect them, and
writes them out.  Adding a column from a fourth table adds a join without
anybody editing a FROM clause -- which is the point, because a hand-written
join that is one table out of date is a query that still runs.

WHAT IS AUTOMATIC AND WHAT IS NOT.  The joins are found for you and their type
is INNER unless you say otherwise.  Say otherwise with leftJoin: and
rightJoin:, which also serve the case the graph cannot know about: a table
reachable two ways, where only you know which way this query means.

EVERY VALUE IS A BOUND PARAMETER.  Conditions are written with question marks
and their values passed beside them; nothing is pasted into the text.  The
values come out in the order the marks were written, which the WHERE tree
guarantees by carrying each condition's values with the condition rather than
in a list beside it.

WHAT THIS DOES NOT DO.  It does not parse SQL.  A condition is text you wrote
and it is copied through; what is read out of it is the `table.column' names,
by scanning, so that the joins can be found.  A condition that names a table
without qualifying a column -- `EXISTS (SELECT 1 FROM audit)' -- will not
cause audit to be joined, and should not: a subquery has its own FROM.
"
Class {
	#name : 'DbQueryBuilder',
	#superclass : 'Object',
	#instVars : [ 'connection', 'graph', 'rootTable', 'selectColumns',
	              'orderByColumns', 'groupByColumns', 'havingGroup',
	              'explicitJoins', 'joinTypes', 'aliasToTable', 'tableToAlias',
	              'distinct', 'maxRows', 'rootWhere', 'currentWhere',
	              'commonTables', 'unions' ],
	#category : 'Database'
}

{ #category : 'instance creation' }
DbQueryBuilder class >> on: aConnection table: aTableName [
	^self new setConnection: aConnection table: aTableName
]

{ #category : 'instance creation' }
DbQueryBuilder class >> onGraph: aGraph table: aTableName [
	"A builder with no connection: it can build SQL and answer parameters,
	 and cannot run anything.  For generating SQL to be run elsewhere, and
	 for testing the generator without a database."

	^self new setGraph: aGraph table: aTableName
]

{ #category : 'private' }
DbQueryBuilder >> setConnection: aConnection table: aTableName [
	connection := aConnection.
	self setGraph: aConnection schemaGraph table: aTableName
]

{ #category : 'private' }
DbQueryBuilder >> setGraph: aGraph table: aTableName [
	graph := aGraph.
	rootTable := aTableName asLowercase.
	selectColumns := OrderedCollection new.
	orderByColumns := OrderedCollection new.
	groupByColumns := OrderedCollection new.
	explicitJoins := OrderedCollection new.
	joinTypes := Dictionary new.
	aliasToTable := Dictionary new.
	tableToAlias := Dictionary new.
	commonTables := OrderedCollection new.
	unions := OrderedCollection new.
	distinct := false.
	maxRows := 0.
	rootWhere := DbWhereGroup and.
	currentWhere := rootWhere.
	havingGroup := DbWhereGroup and
]

{ #category : 'accessing' }
DbQueryBuilder >> connection [ ^connection ]

{ #category : 'accessing' }
DbQueryBuilder >> rootTable [ ^rootTable ]

{ #category : 'selecting' }
DbQueryBuilder >> select: anExpression [
	"A column, or any expression naming one.  `employee.last_name',
	 `UPPER(department.name) AS dn', `1' -- all copied through."

	selectColumns add: anExpression.
	^self
]

{ #category : 'selecting' }
DbQueryBuilder >> selectAll: someExpressions [
	someExpressions do: [:each | self select: each].
	^self
]

{ #category : 'selecting' }
DbQueryBuilder >> distinct [
	distinct := true.
	^self
]

{ #category : 'selecting' }
DbQueryBuilder >> selectCount: anExpression as: aName [
	^self select: 'COUNT(', anExpression, ') AS ', aName
]

{ #category : 'selecting' }
DbQueryBuilder >> selectSum: anExpression as: aName [
	^self select: 'SUM(', anExpression, ') AS ', aName
]

{ #category : 'selecting' }
DbQueryBuilder >> selectAverage: anExpression as: aName [
	^self select: 'AVG(', anExpression, ') AS ', aName
]

{ #category : 'selecting' }
DbQueryBuilder >> selectMinimum: anExpression as: aName [
	^self select: 'MIN(', anExpression, ') AS ', aName
]

{ #category : 'selecting' }
DbQueryBuilder >> selectMaximum: anExpression as: aName [
	^self select: 'MAX(', anExpression, ') AS ', aName
]

{ #category : 'naming' }
DbQueryBuilder >> table: aTableName as: anAlias [
	"Give a table another name for this query.

	 Needed for a self-join, where one table has to appear twice and the two
	 appearances must be told apart, and useful anywhere a name is long.
	 Both directions are recorded: the alias resolves to the real table so
	 that the graph can be asked about it, and the table resolves to the
	 alias so that the SQL can be written with it."

	aliasToTable at: anAlias asLowercase put: aTableName asLowercase.
	tableToAlias at: aTableName asLowercase put: anAlias.
	^self
]

{ #category : 'private' }
DbQueryBuilder >> resolveAlias: aName [
	aName isNil ifTrue: [^nil].
	^aliasToTable at: aName asLowercase ifAbsent: [aName asLowercase]
]

{ #category : 'private' }
DbQueryBuilder >> displayNameOf: aTableName [
	^tableToAlias at: aTableName asLowercase ifAbsent: [aTableName]
]

{ #category : 'conditions' }
DbQueryBuilder >> where: aCondition [
	^self where: aCondition with: #()
]

{ #category : 'conditions' }
DbQueryBuilder >> where: aCondition with: anArrayOfValues [
	"A condition, and the values its question marks want.

	 The condition is text and is copied through; the values are bound.  So
	 `where: ''salary > ?'' with: (Array with: 50000)' is safe with any
	 value, and `where: ''salary > '', amount printString' is not -- which
	 is why every method here that takes a value takes it separately."

	currentWhere add: aCondition with: anArrayOfValues.
	^self
]

{ #category : 'conditions' }
DbQueryBuilder >> startOr [
	"Open a group whose conditions are joined by OR."

	currentWhere := currentWhere addGroup: DbWhereGroup or.
	^self
]

{ #category : 'conditions' }
DbQueryBuilder >> startAnd [
	currentWhere := currentWhere addGroup: DbWhereGroup and.
	^self
]

{ #category : 'conditions' }
DbQueryBuilder >> endGroup [
	"Close the innermost group.  Closing more than were opened is refused
	 rather than silently ignored: it means the code that built this query
	 does not agree with itself about its own shape, and every condition
	 after it would land in the wrong place."

	currentWhere parent isNil ifTrue:
		[^DbError signal: 'endGroup without a matching startOr or startAnd'].
	currentWhere := currentWhere parent.
	^self
]

{ #category : 'conditions' }
DbQueryBuilder >> where: aColumn in: aCollection [
	"An IN list, as that many bound parameters.

	 As parameters and not as a pasted list, which is what makes it safe for
	 values that came from outside.  An empty collection becomes a condition
	 that is false rather than `IN ()', which is a syntax error in every
	 database and would turn an empty filter into a broken query."

	^self where: aColumn in: aCollection negated: false
]

{ #category : 'conditions' }
DbQueryBuilder >> where: aColumn notIn: aCollection [
	^self where: aColumn in: aCollection negated: true
]

{ #category : 'private' }
DbQueryBuilder >> where: aColumn in: aCollection negated: negated [
	| values stream |
	values := aCollection asOrderedCollection.
	values isEmpty ifTrue:
		["IN nothing matches nothing; NOT IN nothing matches everything."
		 ^self where: (negated ifTrue: ['1 = 1'] ifFalse: ['1 = 0']) with: #()].
	stream := WriteStream on: (String new: 64).
	stream nextPutAll: aColumn.
	stream nextPutAll: (negated ifTrue: [' NOT IN ('] ifFalse: [' IN (']).
	1 to: values size do:
		[:i | i > 1 ifTrue: [stream nextPutAll: ', ']. stream nextPut: $?].
	stream nextPut: $).
	^self where: stream contents with: values asArray
]

{ #category : 'conditions' }
DbQueryBuilder >> whereExists: aSubquery [
	^self whereExists: aSubquery negated: false
]

{ #category : 'conditions' }
DbQueryBuilder >> whereNotExists: aSubquery [
	^self whereExists: aSubquery negated: true
]

{ #category : 'private' }
DbQueryBuilder >> whereExists: aSubquery negated: negated [
	"aSubquery is another builder.  Its SQL goes inside the EXISTS and its
	 parameters go in at that point in the order -- which is why the
	 condition and its values are added together and not in two steps."

	| text |
	text := (negated ifTrue: ['NOT EXISTS ('] ifFalse: ['EXISTS (']),
		aSubquery build, ')'.
	^self where: text with: aSubquery parameters
]

{ #category : 'grouping' }
DbQueryBuilder >> groupBy: anExpression [
	groupByColumns add: anExpression.
	^self
]

{ #category : 'grouping' }
DbQueryBuilder >> having: aCondition [
	^self having: aCondition with: #()
]

{ #category : 'grouping' }
DbQueryBuilder >> having: aCondition with: anArrayOfValues [
	havingGroup add: aCondition with: anArrayOfValues.
	^self
]

{ #category : 'ordering' }
DbQueryBuilder >> orderBy: anExpression [
	orderByColumns add: anExpression.
	^self
]

{ #category : 'ordering' }
DbQueryBuilder >> orderByDescending: anExpression [
	orderByColumns add: anExpression, ' DESC'.
	^self
]

{ #category : 'ordering' }
DbQueryBuilder >> limit: anInteger [
	maxRows := anInteger.
	^self
]

{ #category : 'joining' }
DbQueryBuilder >> leftJoin: aTableName [
	"Reach this table with a LEFT JOIN rather than an inner one, however the
	 graph finds it."

	joinTypes at: aTableName asLowercase put: 'LEFT JOIN'.
	^self
]

{ #category : 'joining' }
DbQueryBuilder >> rightJoin: aTableName [
	joinTypes at: aTableName asLowercase put: 'RIGHT JOIN'.
	^self
]

{ #category : 'joining' }
DbQueryBuilder >> join: fromTable column: fromColumn to: toTable column: toColumn [
	^self join: fromTable columns: (Array with: fromColumn)
		to: toTable columns: (Array with: toColumn) type: 'JOIN' alias: nil
]

{ #category : 'joining' }
DbQueryBuilder >> leftJoin: fromTable column: fromColumn to: toTable column: toColumn [
	^self join: fromTable columns: (Array with: fromColumn)
		to: toTable columns: (Array with: toColumn) type: 'LEFT JOIN' alias: nil
]

{ #category : 'joining' }
DbQueryBuilder >> join: fromTable columns: fromColumns to: toTable columns: toColumns type: aType alias: anAliasOrNil [
	"A join written out by hand, for a relationship the graph does not have
	 -- or has, and this query wants a different one of."

	explicitJoins add: (Array
		with: aType
		with: (DbForeignKey from: fromTable columns: fromColumns
				to: toTable columns: toColumns)
		with: anAliasOrNil).
	anAliasOrNil notNil ifTrue: [self table: toTable as: anAliasOrNil].
	^self
]

{ #category : 'common table expressions' }
DbQueryBuilder >> with: aName as: aBuilderOrString [
	"A WITH clause.  Takes another builder, or raw SQL text."

	commonTables add: aName -> aBuilderOrString.
	^self
]

{ #category : 'unions' }
DbQueryBuilder >> union: aBuilder [
	unions add: 'UNION' -> aBuilder.
	^self
]

{ #category : 'unions' }
DbQueryBuilder >> unionAll: aBuilder [
	unions add: 'UNION ALL' -> aBuilder.
	^self
]

{ #category : 'building' }
DbQueryBuilder >> build [
	"The SQL.  Answer parameters for the values that go with it -- and ask
	 for them AFTER this, because building is what puts them in order."

	| stream |
	stream := WriteStream on: (String new: 256).
	self writeOn: stream parameters: (OrderedCollection new).
	^stream contents
]

{ #category : 'building' }
DbQueryBuilder >> parameters [
	"The values for the question marks, in the order the marks appear.

	 Built by writing the SQL, because that is the only order that can be
	 right: a list kept alongside would have to be reordered by hand every
	 time a clause moved, and would be wrong the first time somebody
	 forgot."

	| stream values |
	stream := WriteStream on: (String new: 256).
	values := OrderedCollection new.
	self writeOn: stream parameters: values.
	^values asArray
]

{ #category : 'private' }
DbQueryBuilder >> writeOn: aStream parameters: values [
	| joins |
	self writeCommonTablesOn: aStream parameters: values.
	aStream nextPutAll: 'SELECT '.
	distinct ifTrue: [aStream nextPutAll: 'DISTINCT '].
	selectColumns isEmpty
		ifTrue: [aStream nextPut: $*]
		ifFalse:
			[selectColumns doWithIndex:
				[:each :i |
				i > 1 ifTrue: [aStream nextPutAll: ', '].
				aStream nextPutAll: each]].
	aStream nextPutAll: ' FROM '; nextPutAll: rootTable.
	(tableToAlias includesKey: rootTable) ifTrue:
		[aStream nextPut: $ ; nextPutAll: (tableToAlias at: rootTable)].
	joins := self joinsNeeded.
	self writeJoins: joins on: aStream.
	rootWhere isEmpty ifFalse:
		[aStream nextPutAll: ' WHERE '.
		 rootWhere writeOn: aStream parameters: values].
	groupByColumns isEmpty ifFalse:
		[aStream nextPutAll: ' GROUP BY '.
		 groupByColumns doWithIndex:
			[:each :i |
			i > 1 ifTrue: [aStream nextPutAll: ', '].
			aStream nextPutAll: each]].
	havingGroup isEmpty ifFalse:
		[aStream nextPutAll: ' HAVING '.
		 havingGroup writeOn: aStream parameters: values].
	unions do:
		[:each |
		aStream nextPut: $ ; nextPutAll: each key; nextPut: $ .
		each value writeOn: aStream parameters: values].
	orderByColumns isEmpty ifFalse:
		[aStream nextPutAll: ' ORDER BY '.
		 orderByColumns doWithIndex:
			[:each :i |
			i > 1 ifTrue: [aStream nextPutAll: ', '].
			aStream nextPutAll: each]].
	maxRows > 0 ifTrue: [self writeLimitOn: aStream]
]

{ #category : 'private' }
DbQueryBuilder >> writeCommonTablesOn: aStream parameters: values [
	commonTables isEmpty ifTrue: [^self].
	aStream nextPutAll: 'WITH '.
	commonTables doWithIndex:
		[:each :i |
		i > 1 ifTrue: [aStream nextPutAll: ', '].
		aStream nextPutAll: each key; nextPutAll: ' AS ('.
		(each value isKindOf: DbQueryBuilder)
			ifTrue: [each value writeOn: aStream parameters: values]
			ifFalse: [aStream nextPutAll: each value].
		aStream nextPut: $)].
	aStream nextPutAll: ' '
]

{ #category : 'private' }
DbQueryBuilder >> writeLimitOn: aStream [
	"How a database spells `only this many rows'.

	 Three spellings and no standard one anybody implements the same way.
	 SQL Server puts it at the front, Oracle before 12c had no syntax at all
	 and used ROWNUM in the WHERE clause, and the rest say LIMIT.  Asking the
	 connection what it is talking to is the only way to be right, which is
	 why a builder with no connection refuses rather than guesses."

	| kind |
	kind := connection isNil ifTrue: [#unknown] ifFalse: [connection kind].
	kind == #Oracle ifTrue:
		[^aStream nextPutAll: ' FETCH FIRST '; print: maxRows;
			nextPutAll: ' ROWS ONLY'].
	kind == #SQLServer ifTrue:
		[^aStream nextPutAll: ' OFFSET 0 ROWS FETCH NEXT '; print: maxRows;
			nextPutAll: ' ROWS ONLY'].
	aStream nextPutAll: ' LIMIT '; print: maxRows
]

{ #category : 'private' }
DbQueryBuilder >> writeJoins: someKeys on: aStream [
	"Write each join, naming the table it reaches and the condition.

	 Which end is `reached' is decided by which end is already in the query:
	 a path found from the root outwards arrives at each table once, and the
	 ON condition has to be written from the side already joined."

	| joined table type |
	joined := Set new.
	joined add: rootTable.
	explicitJoins do:
		[:each |
		| key |
		key := each at: 2.
		aStream nextPut: $ ; nextPutAll: (each at: 1); nextPut: $ .
		aStream nextPutAll: key toTable.
		(each at: 3) notNil ifTrue: [aStream nextPut: $ ; nextPutAll: (each at: 3)].
		aStream nextPutAll: ' ON '; nextPutAll: (key
			onConditionFrom: (self displayNameOf: key fromTable)
			to: ((each at: 3) isNil
				ifTrue: [self displayNameOf: key toTable]
				ifFalse: [each at: 3])).
		joined add: key toTable].
	someKeys do:
		[:key |
		table := (joined includes: key fromTable)
			ifTrue: [key toTable]
			ifFalse: [key fromTable].
		(joined includes: table) ifFalse:
			[type := joinTypes at: table ifAbsent: ['JOIN'].
			 aStream nextPut: $ ; nextPutAll: type; nextPut: $ .
			 aStream nextPutAll: table.
			 (tableToAlias includesKey: table) ifTrue:
				[aStream nextPut: $ ; nextPutAll: (tableToAlias at: table)].
			 aStream nextPutAll: ' ON '; nextPutAll: (key
				onConditionFrom: (self displayNameOf: key fromTable)
				to: (self displayNameOf: key toTable)).
			 joined add: table]]
]

{ #category : 'private' }
DbQueryBuilder >> joinsNeeded [
	"The foreign keys connecting every table this query mentions.

	 Explicitly joined tables are excluded from the search: the caller has
	 already said how to reach them, and asking the graph as well would
	 join them twice."

	| tables explicit |
	graph isNil ifTrue: [^OrderedCollection new].
	tables := self tablesMentioned.
	explicit := Set new.
	explicitJoins do: [:each | explicit add: (each at: 2) toTable].
	tables := tables reject: [:each | explicit includes: each].
	^graph joinPathFor: tables root: rootTable
]

{ #category : 'private' }
DbQueryBuilder >> tablesMentioned [
	"Every table named as `table.column' anywhere in this query, plus the
	 root, with aliases resolved to real names."

	| tables |
	tables := OrderedCollection new.
	tables add: rootTable.
	selectColumns do: [:each | self addTablesIn: each to: tables].
	orderByColumns do: [:each | self addTablesIn: each to: tables].
	groupByColumns do: [:each | self addTablesIn: each to: tables].
	self addTablesInGroup: rootWhere to: tables.
	self addTablesInGroup: havingGroup to: tables.
	^tables
]

{ #category : 'private' }
DbQueryBuilder >> addTablesInGroup: aGroup to: aCollection [
	aGroup children do:
		[:each |
		(each isKindOf: DbWhereGroup)
			ifTrue: [self addTablesInGroup: each to: aCollection]
			ifFalse: [self addTablesIn: each key to: aCollection]]
]

{ #category : 'private' }
DbQueryBuilder >> addTablesIn: anExpression to: aCollection [
	"Scan for `name . name' and keep the first name of each pair.

	 By hand rather than by pattern, because there is no pattern matcher
	 here and because the hand-written scan can do two things a pattern
	 cannot.

	 It skips what is inside a quoted string, so that a condition comparing
	 against the literal 'dept.name' does not join a table called dept.

	 And it skips SUBQUERIES, by looking inside each parenthesised group for
	 the word SELECT -- not by skipping parentheses, which is the obvious
	 implementation and is wrong.  `COUNT(employee.employee_id)' is
	 parenthesised and is not a subquery, and skipping it means the table
	 whose column is being counted is never joined; the query then compiles,
	 runs, and answers a count over a cross product.  A subquery has its own
	 FROM and its tables genuinely must not be joined again out here."

	| index size character name |
	index := 1.
	size := anExpression size.
	[index <= size] whileTrue:
		[character := anExpression at: index.
		 character = $'
			ifTrue:
				["Step over a string literal."
				 index := index + 1.
				 [index <= size and: [(anExpression at: index) ~= $']] whileTrue:
					[index := index + 1].
				 index := index + 1]
			ifFalse:
				[character = $(
					ifTrue:
						[| close |
						 close := self matchingParenIn: anExpression from: index.
						 (self isSubquery: (anExpression copyFrom: index to: close))
							ifTrue: [index := close + 1]
							ifFalse: [index := index + 1]]
					ifFalse:
						[(character isLetter or: [character = $_])
							ifTrue:
								[name := WriteStream on: (String new: 16).
								 [index <= size and:
									[| each |
									 each := anExpression at: index.
									 each isLetter or: [each isDigit or: [each = $_]]]]
										whileTrue:
											[name nextPut: (anExpression at: index).
											 index := index + 1].
								 (index <= size and: [(anExpression at: index) = $.])
									ifTrue:
										[| table |
										 table := self resolveAlias: name contents.
										 (aCollection includes: table)
											ifFalse: [aCollection add: table].
										 index := index + 1]]
							ifFalse: [index := index + 1]]]]
]

{ #category : 'private' }
DbQueryBuilder >> matchingParenIn: anExpression from: anIndex [
	"The position of the parenthesis closing the one at anIndex, or the end
	 of the text if it is never closed.

	 Quotes are respected on the way, so a close parenthesis inside a string
	 literal does not end the group early."

	| depth index size character |
	depth := 0.
	index := anIndex.
	size := anExpression size.
	[index <= size] whileTrue:
		[character := anExpression at: index.
		 character = $'
			ifTrue:
				[index := index + 1.
				 [index <= size and: [(anExpression at: index) ~= $']] whileTrue:
					[index := index + 1]]
			ifFalse:
				[character = $( ifTrue: [depth := depth + 1].
				 character = $) ifTrue:
					[depth := depth - 1.
					 depth = 0 ifTrue: [^index]]].
		 index := index + 1].
	^size
]

{ #category : 'private' }
DbQueryBuilder >> isSubquery: aString [
	"Whether a parenthesised group is a subquery rather than a function call
	 or an ordinary grouping."

	| upper |
	upper := aString asUppercase.
	^(upper indexOfSubCollection: 'SELECT' startingAt: 1) > 0
]

{ #category : 'querying' }
DbQueryBuilder >> query [
	"A cursor over the rows."

	| sql |
	connection isNil ifTrue:
		[^DbError signal: 'this builder has no connection, so it can build ',
			'SQL and cannot run it'].
	sql := self build.
	^connection query: sql with: self parameters
]

{ #category : 'querying' }
DbQueryBuilder >> fetchAll [
	^self query fetchAll
]

{ #category : 'querying' }
DbQueryBuilder >> fetchOne [
	^self query fetchOne
]

{ #category : 'querying' }
DbQueryBuilder >> do: aBlock [
	^self query do: aBlock
]

{ #category : 'printing' }
DbQueryBuilder >> printOn: aStream [
	aStream nextPutAll: self build
]
