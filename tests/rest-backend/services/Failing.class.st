Class {
	#name : 'Failing',
	#superclass : 'RestService',
	#category : 'services'
}

{ #category : 'services' }
Failing >> userError [
	RestUserError code: 7 signal: 'no such product'
]

{ #category : 'services' }
Failing >> logged [
	RestLogError new signal: 'noted'
]

{ #category : 'services' }
Failing >> crash [
	^1 / 0
]
