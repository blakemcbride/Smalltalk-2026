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
SystemDictionary >> declare: key from: aDictionary [
	"Under the same lock as at:put:, the whole of it.

	 1983's Dictionary>>declare:from: asks whether I have the key, then
	 whether aDictionary has it -- Undeclared, from the one sender,
	 Metaclass>>name:inEnvironment:subclassOf:..., defining a class whose
	 name some method was compiled against first -- and moves the
	 Association across in two writes.  Mine were under the lock one at a
	 time and the write to Undeclared was under none: a compile on another
	 worker storing a new name into Undeclared at the same moment lost one
	 entry or the other (Bugs6 KERNB-6).  The steps run together under the
	 lock, through the unlocked insides, because the globals lock is a
	 Mutex and does not nest."
	^LibraryLocks holdingGlobals: [
		(super includesKey: key) ifFalse: [
			(aDictionary includesKey: key)
				ifTrue: [
					super add: (aDictionary associationAt: key).
					aDictionary removeKey: key]
				ifFalse: [self lockedAt: key put: nil]]]
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
	 line above them raised, which is why nothing had ever run them.

	 And postSnapshot has to run however the write ends.  preSnapshot
	 takes Delay's AccessProtect and only postSnapshot gives it back, and
	 primitive 97 fails for ordinary reasons -- a .tmp it cannot write, a
	 full disk, a worker that would not park within ten seconds.  1983 had
	 no path out of the primitive but the normal one, so the failure left
	 the lock held for ever: the caller's handler caught the error and went
	 on, and the next Delay anyone waited on blocked on AccessProtect and
	 stopped the image with `every process is blocked; nothing can run'
	 (Bugs5 OM-7).  ifCurtailed: and not ensure:, because the normal path
	 releases it already, in the ifFalse: branch below, and a second
	 signal would leave the lock open to two holders at once.

	 The file is named and not opened (Bugs5 SNAPSHOT-zero-byte).  1983
	 opened it -- `FileStream fileNamed:' and then close -- because on the
	 Alto primitive 97 wrote into the very file it was given.  Here 97
	 writes `<name>.tmp' and renames it over the name, so the open did
	 nothing but create the name, empty, before a byte of image existed;
	 and when 97 then failed, a snapshot to a new name left a zero-byte
	 .im behind that -run refuses as no image at all.  A File that only
	 carries the name is all 128 reads.

	 And it is named after Delay preSnapshot, inside the block the
	 primitive runs in, not before.  128 and 97 talk through one place in
	 the VM, and preSnapshot holds Delay's AccessProtect until
	 postSnapshot: naming the file under that lock is what keeps a second
	 process's snapshotAs: from renaming the first one's image between the
	 two sends.

	 And a run that RESUMED wakes the finalization process once.  The
	 writer puts the ephemerons its own collection fired back on disk as
	 ephemerons, so the loader's collection queues them again (Bugs5
	 OM-9) -- but where to signal is C state no image carries, and the
	 process comes back asleep on a semaphore nothing will signal.  One
	 signal sends it round its loop, and the drain at the top of the loop
	 is what tells the VM where to signal from then on.  A signal and not
	 Finalizer restart, which would terminate a process that may be half
	 way through somebody's #mourn; and looked up rather than named,
	 because the Blue Book profile has no Finalizer.

	 And the changes file goes with the image (Bugs6 FILES-5).  Every
	 method's trailer is an offset in the file SourceFiles at: 2 names.
	 1983's saveAs: copies that file beside the new image and makes the
	 copy the running image's own; snapshotAs: copied nothing, so `x.im'
	 was written, `-run x.im' found no changes file beside it and made an
	 empty one, and every method accepted before the snapshot read its
	 source from the wrong place -- `position out of bounds', or another
	 method's text once anything had been logged there.  The file is
	 copied here too, as `<prefix>.changes', the spelling -run looks for
	 second, but NOT installed: this image goes on running as itself and
	 keeps logging where it was.  The copy is made before the files are
	 released and the image written, and is exactly the file the saved
	 methods were written into, up to this moment."
	| justSnapped imageFile finalizer |
	Cursor execute show.
	self logChange: '''----SNAPSHOT----'''.
	Disk == nil ifFalse:
		[imageFile := Disk fileClass new.
		 imageFile fileName: (Disk pathFor: imageFilePrefix , '.im').
		 self copyChangesFileTo: imageFilePrefix , '.changes'].
	self releaseExternalViews.
	Transcript cr; show: 'Snapshot at: ', Time dateAndTimeNow printString.
	Delay preSnapshot.
	Cursor wait show.
	justSnapped :=
		["Name the file for primitive 97, which is how beSnapshotFile and
		  snapshotPrimitive are one mechanism split across two sends."
		 imageFile == nil ifFalse: [imageFile beSnapshotFile].
		 self snapshotPrimitive isNil]
			ifCurtailed: [Delay postSnapshot].
	justSnapped & quitIfTrue
		ifTrue: [self quitPrimitive]
		ifFalse: [Delay postSnapshot].
	justSnapped ifFalse: [
		finalizer := self at: #Finalizer ifAbsent: [nil].
		(finalizer notNil and: [finalizer process notNil])
			ifTrue: [finalizer mournSemaphore signal]].
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
	 the old changes file or the new one, and never neither.

	 And the new file takes the old one's place in one step, with the lock
	 (Bugs5 FILES-9).  This removed the old file and then renamed the new
	 one onto the free name, which was a moment with no changes file at
	 all; and the lock that keeps a second st2026 off this image's changes
	 file (Bugs4 FILES-B) is held on the FILE, so it stayed with the one
	 removed, and a second -serve of the image was admitted from then on.
	 Primitive 131's command 7 replaces the old file with the new and moves
	 the lock onto the new one before it takes the name.  It is not the
	 rename:newName: protocol, which must refuse a name that is taken
	 (Bugs5 FILES-8); replacing is the whole point here.  The File is told
	 its new name, as rename:newName: tells it."

	| old temp name |
	"Under the source-file lock throughout: a compile writing to the old
	 file while this copies it would be lost with it (Bugs5 FILES-2)."
	LibraryLocks holdingSources: [
		old := SourceFiles at: 2.
		name := old file fileName.
		temp := Disk file: name , '.condensing'.
		temp timeStamp.
		Smalltalk allBehaviorsDo: [:class | class moveChangesTo: temp].
		temp close.
		temp readOnly.
		old close.
		SourceFiles at: 2 put: temp.
		(Disk doPrimitive: 7 arg1: name arg2: temp file) == true
			ifFalse: [^self error: 'cannot put ' , temp file fileName ,
				' in the place of ' , name , ', '
				, (temp file errorString: temp file lastError)].
		temp file fileName: name]
]

{ #category : '*System' }
SystemDictionary >> logChange: aStringOrText [
	"Write the argument, a String or Text, onto the changes file -- 1983's,
	 holding the source-file lock (Bugs5 FILES-2)."
	| aFileStream aString |
	SourceFiles == nil ifTrue: [^self].
	aString := (aStringOrText isMemberOf: Text)
		ifTrue: [aStringOrText string]
		ifFalse: [aStringOrText].
	(aString isMemberOf: String) ifFalse: [^self error: 'cant log this change'].
	(aString findFirst: [:char | char isSeparator not]) = 0
		ifTrue: [^self].  "null doits confuse replay"
	LibraryLocks holdingSources: [
		aFileStream := SourceFiles at: 2.
		aFileStream setToEnd; readWrite.
		aFileStream cr; cr; nextChunkPut: aString.
		aFileStream readOnly]
]

{ #category : '*System-Snapshot' }
SystemDictionary >> copyChangesFileTo: newFileName [
	"Copy the changes file to newFileName, for an image about to be saved
	 beside it (see snapshotAs:thenQuit:) -- without making the copy this
	 image's own, which is what 1983's copyChangesTo: does for saveAs:.

	 Nothing is copied when there is no changes file (an image built
	 without -o keeps its changes in memory), or when newFileName is the
	 changes file itself: saveAs: has installed its copy under that very
	 name by the time it sends snapshotAs:, and a copy onto itself would
	 empty the file.  Under the sources lock, which every writer of the
	 changes file holds, so the copy ends at the end of a whole chunk; and
	 through a stream of its own, since the writers' stream is positioned
	 wherever they left it.  Page by page, because a changes file is as
	 long as the image's history."
	| changes old new target |
	SourceFiles == nil ifTrue: [^self].
	changes := SourceFiles at: 2.
	(changes isKindOf: FileStream) ifFalse: [^self].
	target := Disk pathFor: newFileName.
	target = changes name ifTrue: [^self].
	((Disk includesKey: target)
		and: [(Disk realPathNamed: target) = (Disk realPathNamed: changes name)])
		ifTrue: [^self].
	LibraryLocks holdingSources: [
		old := FileStream oldFileNamed: changes name.
		old readOnly.
		new := FileStream fileNamed: target.
		[old atEnd] whileFalse: [new nextPutAll: (old next: 32768)].
		old close.
		new close]
]

{ #category : '*System-Snapshot' }
SystemDictionary >> imagePrefixFrom: aString [
	"Answer aString without a trailing '.im' or '.image', in either case.

	 What the save prompt is given is a file PREFIX -- saveAs:thenQuit:
	 adds '.im' and '.changes' to it -- but what a person types is the name
	 of the file they want, and that ends in '.im'.  1983's getImagePrefix
	 took the suffix off only inside its retry loop, after a name had
	 already been refused, so the ordinary case, a legal name typed once,
	 went through untouched: typing zz.im wrote zz.im.im and zz.im.changes
	 (Bugs5 GUI-3).  The same mistake ./run made with a typed name, from
	 the other side."
	| lower |
	lower := aString asLowercase.
	#('.image' '.im') do:
		[:suffix |
		 (lower size > suffix size
		  and: [(lower copyFrom: lower size - suffix size + 1 to: lower size)
				= suffix])
			ifTrue: [^aString copyFrom: 1 to: aString size - suffix size]].
	^aString
]

{ #category : '*System-Snapshot' }
SystemDictionary >> getImagePrefix [
	"Prompt the user for a snapshot file name.  Persist until a legal name
	 is supplied.  Answer the name without its suffix, or an empty string
	 if the user gave none.

	 1983's method, with the typed answer passed through imagePrefixFrom:
	 every time it is read rather than only on a retry (Bugs5 GUI-3); see
	 there.  The block assigns to the temporary because this is compiled
	 with closures, where a block's argument belongs to the block: 1983's
	 `action: [:newPrefix | ]' relied on the Blue Book making a block
	 argument one of the method's own temporaries."
	| default newPrefix prompt |
	SourceFiles == nil ifTrue: [^'snapshot'].
	prompt := 'Enter name for image file:'.
	default := 'snapshot'.
	newPrefix := ''.
	FillInTheBlank
		request: prompt
		displayAt: Sensor cursorPoint
		centered: true
		action: [:answer | newPrefix := self imagePrefixFrom: answer]
		initialAnswer: default.
	[newPrefix isEmpty or:
			[(Disk isLegalFileName: newPrefix , '.changes') not]] whileTrue:
		[newPrefix isEmpty ifTrue: [^newPrefix].
		 prompt := 'Illegal file name: ' , newPrefix , '
Enter name for image file:'.
		 default := Disk checkName: newPrefix , '.changes' fixErrors: true.
		 default := default copyFrom: 1 to: default size - 8.
		 FillInTheBlank
			request: prompt
			displayAt: Sensor cursorPoint
			centered: true
			action: [:answer | newPrefix := self imagePrefixFrom: answer]
			initialAnswer: default].
	^newPrefix
]
