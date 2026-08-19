Extension { #name : 'Checker' }

{ #category : '*Scope-Fixes' }
Checker >> unResponsives [
	"Smalltalk unResponsives"

	| aStream aCollection satisfiedSet workingSet aSubclass special byte |
	aStream := WriteStream on: (Array new: 50).
	special := self hasSpecialSelector: #subclassResponsibility
		ifTrueSetByte: [:b | byte := b].
	Smalltalk allBehaviorsDo:
		[:class |
		aCollection := class whichSelectorsReferTo: #subclassResponsibility special: special byte: byte.
		aCollection size > 0
			ifTrue:
				[aCollection do:
					[:selector |
					workingSet := class subclasses asOrderedCollection.
					satisfiedSet := Set new.
					[workingSet isEmpty] whileFalse:
						[aSubclass := workingSet removeFirst.
						(aSubclass includesSelector: selector)
							ifFalse:
								[satisfiedSet add: aSubclass.
								workingSet addAll: aSubclass subclasses]].
					 satisfiedSet do:
						[:subclass |
						aStream nextPut: class name, ' ', selector, '
	', subclass name]]]].
	^aStream contents
]
