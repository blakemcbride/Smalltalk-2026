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
