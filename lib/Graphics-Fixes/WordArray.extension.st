"
WordArray's ceiling is every collection's, not the Alto's.
"
Extension { #name : 'WordArray' }

{ #category : '*Graphics-Fixes' }
WordArray class >> maxSize [
	"The largest number of words one of my instances may hold: what every
	 collection answers (Collection>>maxSize), not the 64,640 of a machine
	 that no longer exists.  Form>>extent: was the one sender, and it
	 clamped every big Form's bitmap to a size the Form could not use
	 (Bugs6 GUI-5); it no longer asks."
	^self new maxSize
]
