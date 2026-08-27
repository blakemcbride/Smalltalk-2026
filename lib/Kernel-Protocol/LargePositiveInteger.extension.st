Extension { #name : 'LargePositiveInteger' }

{ #category : '*Kernel-Protocol' }
LargePositiveInteger >> negated [
	"Minus me, as the smallest class that can hold the answer.

	 1983's version has a special case for SmallInteger minVal, and that
	 case is written for the SmallInteger 1983 had: it asks whether digit 2
	 is `0 - (minVal // 256)' and digit 1 is zero, which identifies 16384
	 and nothing else.  Here minVal is -4611686018427387904, eight bytes
	 wide, so the test never fires and the general path answers an
	 unnormalised LargeNegativeInteger.

	 That is visible from Smalltalk, because it is how the image's own
	 compiler builds a negative literal -- Parser>>primaryExpression reads
	 the magnitude and sends negated, and Scanner>>scanLitVec does the same
	 inside a literal array.  So `-4611686018427387904' compiled to a
	 LargeNegativeInteger, and `-4611686018427387904 == SmallInteger minVal'
	 answered FALSE for a number the system defines: the two were `=' but
	 not `==', and they landed in different buckets of an
	 IdentityDictionary.  The C bootstrap compiler had it right, so the two
	 compilers disagreed about a literal.

	 truncated is the normaliser -- it is what every other arithmetic method
	 here ends with -- and it makes the answer independent of how wide a
	 SmallInteger happens to be."
	((self digitAt: 1) = 0 and: [self digitLength = 1]) ifTrue: [^self].
	^(self copyto: (LargeNegativeInteger new: self digitLength)) truncated
]
