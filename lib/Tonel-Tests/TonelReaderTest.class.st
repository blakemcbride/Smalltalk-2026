"
The reader, on text: every construct that can hide a bracket, every part of a
header, an extension, and a reload that removes what the file no longer says.

The class every test defines is TonelTestSample, made and removed here so
that the image the suite leaves behind is the image it found.
"
Class {
	#name : 'TonelReaderTest',
	#superclass : 'TestCase',
	#category : 'Tonel-Tests'
}

{ #category : 'running' }
TonelReaderTest >> tearDown [
	(Smalltalk includesKey: #TonelTestSample) ifTrue: [
		TonelSource forget: (Smalltalk at: #TonelTestSample).
		(Smalltalk at: #TonelTestSample) removeFromSystem]
]

{ #category : 'private' }
TonelReaderTest >> sampleText [
	"A class exercising the whole grammar, with line feeds as a file has."
	| lf |
	lf := String with: (Character value: 10).
	^'"', lf,
	'A sample with a "" quote in its comment.', lf,
	'"', lf,
	'Class {', lf,
	'	#name : ''TonelTestSample'',', lf,
	'	#superclass : ''Object'',', lf,
	'	#instVars : [ ''a'', ''b'' ],', lf,
	'	#classVars : [ ''Count'' ],', lf,
	'	#classInstVars : [ ''registry'' ],', lf,
	'	#category : ''Tonel-Tests'' ', lf,
	'}', lf,
	lf,
	'{ #category : ''instance creation'' }', lf,
	'TonelTestSample class >> make [', lf,
	'	Count := (Count ifNil: [0]) + 1.', lf,
	'	^self new setA: Count', lf,
	']', lf,
	lf,
	'{ #category : ''accessing'' }', lf,
	'TonelTestSample >> setA: anInteger [', lf,
	'	a := anInteger', lf,
	']', lf,
	lf,
	'{ #category : ''accessing'' }', lf,
	'TonelTestSample >> answer [', lf,
	'	"A comment with a ] in it."', lf,
	'	| s |', lf,
	'	s := ''a string with ] and [ inside''.', lf,
	'	b := #(1 $] $[ ''x]'' (2 3)).', lf,
	'	^a + 41 + (s size - s size) + (b size - 5) + ($] asciiValue - $] asciiValue)', lf,
	']', lf
]

{ #category : 'tests' }
TonelReaderTest >> testALoadedClassWorks [
	| class |
	class := TonelReader loadString: self sampleText named: 'sample'.
	self assert: class name equals: 'TonelTestSample'.
	self assert: class superclass equals: Object.
	self assert: class instVarNames asArray equals: #('a' 'b').
	self assert: (class classVarNames includes: #Count).
	self assert: class class instVarNames asArray equals: #('registry').
	self assert: class category equals: #'Tonel-Tests'.
	self assert: (class comment asString includesSubstring: 'a " quote').
	self assert: (class new setA: 1) answer equals: 42.
	self assert: class make answer equals: 42.
	self assert: (class organization categoryOfElement: #answer) equals: #accessing.
	self assert: (class class organization categoryOfElement: #make) equals: #'instance creation'
]

{ #category : 'tests' }
TonelReaderTest >> testTheBodyIsKeptVerbatim [
	"The source in the image is the pattern, a carriage return, and the body
	 between the brackets exactly as the file had it -- the same assembly
	 the C reader makes -- except for the body's last line break, which the
	 changes log drops on the way back, as it does for every method."
	| class source cr |
	class := TonelReader loadString: self sampleText named: 'sample'.
	cr := String with: Character cr.
	source := class sourceCodeAt: #setA:.
	self assert: source equals: 'setA: anInteger', cr, cr, '	a := anInteger'
]

{ #category : 'tests' }
TonelReaderTest >> testAReloadRemovesWhatTheFileNoLongerSays [
	| class shorter cr |
	class := TonelReader loadString: self sampleText named: 'sample'.
	self assert: (class includesSelector: #answer).
	cr := String with: Character cr.
	shorter := 'Class { #name : ''TonelTestSample'', #superclass : ''Object'', #instVars : [ ''a'' ], #category : ''Tonel-Tests'' }', cr,
		'TonelTestSample >> setA: anInteger [ a := anInteger ]', cr.
	"A redefinition with a different shape makes a new class object in this
	 image, so the class is fetched by name afterwards."
	TonelReader loadString: shorter named: 'sample'.
	class := Smalltalk at: #TonelTestSample.
	self deny: (class includesSelector: #answer).
	self deny: (class class includesSelector: #make).
	self assert: (class includesSelector: #setA:).
	self assert: class instVarNames size equals: 1.
	self assert: class instVarNames first equals: 'a'
]

{ #category : 'tests' }
TonelReaderTest >> testAnExtensionAddsToAnExistingClass [
	| class cr |
	class := TonelReader loadString: self sampleText named: 'sample'.
	cr := String with: Character cr.
	self assert: (TonelReader loadString: 'Extension { #name : ''TonelTestSample'' }', cr,
		'{ #category : ''*Tonel-Tests'' }', cr,
		'TonelTestSample >> twice [ ^self answer * 2 ]', cr named: 'ext') == class.
	self assert: (class new setA: 1) twice equals: 84.
	self assert: (class includesSelector: #answer)
]

{ #category : 'tests' }
TonelReaderTest >> testAPackageFileDefinesNothing [
	self assert: (TonelReader loadString: 'Package { #name : ''Whatever'' }' named: 'package.st') isNil
]

{ #category : 'tests' }
TonelReaderTest >> testASelectorDefinedLaterInTheFileCompiles [
	"A method that sends a selector no method has yet -- one defined
	 further down the same file, or in a file not loaded yet -- compiles,
	 with the selector interned as written.  1983's Parser put that to a
	 menu, and with this reader as requestor the answer depended on what
	 an unhandled doesNotUnderstand: answered, which is not the same in a
	 loaded image as in the bootstrap: the server could not load a file
	 every test loaded."
	| class |
	class := TonelReader loadString: 'Class { #name : ''TonelTestSample'', #superclass : ''Object'' }', (String with: Character cr),
		'TonelTestSample >> first [ ^self definedSecond: 40 plus: 2 ]', (String with: Character cr),
		'TonelTestSample >> definedSecond: a plus: b [ ^a + b ]', (String with: Character cr),
		'TonelTestSample >> third [ ^self neverDefinedAnywhere: 1 ]' named: 'sample'.
	self assert: class new first equals: 42.
	self assert: (Symbol hasInterned: 'neverDefinedAnywhere:' ifTrue: [:s | true]).
	self should: [class new third] raise: Error
]

{ #category : 'tests' }
TonelReaderTest >> testAnUndeclaredVariableIsRefused [
	"A name that is neither a temporary, an instance variable nor a global
	 is a mistake in the file, and the error names it."
	[TonelReader loadString: 'Class { #name : ''TonelTestSample'', #superclass : ''Object'' }', (String with: Character cr),
		'TonelTestSample >> broken [ ^nowhereDeclared + 1 ]' named: 'x'.
	 self assert: false]
		on: TonelError
		do: [:e |
			self assert: (e messageText includesSubstring: 'nowhereDeclared').
			self assert: (e messageText includesSubstring: 'undeclared')]
]

{ #category : 'tests' }
TonelReaderTest >> testRefusals [
	self should: [TonelReader loadString: 'Class { #superclass : ''Object'' }' named: 'x'] raise: TonelError.
	self should: [TonelReader loadString: 'Class { #name : ''TonelTestSample'', #superclass : ''NoSuchClassAnywhere'' }' named: 'x'] raise: TonelError.
	self should: [TonelReader loadString: 'Class { #name : ''TonelTestSample'', #superclass : ''Object'' }
TonelTestSample >> broken [ ^1' named: 'x'] raise: TonelError.
	self should: [TonelReader loadString: 'Widget { #name : ''X'' }' named: 'x'] raise: TonelError.
	self should: [TonelReader loadString: 'not tonel at all' named: 'x'] raise: TonelError
]

{ #category : 'tests' }
TonelReaderTest >> testAnErrorNamesTheFileAndLine [
	| text |
	text := 'Class { #name : ''TonelTestSample'', #superclass : ''Object'' }
TonelTestSample >> fine [ ^1 ]
TonelTestSample >> broken [ ^1'.
	[TonelReader loadString: text named: 'services/X.class.st'. self assert: false]
		on: TonelError
		do: [:e |
			self assert: (e messageText includesSubstring: 'services/X.class.st:').
			self assert: (e messageText includesSubstring: 'not closed')]
]
