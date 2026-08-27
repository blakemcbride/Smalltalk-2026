"
Dividing a LargeInteger by zero signals ZeroDivide.

Integer>>// is where every integer division by zero ends up that the
SmallInteger primitives do not catch -- which is every receiver past 2^62 --
and \\ is built on it, so both were raising a plain Error where a
SmallInteger receiver raised ZeroDivide:

	[(2 raisedTo: 70) // 0] on: ZeroDivide do: [:e | 'caught']
	    ERR Error: division by 0

The message was already the right one; it had been copied from the fixed
SmallInteger method.  It was the class that was wrong, and the class is the
part a handler can name.  Integer>>/ needs nothing: it reaches the divide
through quo:, which SmallInteger already guards, and a LargeInteger
numerator over a zero denominator goes to Fraction>>reduced.

The body is Xerox's, unchanged but for the first line.
"
Extension { #name : 'Integer' }

{ #category : '*Kernel-Exceptions' }
Integer >> // aNumber [
	"Divide the receiver by the argument and return the result.
	 Round the result down towards negative infinity to make it a whole
	 integer."
	| q |
	aNumber = 0 ifTrue: [^ZeroDivide new signal: 'division by 0'].
	self = 0 ifTrue: [^0].
	q := self quo: aNumber.
	(q negative
		ifTrue: [q * aNumber ~= self]
		ifFalse: [q = 0 and: [self negative ~= aNumber negative]])
		ifTrue: [^q - 1"Truncate towards minus infinity"]
		ifFalse: [^q]
]
