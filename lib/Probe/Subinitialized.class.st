"
A subclass that also defines initialize, and the trap the chain rule exists
to avoid.  It must NOT get a new of its own: Initialized class>>new already
sends initialize, and a second new here would send it again -- the outer
super new would run this initialize, and then so would the inner one.  The
answer says which happened, because initialize is not idempotent: 2 means
once, 3 means twice.
"
Class {
	#name : 'Subinitialized',
	#superclass : 'Initialized',
	#category : 'Probe-Core'
}

{ #category : 'initialization' }
Subinitialized >> initialize [
	super initialize.
	self bump
]
