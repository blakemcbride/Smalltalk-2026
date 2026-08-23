"
Checker's `unResponsives' is a CLASS method -- everything from `!Checker
class methodsFor: 'static checks'!' onwards is, and that heading is three
hundred lines above it.  Correcting it on the instance side left the fault
exactly where it was and added an unreachable method beside it, which is
the one way a supersession can look right and do nothing.  The bootstrap
said so all along, in the line that counts undeclared names, and the line
was too coarse to be read: `byte' stayed in the count whether four methods
had it or one.
"
Extension { #name : 'Checker' }

{ #category : '*Scope-Fixes' }
Checker class >> unResponsives [
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
