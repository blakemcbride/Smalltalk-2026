"
A class written out as a Tonel file, in exactly the form the C reader and
TonelReader read back.

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
	| dir name stream |
	dir := FileDirectory directoryFromName: pathString setFileName: [:n | name := n].
	stream := dir textFile: name.
	[stream nextPutAll: (self sourceFor: aClass); shorten] ensure: [stream close]
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
TonelWriter >> writeHeaderFor: aClass [
	| civars |
	stream nextPutAll: 'Class {'; nextPut: Character cr.
	stream tab; nextPutAll: '#name : '. self quoted: aClass name. stream nextPut: $,. self cr.
	stream tab; nextPutAll: '#superclass : '.
	self quoted: (aClass superclass isNil ifTrue: ['nil'] ifFalse: [aClass superclass name]).
	aClass instVarNames isEmpty ifFalse: [
		stream nextPut: $,. self cr.
		stream tab; nextPutAll: '#instVars : '. self list: aClass instVarNames].
	aClass classVarNames isEmpty ifFalse: [
		stream nextPut: $,. self cr.
		stream tab; nextPutAll: '#classVars : '. self list: aClass classVarNames asSortedCollection].
	civars := aClass class instVarNames.
	civars isEmpty ifFalse: [
		stream nextPut: $,. self cr.
		stream tab; nextPutAll: '#classInstVars : '. self list: civars].
	aClass sharedPools isEmpty ifFalse: [
		stream nextPut: $,. self cr.
		stream tab; nextPutAll: '#pools : '.
		self list: (aClass sharedPools collect: [:pool | Smalltalk keyAtValue: pool])].
	aClass isVariable ifTrue: [
		stream nextPut: $,. self cr.
		stream tab; nextPutAll: '#type : #'.
		stream nextPutAll: (aClass isBytes ifTrue: ['bytes'] ifFalse: [aClass isPointers ifTrue: ['variable'] ifFalse: ['words']])].
	stream nextPut: $,. self cr.
	stream tab; nextPutAll: '#category : '. self quoted: aClass category asString.
	self cr.
	stream nextPut: $}.
	self cr
]

{ #category : 'private' }
TonelWriter >> writeMethodsOf: aBehavior named: aString [
	"Every method of aBehavior, by category and then by selector."
	| organization |
	organization := aBehavior organization.
	organization categories asSortedCollection do: [:category |
		(organization listAtCategoryNamed: category) asSortedCollection do: [:selector |
			self writeMethod: selector of: aBehavior named: aString category: category]]
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
	| comment |
	comment := aClass comment.
	(comment notNil and: [comment asString isEmpty not])
		ifTrue: [self writeComment: comment asString].
	self writeHeaderFor: aClass.
	self writeMethodsOf: aClass class named: aClass name, ' class'.
	self writeMethodsOf: aClass named: aClass name
]
