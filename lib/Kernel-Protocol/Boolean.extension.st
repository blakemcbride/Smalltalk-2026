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
	 term that would fail to evaluate otherwise."
	^self not or: [aBlockOrBoolean value]
]
