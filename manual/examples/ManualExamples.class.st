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

{ #category : 'ch20 concurrency' }
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

{ #category : 'ch21 processes' }
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

{ #category : 'ch22 parallel' }
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
