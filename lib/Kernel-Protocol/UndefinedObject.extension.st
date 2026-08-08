"nil answers the other half of the ifNil: family."
Extension { #name : 'UndefinedObject' }

{ #category : '*Kernel-Protocol' }
UndefinedObject >> ifNil: aBlock [
	^aBlock value
]

{ #category : '*Kernel-Protocol' }
UndefinedObject >> ifNotNil: aBlock [
	^nil
]

{ #category : '*Kernel-Protocol' }
UndefinedObject >> ifNil: nilBlock ifNotNil: notNilBlock [
	^nilBlock value
]

{ #category : '*Kernel-Protocol' }
UndefinedObject >> ifNotNil: notNilBlock ifNil: nilBlock [
	^nilBlock value
]
