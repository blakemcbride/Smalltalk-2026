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

{ #category : 'services' }
Failing >> unwritable [
	"A value that is a number until the reply is encoded: JSON has no
	 spelling for an infinity, so this fails in success:, inside the trap
	 that is meant to turn a failure into an error envelope.  Bugs4 NET-7."
	outjson at: 'value' put: 1.0e300 * 1.0e300
]
