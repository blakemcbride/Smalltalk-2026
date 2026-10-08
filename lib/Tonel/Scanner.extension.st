"
The image's own compiler learns `:=' for assignment.

Every file in lib/ writes assignment as `:=', and every one of them was
compiled by the C bootstrap, which has always accepted it.  The compiler INSIDE
the image is 1983's, and 1983 spelt assignment with the left arrow -- the
underscore, in this character set -- and nothing else: its scanner maps `:' to
a colon token and `=' to a binary selector, so `a := 1' is `a : = 1', which is
a syntax error.  Nothing noticed for as long as the only code the image
compiled was typed into a Browser by somebody who knew that.  A Tonel file
loaded at run time is written in the spelling every other file in this tree
uses, and the first one compiled here failed on its first assignment -- and,
because a syntax error with nobody to tell opens a notifier and suspends the
process, failed by hanging a server.

So scanToken is 1983's, with one line after it: a colon followed by an equals
sign is the left-arrow token.  The colon on its own still scans as a colon,
which is what a block argument needs, and keywords never reach here -- they are
read whole by xLetter.

And patternEndIn:, for TonelWriter, which has to split a method's source into
the pattern that goes on the `Class >> pattern [' line and the body that goes
between the brackets, and once did it by counting words (Bugs5 COMP-3); and
patternTokensIn:, the words of that pattern, from which it writes the line
(Bugs5 FILES-12).  Both read the pattern through scanPattern:, which is scan:
with the one difference the closure dialect makes to a pattern: an underscore
is a letter there.  To this scanner it is the left arrow, so a Pharo-ported
`simulate_vmMilliseconds: x' read as the unary selector `simulate' followed by
an assignment, and TonelWriter wrote `Wb >> simulate [' with `_vmMilliseconds:
x' as the first line of the body -- a file that never loaded again, from one
accept of ANY method of the class (Bugs6 COMP-14).  A vertical bar is a binary
selector in a pattern too, and its argument is kept.
"
Extension { #name : 'Scanner' }

{ #category : '*Tonel' }
Scanner >> scanToken [
	"1983's scanToken, and then `:=' read as the assignment arrow."

	[(tokenType := typeTable at: hereChar asciiValue) == #xDelimiter]
		whileTrue: [self step].  "Skip delimiters fast, there almost always is one."
	mark := source position - 1.
	(tokenType at: 1) = $x "x as first letter"
		ifTrue: [self perform: tokenType "means perform to compute token & type"]
		ifFalse: [token := self step asSymbol "else just unique the first char"].
	(tokenType == #colon and: [hereChar = $=]) ifTrue: [
		self step.
		tokenType := #leftArrow.
		token := #'_'].
	^token
]

{ #category : '*Tonel' }
Scanner >> scanPattern: aString [
	"scan:, on the message pattern at the front of aString, a method's
	 source, as the C compiler reads a pattern in the closure dialect --
	 which every Tonel file is written in and compiled from -- and so with
	 an underscore read as a letter.  The type table says it is the left
	 arrow, and for everything else this scanner does it must go on saying
	 so; it is a class variable, so the instance takes a copy and changes
	 its own.  Only when the text has one: most patterns do not, and the
	 copy is 256 slots.  The first token is scanned by scan: with the old
	 table, so the stream is put back and the two characters of lookahead
	 and the token are read again with the new one.  Bugs6 COMP-14.

	 Two spaces are put after the source before it is scanned.  The
	 Scanner reads two characters ahead and takes a token's position from
	 how far it has read, which stops growing at the end of the input, so
	 a token that ends the source -- a unary method with no body -- would
	 be placed one character early."
	self scan: (ReadStream on: aString , '  ').
	(aString includes: $_) ifFalse: [^self].
	typeTable := typeTable copy.
	typeTable at: $_ asciiValue put: #xLetter.
	currentComment := nil.
	source reset.
	self on: source.
	self scanToken
]

{ #category : '*Tonel' }
Scanner >> patternEndIn: aString [
	"The index of the last character of the message pattern at the front
	 of aString, a method's source: the end of the unary selector, or of
	 the argument after the binary selector, or of the argument after the
	 last keyword.

	 Found with the tokens the compiler reads rather than by counting
	 words, which is what TonelWriter did -- two words to a keyword -- and
	 `with:x and:y ^[x + y] value' is two words where it counted four, so
	 the pattern it wrote took `^[x' and `+' with it and the body lost a
	 bracket.  The reload read a method with a different selector and
	 removed the real one as gone from the file (Bugs5 COMP-3).  A comment
	 between the keywords is skipped, as the compiler skips it, and so is
	 one after the pattern, which belongs to the body.

	 Read through scanPattern:, which puts two spaces after the source --
	 see there for why -- and reads an underscore as a letter.  A vertical
	 bar is #verticalBar to this scanner and not #binary, so `| x' used to
	 end at the bar and its argument went into the body (Bugs6 COMP-14)."

	| end |
	self scanPattern: aString.
	tokenType == #keyword ifTrue: [
		[tokenType == #keyword] whileTrue: [
			self scanToken.
			end := mark + token size - 1.
			self scanToken].
		^end].
	(tokenType == #binary or: [tokenType == #verticalBar]) ifTrue: [self scanToken].
	^mark + token size - 1
]

{ #category : '*Tonel' }
Scanner >> patternTokensIn: aString [
	"The words of the message pattern at the front of aString, a method's
	 source, as Strings and in order: the unary selector; or the binary
	 selector and its argument; or each keyword followed by its argument.
	 Read as patternEndIn: reads them, so the two agree on where the
	 pattern ends, and with whatever spacing and comments the source had
	 left out of them.

	 For TonelWriter, which writes these one space apart on the
	 `Class >> pattern [' line rather than copying the pattern's own
	 text (Bugs5 FILES-12): Pharo's reader finds the selector by
	 splitting that line at blanks and colons, so `+x' read as a unary
	 selector of two characters, and a comment there was taken for a
	 keyword or an argument."

	| tokens |
	tokens := OrderedCollection new.
	self scanPattern: aString.
	tokenType == #keyword ifTrue: [
		[tokenType == #keyword] whileTrue: [
			tokens add: token asString.
			self scanToken.
			tokens add: token asString.
			self scanToken].
		^tokens asArray].
	tokens add: token asString.
	(tokenType == #binary or: [tokenType == #verticalBar]) ifTrue: [
		self scanToken.
		tokens add: token asString].
	^tokens asArray
]
