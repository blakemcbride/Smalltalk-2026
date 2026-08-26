"
Pharo's Tonel package format, read into a running image.

The bootstrap reads Tonel in C (src/compiler/tonel.c) to build an image from a
profile, and until this class existed that was the only way a Tonel file got in:
the image's own fileIn reads 1983's chunk format and nothing else.  A server
that loads a service from `backend/services/Foo.class.st' on the first request
for it, and again when the file changes, needs the reader to be here, in the
image, at run time.  So this is the C reader's grammar written a second time,
and where the two could disagree this one defers: a body is read verbatim
between its brackets, the pattern and the body are joined by a carriage return,
and every line ending arrives as the carriage return the image breaks lines on.

THE GRAMMAR, as tonel.c states it:

    ''An optional class comment.''
    Class {  #name : 'Foo',  #superclass : 'Object',  #instVars : [ 'a' ],
             #classVars : [ 'B' ],  #classInstVars : [ 'c' ],  #pools : [ ],
             #type : #bytes,  #category : 'Stuff' }
    { #category : 'accessing' }
    Foo >> a [ ^a ]
    { #category : 'instance creation' }
    Foo class >> new [ ^super new initialize ]

`Extension { #name : 'Foo' }' adds methods to a class defined elsewhere, and
`Package { #name : 'X' }' defines nothing.  A method body balances its opening
bracket, and four constructs may hold a bracket that is not one: a comment, a
string, a character literal such as $], and a literal array, which has bracket
rules of its own.  Each is skipped exactly as tonel.c skips it.

WHAT LOADING DOES.  A Class file defines the class through basicSubclass:...,
the class-side variables through the metaclass, the comment through comment:,
and each method through basicCompile:classified:notifying: -- the layer BELOW
the write-back hooks in ClassDescription and Class, so that reading a file
never writes one.  A class that already exists is redefined in place and keeps
its identity; and because the file is the whole truth about the class, a
method the class has and the file does not is removed, on either side.  That
is what makes editing the file and saving it a complete edit rather than an
additive one.

Line endings: the file holds line feeds, or carriage return and line feed;
the image holds carriage returns and nothing else will do (see
lib/Files-Fixes).  The text is translated once, up front, before anything is
parsed.
"
Class {
	#name : 'TonelReader',
	#superclass : 'Object',
	#instVars : [ 'text', 'position', 'path', 'definedClass', 'selectorsSeen', 'classSelectorsSeen', 'currentSource', 'currentLine' ],
	#category : 'Tonel'
}

{ #category : 'loading' }
TonelReader class >> loadFile: pathString [
	"Read the Tonel file at pathString into the image.  Answer the class it
	 defines, or the class an Extension adds to, or nil for a Package file."

	| dir name file stream contents |
	dir := FileDirectory directoryFromName: pathString setFileName: [:n | name := n].
	file := dir findKey: name.
	stream := file asFileStream.
	[contents := stream contentsOfEntireFile] ensure: [stream close].
	^self loadString: contents named: pathString
]

{ #category : 'loading' }
TonelReader class >> loadString: aString named: pathString [
	"Read aString, which is the text of a Tonel file, into the image."
	^self new setText: aString path: pathString; load
]

{ #category : 'private' }
TonelReader >> setText: aString path: pathString [
	"Translate every line ending to the carriage return before anything
	 else looks at the text, as the C reader does per method."
	| out i c lf cr |
	lf := Character value: 10.
	cr := Character cr.
	out := WriteStream on: (String new: aString size).
	i := 1.
	[i <= aString size] whileTrue: [
		c := aString at: i.
		(c = cr and: [i < aString size and: [(aString at: i + 1) = lf]])
			ifTrue: [out nextPut: cr. i := i + 2]
			ifFalse: [
				out nextPut: (c = lf ifTrue: [cr] ifFalse: [c]).
				i := i + 1]].
	text := out contents.
	position := 1.
	path := pathString.
	selectorsSeen := Set new.
	classSelectorsSeen := Set new
]

{ #category : 'accessing' }
TonelReader >> definedClass [
	^definedClass
]

{ #category : 'private - scanning' }
TonelReader >> atEnd [
	^position > text size
]

{ #category : 'private - scanning' }
TonelReader >> peek [
	^position > text size ifTrue: [nil] ifFalse: [text at: position]
]

{ #category : 'private - scanning' }
TonelReader >> next [
	| c |
	c := self peek.
	c notNil ifTrue: [position := position + 1].
	^c
]

{ #category : 'private - scanning' }
TonelReader >> lineNumber [
	"The line the position is on, for a message; counted when asked for,
	 because only a failure ever asks."
	| n |
	n := 1.
	1 to: (position min: text size) do: [:i | (text at: i) = Character cr ifTrue: [n := n + 1]].
	^n
]

{ #category : 'private - scanning' }
TonelReader >> fail: aString [
	^TonelError new signal: path, ':', self lineNumber printString, ': ', aString
]

{ #category : 'private - scanning' }
TonelReader >> skipSeparators [
	[self atEnd not and: [self peek isSeparator]] whileTrue: [position := position + 1]
]

{ #category : 'private - scanning' }
TonelReader >> readQuoted: quote [
	"The text between two quote characters, with a doubled quote inside
	 answering one.  The opening quote is the current character."
	| out c |
	self next.
	out := WriteStream on: (String new: 64).
	[(c := self next) isNil ifTrue: [^self fail: 'a ', (String with: quote), ' is not closed'].
	 c = quote and: [self peek ~= quote]] whileFalse: [
		(c = quote and: [self peek = quote]) ifTrue: [self next].
		out nextPut: c].
	^out contents
]

{ #category : 'private - scanning' }
TonelReader >> readComment [
	"A comment, if the next thing is one; nil otherwise."
	self skipSeparators.
	self peek = $" ifFalse: [^nil].
	^self readQuoted: $"
]

{ #category : 'private - scanning' }
TonelReader >> readWord [
	| out |
	out := WriteStream on: (String new: 32).
	[self atEnd not and: [self peek isLetter or: [self peek isDigit or: [self peek = $_]]]]
		whileTrue: [out nextPut: self next].
	^out contents
]

{ #category : 'private - ston' }
TonelReader >> readSton [
	"A STON object: `{ #key : value, ... }' as a Dictionary from key
	 Strings to values.  Only what a Tonel header holds: symbols,
	 strings, lists of them, numbers and the three constants."
	| dict key |
	dict := Dictionary new.
	self skipSeparators.
	self peek = ${ ifFalse: [^self fail: 'expected {'].
	self next.
	[self skipSeparators.
	 self peek = $}] whileFalse: [
		self peek = $# ifFalse: [^self fail: 'expected #key'].
		self next.
		key := self readWord.
		self skipSeparators.
		self peek = $: ifFalse: [^self fail: 'expected : after #', key].
		self next.
		dict at: key put: self readStonValue.
		self skipSeparators.
		self peek = $, ifTrue: [self next]].
	self next.
	^dict
]

{ #category : 'private - ston' }
TonelReader >> readStonValue [
	| c |
	self skipSeparators.
	c := self peek.
	c isNil ifTrue: [^self fail: 'a value is missing'].
	c = $' ifTrue: [^self readQuoted: $'].
	c = $# ifTrue: [
		self next.
		self peek = $' ifTrue: [^self readQuoted: $'].
		^self readWord].
	c = $[ ifTrue: [^self readStonList].
	c = ${ ifTrue: [^self readSton].
	c isDigit ifTrue: [^self readWord asNumber].
	c isLetter ifTrue: [ | word |
		word := self readWord.
		word = 'true' ifTrue: [^true].
		word = 'false' ifTrue: [^false].
		word = 'nil' ifTrue: [^nil].
		^word].
	^self fail: 'unexpected ', (String with: c), ' in a value'
]

{ #category : 'private - ston' }
TonelReader >> readStonList [
	| items |
	items := OrderedCollection new.
	self next.
	[self skipSeparators.
	 self peek = $]] whileFalse: [
		items add: self readStonValue.
		self skipSeparators.
		self peek = $, ifTrue: [self next]].
	self next.
	^items asArray
]

{ #category : 'private - methods' }
TonelReader >> skipQuotedInBody: quote [
	"Inside a body: past a string or comment, doubled quotes included."
	self next.
	[self atEnd ifTrue: [^self fail: 'a method body is not closed'].
	 self peek = quote and: [position + 1 > text size or: [(text at: position + 1) ~= quote]]]
		whileFalse: [
			(self peek = quote) ifTrue: [self next].
			self next].
	self next
]

{ #category : 'private - methods' }
TonelReader >> skipLiteralArray [
	"Past `#( ... )', which has bracket rules of its own: strings,
	 comments and characters inside it, and nested parentheses."
	| parens |
	parens := 0.
	self next.				"the #"
	[self atEnd ifTrue: [^self fail: 'a literal array is not closed'].
	 self peek = $$ ifTrue: [self next. self next]
	 ifFalse: [
		(self peek = $" or: [self peek = $']) ifTrue: [self skipQuotedInBody: self peek]
		ifFalse: [
			self peek = $( ifTrue: [parens := parens + 1].
			self peek = $) ifTrue: [
				parens := parens - 1.
				self next.
				parens = 0 ifTrue: [^self]]
			ifFalse: [self next]]].
	 true] whileTrue
]

{ #category : 'private - methods' }
TonelReader >> readBody [
	"The text between the opening bracket, which is the current character,
	 and the bracket that balances it; neither bracket is answered."
	| depth start c |
	depth := 0.
	start := position + 1.
	[self atEnd ifTrue: [^self fail: 'a method body is not closed'].
	 c := self peek.
	 c = $$ ifTrue: [self next. self next]
	 ifFalse: [
		(c = $" or: [c = $']) ifTrue: [self skipQuotedInBody: c]
		ifFalse: [
			(c = $# and: [position < text size and: [(text at: position + 1) = $(]])
				ifTrue: [self skipLiteralArray]
				ifFalse: [
					c = $[ ifTrue: [depth := depth + 1].
					c = $] ifTrue: [
						depth := depth - 1.
						depth = 0 ifTrue: [
							self next.
							^text copyFrom: start to: position - 2]].
					self next]]].
	 true] whileTrue
]

{ #category : 'private - methods' }
TonelReader >> readMethodClassified: category [
	"`Foo >> pattern [ body ]' or `Foo class >> pattern [ body ]', as an
	 Array: class name, class side, category, source, line -- to be
	 compiled once the whole file has been read and the class defined."
	| className classSide pattern body source line |
	self skipSeparators.
	className := self readWord.
	className isEmpty ifTrue: [^self fail: 'expected a class name before >>'].
	self skipSeparators.
	classSide := false.
	(text size >= (position + 4) and: [(text copyFrom: position to: position + 4) = 'class'
		and: [position + 5 > text size or: [(text at: position + 5) isLetter not]]])
			ifTrue: [classSide := true. position := position + 5. self skipSeparators].
	(text size >= (position + 1) and: [(text copyFrom: position to: position + 1) = '>>'])
		ifFalse: [^self fail: 'expected >> after ', className].
	position := position + 2.
	self skipSeparators.
	pattern := WriteStream on: (String new: 64).
	[self atEnd not and: [self peek ~= $[]] whileTrue: [pattern nextPut: self next].
	self atEnd ifTrue: [^self fail: 'expected [ to open the body of ', pattern contents].
	pattern := pattern contents trimmed.
	line := self lineNumber.
	body := self readBody.
	source := pattern, (String with: Character cr), body.
	^Array with: className with: classSide with: category with: source with: line
]

{ #category : 'private - methods' }
TonelReader >> selectorOf: source [
	"The selector a method source's pattern names: the first line, which
	 is a unary word, a binary selector and its argument, or keywords each
	 followed by an argument."
	| pattern words out |
	pattern := source copyFrom: 1 to: ((source indexOf: Character cr) - 1 max: 0).
	words := pattern substrings.
	words isEmpty ifTrue: [^nil].
	words size = 1 ifTrue: [^words first asSymbol].
	(words first last = $:) ifFalse: [^words first asSymbol].		"binary"
	out := WriteStream on: (String new: 32).
	words do: [:w | w last = $: ifTrue: [out nextPutAll: w]].
	^out contents asSymbol
]

{ #category : 'private - methods' }
TonelReader >> compile: aMethodArray [
	"Compile one method read by readMethodClassified:, through the layer
	 below the write-back hook, with this reader as the compiler's
	 requestor so that an error is a TonelError and not a window."
	| target classSide selector |
	target := Smalltalk at: (aMethodArray at: 1) asSymbol
		ifAbsent: [^self fail: (aMethodArray at: 1), ' is not a class in this image'].
	classSide := aMethodArray at: 2.
	classSide ifTrue: [target := target class].
	currentSource := aMethodArray at: 4.
	currentLine := aMethodArray at: 5.
	selector := target basicCompile: currentSource classified: (aMethodArray at: 3) notifying: self.
	selector isNil ifTrue: [^self fail: 'could not compile ', (self selectorOf: currentSource) printString].
	(classSide ifTrue: [classSelectorsSeen] ifFalse: [selectorsSeen]) add: selector
]

{ #category : 'compiler requestor' }
TonelReader >> insertAndSelect: aString at: anInteger [
	"What Compiler>>notify:at: sends its requestor for a syntax error.

	 The requestor the compiler expects is a text editor, which would show
	 the message in the code and let the user fix it.  There is no user
	 here: a Tonel file is being read for a server, and a syntax error
	 with nobody to tell would open a notifier window and suspend the
	 process -- which is what happened, and is why this reader is the
	 requestor rather than nil.  The error becomes a TonelError that names
	 the file, the method and the character."

	| pattern |
	pattern := currentSource copyFrom: 1 to: ((currentSource indexOf: Character cr) - 1 max: 0).
	^TonelError new signal: path, ':', currentLine printString, ': in ', pattern, ': ',
		aString, ' at character ', anInteger printString
]

{ #category : 'compiler requestor' }
TonelReader >> selectFrom: start to: end [
	"What the Parser sends its editor for an undeclared variable, just
	 before the Encoder offers a menu of ways to declare it.  A menu is a
	 question for a user, and a service file with a name in it that the
	 image does not know is a mistake in the file; raising here, before
	 the menu, is how the file's author is told."

	| name |
	name := (start between: 1 and: currentSource size) & (end between: start and: currentSource size)
		ifTrue: [currentSource copyFrom: start to: end]
		ifFalse: ['a variable'].
	^TonelError new signal: path, ':', currentLine printString, ': ', name, ' is undeclared'
]

{ #category : 'compiler requestor' }
TonelReader >> text [
	"The source the compiler is working on, as an editor would answer it."
	^currentSource
]

{ #category : 'private - definitions' }
TonelReader >> define: header comment: commentOrNil [
	"Make, or remake, the class the header describes."
	| name superName superclass ivars cvars civars pools type category existed |
	name := header at: 'name' ifAbsent: [^self fail: 'a Tonel class has no #name'].
	superName := header at: 'superclass' ifAbsent: ['nil'].
	superclass := superName = 'nil' ifTrue: [nil] ifFalse: [
		Smalltalk at: superName asSymbol ifAbsent: [^self fail: superName, ' is not a class in this image']].
	ivars := self namesFrom: (header at: 'instVars' ifAbsent: [header at: 'slots' ifAbsent: [#()]]).
	cvars := self namesFrom: (header at: 'classVars' ifAbsent: [#()]).
	civars := self namesFrom: (header at: 'classInstVars' ifAbsent: [#()]).
	pools := self namesFrom: (header at: 'pools' ifAbsent: [#()]).
	type := header at: 'type' ifAbsent: ['normal'].
	category := header at: 'category' ifAbsent: [header at: 'package' ifAbsent: ['Unclassified']].
	superclass isNil ifTrue: [^self fail: 'a class with no superclass cannot be made at run time'].
	existed := Smalltalk includesKey: name asSymbol.
	definedClass := type = 'bytes'
		ifTrue: [superclass variableByteSubclass: name asSymbol instanceVariableNames: ivars
					classVariableNames: cvars poolDictionaries: pools category: category]
		ifFalse: [type = 'variable'
			ifTrue: [superclass variableSubclass: name asSymbol instanceVariableNames: ivars
					classVariableNames: cvars poolDictionaries: pools category: category]
			ifFalse: [type = 'words'
				ifTrue: [superclass variableWordSubclass: name asSymbol instanceVariableNames: ivars
					classVariableNames: cvars poolDictionaries: pools category: category]
				ifFalse: [superclass basicSubclass: name asSymbol instanceVariableNames: ivars
					classVariableNames: cvars poolDictionaries: pools category: category]]].
	definedClass class instanceVariableNames: civars.
	commentOrNil notNil ifTrue: [definedClass comment: commentOrNil].
	^existed
]

{ #category : 'private - definitions' }
TonelReader >> namesFrom: anArray [
	"A list of names as the space-separated String the 1983 definition
	 messages take."
	| out |
	out := WriteStream on: (String new: 64).
	anArray do: [:each | out nextPutAll: each asString] separatedBy: [out nextPut: $ ].
	^out contents
]

{ #category : 'private - definitions' }
TonelReader >> removeMethodsNotIn: methods from: aClass [
	"The file is the whole truth about the class: what it no longer says,
	 the class no longer has.  Done BEFORE the class is redefined, because
	 a redefinition that drops a class variable refuses while a method still
	 names it -- and that method is exactly the one the file dropped too.
	 Through the layer below the write-back hook."
	| keep classKeep |
	keep := Set new.
	classKeep := Set new.
	methods do: [:m |
		((m at: 1) = aClass name) ifTrue: [ | selector |
			selector := self selectorOf: (m at: 4).
			selector notNil ifTrue: [((m at: 2) ifTrue: [classKeep] ifFalse: [keep]) add: selector]]].
	(aClass selectors reject: [:s | keep includes: s])
		do: [:s | aClass basicRemoveSelector: s].
	(aClass class selectors reject: [:s | classKeep includes: s])
		do: [:s | aClass class basicRemoveSelector: s]
]

{ #category : 'loading' }
TonelReader >> load [
	"Read the whole file, then act on it: the file is parsed to the end
	 before anything in the image changes, so a file with a mistake in
	 its last method leaves the class as it was.  Answer the class defined
	 or extended, or nil for a Package."
	| comment type header methods name |
	comment := self readComment.
	self skipSeparators.
	type := self readWord.
	type isEmpty ifTrue: [^self fail: 'not a Tonel file'].
	header := self readSton.
	type = 'Package' ifTrue: [^nil].
	(type = 'Class' or: [type = 'Extension']) ifFalse: [^self fail: 'unknown Tonel type ', type].
	name := header at: 'name' ifAbsent: [^self fail: 'a Tonel ', type, ' has no #name'].
	methods := OrderedCollection new.
	[self skipSeparators.
	 self atEnd] whileFalse: [ | category |
		category := ''.
		self peek = ${ ifTrue: [
			category := (self readSton at: 'category' ifAbsent: ['']) asString.
			self skipSeparators].
		self atEnd ifFalse: [methods add: (self readMethodClassified: category)]].
	type = 'Class'
		ifTrue: [
			(Smalltalk includesKey: name asSymbol)
				ifTrue: [self removeMethodsNotIn: methods from: (Smalltalk at: name asSymbol)].
			self define: header comment: comment]
		ifFalse: [
			definedClass := Smalltalk at: name asSymbol
				ifAbsent: [^self fail: name asString, ' is not a class in this image']].
	methods do: [:each | self compile: each].
	^definedClass
]
