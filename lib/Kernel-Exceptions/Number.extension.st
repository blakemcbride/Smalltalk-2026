"
Zero has no reciprocal, and saying so is a ZeroDivide.

See Fraction.extension.st, which is the same fault at the other receiver.
"
Extension { #name : 'Number' }

{ #category : '*Kernel-Exceptions' }
Number >> reciprocal [
	"Answer 1 divided by the receiver.  Signal ZeroDivide if the receiver
	 is 0."
	self = 0
		ifTrue: [^ZeroDivide new signal: 'zero has no reciprocal']
		ifFalse: [^1 / self]
]
