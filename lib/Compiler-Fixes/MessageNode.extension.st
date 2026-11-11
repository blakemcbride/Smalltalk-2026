Extension { #name : 'MessageNode' }

{ #category : '*Compiler-Fixes' }
MessageNode >> store: expr from: encoder [
	"An assignment to a name a Debugger's code pane binds to a message:
	 `ctxt tempAt: n' becomes `ctxt tempAt: n put: expr', as in 1983, and
	 `vector at: k' becomes `vector at: k put: expr' -- the binding
	 Encoder>>init:context:notifying: gives a temporary a method shares
	 with its blocks, which lives in an Array in one of the frame's slots
	 (Bugs07 X-27)."
	| storeSelector |
	storeSelector := selector key == #tempAt:
		ifTrue: [#tempAt:put:]
		ifFalse: [selector key == #at: ifTrue: [#at:put:] ifFalse: [nil]].
	storeSelector isNil ifTrue: [^self error: 'cant transform this message'].
	^MessageNode new
		receiver: receiver
		selector: storeSelector
		arguments: (arguments copyWith: expr)
		precedence: precedence
		from: encoder
]
