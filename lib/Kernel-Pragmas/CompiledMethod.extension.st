"
Reading a method's pragmas back.

The AdditionalMethodState is found by scanning the literal frame for one,
rather than at a fixed index.  Pharo puts it next to last; next to last
here is where the Blue Book header extension goes when a method declares a
primitive, so the position is spoken for.  A frame holds at most a few
dozen literals and nothing else instantiates that class, so the scan is
both cheap and unambiguous.
"
Extension { #name : 'CompiledMethod' }

{ #category : '*Kernel-Pragmas' }
CompiledMethod >> methodState [
	"The AdditionalMethodState in my literal frame, or nil."
	1 to: self numLiterals do: [:i |
		| each |
		each := self literalAt: i.
		each class == AdditionalMethodState ifTrue: [^each]].
	^nil
]

{ #category : '*Kernel-Pragmas' }
CompiledMethod >> pragmas [
	| state |
	state := self methodState.
	state isNil ifTrue: [^Array new: 0].
	^state pragmasFor: self
]

{ #category : '*Kernel-Pragmas' }
CompiledMethod >> pragmaAt: aSymbol [
	"The first pragma with this keyword, or nil."
	self pragmas do: [:each | (each hasKeyword: aSymbol) ifTrue: [^each]].
	^nil
]

{ #category : '*Kernel-Pragmas' }
CompiledMethod >> hasPragma: aSymbol [
	^(self pragmaAt: aSymbol) notNil
]
