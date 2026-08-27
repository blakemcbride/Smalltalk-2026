"
How many rows Init left in `marks': what the live tests read after a start
that finished and after one that did not.
"
Class {
	#name : 'Marks',
	#superclass : 'RestService',
	#category : 'services'
}

{ #category : 'services' }
Marks >> count [
	outjson at: 'count' put: ((db fetchOne: 'SELECT COUNT(*) AS n FROM marks') integerAt: 'n')
]
