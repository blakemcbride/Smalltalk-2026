"
Asking for a collection.

There is no garbageCollect anywhere in the 1983 library -- the image simply
cannot ask.  That is livable while nothing depends on when a collection
happens, and not livable once weak references exist: a weak slot is cleared
by a collection, so without a way to request one there is no way to observe
the mechanism working, and therefore no way to test it from inside.
"
Extension { #name : 'SystemDictionary' }

{ #category : '*System-Runtime' }
SystemDictionary >> garbageCollect [
	"Collect now, and answer how many objects were reclaimed."
	<primitive: 250>
	^0
]
