"
A Boolean answers cull: as itself, so that assert: and deny: can be handed
either a boolean or a block that computes one.  Without this the caller has
to know which the library wants, which is exactly the kind of thing a caller
should not have to know.
"
Extension { #name : 'Boolean' }

{ #category : '*Kernel-Protocol' }
Boolean >> cull: anObject [
	^self
]

{ #category : '*Kernel-Protocol' }
Boolean >> ==> aBlockOrBoolean [
	"Material implication: false unless I am true and it is not.

	 The argument is evaluated only when the receiver is true, which is what
	 makes this usable to say 'if this holds then that must too' about a
	 term that would fail to evaluate otherwise -- and is also why a block
	 is the useful thing to pass.

	 A plain Boolean is accepted as well, because that is how it reads when
	 both sides are already computed: `(a = b) & (b = c) ==> (a = c)' is the
	 whole of a transitivity test and there is nothing there to defer.
	 Sending #value to it would be tidier and does not work -- Boolean does
	 not answer #value, and adding it so that this one line could stay short
	 would put a method on every true and false in the image to serve a
	 caller that already has its answer."
	^self not or: [
		aBlockOrBoolean isBlock
			ifTrue: [aBlockOrBoolean value]
			ifFalse: [aBlockOrBoolean]]
]
