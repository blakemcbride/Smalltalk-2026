"
The one operation a lock-free algorithm cannot be written without.
"
Extension { #name : 'Object' }

{ #category : '*Concurrency' }
Object >> compareAndSwapSlot: index from: expected to: wanted [
	"Store wanted in the indexed field only if it currently holds
	 expected, and answer whether it did.

	 It answers the BOOLEAN rather than the old value, because that is
	 what every caller tests and it leaves no room to forget the
	 comparison.  The primitive fails for a receiver with no pointer
	 fields, or an index outside them, rather than writing somewhere
	 else."
	<primitive: 245>
	^self primitiveFailed
]
