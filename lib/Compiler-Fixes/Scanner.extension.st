Extension { #name : 'Scanner' }

{ #category : '*Compiler-Fixes' }
Scanner class >> initialize [
	"The 1983 character-type table, with braces added.

	 `{' and `}' fell through to the default, #xBinary, so `{1. 2}' scanned
	 as the binary selector #'{' and the Parser had nothing to do with it.
	 They are punctuation in every dialect that has dynamic arrays and they
	 are not legal in a binary selector anywhere, so giving them types of
	 their own takes nothing away -- Scanner class>>isLiteralSymbol: stops
	 calling #'{' a literal symbol, which it never was.

	 The table is written out whole rather than patched afterwards because
	 it is a class variable read by every Scanner: one method that builds it
	 is something a reader can check, and a second method that edits it
	 later is a thing that has to be found first.

	 Scanner initialize."

	| newTable |
	newTable := Array new: 256 withAll: #xBinary. "default"
	newTable atAll: #(9 10 12 13 32 ) put: #xDelimiter. "tab lf ff cr space"
	newTable atAll: ($0 asciiValue to: $9 asciiValue) put: #xDigit.
	newTable atAll: ($A asciiValue to: $Z asciiValue) put: #xLetter.
	newTable atAll: ($a asciiValue to: $z asciiValue) put: #xLetter.
	newTable at: 30 put: #doIt.
	newTable at: $" asciiValue put: #xDoubleQuote.
	newTable at: $# asciiValue put: #xLitQuote.
	newTable at: $$ asciiValue put: #xDollar.
	newTable at: $' asciiValue put: #xSingleQuote.
	newTable at: $( asciiValue put: #leftParenthesis.
	newTable at: $) asciiValue put: #rightParenthesis.
	newTable at: $. asciiValue put: #period.
	newTable at: $: asciiValue put: #colon.
	newTable at: $; asciiValue put: #semicolon.
	newTable at: $[ asciiValue put: #leftBracket.
	newTable at: $] asciiValue put: #rightBracket.
	newTable at: ${ asciiValue put: #leftBrace.
	newTable at: $} asciiValue put: #rightBrace.
	newTable at: $^ asciiValue put: #upArrow.
	newTable at: $_ asciiValue put: #leftArrow.
	newTable at: $| asciiValue put: #verticalBar.
	EndChar := 30 asCharacter.
	TypeTable := newTable
]

{ #category : '*Compiler-Fixes' }
Scanner >> xLitQuote [
	"1983's, plus #[1 2 3].

	 A `#' followed by `[' is a byte array literal, which the C bootstrap
	 compiler has always read and this one did not: it scanned the `[' as a
	 left bracket and the Parser saw a block where a literal was meant.
	 Answered as a #literal token, exactly as #(1 2 3) is, so nothing
	 downstream has to know it exists."

	| start |
	self step. "litQuote"
	self scanToken.
	tokenType = #leftParenthesis
		ifTrue: [
			start := mark.
			self scanToken; scanLitVec.
			tokenType == #doIt ifTrue: [
				mark := start.
				self offEnd: 'Unmatched parenthesis']]
		ifFalse: [
			tokenType = #leftBracket
				ifTrue: [
					start := mark.
					self scanToken; scanLitByteArray.
					tokenType == #doIt ifTrue: [
						mark := start.
						self offEnd: 'Unmatched bracket']]
				ifFalse: [
					(#(word keyword colon) includes: tokenType)
						ifTrue: [self scanLitWord]]].
	tokenType := #literal
]

{ #category : '*Compiler-Fixes' }
Scanner >> scanLitByteArray [
	"The elements of #[...], the opening bracket already past.

	 Shaped like scanLitVec: it is entered with the first element scanned
	 and it leaves the closing bracket as the current token, so that
	 xLitQuote can turn the whole thing into one literal.

	 A byte array holds bytes and nothing else, so anything that is not an
	 integer 0 to 255 is refused here rather than stored and found later --
	 which is the same rule Collection>>asByteArray states and the reason
	 that method exists."

	| bytes |
	bytes := OrderedCollection new.
	[tokenType == #rightBracket or: [tokenType == #doIt]] whileFalse: [
		(tokenType == #number
			and: [token isInteger and: [token between: 0 and: 255]])
				ifFalse: [^self offEnd: 'a byte array holds integers 0 to 255'].
		bytes addLast: token.
		self scanToken].
	token := bytes asByteArray
]

{ #category : '*Compiler-Fixes' }
Scanner >> xBinary [
	"A binary selector: a run of binary characters.

	 1983 takes TWO at most, which is the Smalltalk-80 grammar, and the
	 image contains selectors that need more -- Boolean>>==> and
	 Collection class>><- are both in it, put there by the C bootstrap
	 compiler, and neither could be re-parsed by the image that holds them:
	 `==>' scanned as `==' then `>', and `<-' as `<' then `-' because 1983
	 refuses to take a minus as the second character at all.

	 That refusal is doing one real job, and it is kept in the only place it
	 matters: a minus that begins a NUMBER is not part of the selector, so
	 `-2@-2' reads as `-2 @ -2' rather than as `#@-' sent to a number.
	 Cursor class>>initialize is full of offsets written that way.  Testing
	 the character after the minus is what lets the rest of the run be taken
	 whole -- which is exactly the rule src/compiler/lexer.c states for the
	 closures dialect, and this is the same rule in the same words."

	tokenType := #binary.
	token := Symbol internCharacter: self step.
	[(typeTable at: hereChar asciiValue) == #xBinary
		and: [(hereChar = $- and: [aheadChar isDigit]) not]]
			whileTrue: [token := (token , (String with: self step)) asSymbol]
]
