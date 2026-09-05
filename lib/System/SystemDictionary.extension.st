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

{ #category : '*System' }
SystemDictionary >> at: aKey put: anObject [
	"Declare a global, holding the lock every writer to Smalltalk holds.

	 Declaring is find-the-slot-then-fill-it, and two workers declaring at
	 once both found the same empty slot: eight workers declaring 200
	 globals each lost 245 of the 1,600.  The body is lockedAt:put:, which
	 is 1983's here and Pharo's in lib/Collections-Compat, where Dictionary
	 is Pharo's and the 1983 insides mean nothing."
	^LibraryLocks holdingGlobals: [self lockedAt: aKey put: anObject]
]

{ #category : '*System' }
SystemDictionary >> lockedAt: aKey put: anObject [
	"1983's at:put:, to be run with the globals lock held: a name found in
	 Undeclared is adopted rather than duplicated, so a method compiled
	 against the forward reference sees the value."
	| index element |
	index := self findKeyOrNil: aKey.
	element := self basicAt: index.
	element == nil
		ifTrue:
			[self
				valueAtNewKey: aKey
				put: anObject
				atIndex: index
				declareFrom: Undeclared]
		ifFalse:
			[element value: anObject].
	^anObject
]

{ #category : '*System' }
SystemDictionary >> add: anAssociation [
	"Under the same lock as at:put:; the two are the only ways in."
	^LibraryLocks holdingGlobals: [super add: anAssociation]
]

{ #category : '*System' }
SystemDictionary >> removeKey: key ifAbsent: aBlock [
	"Under the same lock as at:put:: removal closes the gap it leaves by
	 moving later entries back, which a concurrent declare would land in."
	^LibraryLocks holdingGlobals: [super removeKey: key ifAbsent: aBlock]
]

{ #category : '*System' }
SystemDictionary >> classNames [
	"Answer a SortedCollection of all class names, cached.

	 1983 read CachedClassNames twice -- once to test it for nil and once
	 to answer it -- and flushClassNameCache, which every class definition
	 sends, could set it to nil between the two.  Read once."
	| names |
	names := CachedClassNames.
	names == nil ifTrue: [^self newClassNames].
	^names
]

{ #category : '*System' }
SystemDictionary >> at: key ifAbsent: aBlock [
	"Under the globals lock, like the writers -- because of how a Set grows.

	 1983's Set>>grow makes a bigger copy and sends become:, so that every
	 reference to the old collection now refers to the new one.  A reader
	 that had already read the old basicSize and computed its probe from
	 it then probes the NEW table with the old arithmetic, and misses.
	 Eight workers reading `Smalltalk at:' in a loop while others declared
	 globals answered nil for a key that was there.  Globals compiled into
	 methods are bindings and never come through here, so the lock costs
	 the tools and the tests, and buys every reader a table that is not
	 being replaced under it."
	^LibraryLocks holdingGlobals: [super at: key ifAbsent: aBlock]
]

{ #category : '*System' }
SystemDictionary >> associationAt: key ifAbsent: aBlock [
	"Under the globals lock; see at:ifAbsent:."
	^LibraryLocks holdingGlobals: [super associationAt: key ifAbsent: aBlock]
]

{ #category : '*System' }
SystemDictionary >> includesKey: key [
	"Under the globals lock; see at:ifAbsent:."
	^LibraryLocks holdingGlobals: [super includesKey: key]
]

{ #category : '*System-Snapshot' }
SystemDictionary >> snapshotAs: imageFilePrefix thenQuit: quitIfTrue [
	"Write the whole object memory onto imageFilePrefix, '.im'.  Answer true
	 if this is the run that wrote it, false if it is a run that resumed it.

	 1983's method with its two display steps removed, because in this
	 system neither can succeed and between them they made the documented
	 way to save an image the one way that could not (Bugs4 FILES-4).

	 What 1983 did, and why it cannot be done here.  The Display Form was
	 ordinary object memory on an Alto, so shrinking it to a hundred rows
	 before the write and growing it back afterwards saved real space in
	 the file, and `DisplayScreen displayHeight:' did that by building a
	 smaller Form and sending become:.  Here the Display is the screen the
	 VM draws through: primitive 102 handed C the Form, C holds it in a
	 place with no setter, and the object memory refuses to exchange the
	 body of anything C is holding -- `a pinned object may not exchange at
	 all'.  So the shrink raised, before a single byte of image had been
	 written, and left behind the empty .im file the line above had just
	 created.  `Smalltalk snapshotAs: ''snapA'' thenQuit: false' answered an
	 error and `snapA.im' was zero bytes long.

	 And `self install' after the write cannot succeed either, for a
	 different reason: it ends in `DisplayScreen currentDisplay: Display',
	 which insists the Form's bits be a DisplayBitmap, and this VM builds
	 the screen over a WordArray.  It is also not wanted.  install exists to
	 reconnect an image to hardware it has just been resumed onto; the
	 process that took this snapshot never went anywhere, its screen and its
	 files are still open, and the reconnecting an image needs when it is
	 loaded is done by the VM and by the startup expression, before any
	 Smalltalk of ours runs.

	 What is left is exactly the 1983 sequence otherwise, and that includes
	 Delay preSnapshot and postSnapshot -- the hooks that stop and restart
	 the timing process across a snapshot.  They were unreachable while the
	 line above them raised, which is why nothing had ever run them."
	| justSnapped |
	Cursor execute show.
	self logChange: '''----SNAPSHOT----'''.
	Disk == nil ifFalse:
		["Name the file for primitive 97, which is how beSnapshotFile and
		  snapshotPrimitive are one mechanism split across two sends."
		 (FileStream fileNamed: imageFilePrefix , '.im')
			beSnapshotFile; readWrite; close].
	self releaseExternalViews.
	Transcript cr; show: 'Snapshot at: ', Time dateAndTimeNow printString.
	Delay preSnapshot.
	Cursor wait show.
	justSnapped := self snapshotPrimitive isNil.
	justSnapped & quitIfTrue
		ifTrue: [self quitPrimitive]
		ifFalse: [Delay postSnapshot].
	Cursor normal show.
	^justSnapped
]

{ #category : '*System-Snapshot' }
SystemDictionary >> snapshotAs: imageFilePrefix [
	"Write the object memory onto imageFilePrefix, '.im' and carry on
	 running.  Answer true if this is the run that wrote it.

	 The manual has documented this one-argument form since chapter 24 and
	 the image did not have it (Bugs4 DOCS-4): only the two-keyword
	 snapshotAs:thenQuit: existed, so the example in the book answered
	 doesNotUnderstand.  Saving and going on is much the commoner of the two
	 things one wants, and `thenQuit: false' is a poor way to have to say
	 it."
	^self snapshotAs: imageFilePrefix thenQuit: false
]

{ #category : '*System' }
SystemDictionary >> condenseChanges [
	"Rewrite the changes file with only the source that is still reachable,
	 and leave every method's source readable afterwards.

	 This is 1983's one remedy for a changes file that has grown too big,
	 and it is what CompiledMethod>>setSourcePosition:inFile: sends the user
	 to when a source position will not fit the three-byte trailer.  It has
	 never worked here, and it did not fail -- it answered, said nothing,
	 and left every method's source unreadable, which is the worst of the
	 three things it could have done with a file it was asked to compact.

	 Three faults, all of them the same fault: 1983 was written for a
	 machine with ONE directory, so it treats a file's name and its place as
	 the same thing.

	   `Disk file: ''ST80.temp''' makes the scratch file where the system
	   was started, and the changes file may be anywhere -- `-serve
	   /tmp/x/img.im' keeps its changes beside the image.  The rename at the
	   end then crosses filesystems, which POSIX refuses outright.  The
	   scratch file is made beside the file it will replace.

	   `Disk removeKey: (SourceFiles at: 2) name' asks a directory keyed by
	   the names it lists to forget a name written as a path, which it does
	   not hold -- `file not found', raised and unhandled, before the
	   rename.  removeOld: is the message that takes a file rather than a
	   key, and it unlinks the name the file opens itself by.

	   Disk stays the directory throughout, as 1983 has it.  It is the one
	   whose names are written from where the system was started, so a name
	   with a path in it is a name it can open; the file's own
	   fileDirectory may be a directory that would prefix the path a second
	   time.

	   And the rename left the File saying the name it no longer had; see
	   PosixFileDirectory>>rename:newName' in lib/Files-Fixes, which is the
	   half of this fix that is not here.

	 The order matters and is 1983's: everything is written and closed
	 before anything is removed, so an interruption anywhere leaves either
	 the old changes file or the new one, and never neither."

	| old temp name |
	old := SourceFiles at: 2.
	name := old file fileName.
	temp := Disk file: name , '.condensing'.
	temp timeStamp.
	Smalltalk allBehaviorsDo: [:class | class moveChangesTo: temp].
	temp close.
	temp readOnly.
	old close.
	SourceFiles at: 2 put: temp.
	Disk removeOld: old file.
	temp file rename: name
]
