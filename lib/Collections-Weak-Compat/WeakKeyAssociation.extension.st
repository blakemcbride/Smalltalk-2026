"
The message a WeakKeyAssociation sends its dictionary when its key dies.

Pharo's mourn is `container removeKey: key ifAbsent: []', the owner's own
removal, which edits the table in place -- under the owner's readers, on
another worker (Bugs07 FIXES-4).  This sends the dictionary a message of
its own instead, so that the removal the Finalizer makes can be the one
that is safe to make beside a reader, and the owner's can stay the
ordinary one.  WeakKeyDictionary and WeakIdentityKeyDictionary answer it.
"
Extension { #name : 'WeakKeyAssociation' }

{ #category : '*Collections-Weak-Compat' }
WeakKeyAssociation >> mourn [
	"My key has died: have my dictionary take me out, if I am in one."
	container isNil ifFalse: [container removeMourned: self]
]
