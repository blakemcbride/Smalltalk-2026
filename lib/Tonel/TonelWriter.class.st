"
A class written out as a Tonel file, in exactly the form the C reader and
TonelReader read back; and a package's extension of a class written out as
the Extension file it came from.

The point is the round trip.  A method's source in the image is its pattern, a
carriage return, and its body -- that is how both readers assemble one -- so
this writes `Class >> pattern [', the body verbatim, and `]', and the file
reads back to the same source character for character.  A method that was
compiled in the Browser rather than read from a file has a body that neither
begins nor ends with a line break; those get one each, so that the file is laid
out like every other and the reloaded source differs from the original by two
line breaks, once, and never again.

Methods are written in category order and by selector within a category, class
side first, so that two writes of the same class are the same file and a diff
between two versions shows the change and nothing else.

WHICH METHODS A FILE HOLDS is the format's rule and not this writer's: a
category that begins with a star names another package -- `*Concurrency' is
Concurrency's extension of this class, `*trait:TGreeting' is a trait the
loader flattened in -- and a Class file holds none of those, because each of
them has a file of its own.  An Extension file holds exactly the methods under
its package's prefix: the prefix itself and any tag under it,
`*Rest-Server-private'.  Until the writer knew this it wrote the whole class
every time, whatever file the class had been loaded from, and one Browser
accept on a class loaded from a two-method extension file replaced that file
with a Class definition and forty-four methods from four packages (Bugs3 B59).

Two things the loader adds to a class are left out for the same reason: the
trait's methods, whose file is the trait's, and the `new' it synthesizes for a
class that defines initialize and no new, which the file never had and would
not read back byte for byte.  The header says what the loader read: a weak
class is `#type : 'weak'' and an ephemeron `#type : 'ephemeron'', and a class
that took a trait names it in `#traits' and `#classTraits' from the record
the bootstrap keeps in TraitCompositions (Bugs3 B61: weak came back as
variable, and Greeter came back owning its trait's methods).

Line endings: the text is built with carriage returns and written through a
TextFileStream, which is where the image's line ending becomes the host's.
"
Class {
	#name : 'TonelWriter',
	#superclass : 'Object',
	#instVars : [ 'stream' ],
	#category : 'Tonel'
}

{ #category : 'writing' }
TonelWriter class >> sourceFor: aClass [
	"The Tonel text for aClass, as a String with carriage returns."
	^self new writeClass: aClass; contents
]

{ #category : 'writing' }
TonelWriter class >> write: aClass toFile: pathString [
	"Write aClass to the file at pathString, replacing what was there."
	self writeText: (self sourceFor: aClass) toFile: pathString
]

{ #category : 'writing' }
TonelWriter class >> extensionSourceFor: aClass prefix: prefixString comment: commentOrNil [
	"The Tonel text of an Extension file: commentOrNil at the top, the
	 `Extension { #name : ... }' line, and the methods of aClass whose
	 category is prefixString -- `*Concurrency' -- or a tag under it."
	^self new writeExtensionOf: aClass prefix: prefixString comment: commentOrNil; contents
]

{ #category : 'writing' }
TonelWriter class >> writeExtensionOf: aClass prefix: prefixString comment: commentOrNil toFile: pathString [
	"Write the Extension file for aClass under prefixString to pathString."
	self writeText: (self extensionSourceFor: aClass prefix: prefixString comment: commentOrNil)
		toFile: pathString
]

{ #category : 'private' }
TonelWriter class >> writeText: aString toFile: pathString [
	| dir name stream |
	dir := FileDirectory directoryFromName: pathString setFileName: [:n | name := n].
	stream := dir textFile: name.
	[stream nextPutAll: aString; shorten] ensure: [stream close]
]

{ #category : 'shapes' }
TonelWriter class >> typeOf: aClass [
	"The word a Tonel header's #type gives aClass's instances, or nil for
	 an ordinary class: what both readers understand, weak and ephemeron
	 included.  Asked by TonelReader too, to decide whether a header
	 describes the shape a class already has."
	aClass isEphemeron ifTrue: [^'ephemeron'].
	aClass isVariable ifFalse: [^nil].
	aClass isBytes ifTrue: [^'bytes'].
	aClass isPointers ifFalse: [^'words'].
	^aClass isWeak ifTrue: ['weak'] ifFalse: ['variable']
]

{ #category : 'shapes' }
TonelWriter class >> category: category isUnder: prefixString [
	"Whether a method category belongs to the package whose extension
	 categories begin with prefixString: the prefix itself,
	 `*Concurrency', or a tag under it, `*Concurrency-private'.

	 The hyphen is the convention's own ambiguity: a package named
	 Foo-Bar writes `*Foo-Bar', which reads as Foo's tag Bar.  Pharo tells
	 them apart by knowing every package's name; here the prefix comes
	 from the package.st beside the file, which is the same knowledge for
	 the one package that matters."
	category = prefixString ifTrue: [^true].
	^category size > prefixString size
		and: [(category copyFrom: 1 to: prefixString size) = prefixString
			and: [(category at: prefixString size + 1) = $-]]
]

{ #category : 'shapes' }
TonelWriter class >> isOwnCategory: category [
	"Whether a category is the class's own rather than another package's
	 or a trait's: anything that does not begin with a star."
	^category isEmpty or: [category first ~= $*]
]

{ #category : 'shapes' }
TonelWriter class >> classTraitsFor: composition [
	"The `#classTraits' line Pharo writes beside a `#traits' line, which
	 the loader accepts and drops as saying nothing the first does not:
	 each trait's name followed by ` classTrait', joined by ` + ', with an
	 exclusion (`- {#x}') and its parentheses left out, since those
	 narrow the instance side only.  `TA + (TB - {#x})' gives
	 `TA classTrait + TB classTrait'."
	| out terms word |
	terms := OrderedCollection new.
	word := WriteStream on: (String new: 32).
	composition do: [:c |
		c = $+
			ifTrue: [terms add: word contents. word := WriteStream on: (String new: 32)]
			ifFalse: [word nextPut: c]].
	terms add: word contents.
	out := WriteStream on: (String new: composition size + 32).
	terms do: [:term | | name minus |
		name := term reject: [:c | c = $( or: [c = $)]].
		minus := name indexOf: $-.
		minus > 0 ifTrue: [name := name copyFrom: 1 to: minus - 1].
		out nextPutAll: name trimmed; nextPutAll: ' classTrait']
		separatedBy: [out nextPutAll: ' + '].
	^out contents
]

{ #category : 'initialization' }
TonelWriter >> initialize [
	stream := WriteStream on: (String new: 4096)
]

{ #category : 'accessing' }
TonelWriter >> contents [
	^stream contents
]

{ #category : 'private' }
TonelWriter >> cr [
	stream nextPut: Character cr
]

{ #category : 'private' }
TonelWriter >> quoted: aString [
	"A STON string: single quotes, doubled inside."
	stream nextPut: $'.
	aString do: [:c |
		c = $' ifTrue: [stream nextPut: $'].
		stream nextPut: c].
	stream nextPut: $'
]

{ #category : 'private' }
TonelWriter >> list: aCollection [
	stream nextPutAll: '[ '.
	aCollection do: [:each | self quoted: each asString] separatedBy: [stream nextPutAll: ', '].
	stream nextPutAll: ' ]'
]

{ #category : 'private' }
TonelWriter >> writeComment: aString [
	"The class comment, in double quotes with any inside doubled, on lines
	 of its own.  Trimmed first: the reader hands the comment over with the
	 line breaks around it and the changes log hands it back without, and
	 the file must come out the same either way."
	stream nextPut: $".
	self cr.
	aString trimmed do: [:c |
		c = $" ifTrue: [stream nextPut: $"].
		stream nextPut: c].
	self cr.
	stream nextPut: $".
	self cr
]

{ #category : 'private' }
TonelWriter >> patternLengthOf: source selector: selector [
	"How many characters of source are the message pattern: one word for a
	 unary selector, two for a binary one, and two per keyword otherwise.
	 A source compiled from a single line in the Browser has no line break
	 between its pattern and its body, so the pattern is found by counting
	 rather than by looking for one."
	| words i n |
	words := selector isEmpty ifTrue: [1] ifFalse: [
		(selector last = $:)
			ifTrue: [2 * (selector occurrencesOf: $:)]
			ifFalse: [selector first isLetter ifTrue: [1] ifFalse: [2]]].
	i := 1.
	n := 0.
	[n < words and: [i <= source size]] whileTrue: [
		[i <= source size and: [(source at: i) isSeparator]] whileTrue: [i := i + 1].
		[i <= source size and: [(source at: i) isSeparator not]] whileTrue: [i := i + 1].
		n := n + 1].
	^i - 1
]

{ #category : 'private' }
TonelWriter >> field: aString [
	"The comma and line break that end the previous header field, and the
	 tab and key that begin this one."
	stream nextPut: $,. self cr.
	stream tab; nextPutAll: aString
]

{ #category : 'private' }
TonelWriter >> writeHeaderFor: aClass [
	"The Class header, its fields in the order Pharo writes them, so that a
	 file that came from Pharo or was written by hand in that order comes
	 back as it went: name, superclass, the traits, the type, the four
	 kinds of variable, and the category."
	| civars traits type |
	stream nextPutAll: 'Class {'; nextPut: Character cr.
	stream tab; nextPutAll: '#name : '. self quoted: aClass name. stream nextPut: $,. self cr.
	stream tab; nextPutAll: '#superclass : '.
	self quoted: (aClass superclass isNil ifTrue: ['nil'] ifFalse: [aClass superclass name]).
	traits := aClass traitComposition.
	traits notNil ifTrue: [
		self field: '#traits : '. self quoted: traits.
		self field: '#classTraits : '. self quoted: (TonelWriter classTraitsFor: traits)].
	type := TonelWriter typeOf: aClass.
	type notNil ifTrue: [
		self field: '#type : '. self quoted: type].
	aClass instVarNames isEmpty ifFalse: [
		self field: '#instVars : '. self list: aClass instVarNames].
	aClass classVarNames isEmpty ifFalse: [
		self field: '#classVars : '. self list: aClass classVarNames asSortedCollection].
	aClass sharedPools isEmpty ifFalse: [
		self field: '#pools : '.
		self list: (aClass sharedPools collect: [:pool | Smalltalk keyAtValue: pool])].
	civars := aClass class instVarNames.
	civars isEmpty ifFalse: [
		self field: '#classInstVars : '. self list: civars].
	self field: '#category : '. self quoted: aClass category asString.
	self cr.
	stream nextPut: $}.
	self cr
]

{ #category : 'private' }
TonelWriter >> writeMethodsOf: aBehavior named: aString selecting: aBlock [
	"Every method of aBehavior in a category aBlock accepts, by category
	 and then by selector -- except the loader's synthesized `new'."
	| organization |
	organization := aBehavior organization.
	organization categories asSortedCollection do: [:category |
		(aBlock value: category asString) ifTrue: [
			(organization listAtCategoryNamed: category) asSortedCollection do: [:selector |
				(self isSynthesized: selector of: aBehavior) ifFalse: [
					self writeMethod: selector of: aBehavior named: aString category: category]]]]
]

{ #category : 'private' }
TonelWriter >> isSynthesized: selector of: aBehavior [
	"Whether the method is the `new ^super new initialize' the loader
	 writes for a class that defines initialize and no new, rather than
	 one the file had.  Known by the comment the loader puts in it --
	 src/boot/bootstrap.c, synthesize_initializing_new, says the words are
	 a marker and must change here and there together."
	| source |
	(selector == #new and: [aBehavior isMeta]) ifFalse: [^false].
	source := aBehavior sourceCodeAt: #new.
	^source notNil and: [source includesSubstring: 'Synthesized by the loader']
]

{ #category : 'private' }
TonelWriter >> writeMethod: selector of: aBehavior named: aString category: category [
	| source cut pattern body cr |
	cr := Character cr.
	source := aBehavior sourceCodeAt: selector.
	cut := self patternLengthOf: source selector: selector.
	pattern := source copyFrom: 1 to: cut.
	body := source copyFrom: cut + 1 to: source size.
	"The one separator after the pattern is the reader's join, or the
	 Browser's space; it is not part of the body between the brackets."
	(body isEmpty not and: [body first isSeparator]) ifTrue: [body := body copyFrom: 2 to: body size].
	(body isEmpty or: [body first ~= cr]) ifTrue: [body := (String with: cr), body].
	body last ~= cr ifTrue: [body := body, (String with: cr)].
	self cr.
	stream nextPutAll: '{ #category : '. self quoted: category asString. stream nextPutAll: ' }'.
	self cr.
	stream nextPutAll: aString; nextPutAll: ' >> '; nextPutAll: pattern trimmed; nextPutAll: ' ['.
	stream nextPutAll: body.
	stream nextPut: $].
	self cr
]

{ #category : 'writing' }
TonelWriter >> writeClass: aClass [
	"The Class file: comment, header, and the class's own methods -- those
	 in categories that do not begin with a star, the others being some
	 other package's or a trait's and written by their own files."
	| comment own |
	comment := aClass comment.
	(comment notNil and: [comment asString isEmpty not])
		ifTrue: [self writeComment: comment asString].
	self writeHeaderFor: aClass.
	own := [:category | TonelWriter isOwnCategory: category].
	self writeMethodsOf: aClass class named: aClass name, ' class' selecting: own.
	self writeMethodsOf: aClass named: aClass name selecting: own
]

{ #category : 'writing' }
TonelWriter >> writeExtensionOf: aClass prefix: prefixString comment: commentOrNil [
	"The Extension file for one package's methods on aClass: the file's
	 own comment, if the file had one -- an extension has no class comment
	 to carry, so the reader keeps what was at the top of the file and
	 hands it back here -- the one-line header, and the methods under the
	 package's prefix, class side first."
	| mine |
	(commentOrNil notNil and: [commentOrNil isEmpty not])
		ifTrue: [self writeComment: commentOrNil].
	stream nextPutAll: 'Extension { #name : '. self quoted: aClass name. stream nextPutAll: ' }'.
	self cr.
	mine := [:category | TonelWriter category: category isUnder: prefixString].
	self writeMethodsOf: aClass class named: aClass name, ' class' selecting: mine.
	self writeMethodsOf: aClass named: aClass name selecting: mine
]
