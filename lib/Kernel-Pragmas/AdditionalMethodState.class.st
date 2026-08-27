"
Where a method keeps its pragmas.

One of these sits in the literal frame of any method that declares a
pragma other than the two primitive forms, which mean something to the
compiler rather than to the image.  It is found by its CLASS rather than by
its position: Pharo puts it next to last, and next to last here is where
the Blue Book header extension goes when a method declares a primitive, so
that position is not available to borrow.

Each pragma arrives as an Array whose first element is the keyword and
whose rest are its arguments -- #(#deprecated: 'use #other') -- which is a
shape the compiler can build with the literal factories it already has.
"
Class {
	#name : 'AdditionalMethodState',
	#superclass : 'Object',
	#instVars : [ 'pragmaSpecs' ],
	#category : 'Kernel-Pragmas'
}

{ #category : 'accessing' }
AdditionalMethodState >> pragmaSpecs [
	^pragmaSpecs
]

{ #category : 'accessing' }
AdditionalMethodState >> pragmaSpecs: anArray [
	"Set the specs.

	 Here because the C bootstrap compiler builds one of these through a
	 factory in the compile context, and the IMAGE's compiler -- which now
	 reads pragmas too, see lib/Compiler-Fixes -- has only ordinary sends to
	 build it with."
	pragmaSpecs := anArray
]

{ #category : 'accessing' }
AdditionalMethodState >> pragmasFor: aMethod [
	| all |
	pragmaSpecs isNil ifTrue: [^Array new: 0].
	all := Array new: pragmaSpecs size.
	1 to: pragmaSpecs size do: [:i |
		all at: i put: (Pragma from: (pragmaSpecs at: i) method: aMethod)].
	^all
]
