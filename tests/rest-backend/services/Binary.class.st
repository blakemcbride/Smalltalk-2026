Class {
	#name : 'Binary',
	#superclass : 'RestService',
	#category : 'services'
}

{ #category : 'services' }
Binary >> download [
	outjson at: 'name' put: 'x.bin'.
	self returnBinary: 'BINARY', (String with: (Character value: 0) with: (Character value: 255)), 'BYTES'
]
