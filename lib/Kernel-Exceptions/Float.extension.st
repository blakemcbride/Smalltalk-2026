"
Dividing a Float by zero signals ZeroDivide, as dividing a SmallInteger does.

See SmallInteger.extension.st for the shape.  The reason this file exists is
that the fix was applied to SmallInteger alone, and a SmallInteger receiver
is the one case a careful caller could have got right by accident:

	[1/0]     on: ZeroDivide do: [:e | 'caught']    'caught'
	[1.0/0]   on: ZeroDivide do: [:e | 'caught']    ERR Error: attempt to
	                                                divide by zero
	[1.0/0.0] on: ZeroDivide do: [:e | 'caught']    likewise

-- so a handler written for the thing that actually happens in a program,
which is dividing computed Floats, did not catch it.  ZeroDivide is what a
caller can name; `self error:' is what a caller has to catch by matching on
prose, which is not an interface.

The body is Xerox's, unchanged but for that last line.
"
Extension { #name : 'Float' }

{ #category : '*Kernel-Exceptions' }
Float >> / aNumber [
	"Divide the receiver by the argument and return the exact result as a
	 Float.  Fail if the argument is not a Float."
	<primitive: 50>
	aNumber = 0
		ifTrue: [^ZeroDivide new signal: 'attempt to divide by zero']
		ifFalse: [^self retry: #/ coercing: aNumber]
]
