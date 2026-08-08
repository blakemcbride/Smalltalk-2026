"
A class that takes the trait whole, overriding one method of it.  The
class's own method wins: flattening never installs over what the class
says itself.
"
Class {
	#name : 'Greeter',
	#superclass : 'Object',
	#traits : 'TGreeting',
	#classTraits : 'TGreeting classTrait',
	#category : 'Probe-Core'
}

{ #category : 'greeting' }
Greeter >> subject [
	^'a class'
]
