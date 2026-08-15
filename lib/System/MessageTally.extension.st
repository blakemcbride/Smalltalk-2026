Extension { #name : 'MessageTally' }

{ #category : '*System' }
MessageTally >> into: aCollection [
	"Fold my tally into the one that collection already holds for my method,
	 adding a fresh tally if it holds none.

	 1983 writes this as `aCollection find: self ifAbsent: [...]' followed by
	 `(aCollection basicAt: index) bump: tally' -- it asks a Set for the
	 INDEX of an element and then reads the element back out of the Set's
	 indexed part by hand.  Both halves are 1983 Set internals: Pharo's Set
	 keeps its elements in an `array' instance variable, so it has no
	 find:ifAbsent: and its indexed part is not where the elements are.
	 Restoring find:ifAbsent: alone would have answered an index into a
	 place basicAt: does not read.

	 So this asks the question directly instead.  detect:ifNone: uses = ,
	 which is what the Set was using through findElementOrNil: anyway --
	 MessageTally>>= compares methods by identity -- so the answer is the
	 same one, arrived at without reaching inside anybody."
	| existing |
	existing := aCollection detect: [:each | each = self] ifNone: [nil].
	existing isNil ifTrue: [
		existing := aCollection add:
			(MessageTally new class: class method: method)].
	existing bump: tally
]
