"
SmallInteger>>* with 1983's zero shortcut kept for Integers only.
"
Extension { #name : 'SmallInteger' }

{ #category : '*Kernel-Protocol' }
SmallInteger >> * aNumber [
	"Multiply the receiver by the argument and answer with the result if it
	 is a SmallInteger.  Fail if the argument or the result is not a
	 SmallInteger.  Essential.  No Lookup.  See Object documentation
	 whatIsAPrimitive.

	 1983's fallback answered the SmallInteger 0 for a zero receiver before
	 coercing anything, so `0 * 2.5' was 0 where `2.5 * 0' was 0.0, `0 *
	 Float nan' was 0 where IEEE says NaN, and `0.1 roundTo: 0.25' -- which
	 multiplies a rounded quotient back -- answered an Integer while `0.3
	 roundTo: 0.25' answered a Float (Bugs6 KERNA-8).  The shortcut stays
	 for an Integer argument, where it spares LargeInteger>>* a check of
	 its own; anything else is coerced as the manual's coercion rule says."

	<primitive: 9>
	aNumber isInteger ifFalse: [^self retry: #* coercing: aNumber].
	self = 0 ifTrue: [^0].
	^super * aNumber
]
