"
Searching the system for who sends what.

This is the Browser's `senders', and for a SPECIAL selector -- the sixteen
arithmetic and sixteen common ones that have their own bytecodes -- it used
to answer nothing at all, silently.  The byte identifying the bytecode was
passed as nil, because the method that fetches it hands it back through a
block argument that the caller then reads from outside the block.
"
Class {
	#name : 'St80ReflectionTest',
	#superclass : 'TestCase',
	#category : 'Library-Tests'
}

{ #category : 'senders' }
St80ReflectionTest >> testSendersOfASpecialSelector [
	"#+ has its own bytecode, so finding its senders needs the byte.
	 Answered 0 for every special selector before the scope fix."
	self assert: (Number whichSelectorsReferTo: #+) notEmpty.
	self assert: (Object whichSelectorsReferTo: #==) notEmpty
]

{ #category : 'senders' }
St80ReflectionTest >> testSendersOfAnOrdinarySelector [
	"These never needed the byte and always worked; here so that a
	 regression in one is told apart from a regression in both."
	self assert: (Number whichSelectorsReferTo: #printOn:) notEmpty
]

{ #category : 'senders' }
St80ReflectionTest >> testSendersAnswersSelectorsThatReallySend [
	| senders |
	senders := Number whichSelectorsReferTo: #+.
	senders do: [:sel |
		self assert: (Number includesSelector: sel)]
]

{ #category : 'reflection' }
St80ReflectionTest >> testBasicReflection [
	self assert: 3 class equals: SmallInteger.
	self assert: SmallInteger superclass equals: Integer.
	self assert: Object superclass equals: nil.
	self assert: (Integer inheritsFrom: Number).
	self assert: (Integer includesSelector: #factorial).
	self assert: (3 respondsTo: #printString).
	self assert: (3 isKindOf: Number).
	self assert: (3 isMemberOf: SmallInteger).
	self assert: (Smalltalk at: #Object) equals: Object.
	self assert: (Smalltalk includesKey: #Object)
]
