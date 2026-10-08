"
A weak-keyed dictionary whose writers are serialised against the Finalizer.

Pharo's WeakKeyDictionary is a Dictionary of WeakKeyAssociations, and each
association, when its key dies, asks the dictionary to remove it -- from
the finalization process, which runs on whatever worker is free at the
moment the collector queues it.  The owner may be inside at:put: on the
same dictionary just then, growing it; the two writers lost each other's
entries, and a dead key stayed in the array, held strongly, for ever
(Bugs6 OM-6).  See LibraryLocks class>>holdingMourning:.

The two writers below are Pharo's bodies under that lock; grow, rehash and
fixCollisionsFrom: are reached only through them.  WeakKeyAssociation>>mourn
is untouched: its removeKey:ifAbsent: is the one here.
"
Extension { #name : 'WeakKeyDictionary' }

{ #category : '*Collections-Weak-Compat' }
WeakKeyDictionary >> at: key put: anObject [
	"Set the value at key to be anObject.  If key is not found, create a
	new entry for key and set is value to anObject. Answer anObject."

	| index assoc |
	LibraryLocks holdingMourning: [
		index := self findElementOrNil: key.
		assoc := self array at: index.
		assoc
			ifNil: [self atNewIndex: index put: ((WeakKeyAssociation key: key value: anObject) container: self)]
			ifNotNil: [assoc value: anObject]].
	^ anObject
]

{ #category : '*Collections-Weak-Compat' }
WeakKeyDictionary >> removeKey: key ifAbsent: aBlock [
	"Remove key (and its associated value) from the receiver. If key is not in
	the receiver, answer the result of evaluating aBlock. Otherwise, answer
	the value externally named by key."

	| index assoc |
	^LibraryLocks holdingMourning: [
		index := self findElementOrNil: key.
		assoc := array at: index.
		assoc isNil
			ifTrue: [aBlock value]
			ifFalse: [
				array at: index put: nil.
				tally := tally - 1.
				self fixCollisionsFrom: index.
				assoc value]]
]
