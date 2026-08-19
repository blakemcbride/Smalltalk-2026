Extension { #name : 'SystemDictionary' }

{ #category : '*Scope-Fixes' }
SystemDictionary >> allCallsOn: aLiteral [
	"Answer a SortedCollection of all the methods that call on aLiteral."

	| aSortedCollection special byte |
	aSortedCollection := SortedCollection new.
	special := self hasSpecialSelector: aLiteral ifTrueSetByte: [:b | byte := b].
	Cursor execute showWhile:
		[self allBehaviorsDo:
			[:class |
			 (class whichSelectorsReferTo: aLiteral special: special byte: byte) do:
				[:sel | sel ~~ #DoIt
					ifTrue: [aSortedCollection add: class name , ' ' , sel]]]].
	^aSortedCollection
]

{ #category : '*Scope-Fixes' }
SystemDictionary >> allCallsOn: firstLiteral and: secondLiteral [
	"Answer a SortedCollection of all the methods that call on both aLiteral and
	secondLiteral."

	| aCollection secondArray firstSpecial secondSpecial firstByte secondByte |
	aCollection := SortedCollection new.
	firstSpecial := self hasSpecialSelector: firstLiteral
		ifTrueSetByte: [:b | firstByte := b].
	secondSpecial := self hasSpecialSelector: secondLiteral
		ifTrueSetByte: [:b | secondByte := b].
	Cursor execute showWhile:
		[self allBehaviorsDo:
			[:class |
			secondArray := class whichSelectorsReferTo: secondLiteral special: secondSpecial byte: secondByte.
			((class whichSelectorsReferTo: firstLiteral special: firstSpecial byte: firstByte) select:
				[:aSel | (secondArray includes: aSel)]) do:
						[:sel | aCollection add: class name , ' ' , sel]]].
	^aCollection
]

{ #category : '*Scope-Fixes' }
SystemDictionary >> recompileCallsOn: aLiteral [
	"Recompile every message in the system that refers to aLiteral."

	| special byte |
	special := self hasSpecialSelector: aLiteral ifTrueSetByte: [:b | byte := b].
	Cursor execute showWhile:
		[self allBehaviorsDo:
			[:class | (class whichSelectorsReferTo: aLiteral special: special byte: byte)
				do: [:sel | sel ~~ #DoIt ifTrue: [class recompile: sel]]]]
]
