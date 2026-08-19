Extension { #name : 'SystemTracer' }

{ #category : '*Scope-Fixes' }
SystemTracer >> allCallsOn: aLiteral clampedBy: nameSet [
	"Specialized version of the SystemDictionary allCallsOn:,
	which doesnt include calls from clamped classes."

	| aSortedCollection classNames class special byte |
	aSortedCollection := SortedCollection new.
	special := Smalltalk hasSpecialSelector: aLiteral ifTrueSetByte: [:b | byte := b].
	classNames := Smalltalk classNames.
	nameSet do: [:name | classNames remove: name ifAbsent: []].
	Cursor execute showWhile:
		[classNames do:
			[:name | class := Smalltalk at: name.
			(class whichSelectorsReferTo: aLiteral special: special byte: byte) do:
				[:sel | sel ~~ #DoIt
					ifTrue: [aSortedCollection add: class name , ' ' , sel]].
			class := class class.
			(class whichSelectorsReferTo: aLiteral special: special byte: byte) do:
				[:sel | sel ~~ #DoIt
					ifTrue: [aSortedCollection add: class name , ' ' , sel]]]].
	^aSortedCollection
]
