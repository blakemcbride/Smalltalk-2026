"
A trait: a named bag of methods with no instances and no place in the
superclass chain.  It is applied by flattening -- its source is compiled
into each class that names it, which is what makes an instance variable
reference mean the same thing in every one of them.
"
Trait {
	#name : 'TGreeting',
	#category : 'Probe-Core'
}

{ #category : 'greeting' }
TGreeting >> greeting [
	^'hello from ', self subject
]

{ #category : 'greeting' }
TGreeting >> subject [
	^'a trait'
]

{ #category : 'greeting' }
TGreeting class >> defaultGreeting [
	^'hello'
]
