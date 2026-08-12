"
A namespace of constants that several classes share.

Pharo's Chronology classes declare `#pools : [ 'ChronologyConstants' ]' and
then name SecondsInDay, MonthNames and the rest as if they were their own
class variables.  ChronologyConstants is a SharedPool: a class with class
variables and no instances, existing only to hold them.

1983 had pool dictionaries -- TextConstants is one -- but no class to
subclass for the purpose, so this is ours rather than an import.  It is
deliberately almost empty: the bootstrap already resolves a pooled class's
capitalised names to globals and remembers the binding so the pool's own
initializer writes through it (see pool_bindings in bootstrap.c).  What was
missing was only something for the pool class to BE.

Pharo's version also answers reflective questions -- bindingOf:, poolUsers,
isUsed -- that only its tooling asks.  Those are absent until something
needs them, rather than transcribed on the chance that it might.
"
Class {
	#name : 'SharedPool',
	#superclass : 'Object',
	#category : 'Kernel'
}

{ #category : 'testing' }
SharedPool class >> isPool [
	^true
]
