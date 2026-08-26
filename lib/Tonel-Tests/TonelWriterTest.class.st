"
The writer, and the round trip: a class written and read back is the same
class, method for method and character for character.
"
Class {
	#name : 'TonelWriterTest',
	#superclass : 'TestCase',
	#category : 'Tonel-Tests'
}

{ #category : 'running' }
TonelWriterTest >> tearDown [
	(Smalltalk includesKey: #TonelTestSample) ifTrue: [
		TonelSource forget: (Smalltalk at: #TonelTestSample).
		(Smalltalk at: #TonelTestSample) removeFromSystem]
]

{ #category : 'private' }
TonelWriterTest >> makeSample [
	| cr |
	cr := String with: Character cr.
	^TonelReader loadString: '"', cr, 'The sample.', cr, '"', cr,
		'Class { #name : ''TonelTestSample'', #superclass : ''Object'', #instVars : [ ''a'', ''b'' ], #classVars : [ ''Count'' ], #category : ''Tonel-Tests'' }', cr,
		'{ #category : ''accessing'' }', cr,
		'TonelTestSample >> a [', cr, '	^a', cr, ']', cr,
		'{ #category : ''accessing'' }', cr,
		'TonelTestSample >> a: x [', cr, '	a := x', cr, ']', cr,
		'{ #category : ''instance creation'' }', cr,
		'TonelTestSample class >> with: x [', cr, '	^self new a: x', cr, ']', cr
		named: 'sample'
]

{ #category : 'tests' }
TonelWriterTest >> testTheHeader [
	| text |
	self makeSample.
	text := TonelWriter sourceFor: (Smalltalk at: #TonelTestSample).
	self assert: (text includesSubstring: '#name : ''TonelTestSample''').
	self assert: (text includesSubstring: '#superclass : ''Object''').
	self assert: (text includesSubstring: '#instVars : [ ''a'', ''b'' ]').
	self assert: (text includesSubstring: '#classVars : [ ''Count'' ]').
	self assert: (text includesSubstring: '#category : ''Tonel-Tests''').
	self assert: (text includesSubstring: 'The sample.').
	self deny: (text includesSubstring: '#classInstVars').
	self deny: (text includesSubstring: '#type')
]

{ #category : 'tests' }
TonelWriterTest >> testTheRoundTripIsExact [
	"Written, read back into a fresh definition, written again: the two
	 texts are the same, and every method's source survived unchanged."
	| class first sources second |
	class := self makeSample.
	first := TonelWriter sourceFor: class.
	sources := Dictionary new.
	class selectors do: [:s | sources at: s put: (class sourceCodeAt: s)].
	class class selectors do: [:s | sources at: s put: (class class sourceCodeAt: s)].
	class removeFromSystem.
	class := TonelReader loadString: first named: 'again'.
	second := TonelWriter sourceFor: class.
	self assert: second equals: first.
	class selectors do: [:s | self assert: (class sourceCodeAt: s) equals: (sources at: s)].
	class class selectors do: [:s | self assert: (class class sourceCodeAt: s) equals: (sources at: s)]
]

{ #category : 'tests' }
TonelWriterTest >> testClassSideComesFirstAndMethodsAreSorted [
	| text with a aColon |
	self makeSample.
	text := TonelWriter sourceFor: (Smalltalk at: #TonelTestSample).
	with := text indexOfSubCollection: 'TonelTestSample class >> with:' startingAt: 1.
	a := text indexOfSubCollection: 'TonelTestSample >> a [' startingAt: 1.
	aColon := text indexOfSubCollection: 'TonelTestSample >> a: x [' startingAt: 1.
	self assert: with > 0.
	self assert: with < a.
	self assert: a < aColon
]

{ #category : 'tests' }
TonelWriterTest >> testABrowserMethodGetsItsLineBreaks [
	"A method compiled from a one-line source has no line break around its
	 body; the file gets one on each side, and reads back with them."
	| class text |
	class := self makeSample.
	class basicCompile: 'b ^b' classified: 'accessing' notifying: nil.
	text := TonelWriter sourceFor: class.
	self assert: (text includesSubstring: 'TonelTestSample >> b [', (String with: Character cr), '^b', (String with: Character cr), ']')
]
