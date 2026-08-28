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

WHAT A MISTAKE IN THE FILE DOES: nothing.  Every method is compiled before
any is installed and before any is removed, so a file with a syntax error in
its last method changes no class, and the TonelError names the file, the
method and the character.  This is not what the comment on `load' used to
promise -- it said the file was parsed to the end first, which was true, and
then defined the class and compiled method by method into it, so a bad
seventh method left a class with six (Bugs3 B63).  A new class is defined,
its methods compiled against it, and the class taken out again if one of
them fails; a class being redefined into the same shape has the new methods
compiled against the class it is, installed only when all of them compiled;
and a class being redefined into a NEW shape -- an instance variable added
-- has them compiled first against a scratch class of that shape, defined
under a name with `ScratchForTonel' on the end and removed before anything
is said, because a method that names a new variable cannot be compiled
against the old class and the old class cannot be reshaped without changing
the image.  The last case is the one place the promise is weaker than
`nothing': the reshaping itself can be refused by 1983's own rules, and is,
before any method is touched.

WHAT A FILE OWNS.  A Class file is the whole truth about the class's OWN
methods -- the ones in categories that do not begin with a star.  A starred
category names another package, `*Concurrency', or a trait the loader
flattened in, `*trait:TGreeting', and those methods have files of their own,
so a reload of the class file leaves them alone; before it did not, and
reloading any class another package had extended stripped the extension.  An
Extension file is the whole truth about the class's methods under ITS
categories, the ones it used, and a method it no longer has is removed from
those.

A file that names a trait in its header is refused.  The bootstrap flattens
traits from the trait's source, which it keeps in C and frees when the image
is built; nothing in the image has it, so this reader could only define the
class without the trait's methods and say nothing, which is the outcome the
refusal replaces.

Line endings: the file holds line feeds, or carriage return and line feed;
the image holds carriage returns and nothing else will do (see
lib/Files-Fixes).  The text is translated once, up front, before anything is
parsed; a UTF-8 byte-order mark at the front is dropped at the same time,
since the type word was read from the first byte and a file some editor had
marked was `not a Tonel file' (Bugs3 B63).
"
Class {
	#name : 'TonelReader',
	#superclass : 'Object',
	#instVars : [ 'text', 'position', 'path', 'definedClass', 'fileType', 'fileComment', 'categoriesRead', 'currentSource', 'currentLine' ],
	#category : 'Tonel'
}

{ #category : 'loading' }
TonelReader class >> loadFile: pathString [
	"Read the Tonel file at pathString into the image.  Answer the class it
	 defines, or the class an Extension adds to, or nil for a Package file."
	^(self onFile: pathString) load
]

{ #category : 'loading' }
TonelReader class >> onFile: pathString [
	"A reader holding the text of the file at pathString, not yet loaded:
	 for TonelSource, which sends it load and then asks what kind of file
	 it was and what the file said about itself."
	| dir name file stream contents |
	dir := FileDirectory directoryFromName: pathString setFileName: [:n | name := n].
	file := dir findKey: name.
	stream := file asFileStream.
	[contents := stream contentsOfEntireFile] ensure: [stream close].
	^self new setText: contents path: pathString
]

{ #category : 'loading' }
TonelReader class >> loadString: aString named: pathString [
	"Read aString, which is the text of a Tonel file, into the image."
	^self new setText: aString path: pathString; load
]

{ #category : 'private' }
TonelReader >> setText: aString path: pathString [
	"Translate every line ending to the carriage return before anything
	 else looks at the text, as the C reader does per method; and drop a
	 UTF-8 byte-order mark, the three bytes EF BB BF some editors put in
	 front of the first character, which read as a type word of no
	 letters and made the file `not a Tonel file'.  The C reader skips
	 the same three bytes (src/compiler/tonel.c)."
	| out i c lf cr |
	lf := Character value: 10.
	cr := Character cr.
	out := WriteStream on: (String new: aString size).
	i := 1.
	(aString size >= 3
		and: [(aString at: 1) value = 239
		and: [(aString at: 2) value = 187
		and: [(aString at: 3) value = 191]]]) ifTrue: [i := 4].
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
	categoriesRead := Set new
]

{ #category : 'accessing' }
TonelReader >> definedClass [
	^definedClass
]

{ #category : 'accessing' }
TonelReader >> fileType [
	"'Class', 'Extension' or 'Package', once the header has been read."
	^fileType
]

{ #category : 'accessing' }
TonelReader >> fileComment [
	"The comment at the top of the file, or nil.  For a Class file that is
	 the class comment; for an Extension file it belongs to the file and
	 nothing in the image keeps it, so TonelSource does, for the writer."
	^fileComment
]

{ #category : 'accessing' }
TonelReader >> categoriesRead [
	"The categories the file's methods were under."
	^categoriesRead
]

{ #category : 'accessing' }
TonelReader >> packageName [
	"For a Package file, the name it declares; nil for any other kind.
	 Reads the header and no further: how TonelSource learns the name
	 of the package an extension file sits in."
	| type header |
	self readComment.
	self skipSeparators.
	type := self readWord.
	type = 'Package' ifFalse: [^nil].
	header := self readSton.
	^header at: 'name' ifAbsent: [nil]
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
TonelReader >> targetOf: aMethodArray substituting: aClassOrNil for: aSymbol [
	"The class or metaclass a method read by readMethodClassified: is
	 for, with aClassOrNil standing in for the class named aSymbol -- the
	 scratch class of a reshaping, or nil when there is none."
	| target |
	target := ((aMethodArray at: 1) asSymbol = aSymbol and: [aClassOrNil notNil])
		ifTrue: [aClassOrNil]
		ifFalse: [Smalltalk at: (aMethodArray at: 1) asSymbol
			ifAbsent: [^self fail: (aMethodArray at: 1), ' is not a class in this image']].
	^(aMethodArray at: 2) ifTrue: [target class] ifFalse: [target]
]

{ #category : 'private - methods' }
TonelReader >> compile: aMethodArray for: aBehavior [
	"Compile one method read by readMethodClassified: for aBehavior WITHOUT
	 installing it, with this reader as the compiler's requestor so that
	 an error is a TonelError and not a window.  Answer an Array of what
	 install: needs: the behavior, the selector, the CompiledMethod, the
	 source and the category."
	| selector method |
	currentSource := aMethodArray at: 4.
	currentLine := aMethodArray at: 5.
	aBehavior
		compileOnly: currentSource
		notifying: self
		trailer: #(0 0 0)
		ifFail: [^self fail: 'could not compile ', (self selectorOf: currentSource) printString]
		into: [:sel :m | selector := sel. method := m].
	^Array with: aBehavior with: selector with: method with: currentSource with: (aMethodArray at: 3)
]

{ #category : 'private - methods' }
TonelReader >> compileAll: methods substituting: aClassOrNil for: aSymbol [
	"Every method in the file compiled and none installed, in the file's
	 order; the first that does not compile raises, and the image is as
	 it was."
	^methods collect: [:each |
		self compile: each for: (self targetOf: each substituting: aClassOrNil for: aSymbol)]
]

{ #category : 'private - methods' }
TonelReader >> installAll: compiledMethods [
	"Install what compileAll:substituting:for: answered, through the layer
	 below the write-back hook."
	compiledMethods do: [:each |
		(each at: 1)
			basicInstall: (each at: 3)
			selector: (each at: 2)
			source: (each at: 4)
			classified: (each at: 5)]
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
TonelReader >> editor [
	"What Parser>>editor asks its requestor for: the thing it will send
	 selectFrom:to: to, and offer menus through.  This reader is its own
	 editor, so that an undeclared variable reaches selectFrom:to: below
	 and raises.

	 It was not always answered.  Without it the Parser's `requestor
	 editor' was a doesNotUnderstand:, and what an unhandled one answers
	 differs between the bootstrap process (nil, so the Parser went on as
	 if there were no editor) and an image read back from disk (not nil,
	 so the Parser took the answer for an editor).  That is how a service
	 sending a selector defined later in its own file loaded in every test
	 and failed on the server; lib/Tonel/Parser.extension.st has the rest."
	^self
]

{ #category : 'compiler requestor' }
TonelReader >> internsNewSelectors [
	"Yes: a selector no method has yet is interned as written, not put to
	 a menu (Parser>>makeNewSymbol:startingAt:).  A file being loaded
	 sends selectors defined later in the file, or in files not loaded
	 yet, as the ordinary case; there is no user to ask, and a typo is a
	 doesNotUnderstand: at run time, as it would be anywhere."
	^true
]

{ #category : 'compiler requestor' }
TonelReader >> selectFrom: start to: end [
	"What the Parser sends its editor for an undeclared variable, just
	 before the Encoder offers a menu of ways to declare it.  A menu is a
	 question for a user, and a service file with a name in it that the
	 image does not know is a mistake in the file; raising here, before
	 the menu, is how the file's author is told.  (A selector nobody has
	 defined does not come this way: internsNewSelectors.)"

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
TonelReader >> define: header comment: commentOrNil as: aSymbol [
	"Make, or remake, the class the header describes, under the name
	 aSymbol -- the header's own, or the scratch name a reshaping compiles
	 against first.  Answer the class.

	 Through the definition messages below the write-back hook, and never
	 through subclass:..., because a load holds TonelSource's lock and the
	 hook wants it.  The two shapes the 1983 messages have no word for --
	 weak and ephemeron -- are the variable and the plain shape with the
	 bit set afterwards; lib/Kernel-Protocol's Class>>weakSubclass:... is
	 the same thing for the Browser."
	| superName superclass ivars cvars civars pools type category class |
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
	class := type = 'bytes'
		ifTrue: [superclass variableByteSubclass: aSymbol instanceVariableNames: ivars
					classVariableNames: cvars poolDictionaries: pools category: category]
		ifFalse: [type = 'variable'
			ifTrue: [superclass variableSubclass: aSymbol instanceVariableNames: ivars
					classVariableNames: cvars poolDictionaries: pools category: category]
			ifFalse: [type = 'words'
				ifTrue: [superclass variableWordSubclass: aSymbol instanceVariableNames: ivars
					classVariableNames: cvars poolDictionaries: pools category: category]
				ifFalse: [type = 'weak'
					ifTrue: [superclass weakSubclass: aSymbol instanceVariableNames: ivars
						classVariableNames: cvars poolDictionaries: pools category: category]
					ifFalse: [
						(type = 'normal' or: [type = 'ephemeron'])
							ifFalse: [^self fail: 'a #type of ', type printString, ' cannot be made at run time'].
						class := superclass basicSubclass: aSymbol instanceVariableNames: ivars
							classVariableNames: cvars poolDictionaries: pools category: category.
						type = 'ephemeron' ifTrue: [class beEphemeron].
						class]]]].
	"The definition messages carry a weak or ephemeron bit the class
	 already has (Behavior>>format:variable:words:pointers:); a header
	 that no longer asks for one is obeyed."
	(type = 'weak' or: [type = 'ephemeron']) ifFalse: [class beStrong].
	class class instanceVariableNames: civars.
	commentOrNil notNil ifTrue: [class comment: commentOrNil].
	^class
]

{ #category : 'private - definitions' }
TonelReader >> header: header describesShapeOf: aClass [
	"Whether the header asks for exactly the shape aClass has: the same
	 superclass, instance, class and class-instance variables, pools and
	 #type.  When it does, the file's methods can be compiled against the
	 class as it is and installed with nothing redefined first."
	| superName type wanted |
	superName := header at: 'superclass' ifAbsent: ['nil'].
	(aClass superclass notNil and: [aClass superclass name = superName]) ifFalse: [^false].
	(self namesFrom: (header at: 'instVars' ifAbsent: [header at: 'slots' ifAbsent: [#()]]))
		= (self namesFrom: aClass instVarNames) ifFalse: [^false].
	(self sortedNamesFrom: (header at: 'classVars' ifAbsent: [#()]))
		= (self sortedNamesFrom: aClass classVarNames) ifFalse: [^false].
	(self namesFrom: (header at: 'classInstVars' ifAbsent: [#()]))
		= (self namesFrom: aClass class instVarNames) ifFalse: [^false].
	(self sortedNamesFrom: (header at: 'pools' ifAbsent: [#()]))
		= (self sortedNamesFrom: (aClass sharedPools collect: [:pool | Smalltalk keyAtValue: pool]))
			ifFalse: [^false].
	type := header at: 'type' ifAbsent: ['normal'].
	wanted := type = 'normal' ifTrue: [nil] ifFalse: [type].
	^(TonelWriter typeOf: aClass) = wanted
]

{ #category : 'private - definitions' }
TonelReader >> sortedNamesFrom: aCollection [
	"The names in a collection of Strings or Symbols, sorted, as one
	 String, for comparing two sets of them."
	^self namesFrom: (aCollection collect: [:each | each asString]) asSortedCollection asArray
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
	"The file is the whole truth about the class's own methods: what it no
	 longer says, the class no longer has.  A method in a starred category
	 is another package's extension or a trait's, with a file of its own,
	 and is left alone.  Done BEFORE the class is redefined, because a
	 redefinition that drops a class variable refuses while a method still
	 names it -- and that method is exactly the one the file dropped too.
	 Through the layer below the write-back hook."
	| keep classKeep |
	keep := Set new.
	classKeep := Set new.
	methods do: [:m |
		((m at: 1) = aClass name) ifTrue: [ | selector |
			selector := self selectorOf: (m at: 4).
			selector notNil ifTrue: [((m at: 2) ifTrue: [classKeep] ifFalse: [keep]) add: selector]]].
	self removeFrom: aClass allBut: keep in: [:category | TonelWriter isOwnCategory: category].
	self removeFrom: aClass class allBut: classKeep in: [:category | TonelWriter isOwnCategory: category]
]

{ #category : 'private - definitions' }
TonelReader >> removeExtensionMethodsNotIn: methods from: aClass [
	"An Extension file is the whole truth about the class's methods under
	 the starred categories it used: a method in one of those that the
	 file no longer has is removed.  Only the starred ones -- a plain
	 category in an extension file is a mistake in the file, and taking
	 the class's own methods away for it would be worse."
	| keep classKeep mine |
	keep := Set new.
	classKeep := Set new.
	methods do: [:m |
		((m at: 1) = aClass name) ifTrue: [ | selector |
			selector := self selectorOf: (m at: 4).
			selector notNil ifTrue: [((m at: 2) ifTrue: [classKeep] ifFalse: [keep]) add: selector]]].
	mine := [:category | (TonelWriter isOwnCategory: category) not and: [categoriesRead includes: category]].
	self removeFrom: aClass allBut: keep in: mine.
	self removeFrom: aClass class allBut: classKeep in: mine
]

{ #category : 'private - definitions' }
TonelReader >> removeFrom: aBehavior allBut: selectors in: categoryBlock [
	"Remove every method of aBehavior whose category categoryBlock accepts
	 and whose selector is not in selectors."
	| organization |
	organization := aBehavior organization.
	organization categories do: [:category |
		(categoryBlock value: category asString) ifTrue: [
			((organization listAtCategoryNamed: category) reject: [:s | selectors includes: s])
				do: [:s | aBehavior basicRemoveSelector: s]]]
]

{ #category : 'loading' }
TonelReader >> load [
	"Read the whole file, then act on it.  Answer the class defined or
	 extended, or nil for a Package.  The class comment says what is
	 promised: every method compiles before any is installed or removed."
	| comment type header methods name |
	comment := self readComment.
	self skipSeparators.
	type := self readWord.
	type isEmpty ifTrue: [^self fail: 'not a Tonel file'].
	header := self readSton.
	fileType := type.
	fileComment := comment.
	type = 'Package' ifTrue: [^nil].
	(type = 'Class' or: [type = 'Extension']) ifFalse: [^self fail: 'unknown Tonel type ', type].
	name := header at: 'name' ifAbsent: [^self fail: 'a Tonel ', type, ' has no #name'].
	methods := self readMethods.
	methods do: [:each | categoriesRead add: (each at: 3)].
	type = 'Class'
		ifTrue: [self loadClass: header comment: comment methods: methods]
		ifFalse: [self loadExtension: name asSymbol methods: methods].
	^definedClass
]

{ #category : 'loading' }
TonelReader >> readMethods [
	"Every method after the header, each with the category of the
	 `{ #category : ... }' before it, as readMethodClassified: answers
	 them."
	| methods |
	methods := OrderedCollection new.
	[self skipSeparators.
	 self atEnd] whileFalse: [ | category |
		category := ''.
		self peek = ${ ifTrue: [
			category := (self readSton at: 'category' ifAbsent: ['']) asString.
			self skipSeparators].
		self atEnd ifFalse: [methods add: (self readMethodClassified: category)]].
	^methods
]

{ #category : 'loading' }
TonelReader >> loadClass: header comment: comment methods: methods [
	"A Class file: new, or a redefinition into the same shape, or into a
	 new one.  Each is arranged so that a method that will not compile
	 changes nothing."
	| name old traits |
	name := (header at: 'name') asSymbol.
	traits := header at: 'traits' ifAbsent: [nil].
	(traits notNil and: [traits asString isEmpty not])
		ifTrue: [^self fail: name, ' names a trait, and only the bootstrap can flatten one: load it through a profile'].
	old := Smalltalk at: name ifAbsent: [nil].
	old isNil ifTrue: [^self defineNew: header comment: comment methods: methods as: name].
	(old isKindOf: Behavior)
		ifFalse: [^self fail: name, ' is a global that is not a class'].
	^(self header: header describesShapeOf: old)
		ifTrue: [self redefine: old inPlaceFrom: header comment: comment methods: methods]
		ifFalse: [self redefine: old reshapedFrom: header comment: comment methods: methods]
]

{ #category : 'loading' }
TonelReader >> defineNew: header comment: comment methods: methods as: name [
	"A class the image has not got: defined, its methods compiled against
	 it, and taken out again -- removeFromSystem, which is what the
	 Browser's `remove class' does -- if one of them fails, so that a bad
	 file leaves no class behind."
	| compiled |
	definedClass := self define: header comment: comment as: name.
	compiled := [self compileAll: methods substituting: nil for: name]
		ifCurtailed: [definedClass removeFromSystem. definedClass := nil].
	self installAll: compiled
]

{ #category : 'loading' }
TonelReader >> redefine: old inPlaceFrom: header comment: comment methods: methods [
	"The class exists in the shape the header asks for: its new methods
	 are compiled against it as it is, installed together once every one
	 has compiled, and then the methods the file no longer has go.  The
	 definition message is still sent, for the comment and the category;
	 with the shape unchanged it makes no new class object."
	| compiled |
	compiled := self compileAll: methods substituting: nil for: old name.
	self installAll: compiled.
	self removeMethodsNotIn: methods from: old.
	definedClass := self define: header comment: comment as: old name
]

{ #category : 'loading' }
TonelReader >> redefine: old reshapedFrom: header comment: comment methods: methods [
	"The header asks for a different shape.  The methods are compiled
	 first against a scratch class of that shape, so that a method naming
	 a variable the file adds compiles and one with a mistake in it is
	 refused before the image changes; the scratch class is removed either
	 way.  Then the old order, which the class-variable rule on
	 removeMethodsNotIn:from: needs: remove, redefine, compile in."
	| scratchName scratch |
	scratchName := (old name, 'ScratchForTonel') asSymbol.
	(Smalltalk includesKey: scratchName)
		ifTrue: [^self fail: scratchName, ' is in the way of redefining ', old name, '; remove it'].
	scratch := self define: header comment: nil as: scratchName.
	[self compileAll: methods substituting: scratch for: old name]
		ensure: [scratch removeFromSystem].
	self removeMethodsNotIn: methods from: old.
	definedClass := self define: header comment: comment as: old name.
	methods do: [:each |
		self installAll: (Array with: (self compile: each
			for: (self targetOf: each substituting: nil for: old name)))]
]

{ #category : 'loading' }
TonelReader >> loadExtension: name methods: methods [
	"An Extension file: the class must exist, the methods are compiled
	 against it and installed together, and what the file no longer has
	 under its own categories is removed."
	| compiled |
	definedClass := Smalltalk at: name
		ifAbsent: [^self fail: name asString, ' is not a class in this image'].
	compiled := self compileAll: methods substituting: nil for: name.
	self installAll: compiled.
	self removeExtensionMethodsNotIn: methods from: definedClass
]
