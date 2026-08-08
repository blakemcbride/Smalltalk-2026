"
Says new itself, so the loader leaves it alone -- even though it defines
initialize.  A class that writes its own instance creation has said what it
means, and answering nil here is the loader keeping out of the way.
"
Class {
	#name : 'SelfMade',
	#superclass : 'Object',
	#instVars : [ 'count' ],
	#category : 'Probe-Core'
}

{ #category : 'initialization' }
SelfMade >> initialize [
	count := 100
]

{ #category : 'accessing' }
SelfMade >> count [
	^count
]

{ #category : 'instance creation' }
SelfMade class >> new [
	^super new
]
