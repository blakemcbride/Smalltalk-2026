"
The extended variable bytecodes hold a six-bit index, and 1983's emitter did
not check it.

LeafNode>>emitLong:on: writes `type * 64 + index' as the operand of a 128,
129 or 130, and for the sixty-fifth instance variable index is 64: the sum is
0 * 64 + 64, which is the operand for TEMPORARY 0.  So a class with more than
sixty-four instance variables compiled `v65 ^v65' as `128 64 124' -- push
temporary 0, return -- and answered nil, and `v65 := x' stored into the
frame.  Bugs3 B23.  The C compiler refuses the same reference by name; this
makes the 1983 compiler refuse it too, so that `Compiler new compile:in:...'
and the Browser's decompile-and-recompile paths cannot install what the C
path would not.
"
Extension { #name : 'LeafNode' }

{ #category : '*Compiler-Fixes' }
LeafNode >> emitLong: mode on: aStream [
	"emit extended variable access -- 1983's method, with the index checked
	 against the six bits that will hold it before it is written.  Every
	 line but the check is Xerox's."

	| type index |
	code < 256
		ifTrue: [code < 16
				ifTrue:
					[type := 0.
					index := code]
				ifFalse:
					[code < 32
						ifTrue:
							[type := 1.
							index := code - 16]
						ifFalse:
							[code < 96
								ifTrue:
									[type := code // 32 + 1.
									index := code \\ 32]
								ifFalse: [self error:
										'Sends should be handled in SelectorNode']]]]
		ifFalse:
			[index := code \\ 256.
			type := code // 256 - 1].
	index > 63 ifTrue:
		[^self error: 'variable ' , (index + 1) printString
			, ' is beyond the 64 the bytecode set can address'].
	aStream nextPut: mode.
	aStream nextPut: type * 64 + index
]
