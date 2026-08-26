"
Every expression in the manual whose answer the book prints.

This is not decoration.  A manual whose examples were typed rather than run
is a manual that is wrong somewhere and cannot tell you where, so each one
is a doctest here and `make verify' in this directory runs the lot against
a live image:

    ./st80 -bootstrap -profile profiles/st2026.profile \
           -doctests manual/examples/ManualExamples.class.st

The notation is Pharo's own: `expression >>> expected value' inside a method
comment, checked by evaluating (expression) = (expected).  The runner tells
three outcomes apart -- passed, WRONG (the method is here and disagrees, which
is a bug in the book or in the system), and could-not-run (a selector this
image has not got).  All three matter and only the first is acceptable here.

One constraint shapes how these are written: a doctest is a single
EXPRESSION, so anything needing a temporary is wrapped in a block and
evaluated -- ([:n | ... ] value: 0) rather than | n | ... .  The book shows
the readable form; this file shows the checkable one.  Where the two differ
the difference is only that wrapping.

The methods are named after the chapter they check, so a failure names the
page to fix.
"
Class {
	#name : 'ManualExamples',
	#superclass : 'Object',
	#category : 'Manual'
}

{ #category : 'ch04 syntax' }
ManualExamples >> literals [
	"16rFF >>> 255"
	"16r1F >>> 31"
	"2r1011 >>> 11"
	"1.5e2 >>> 150.0"
	"1.5e-2 >>> 0.015"
	"3factorial >>> 6"
	"$a asciiValue >>> 97"
	"$a asInteger >>> 97"
	"$a value >>> $a"
	"'it''s' size >>> 4"
	"#at:put: numArgs >>> 2"
	"#+ numArgs >>> 1"
	"#(1 $a 'str' #sym (2 3)) size >>> 5"
	"#(foo bar) first class >>> Symbol"
	"#(true false nil) first class >>> Symbol"
	"{ true. false. nil } first >>> true"
	"#[1 2 255] size >>> 3"
	"#[1 2 255] at: 2 >>> 2"
	"#(#[1 2] #[3 4]) first class >>> ByteArray"
	"{ 1. 2 + 3 } >>> #(1 5)"
	"the book says 100 factorial is 158 digits, and it is -- but that one does
	 not fit the doctest runner's reduced bytecode budget, so a smaller
	 factorial stands in for it here"
	"50 factorial numberOfDigits >>> 65"
	^self
]

{ #category : 'ch04 syntax' }
ManualExamples >> messagesAndCascades [
	"3 + 4 * 2 >>> 14"
	"3 + (4 * 2) >>> 11"
	"2 raisedTo: 3 + 1 >>> 16"
	"5 between: 1 and: 10 >>> true"
	"(OrderedCollection new add: 1; add: 2; yourself) size >>> 2"
	"(OrderedCollection new add: 1; add: 2) >>> 2"
	^self
]

{ #category : 'ch05 objects' }
ManualExamples >> objectsAndMessages [
	"3 class >>> SmallInteger"
	"3 class class >>> SmallInteger class"
	"true class >>> True"
	"nil class >>> UndefinedObject"
	"[:x | x] class >>> BlockClosure"
	"...but only in the closures dialect; a doit compiled Blue Book answers BlockContext"
	"Object class class >>> Metaclass"
	"Object superclass >>> nil"
	"SmallInteger superclass >>> Integer"
	"('abc' = 'abc') >>> true"
	"('abc' == 'abc') >>> false"
	"(#abc == #abc) >>> true"
	"(3 = 3.0) >>> true"
	"(3 isKindOf: Number) >>> true"
	"(3 isMemberOf: SmallInteger) >>> true"
	"(3 respondsTo: #printString) >>> true"
	"(Integer inheritsFrom: Number) >>> true"
	"([nil foo] on: MessageNotUnderstood do: [:e | e message selector]) >>> #foo"
	"(nil ifNil: [7]) >>> 7"
	"(Smalltalk at: #Object) >>> Object"
	^self
]

{ #category : 'ch05 objects' }
ManualExamples >> printing [
	"#foo printString >>> 'foo'"
	"#(1 2) printString >>> '(1 2 )'"
	"(3/4) printString >>> '(3/4)'"
	"'abc' printString size >>> 5"
	"'abc' displayString size >>> 3"
	"3 printString >>> '3'"
	"$a printString >>> '$a'"
	"nil printString >>> 'nil'"
	"(#a -> 1) printString >>> 'a->1'"
	"(OrderedCollection withAll: #(1 2)) printString >>> 'an OrderedCollection(1 2 )'"
	"#[1 2] printString >>> 'a ByteArray(1 2 )'"
	^self
]

{ #category : 'ch06 blocks' }
ManualExamples >> blocks [
	"[3 + 4] value >>> 7"
	"([:x | x * x] value: 9) >>> 81"
	"([:a :b | a + b] value: 1 value: 2) >>> 3"
	"([:x :y | x + y] valueWithArguments: #(3 4)) >>> 7"
	"[:x | x] numArgs >>> 1"
	"([:x | | doubled | doubled := x * 2. doubled + 1] value: 3) >>> 7"
	"[ | t | t := 10. t squared ] value >>> 100"
	"([3] ensure: [4]) >>> 3"
	^self
]

{ #category : 'ch06 blocks' }
ManualExamples >> closuresCapture [
	"a captured variable is shared with the enclosing scope, and outlives it"
	"([:start | | n bump | n := start. bump := [n := n + 1]. bump value. bump value. n] value: 0) >>> 2"
	"a counter factory: each call makes an independent n"
	"([:mk | | a b | a := mk value. b := mk value. a value. a value. b value] value: [ [:s | | n | n := s. [n := n + 1]] value: 0 ]) >>> 1"
	^self
]

{ #category : 'ch07 control flow' }
ManualExamples >> controlFlow [
	"((3 > 2) ifTrue: ['y'] ifFalse: ['n']) >>> 'y'"
	"(true and: [false]) >>> false"
	"(false or: [true]) >>> true"
	"(true & false) >>> false"
	"([:n | 1 to: 4 do: [:i | n := n + i]. n] value: 0) >>> 10"
	"([:n | 4 timesRepeat: [n := n + 2]. n] value: 0) >>> 8"
	"([:n | [n < 5] whileTrue: [n := n + 1]. n] value: 0) >>> 5"
	"((1 to: 10) inject: 0 into: [:a :b | a + b]) >>> 55"
	^self
]

{ #category : 'ch10 exceptions' }
ManualExamples >> exceptions [
	"([1/0] on: ZeroDivide do: [:e | 'caught']) >>> 'caught'"
	"([Error signal: 'x'] on: Error do: [:e | e messageText]) >>> 'x'"
	"([Error new signal: 'y'] on: Error do: [:e | e return: 5]) >>> 5"
	"([Error signal] on: Error do: [:e | e class]) >>> Error"
	"([Warning signal: 'w'] on: Warning do: [:e | e resume: 9]) >>> 9"
	"([#(1 2) at: 5] on: Error do: [:e | 'oob']) >>> 'oob'"
	"description is the class's sentence, NOT the message text"
	"([Error signal: 'a'] on: Error do: [:e | e description]) >>> 'An error has occurred'"
	"([Error signal: 'a'] on: Error do: [:e | e messageText]) >>> 'a'"
	"ZeroDivide is caught by a handler on Error, being a subclass"
	"([1/0] on: Error do: [:e | e class]) >>> ZeroDivide"
	"retry, with a counter, until the third attempt"
	"([:n | [n := n + 1. n < 3 ifTrue: [Error signal]. n] on: Error do: [:e | e retry]] value: 0) >>> 3"
	"ensure: runs on the way out of a non-local return too"
	"([:flag | [[true] value] ensure: [flag := true]] value: false) >>> true"
	^self
]

{ #category : 'ch11 beyond the blue book' }
ManualExamples >> extensions [
	"traits are flattened at load time; the class's own method wins"
	"Greeter new greeting >>> 'hello from a class'"
	"Greeter defaultGreeting >>> 'hello'"
	"the loader synthesises ^super new initialize for a loaded class that needs it"
	"Initialized new count >>> 1"
	"pragmas are kept on the compiled method and readable back"
	"(Unwind class >> #annotated) pragmas size >>> 3"
	"((Unwind class >> #annotated) hasPragma: #shared:) >>> true"
	"((Unwind class >> #annotated) pragmaAt: #deprecated:) arguments first >>> 'use #normal'"
	"((Unwind class >> #annotated) pragmaAt: #shared:) keyword >>> #shared:"
	^self
]

{ #category : 'ch12 numbers' }
ManualExamples >> integers [
	"10 factorial >>> 3628800"
	"(2 raisedTo: 10) >>> 1024"
	"(2 raisedTo: 100) printString >>> '1267650600228229401496703205376'"
	"(2 raisedTo: 100) class >>> LargePositiveInteger"
	"(0 - (2 raisedTo: 70)) class >>> LargeNegativeInteger"
	"(1000000 * 1000000) >>> 1000000000000"
	"(12 gcd: 18) >>> 6"
	"(4 lcm: 6) >>> 12"
	"(2 ** 8) >>> 256"
	"7 isPrime >>> true"
	"4 isPrime >>> false"
	"5 odd >>> true"
	"5 even >>> false"
	"3 sign >>> 1"
	"0 sign >>> 0"
	"-5 abs >>> 5"
	"5 negated >>> -5"
	"(3 max: 9) >>> 9"
	"5 printStringPadded: 3 >>> '005'"
	^self
]

{ #category : 'ch12 numbers' }
ManualExamples >> division [
	"7 // 2 >>> 3"
	"7 \\ 2 >>> 1"
	"(7 rem: 2) >>> 1"
	"(7 quo: 2) >>> 3"
	"-7 // 2 >>> -4"
	"-7 \\ 2 >>> 1"
	"(-7 quo: 2) >>> -3"
	"(-7 rem: 2) >>> -1"
	"(7 / 2) >>> (7/2)"
	^self
]

{ #category : 'ch12 numbers' }
ManualExamples >> fractionsAndFloats [
	"((1/3) + (1/6)) >>> (1/2)"
	"(1/4) numerator >>> 1"
	"(1/4) denominator >>> 4"
	"(3/4) asFloat >>> 0.75"
	"(1/10) + (2/10) = (3/10) >>> true"
	"0.1 + 0.2 = 0.3 >>> false"
	"(1/3) asFloat printString >>> '0.3333333333333333'"
	"9 sqrt >>> 3.0"
	"(10 log: 10) >>> 1.0"
	"100 ln >>> 4.605170185988092"
	"3.7 rounded >>> 4"
	"3.7 truncated >>> 3"
	"3.7 floor >>> 3"
	"3.7 ceiling >>> 4"
	"-3.7 floor >>> -4"
	"-3.7 truncated >>> -3"
	"(3.7 roundTo: 0.5) >>> 3.5"
	"Float pi truncated >>> 3"
	"(1 + 2.5) >>> 3.5"
	"((1/2) + 0.5) >>> 1.0"
	"(1 + (1/2)) >>> (3/2)"
	^self
]

{ #category : 'ch12 numbers' }
ManualExamples >> intervalsAndPoints [
	"(1 to: 5) asArray >>> #(1 2 3 4 5)"
	"(1 to: 10 by: 3) asArray >>> #(1 4 7 10)"
	"(1 to: 5) size >>> 5"
	"((1 to: 3) collect: [:i | i * i]) >>> #(1 4 9)"
	"((1 to: 3) collect: [:i | i * i]) class >>> Array"
	"(3 @ 4) x >>> 3"
	"((3 @ 4) + (1 @ 1)) >>> (4 @ 5)"
	"((3 @ 4) * 2) >>> (6 @ 8)"
	^self
]

{ #category : 'ch13 collections' }
ManualExamples >> theFiveWorkhorses [
	"(#(1 2 3) collect: [:x | x * 10]) >>> #(10 20 30)"
	"(#(1 2 3 4) select: [:x | x even]) >>> #(2 4)"
	"(#(1 2 3 4) reject: [:x | x even]) >>> #(1 3)"
	"(#(1 2 3) detect: [:x | x > 1]) >>> 2"
	"(#(1 2 3) detect: [:x | x > 9] ifNone: [0]) >>> 0"
	"(#(1 2 3) inject: 0 into: [:a :b | a + b]) >>> 6"
	"(#('a' 'b') inject: '' into: [:acc :each | acc , each]) >>> 'ab'"
	^self
]

{ #category : 'ch13 collections' }
ManualExamples >> collectionProtocol [
	"#(1 2 3) size >>> 3"
	"#(1 2 3) isEmpty >>> false"
	"#() notEmpty >>> false"
	"(#(1 2 3) includes: 2) >>> true"
	"#(1 2 3) sum >>> 6"
	"(#(1 2 3) sum: [:x | x * 2]) >>> 12"
	"#(3 1 2) max >>> 3"
	"(#(1 2 3 4) count: [:x | x odd]) >>> 2"
	"(#(2 4) allSatisfy: [:x | x even]) >>> true"
	"(#(1 2) anySatisfy: [:x | x even]) >>> true"
	"(#(#(1 2) #(3)) flatCollect: [:x | x]) >>> #(1 2 3)"
	"(#(1 2 1) copyWithout: 1) >>> #(2)"
	"(#(1 2) , #(3)) >>> #(1 2 3)"
	"#(1 2 3) reverse >>> #(3 2 1)"
	"#(1 2 3) sorted >>> #(1 2 3)"
	"(#(3 1 2) sorted: [:a :b | a > b]) >>> #(3 2 1)"
	"(#() ifEmpty: ['none']) >>> 'none'"
	"(#(1 2 3) first: 2) >>> #(1 2)"
	"#(1 2 3) allButFirst >>> #(2 3)"
	"#(1 2 3) last >>> 3"
	"(#(1 2 3) indexOf: 2) >>> 2"
	"(#(1 2 3) copyFrom: 2 to: 3) >>> #(2 3)"
	^self
]

{ #category : 'ch13 collections' }
ManualExamples >> collectionKinds [
	"(Set withAll: #(1 1 2)) size >>> 2"
	"(Bag withAll: #(1 1 2)) size >>> 3"
	"((Bag withAll: #(1 1 2)) occurrencesOf: 1) >>> 2"
	"#(3 1 2) asSortedCollection asArray >>> #(1 2 3)"
	"(((SortedCollection sortBlock: [:a :b | a > b]) addAll: #(1 3 2); yourself) asArray) >>> #(3 2 1)"
	"#(1 2 3) asOrderedCollection removeFirst >>> 1"
	"(OrderedCollection withAll: #(1 2 3)) removeLast >>> 3"
	"(OrderedCollection new add: 2; addFirst: 1; yourself) first >>> 1"
	"(Array with: 1 with: 2) >>> #(1 2)"
	"(Array new: 3) size >>> 3"
	"(Array new: 3) first >>> nil"
	^self
]

{ #category : 'ch13 collections' }
ManualExamples >> dictionaries [
	"(((Dictionary new) at: #a put: 1; yourself) at: #a) >>> 1"
	"((Dictionary new) at: #z ifAbsent: [99]) >>> 99"
	"((Dictionary new) at: #z ifAbsentPut: [3]) >>> 3"
	"(((Dictionary new) at: #a put: 1; yourself) includesKey: #a) >>> true"
	"removeKey: answers the ASSOCIATION, not the value"
	"(((Dictionary new) at: #a put: 1; yourself) removeKey: #a) >>> (#a -> 1)"
	"(((Dictionary new) at: #a put: 1; yourself) keys) class >>> Set"
	"(((Dictionary new) at: #a put: 1; yourself) keyAtValue: 1) >>> #a"
	"(#a -> 1) key >>> #a"
	"(#a -> 1) value >>> 1"
	^self
]

{ #category : 'ch14 streams' }
ManualExamples >> readStreams [
	"(ReadStream on: #(1 2 3)) next >>> 1"
	"([:s | s next. s peek] value: (ReadStream on: #(1 2 3))) >>> 2"
	"([:s | s skip: 2. s next] value: (ReadStream on: #(1 2 3))) >>> 3"
	"(ReadStream on: 'hello') upToEnd >>> 'hello'"
	"([:s | s next: 2] value: (ReadStream on: #(1 2 3))) >>> #(1 2)"
	"([:s | s next. s atEnd] value: (ReadStream on: #(1))) >>> true"
	"([:s | s next. s reset. s next] value: (ReadStream on: #(7 8))) >>> 7"
	^self
]

{ #category : 'ch14 streams' }
ManualExamples >> writeStreams [
	"((WriteStream on: (String new: 0)) nextPutAll: 'ab'; contents) >>> 'ab'"
	"((WriteStream on: (String new: 0)) nextPut: $a; nextPutAll: 'bcd'; contents) >>> 'abcd'"
	"((WriteStream on: (String new: 0)) print: 42; contents) >>> '42'"
	"((WriteStream with: 'ab') nextPutAll: 'cd'; contents) >>> 'abcd'"
	"((ReadWriteStream on: (String new: 0)) nextPutAll: 'hi'; reset; upToEnd) >>> 'hi'"
	"the loop the book shows for building a string"
	"([:s | 1 to: 5 do: [:i | s print: i; nextPut: $ ]. s contents] value: (WriteStream on: (String new: 0))) >>> '1 2 3 4 5 '"
	^self
]

{ #category : 'ch15 text' }
ManualExamples >> strings [
	"'hello' asUppercase >>> 'HELLO'"
	"'HELLO' asLowercase >>> 'hello'"
	"'hello' reversed >>> 'olleh'"
	"'hello' sorted >>> 'ehllo'"
	"('hello' indexOf: $l) >>> 3"
	"('hello' copyFrom: 2 to: 4) >>> 'ell'"
	"('abcabc' copyReplaceAll: 'b' with: 'X') >>> 'aXcaXc'"
	"('hello' , ' ' , 'world') >>> 'hello world'"
	"'hello' size >>> 5"
	"'hello' asSymbol >>> #hello"
	"#hello asString >>> 'hello'"
	"there is no one-keyword indexOfSubCollection:; these are the two that exist"
	"('hello world' indexOfSubCollection: 'world' startingAt: 1) >>> 7"
	"('hello world' findString: 'world' startingAt: 1) >>> 7"
	"('hello' includesSubstring: 'ell') >>> true"
	"('hello' startsWith: 'he') >>> true"
	^self
]

{ #category : 'ch15 text' }
ManualExamples >> characters [
	"$a asciiValue >>> 97"
	"$a asInteger >>> 97"
	"(Character value: 97) >>> $a"
	"$a asUppercase >>> $A"
	"$a isVowel >>> true"
	"$a isLetter >>> true"
	"$5 digitValue >>> 5"
	"Character cr asciiValue >>> 13"
	"Character tab asciiValue >>> 9"
	"Character space asciiValue >>> 32"
	^self
]

{ #category : 'ch24 concurrency' }
ManualExamples >> concurrencyClasses [
	"(Semaphore forMutualExclusion critical: [7]) >>> 7"
	"(Mutex new critical: [7]) >>> 7"
	"Mutex new isHeld >>> false"
	"(Monitor new critical: [7]) >>> 7"
	"Monitor new hasWaiters >>> false"
	"SharedQueue new isEmpty >>> true"
	"(((SharedQueue new) nextPut: 10; nextPut: 20; yourself) next) >>> 10"
	"((SharedQueue new) nextOrNil) >>> nil"
	^self
]

{ #category : 'ch25 processes' }
ManualExamples >> processes [
	"Processor activePriority >>> 4"
	"the eight priorities, by the names ProcessorScheduler answers to"
	"Processor timingPriority >>> 8"
	"Processor highIOPriority >>> 7"
	"Processor lowIOPriority >>> 6"
	"Processor userInterruptPriority >>> 5"
	"Processor userSchedulingPriority >>> 4"
	"Processor userBackgroundPriority >>> 3"
	"Processor systemBackgroundPriority >>> 2"
	"Processor activeProcess class >>> Process"
	"[1] newProcess class >>> Process"
	"(Processor yield) >>> Processor"
	"([:d | d wait] value: (Delay forMilliseconds: 1)) class >>> Delay"
	^self
]

{ #category : 'ch26 parallel' }
ManualExamples >> parallel [
	"a Promise carries the value back"
	"(Processor forkParallel: [(1 to: 100) inject: 0 into: [:a :b | a + b]]) value >>> 5050"
	"and carries the failure back too, rather than losing it"
	"(Processor forkParallel: [1/0]) valueOrNil >>> nil"
	"([:p | p value. p isSettled] value: (Processor forkParallel: [1])) >>> true"
	"map-parallel answers the results in order"
	"(Processor parallelCollect: #(1 2 3) do: [:x | x * x]) >>> #(1 4 9)"
	"(Processor parallelCollect: (1 to: 4) do: [:x | x]) asArray >>> #(1 2 3 4)"
	^self
]

{ #category : 'ch17 database' }
ManualExamples >> databaseWithoutADatabase [
	"The half of Chapter 17 that needs no database: the SQL the query builder
	 generates, the join path it finds, and the exactness of a DECIMAL.

	 That half is most of what the chapter claims, which is the point --
	 a generator's job is to produce one particular string, and a test that
	 ran the query would need a database to say what checking the string
	 says directly.  The connection, cursor and record examples cannot be
	 here: they need a driver this machine may not have, and are covered by
	 lib/Database-Live-Tests under its own profile."
	"a DECIMAL column is exact, and the image's own reader is not"
	"(Odbc numberFromDigits: '0.10') * 10 >>> 1"
	"((Odbc numberFromDigits: '0.10') isKindOf: Fraction) >>> true"
	"('0.10' asNumber * 10) >>> 1.0"
	"(Odbc numberFromDigits: '123.45') >>> 2469/20"
	"the join is found from the columns, not written by hand"
	"([:g | g from: 'employee' column: 'department_id' to: 'department' column: 'department_id'. ((DbQueryBuilder onGraph: g table: 'employee') select: 'employee.last_name'; select: 'department.name'; build)] value: DbSchemaGraph new) >>> 'SELECT employee.last_name, department.name FROM employee JOIN department ON employee.department_id = department.department_id'"
	"and through a table the query never named"
	"([:g | g from: 'employee' column: 'employee_id' to: 'assignment' column: 'employee_id'; from: 'assignment' column: 'project_id' to: 'project' column: 'project_id'. ((DbQueryBuilder onGraph: g table: 'employee') select: 'employee.last_name'; where: 'project.project_id = ?' with: #(17); build)] value: DbSchemaGraph new) >>> 'SELECT employee.last_name FROM employee JOIN assignment ON employee.employee_id = assignment.employee_id JOIN project ON assignment.project_id = project.project_id WHERE project.project_id = ?'"
	"a OR group is parenthesised, and its values keep their order"
	"([:q | q where: 'employee.active = ?' with: #(1); startOr; where: 'employee.last_name = ?' with: #('Hopper'); where: 'employee.last_name = ?' with: #('Lovelace'); endGroup; build] value: (DbQueryBuilder onGraph: DbSchemaGraph new table: 'employee')) >>> 'SELECT * FROM employee WHERE employee.active = ? AND (employee.last_name = ? OR employee.last_name = ?)'"
	"([:q | q where: 'employee.active = ?' with: #(1); startOr; where: 'employee.last_name = ?' with: #('Hopper'); where: 'employee.last_name = ?' with: #('Lovelace'); endGroup; parameters] value: (DbQueryBuilder onGraph: DbSchemaGraph new table: 'employee')) >>> #(1 'Hopper' 'Lovelace')"
	"an IN list binds every value; an empty one is not a syntax error"
	"([:q | q where: 'employee.employee_id' in: #(1 2 3); build] value: (DbQueryBuilder onGraph: DbSchemaGraph new table: 'employee')) >>> 'SELECT * FROM employee WHERE employee.employee_id IN (?, ?, ?)'"
	"([:q | q where: 'employee.employee_id' in: #(); build] value: (DbQueryBuilder onGraph: DbSchemaGraph new table: 'employee')) >>> 'SELECT * FROM employee WHERE 1 = 0'"
	"a composite foreign key joins on both columns"
	"([:g | g from: 'order_line' columns: #('order_id' 'product_id') to: 'order_product' columns: #('order_id' 'product_id'). ((g joinPathFor: #('order_line' 'order_product') root: 'order_line') first onConditionFrom: nil to: nil)] value: DbSchemaGraph new) >>> 'order_line.order_id = order_product.order_id AND order_line.product_id = order_product.product_id'"
	"a build with no ODBC still answers rather than failing to understand"
	"(Odbc isAvailable isKindOf: Boolean) >>> true"
	^self
]

{ #category : 'ch18 json' }
ManualExamples >> json [
	"Chapter 18, which needs nothing this machine may not have -- a document
	 is a String, so every claim the chapter makes is checkable here.

	 The doubled quotes are Smalltalk's, not JSON's: a doctest lives inside a
	 method comment and a comment ends at the first lone double quote, so
	 every quote in a JSON document has to be written twice.  What the reader
	 of the book sees is the single-quoted form, and the two differ only in
	 that."
	"asJson and asJsonString are not opposites, and both are on String"
	"'[1,2]' asJson class >>> JSONArray"
	"'[1,2]' asJsonString >>> '""[1,2]""'"
	"reading, by name and with the type checked"
	"('{""name"": ""Ada"", ""born"": 1815}' asJson) stringAt: 'name' >>> 'Ada'"
	"('{""name"": ""Ada"", ""born"": 1815}' asJson) integerAt: 'born' >>> 1815"
	"(('{""tags"": [""maths""]}' asJson arrayAt: 'tags') stringAt: 1) >>> 'maths'"
	"('{""a"":[1,2]}' asJson arrayAt: 'a') asArray >>> #(1 2)"
	"the wrong type raises rather than converting"
	"([:o | [o stringAt: 'n'. 'accepted'] on: JSONError do: [:e | 'refused']] value: '{""n"":42}' asJson) >>> 'refused'"
	"an absent name answers nil, and null is not the same as absent"
	"('{""middleName"": null}' asJson at: 'nickname') >>> nil"
	"('{""middleName"": null}' asJson includesKey: 'middleName') >>> true"
	"('{""middleName"": null}' asJson isNullAt: 'middleName') >>> true"
	"('{""middleName"": null}' asJson includesKey: 'nickname') >>> false"
	"('{""middleName"": null}' asJson isNullAt: 'nickname') >>> false"
	"writing: anything at all can be asked for its JSON"
	"#(1 2 3) asJsonString >>> '[1,2,3]'"
	"(1 to: 3) asJsonString >>> '[1,2,3]'"
	"'hello' asJsonString >>> '""hello""'"
	"(Dictionary new at: 'k' put: 1; yourself) asJsonString >>> '{""k"":1}'"
	"names keep the order they were first put in"
	"([:o | o at: 'zebra' put: 1; at: 'apple' put: 2; asJsonString] value: JSONObject new) >>> '{""zebra"":1,""apple"":2}'"
	"the indented form is one member to a line"
	"(('{""a"":1}' asJson asJsonStringIndent: 2) lines size) >>> 3"
	"numbers are exact"
	"'0.1' asJson * 10 >>> 1"
	"'1.5' asJson >>> 3/2"
	"'1.5' asJson asJsonString >>> '1.5'"
	"'3.0' asJson >>> 3"
	"('3.0' asJson isKindOf: Integer) >>> true"
	"'1e2' asJson >>> 100"
	"'123456789012345678901234567890' asJson >>> 123456789012345678901234567890"
	"and a Float is what you ask for, not what you are given"
	"('{""rate"": 0.1}' asJson floatAt: 'rate') >>> 0.1"
	"the six documents the chapter says are refused, and they are"
	"([:t | [t asJson. 'accepted'] on: JSONError do: [:e | 'refused']] value: '{""a"":1,}') >>> 'refused'"
	"([:t | [t asJson. 'accepted'] on: JSONError do: [:e | 'refused']] value: '{a:1}') >>> 'refused'"
	"([:t | [t asJson. 'accepted'] on: JSONError do: [:e | 'refused']] value: '{''a'':1}') >>> 'refused'"
	"([:t | [t asJson. 'accepted'] on: JSONError do: [:e | 'refused']] value: '01') >>> 'refused'"
	"([:t | [t asJson. 'accepted'] on: JSONError do: [:e | 'refused']] value: 'banana') >>> 'refused'"
	"([:t | [t asJson. 'accepted'] on: JSONError do: [:e | 'refused']] value: '{""a"":1} {""b"":2}') >>> 'refused'"
	"and the message says where"
	"([:t | [t asJson. ''] on: JSONError do: [:e | e messageText]] value: '{""a"":1,""b"":}') >>> 'a value cannot begin with $} at character 12'"
	"an escape becomes UTF-8 bytes, so e-acute is two of them"
	"'""\u00e9""' asJson size >>> 2"
	"'""\u0041""' asJson >>> 'A'"
	"building a document converts as it goes, and refuses what has no spelling"
	"([:r | r at: 'ok' put: true; at: 'rows' put: #(1 2 3); at: 'error' put: nil; asJsonString] value: JSONObject new) >>> '{""ok"":true,""rows"":[1,2,3],""error"":null}'"
	"([:r | [r at: 'a' put: 1@2. 'accepted'] on: JSONError do: [:e | 'refused']] value: JSONObject new) >>> 'refused'"
	"an array answers to enough of the collection protocol to be useful"
	"('[1,2,3,4]' asJson select: [:each | each odd]) asJsonString >>> '[1,3]'"
	"('[1,2,3,4]' asJson inject: 0 into: [:a :b | a + b]) >>> 10"
	"('[1,2,3]' asJson asArray) >>> #(1 2 3)"
	"(('[1,2]' asJson collect: [:each | each class]) isKindOf: OrderedCollection) >>> true"
	"and an index past the end raises, because an array has a length"
	"([:a | [a at: 9. 'accepted'] on: JSONError do: [:e | 'refused']] value: '[1]' asJson) >>> 'refused'"
	"a stream can be read one document at a time"
	"([:p | p nextValue. p nextValue] value: (JSONParser on: (ReadStream on: '[1] [2]'))) asArray >>> #(2)"
	^self
]

{ #category : 'ch19 serving' }
ManualExamples >> serving [
	"Chapter 19 without a socket: the codec, the request parser on text, a
	 response, a uuid, and what the service loader refuses by name.  The
	 server itself is checked where it can listen, in
	 lib/JSON-RPC-Server-Tests and lib/Rest-Server-Tests; a doctest is one
	 line and a listener is not."
	"the codec: percent-encoding, a query string, a content type by extension"
	"(HttpCodec percentDecode: 'two+words%21') >>> 'two words!'"
	"((HttpCodec parseQuery: 'a=1&b=two+words') at: 'b') >>> 'two words'"
	"((HttpCodec parseQuery: 'flag&a=1') at: 'flag') >>> ''"
	"(HttpCodec mimeTypeForName: 'index.HTML') >>> 'text/html'"
	"(HttpCodec mimeTypeForName: 'photo.png') >>> 'image/png'"
	"(HttpCodec mimeTypeForName: 'no-extension') >>> 'application/octet-stream'"
	"(HttpCodec reasonFor: 404) >>> 'Not Found'"
	"a request parsed from text: the path decoded, the query taken apart, headers by name in any case"
	"([:nl | (HttpRequest fromString: 'GET /a%20b?x=1&y=two+words HTTP/1.1', nl, 'Host: here', nl, nl) path] value: (String with: (Character value: 13) with: (Character value: 10))) >>> '/a b'"
	"([:nl | (HttpRequest fromString: 'GET /a%20b?x=1&y=two+words HTTP/1.1', nl, 'Host: here', nl, nl) parameterAt: 'y'] value: (String with: (Character value: 13) with: (Character value: 10))) >>> 'two words'"
	"([:nl | (HttpRequest fromString: 'GET / HTTP/1.1', nl, 'Host: here', nl, nl) headerAt: 'HOST'] value: (String with: (Character value: 13) with: (Character value: 10))) >>> 'here'"
	"([:nl | (HttpRequest fromString: 'GET / HTTP/1.1', nl, 'Host: here', nl, nl) headerAt: 'cookie'] value: (String with: (Character value: 13) with: (Character value: 10))) >>> nil"
	"([:nl | (HttpRequest fromString: 'POST /rest HTTP/1.1', nl, 'Content-Length: 5', nl, nl, 'hello') body] value: (String with: (Character value: 13) with: (Character value: 10))) >>> 'hello'"
	"and a body with no declared length is refused, with 411"
	"([:nl | [HttpRequest fromString: 'POST / HTTP/1.1', nl, 'Transfer-Encoding: chunked', nl, nl, '5', nl, 'hello'. 0] on: HttpError do: [:e | e status]] value: (String with: (Character value: 13) with: (Character value: 10))) >>> 411"
	"a response is 200 until told otherwise"
	"HttpResponse new status >>> 200"
	"(HttpResponse error: 404 reason: 'not here') status >>> 404"
	"(HttpResponse json: '{}') headerAt: 'Content-Type' >>> 'application/json'"
	"a session id is a version-4 uuid, thirty-six characters with a 4 where the version goes"
	"RestUuid new size >>> 36"
	"(RestUuid new at: 15) >>> $4"
	"(RestUuid new = RestUuid new) >>> false"
	"a service is named by its file, and only a name reaches the file system"
	"((RestServiceLoader on: 'backend') pathFor: 'services.Adder') >>> 'backend/services/Adder.class.st'"
	"((RestServiceLoader on: 'backend') pathFor: '../../etc/passwd') >>> nil"
	"((RestServiceLoader on: 'backend') pathFor: '') >>> nil"
	"login is required when the configuration says so, and otherwise whenever there is a database"
	"RestServer new requiresAuthentication >>> false"
	"(RestServer new database: 'DSN=shop'; yourself) requiresAuthentication >>> true"
	"(RestServer new database: 'DSN=shop'; requireAuthentication: false; yourself) requiresAuthentication >>> false"
	"([:s | s allowWithoutAuthentication: 'services.Catalog' method: 'list'. s allowsWithoutAuthentication: 'services.Catalog' method: 'list'] value: RestServer new) >>> true"
	"sockets by hand, on the loopback interface: a listener on a port the system picks, a client, a byte each way"
	"([:l | [:c | [:s | s send: 'hi'. [c receiveUpTo: 2] ensure: [s close. c close. l close]] value: l accept] value: (Socket connectTo: '127.0.0.1' port: l localPort)] value: (ServerSocket listenOn: 0)) >>> 'hi'"
	"([:l | [:c | [:s | (SocketStream on: s) nextPutAll: 'one'; nextPut: (Character value: 10); nextPutAll: 'two'; flush. [(SocketStream on: c) nextLine] ensure: [s close. c close. l close]] value: l accept] value: (Socket connectTo: '127.0.0.1' port: l localPort)] value: (ServerSocket listenOn: 0)) >>> 'one'"
	"([:l | [:c | [:s | s close. [c receiveUpTo: 10] ensure: [c close. l close]] value: l accept] value: (Socket connectTo: '127.0.0.1' port: l localPort)] value: (ServerSocket listenOn: 0)) >>> ''"
	"([Socket connectTo: '127.0.0.1' port: 1. 'connected'] on: NetError do: [:e | 'refused']) >>> 'refused'"
	"the user inside a service: who they are, and what the application keeps beside them"
	"(RestUserData username: 'ada' password: 'x' userId: 1) username >>> 'ada'"
	"(RestUserData username: 'ada' password: 'x' userId: 1) userId >>> 1"
	"([:u | u at: #cart put: 3. u at: #cart] value: (RestUserData username: 'ada' password: 'x' userId: 1)) >>> 3"
	"the error tiers and their codes"
	"([RestUserError code: 7 signal: 'no such product'] on: RestError do: [:e | e errorCode]) >>> 7"
	"([RestLogError new signal: 'stock is low'] on: RestError do: [:e | e errorCode]) >>> -1"
	"([RestLoginRequiredError new signal: 'login required'] on: RestError do: [:e | e errorCode]) >>> 2"
	"([RestUserError code: 7 signal: 'no'] on: RestError do: [:e | e messageText]) >>> 'no'"
	^self
]

{ #category : 'ch27 packages' }
ManualExamples >> packages [
	"Chapter 27: a class read from a Tonel file by the running image, and
	 written back out.  The text stands in for the file."
	"(TonelReader loadString: 'Class { #name : ''DoctestWidget'', #superclass : ''Object'', #category : ''Doctests'' }' named: 'DoctestWidget.class.st') name >>> 'DoctestWidget'"
	"(TonelReader loadString: 'Class { #name : ''DoctestWidget'', #superclass : ''Object'', #category : ''Doctests'' }' named: 'DoctestWidget.class.st') superclass >>> Object"
	"((TonelWriter sourceFor: (TonelReader loadString: 'Class { #name : ''DoctestWidget'', #superclass : ''Object'', #category : ''Doctests'' }' named: 'DoctestWidget.class.st')) includesSubstring: '#name : ''DoctestWidget''') >>> true"
	"and text that is not a class file is a TonelError, not a notifier"
	"([TonelReader loadString: 'Class { #name : ' named: 'Broken.class.st'. 'accepted'] on: TonelError do: [:e | 'refused']) >>> 'refused'"
	^self
]
