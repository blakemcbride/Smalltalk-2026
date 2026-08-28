"
The class-definition funnel, with the one ceiling the bytecode set imposes.

Every subclass:instanceVariableNames:... form, the variable and byte and word
kinds included, and the metaclass's instanceVariableNames:, reaches
subclassOf:oldClass:instanceVariableNames:variable:words:pointers:ifBad: to
lay the instance variables out.  The extended push and store bytecodes carry
a six-bit index, so a class may have sixty-four instance variables, its
superclasses' counted, and no method can reach a sixty-fifth: the C compiler
refuses the reference (Bugs3 B23) and 1983's Encoder emitted a push of
temporary 0 for it.  A class that declares more used to be created anyway,
its far fields unreachable from any method and each accessor refused one at a
time with a message about the variable rather than the class.  The class is
what is wrong, so the class is what is refused, here, where every definition
passes.
"
Extension { #name : 'ClassDescription' }

{ #category : '*Kernel-Protocol' }
ClassDescription >> subclassOf: newSuper oldClass: oldClass instanceVariableNames: newInstVarString variable: v words: w pointers: p ifBad: badBlock [
	"Basic initialization message for creating classes using the information
	 provided as arguments.  Answer whether old instances will be
	 invalidated.

	 1983's method with one check added, at the point where the full list
	 of names is first known -- the same point as the reserved-name check,
	 and refused the same way, with an error and the bad block: more than
	 sixty-four instance variables in all.  See the class comment above.
	 Everything else is Xerox's, in Xerox's order -- the superclass
	 change, the reserved-name check, the format -- because the Browser
	 and TonelReader both depend on which of those happens when."

	| old new usedNames invalid oldSuperMeta newInstVarArray |
	old := self allInstVarNames.
	usedNames := #(self super thisContext true false nil ) asSet.
	newInstVarArray := Scanner new scanFieldNames: newInstVarString.
	(invalid := superclass ~~ newSuper)
		ifTrue:
			["superclass changed"
			oldSuperMeta := superclass class.
			superclass removeSubclass: self.
			superclass := newSuper.
			superclass addSubclass: self.
			self class superclass == oldSuperMeta
				ifTrue: ["Only false when self is a metaclass"
						self class superclass: newSuper class]].
	instanceVariables := nil.  "To give us all super names"
	new := self allInstVarNames , newInstVarArray.
	new size > 64
		ifTrue:
			[self error: self name , ' would have ' , new size printString
				, ' instance variables, its superclasses'' included; the bytecode set addresses 64'.
			^badBlock value].
	new do:
		[:fieldName |
		(usedNames includes: fieldName)
			ifTrue:
				[self error: fieldName , ' is reserved (maybe in a superclass)'.
				^badBlock value].
		usedNames add: fieldName].
	instanceVariables := newInstVarArray size = 0
		ifTrue: [nil]
		ifFalse: [newInstVarArray].
	invalid := invalid | (new ~= old).
	"field names changed"
	old := format.
	self
		format: new size
		variable: v
		words: w
		pointers: p.
	invalid := invalid | (format ~= old).
	"format changed"
	^invalid
]
