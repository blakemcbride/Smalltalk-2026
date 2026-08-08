"
How much stack a method has, counted once.

numStack answered 'frameSize - numTemps - numArgs', and numTemps already
counts the arguments -- the same mistake as MethodContext>>restart and for
the same reason.  For BlockClosure>>on:do: it answered 7 where the frame
has 9 slots free.
"
Extension { #name : 'CompiledMethod' }

{ #category : '*Kernel-Methods-Fixes' }
CompiledMethod >> numStack [
	"Answer the size of the available stack."
	self isQuick ifTrue: [^0].
	^self frameSize - self numTemps
]
