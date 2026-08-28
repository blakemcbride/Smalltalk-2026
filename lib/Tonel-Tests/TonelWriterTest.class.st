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

{ #category : 'private' }
TonelWriterTest >> contentsOf: aPath [
	"The text of a file in the tree, with carriage returns for line
	 endings, as the writer produces its text."
	| dir name file stream text |
	dir := FileDirectory directoryFromName: aPath setFileName: [:n | name := n].
	file := dir findKey: name.
	stream := file asFileStream.
	text := [stream contentsOfEntireFile] ensure: [stream close].
	^text copyReplaceAll: (String with: (Character value: 10)) with: (String with: Character cr)
]

{ #category : 'private' }
TonelWriterTest >> libWeakArray [
	"lib/Kernel's WeakArray, or nil in a profile that replaces it with
	 Pharo's (pharo-weak) or leaves it out (pharo-collections).  The
	 tests that compare against lib/Kernel/WeakArray.class.st can only
	 run where that is the class the image has."
	| class |
	class := Smalltalk at: #WeakArray ifAbsent: [nil].
	^(class notNil and: [class category = #'Kernel-Closures']) ifTrue: [class] ifFalse: [nil]
]

{ #category : 'tests' }
TonelWriterTest >> testAWeakClassIsWrittenAsWeak [
	"Bugs3 B61.  WeakArray's header said #type : 'weak' and came back as
	 'variable', which reads back as a strong array.  Checked on a weak
	 class of the test's own, so that it holds in every profile, and on
	 lib's WeakArray where the profile has it."
	| class text |
	class := TonelReader loadString: 'Class { #name : ''TonelTestSample'', #superclass : ''Object'', #type : ''weak'' }' named: 'weak'.
	text := TonelWriter sourceFor: class.
	self assert: (text includesSubstring: '#type : ''weak''').
	self deny: (text includesSubstring: '''variable''').
	self assert: class isWeak.
	self deny: class isEphemeron.
	self deny: Array isWeak.
	self libWeakArray notNil ifTrue: [
		self assert: WeakArray isWeak.
		self deny: WeakArray isEphemeron.
		self assert: ((TonelWriter sourceFor: WeakArray) includesSubstring: '#type : ''weak''')]
]

{ #category : 'tests' }
TonelWriterTest >> testATraitUserNamesItsTraitAndNotItsMethods [
	"Bugs3 B61.  Greeter takes TGreeting; its file names the trait and
	 defines one method.  The write-back had no #traits line and owned
	 the trait's two methods, so a reload of it would have made a class
	 that had stopped taking the trait without saying so."
	| text |
	text := TonelWriter sourceFor: Greeter.
	self assert: Greeter traitComposition equals: 'TGreeting'.
	self assert: Object traitComposition isNil.
	self assert: (text includesSubstring: '#traits : ''TGreeting''').
	self assert: (text includesSubstring: '#classTraits : ''TGreeting classTrait''').
	self assert: (text includesSubstring: 'Greeter >> subject [').
	self deny: (text includesSubstring: 'Greeter >> greeting').
	self deny: (text includesSubstring: 'defaultGreeting').
	self deny: (text includesSubstring: '*trait:')
]

{ #category : 'tests' }
TonelWriterTest >> testTheLoaderSynthesizedNewIsNotWritten [
	"The `new ^super new initialize' the loader writes for a class with
	 an initialize and no new was never in the file, and the file is what
	 is being written."
	self assert: (Greeter class includesSelector: #new).
	self deny: ((TonelWriter sourceFor: Greeter) includesSubstring: 'Synthesized').
	self deny: ((TonelWriter sourceFor: Greeter) includesSubstring: 'Greeter class >> new')
]

{ #category : 'tests' }
TonelWriterTest >> testAClassFileRoundTripsByteForByte [
	"The manual's promise: a class can go out and come back byte for
	 byte.  The two classes the audit found it broken for, written from
	 the image and compared with the files the image was built from.
	 Greeter is in every profile; WeakArray only where the profile has
	 lib's, since pharo-weak takes Pharo's instead and pharo-collections
	 has none."
	self assert: (TonelWriter sourceFor: Greeter) equals: (self contentsOf: 'lib/Probe/Greeter.class.st').
	self libWeakArray notNil ifTrue: [
		self assert: (TonelWriter sourceFor: WeakArray) equals: (self contentsOf: 'lib/Kernel/WeakArray.class.st')]
]

{ #category : 'tests' }
TonelWriterTest >> testTheClassTraitsLineIsTheMechanicalCompanion [
	"What Pharo writes beside #traits, which the loader accepts and
	 drops: each trait's classTrait, exclusions and parentheses left out."
	self assert: (TonelWriter classTraitsFor: 'TGreeting') equals: 'TGreeting classTrait'.
	self assert: (TonelWriter classTraitsFor: 'TA + TB') equals: 'TA classTrait + TB classTrait'.
	self assert: (TonelWriter classTraitsFor: 'TA + (TB - {#x. #y}) + TC')
		equals: 'TA classTrait + TB classTrait + TC classTrait'
]

{ #category : 'tests' }
TonelWriterTest >> testAnExtensionHoldsOnlyItsPackagesMethods [
	"Bugs3 B59.  The extension writer: the methods under the prefix and
	 its tags, class side first, with the file comment on top; and the
	 class writer leaves those same methods out."
	| class text classText |
	class := self makeSample.
	class basicCompile: 'extra ^1' classified: '*Tonel-Tests' notifying: nil.
	class basicCompile: 'hidden ^2' classified: '*Tonel-Tests-private' notifying: nil.
	class basicCompile: 'other ^3' classified: '*Elsewhere' notifying: nil.
	class class basicCompile: 'extraClass ^4' classified: '*Tonel-Tests' notifying: nil.
	text := TonelWriter extensionSourceFor: class prefix: '*Tonel-Tests' comment: 'What it is for.'.
	self assert: (text indexOfSubCollection: '"', (String with: Character cr), 'What it is for.' startingAt: 1) equals: 1.
	self assert: (text includesSubstring: 'Extension { #name : ''TonelTestSample'' }').
	self assert: (text includesSubstring: 'TonelTestSample class >> extraClass [').
	self assert: (text includesSubstring: 'TonelTestSample >> extra [').
	self assert: (text includesSubstring: 'TonelTestSample >> hidden [').
	self deny: (text includesSubstring: 'other').
	self deny: (text includesSubstring: 'Class {').
	self deny: (text includesSubstring: ' >> a [').
	classText := TonelWriter sourceFor: class.
	self assert: (classText includesSubstring: 'TonelTestSample >> a [').
	self deny: (classText includesSubstring: 'extra').
	self deny: (classText includesSubstring: 'hidden').
	self deny: (classText includesSubstring: 'other')
]
