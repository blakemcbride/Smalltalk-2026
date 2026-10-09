Extension { #name : 'Array' }

{ #category : '*Kernel-Protocol' }
Array >> isArray [
	^true
]

{ #category : '*Kernel-Protocol' }
Array >> storeOn: aStream [
	"Use the literal form if possible -- with each element STORED, not
	 printed.  Since #(nil true false) inside a literal array means the
	 three objects (Bugs2 B8), the Symbols #nil, #true and #false have no
	 spelling there without their hash, and printOn: left it off:
	 `#(#nil #true #false) storeString' was `#(nil true false )', which
	 read back as the objects (Bugs6 KERNA-15).  storeOn: writes the hash,
	 and for every other literal writes what printOn: wrote."
	self isLiteral
		ifTrue: [
			aStream nextPut: $#; nextPut: $(.
			self do: [:element |
				element storeOn: aStream.
				aStream space].
			aStream nextPut: $)]
		ifFalse: [super storeOn: aStream]
]
