"
A method is not copied deeply.
"
Extension { #name : 'CompiledMethod' }

{ #category : '*Kernel-Protocol' }
CompiledMethod >> deepCopy [
	"Myself.  A method's literal frame names its class through an
	 Association whose value is the class, and a class is the image
	 (Bugs3 B47, and Behavior>>deepCopy).  Code is shared, not owned:
	 every context running this method and every block made from it hold
	 the same object, and a deep copy of anything that holds a method --
	 a MessageSend, a context, a class -- wants that to stay true."
	^self
]
