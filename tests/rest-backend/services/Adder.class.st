"
The service every Kiss example begins with.
"
Class {
	#name : 'Adder',
	#superclass : 'RestService',
	#category : 'services'
}

{ #category : 'services' }
Adder >> addNumbers [
	outjson at: 'num3' put: (injson integerAt: 'num1') + (injson integerAt: 'num2')
]

{ #category : 'services' }
Adder >> whoAmI [
	"The sum, and which worker computed it: what the parallel gate reads."
	outjson at: 'num3' put: (injson integerAt: 'num1') + (injson integerAt: 'num2').
	outjson at: 'worker' put: Processor activeWorkerIndex.
	(Smalltalk includesKey: #RestGateLock) ifTrue: [
		(Smalltalk at: #RestGateLock) critical: [
			Smalltalk at: #RestGateCount put: (Smalltalk at: #RestGateCount) + 1]]
]

{ #category : 'services' }
Adder >> helper: anArgument [
	"Has an argument: not callable from outside."
	^anArgument
]
