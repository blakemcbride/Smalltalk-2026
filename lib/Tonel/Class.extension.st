"
The class-definition message, with the write-back hook.

subclass:instanceVariableNames:classVariableNames:poolDictionaries:category:
is what the Browser evaluates when a class template is accepted, and what a
redefinition -- an instance variable added -- goes through.  1983's body is
kept as basicSubclass:... for TonelReader, and the hooked one tells TonelSource
afterwards, so that changing a file-backed class's shape in the Browser changes
the file's header too.
"
Extension { #name : 'Class' }

{ #category : '*Tonel' }
Class >> basicSubclass: t instanceVariableNames: f classVariableNames: d poolDictionaries: s category: cat [
	"1983's subclass:instanceVariableNames:classVariableNames:poolDictionaries:category:,
	 verbatim.  The layer below the write-back hook."

	self isVariable
		ifTrue:
			[self isPointers
				ifTrue: [^self
							variableSubclass: t
							instanceVariableNames: f
							classVariableNames: d
							poolDictionaries: s
							category: cat].
			self isBytes
				ifTrue: [^self
							variableByteSubclass: t
							instanceVariableNames: f
							classVariableNames: d
							poolDictionaries: s
							category: cat].
			^self
				variableWordSubclass: t
				instanceVariableNames: f
				classVariableNames: d
				poolDictionaries: s
				category: cat].
	^self class
		name: t
		inEnvironment: Smalltalk
		subclassOf: self
		instanceVariableNames: f
		variable: false
		words: true
		pointers: true
		classVariableNames: d
		poolDictionaries: s
		category: cat
		comment: nil
		changed: false
]

{ #category : '*Tonel' }
Class >> subclass: t instanceVariableNames: f classVariableNames: d poolDictionaries: s category: cat [
	"Define or redefine the class, as 1983 did -- and if it came from a Tonel
	 file, write the file."

	| newClass |
	newClass := self basicSubclass: t instanceVariableNames: f classVariableNames: d
		poolDictionaries: s category: cat.
	newClass notNil ifTrue: [TonelSource noteChangeIn: newClass].
	^newClass
]
