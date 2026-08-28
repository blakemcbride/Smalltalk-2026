"
Strings, Symbols, Characters and Streams.
"
Class {
	#name : 'St80TextTest',
	#superclass : 'TestCase',
	#category : 'Library-Tests'
}

{ #category : 'strings' }
St80TextTest >> testStringBasics [
	self assert: 'hello' size equals: 5.
	self assert: 'hello' asUppercase equals: 'HELLO'.
	self assert: 'HELLO' asLowercase equals: 'hello'.
	self assert: 'hello' reversed equals: 'olleh'.
	self assert: ('hello' copyFrom: 2 to: 4) equals: 'ell'.
	self assert: ('hello' indexOf: $l) equals: 3.
	self assert: ('hello' , ' ' , 'world') equals: 'hello world'.
	self assert: ('hello' at: 1) equals: $h.
	self assert: ('abcabc' copyReplaceAll: 'b' with: 'X') equals: 'aXcaXc'
]

{ #category : 'strings' }
St80TextTest >> testStringsCompareByValue [
	self assert: 'abc' = 'abc' copy.
	self deny: 'abc' == 'abc' copy.
	self assert: ('abc' < 'abd').
	self assert: 'abc' hash equals: 'abc' copy hash
]

{ #category : 'strings' }
St80TextTest >> testStringsAreMutable [
	| s |
	s := 'hello' copy.
	s at: 1 put: $H.
	self assert: s equals: 'Hello'.
	self should: [s at: 99 put: $x] raise: Error
]

{ #category : 'symbols' }
St80TextTest >> testSymbolsAreUnique [
	self assert: #hello == #hello.
	self assert: 'hello' asSymbol == #hello.
	self assert: #hello asString equals: 'hello'.
	self assert: #at:put: numArgs equals: 2.
	self assert: #+ numArgs equals: 1.
	self assert: #hello numArgs equals: 0
]

{ #category : 'characters' }
St80TextTest >> testCharacters [
	self assert: $a asciiValue equals: 97.
	self assert: (Character value: 97) equals: $a.
	self assert: (Character value: 97) == $a.
	self assert: $a asUppercase equals: $A.
	self assert: $a isVowel.
	self assert: $a isLetter.
	self deny: $1 isLetter.
	self assert: $5 digitValue equals: 5.
	self assert: ($a < $b).
	self assert: Character cr asciiValue equals: 13.
	self assert: Character tab asciiValue equals: 9.
	self assert: Character space asciiValue equals: 32
]

{ #category : 'streams' }
St80TextTest >> testReadStream [
	| s |
	s := ReadStream on: #(1 2 3 4).
	self assert: s next equals: 1.
	self assert: s peek equals: 2.
	self assert: s next equals: 2.
	self assert: (s next: 2) equals: #(3 4).
	self assert: s atEnd.
	self assert: s next equals: nil.
	s reset.
	self assert: s next equals: 1
]

{ #category : 'streams' }
St80TextTest >> testReadStreamOnString [
	| s |
	s := ReadStream on: 'hello world'.
	self assert: (s upTo: $ ) equals: 'hello'.
	self assert: s upToEnd equals: 'world'.
	self assert: s atEnd
]

{ #category : 'streams' }
St80TextTest >> testWriteStream [
	| s |
	s := WriteStream on: (String new: 0).
	s nextPut: $a.
	s nextPutAll: 'bc'.
	s print: 42.
	self assert: s contents equals: 'abc42'
]

{ #category : 'streams' }
St80TextTest >> testWriteStreamGrowsPastItsInitialSize [
	| s |
	s := WriteStream on: (String new: 0).
	1 to: 500 do: [:i | s nextPut: $x].
	self assert: s contents size equals: 500
]

{ #category : 'streams' }
St80TextTest >> testWriteStreamWithAppends [
	self assert: ((WriteStream with: 'ab') nextPutAll: 'cd'; contents)
		equals: 'abcd'.
	self assert: ((WriteStream on: 'ab' copy) nextPutAll: 'cd'; contents)
		equals: 'cd'
]

{ #category : 'streams' }
St80TextTest >> testReadWriteStream [
	| s |
	s := ReadWriteStream on: (String new: 0).
	s nextPutAll: 'hi'.
	s reset.
	self assert: s upToEnd equals: 'hi'
]

{ #category : 'printing' }
St80TextTest >> testPrintString [
	self assert: 3 printString equals: '3'.
	self assert: 3.5 printString equals: '3.5'.
	self assert: $a printString equals: '$a'.
	self assert: nil printString equals: 'nil'.
	self assert: true printString equals: 'true'.
	self assert: 'ab' printString equals: '''ab'''.
	self assert: 'ab' displayString equals: 'ab'.
	self assert: (3/4) printString equals: '(3/4)'.
	self assert: (1/3) asFloat printString equals: '0.3333333333333333'
]

{ #category : 'printing' }
St80TextTest >> testPrintStringRoundTripsForNumbers [
	"Whatever printString says, reading it back has to give the same number."
	#(0 1 -1 42 1000000) do: [:n |
		self assert: (Number readFrom: (ReadStream on: n printString))
			equals: n]
]

{ #category : 'streams' }
St80TextTest >> testStreamsOverAnOrderedCollection [
	"Every method of a ReadStream that builds a result -- upToEnd, next:,
	 upTo:, nextLine -- failed over an OrderedCollection, and a WriteStream
	 on one could not take a single nextPut:, because `OrderedCollection
	 new: n' is EMPTY and refuses at:put: past its end (Bugs3 B46)."
	| oc r w |
	oc := OrderedCollection withAll: #(1 2 3).
	self assert: oc readStream upToEnd asArray equals: #(1 2 3).
	self assert: oc readStream upToEnd class equals: OrderedCollection.
	r := ReadStream on: oc.
	self assert: (r next: 2) asArray equals: #(1 2).
	self assert: (r next: 5) asArray equals: #(3).
	self assert: r atEnd.
	self assert: (r next: 2) isEmpty.
	self assert: ((ReadStream on: oc) upTo: 2) asArray equals: #(1).
	self assert: (ReadStream on: (OrderedCollection with: $a with: Character lf with: $b))
		nextLine asArray equals: #($a).
	w := WriteStream on: OrderedCollection new.
	w nextPut: 1; nextPut: 2; nextPutAll: #(3 4).
	self assert: w contents asArray equals: #(1 2 3 4).
	self assert: w contents class equals: OrderedCollection.
	w := WriteStream with: (OrderedCollection with: 0).
	w nextPut: 1.
	self assert: w contents asArray equals: #(0 1).
	w := ReadWriteStream on: OrderedCollection new.
	w nextPut: 7; nextPut: 8; reset.
	self assert: w upToEnd asArray equals: #(7 8).
	self assert: (ReadStream on: (1 to: 5)) upToEnd equals: #(1 2 3 4 5).
	self assert: ((ReadStream on: 'hello world') next: 30) equals: 'hello world'.
	self assert: ((ReadStream on: 'hello world') next: 3) equals: 'hel'.
	self assert: (ReadStream on: #sym) upToEnd equals: 'sym'.
	self should: [(WriteStream on: SortedCollection new) nextPut: 1] raise: Error
]
