"
Which classes came from which Tonel files, so that a file can be reloaded when
it changes and a class can be written back when it does.

This is the registry behind two things a server wants and the Browser never
had: a class loaded from a file on first use, kept current with the file; and
the file kept current with the class, so that accepting a method in the Browser
edits the file the class came from.  Both directions run through here.

    TonelSource loadFile: 'backend/services/Adder.class.st'
    TonelSource reloadIfChanged: Adder
    TonelSource noteChangeIn: Adder          ''sent by the Browser's hooks''

WHAT ''CHANGED'' MEANS.  A file is reloaded when its modification time or its
size differs from what was recorded at the last load.  The time is a second on
every file system this runs on, so a save within the same second as the last
load, that also left the size the same, is not seen -- the size is checked
precisely because it catches most of those.  It is the same test Kiss makes
with lastModified, and the same blind spot.

THE KEY IS THE NAME.  Redefining a class with a different shape -- an
instance variable added in the Browser -- makes a NEW class object in this
image and leaves the old one obsolete, so a table keyed by the class itself
would forget a class the moment its definition changed, which is exactly the
moment it has to be written back.  The name survives a redefinition; the
class is looked up through Smalltalk when it is wanted.

ONE CLASS, SEVERAL FILES.  A class has at most one Class file and any number
of Extension files, one per package that adds methods to it, and each file is
its own entry here: what kind it was, and for an extension the `*Package'
prefix of the categories it owns and the comment at the top of it.  A change
in the class writes every file the class has, each with what belongs to it --
the Class file the class's own methods, an Extension file the methods under
its prefix.  The registry did not always know the difference: it recorded a
name and a path whatever the file's type, and wrote the whole class to it, so
one Browser accept on SmallInteger, loaded from Concurrency's two-method
extension file, replaced that file with a Class definition and forty-four
methods, which the next bootstrap read as a second SmallInteger (Bugs3 B59).

The prefix is the package's name with a star in front, read from the
package.st beside the file when there is one, since that is where the name
is stated; a file with no package.st beside it is taken to own the shortest
starred category it used.  A file with neither owns nothing and is never
written.

AN EXTENSION FILE ALSO OWNS EVERY STARRED CATEGORY IT WAS READ WITH, whatever
its prefix says.  The prefix was all it owned, and nothing checked that the
file's methods were under it: lib/System/SystemDictionary.extension.st is in
package System-Runtime and holds eleven methods in `*System' and five in
`*System-Snapshot', so one accept on SystemDictionary rewrote the file with
the one method under `*System-Runtime' and the accepted method nowhere; a
file with no package.st holding `*Acme' and `*Zed-Extra' lost the second; and
Pharo's lower-case `*mystuff' in package MyStuff lost everything, because the
comparison was case-sensitive (Bugs07 COMP-1).  The reader already took the
file to own the categories it used -- what a reload removes is decided by
them -- so the writer now writes what the reader reads: the categories read,
and the prefix and the tags under it, compared without regard to case as
Pharo compares them.  And the methods the file held at the last load or
write are remembered, and a write-back that would leave out one the class
still has is refused with the file's name and the methods, before anything
reaches the disk: a method moved in the Browser into a category no file
owns would otherwise be written nowhere and gone at the next load.

A CLASS FILE IS HELD TO THE SAME RULE.  It owns the class's own categories,
the ones without a star, and its write-back writes those and no others; so a
method the file held, moved in the Browser into a starred category that no
extension file of the class owns, was written to no file at all, and the
accept answered as if all were well (Bugs07 X-19, the half of COMP-1 that
covered extension files only).  The methods a Class file held at its last
load or write are remembered as an extension file's are, and the same
refusal names them.

WHAT IS SHARED.  One Dictionary from class name to entries, for the image, read
by every request and written by the loads; so every access takes the Mutex,
the reads too, for the reason doc/CONCURRENCY.md gives about Set>>grow.  A
load holds the lock for its whole duration: two requests arriving for a class
nobody has loaded yet then load it once, and the second finds it present.
The reader compiles through the layer below the Browser's hooks, so nothing a
load does comes back here wanting the lock it already holds -- which matters,
because Mutex is not re-entrant and would report exactly that.

A write-back records the file's new time and size, so that the next reload
check does not read back what was just written.

AND A WRITE-BACK COMPARES THEM FIRST.  The stamp was recorded at every load
and every write and never consulted before writing, so a method added to the
file in an editor after the load was silently replaced by the next Browser
accept, which wrote the class as the image had it and nothing the file had
gained (Bugs5 FILES-4).  Now a file whose time or size differs from the
stamp is not written: the change is refused, with a TonelError naming the
file, BEFORE the image changes -- the Browser's hooks ask checkChangeTo:
source: first -- so that the image and the file stay a pair that agree.
What to do next is the person's choice, and the message says both: load
the file again, which takes its edits, and then make the change; or forget
it, which leaves the file alone and stops writing it.  The blind spot is the one
reloadIfChanged: has: a save in the same second at the same size.

A SOURCE WITH A LINE FEED IN IT IS REFUSED THE SAME WAY.  The file's lines
end in line feeds and the image's in carriage returns, and both readers turn
every line ending in the file into a carriage return -- they have to, or a
multi-line method would come back as one line.  So a line feed the image
held inside a string literal -- the HTTP line end, a carriage return and a
line feed -- was written as a line ending and read back as a carriage
return, and the reloaded method answered something else (Bugs5 FILES-5).
There is no escape for it inside a Smalltalk literal; `Character value: 10'
built into the String when the method runs says the same thing in a form
that survives, and the refusal says so.
"
Class {
	#name : 'TonelSource',
	#superclass : 'Object',
	#instVars : [ 'name', 'path', 'modificationTime', 'size', 'type', 'prefix', 'comment',
	              'categories', 'held' ],
	#classVars : [ 'Lock', 'Registry' ],
	#category : 'Tonel'
}

{ #category : 'class initialization' }
TonelSource class >> initialize [
	"Eagerly, never lazily: two workers would each make one."
	Lock := Mutex new.
	Registry := Dictionary new
]

{ #category : 'private' }
TonelSource class >> fileFor: pathString [
	"The File at pathString, or nil if there is none -- or if the directory
	 in front of the name is not one, which the 1983 directory protocol
	 cannot be asked about without walking it."
	| dir name cut |
	cut := 0.
	1 to: pathString size do: [:i | (pathString at: i) = $/ ifTrue: [cut := i]].
	(cut > 1 and: [(Disk isDirectoryNamed: (pathString copyFrom: 1 to: cut - 1)) not])
		ifTrue: [^nil].
	dir := FileDirectory directoryFromName: pathString setFileName: [:n | name := n].
	(dir includesKey: name) ifFalse: [^nil].
	^dir findKey: name
]

{ #category : 'private' }
TonelSource class >> stampFrom: aFile [
	"The file's time and size, as an Array, or nil when it cannot be asked.

	 THE FILE IS CLOSED AFTERWARDS, and that is not housekeeping.  A File
	 from fileFor: has never been opened -- 1983's directory answers a
	 partly initialised File and opens nothing -- and primitive 130 opens
	 one ON DEMAND for the two commands asked here, caching the descriptor
	 in the File's own fd field.  Nothing finalizes a File, so dropping it
	 dropped an open descriptor with it, one per call.

	 The REST server asks this question once per request, through
	 RestServiceLoader>>classFor: -> reloadIfChanged:, so the descriptors
	 climbed by one per request and never came back down: Bugs4 NET-2
	 counted 408 open where 6 had been after 400 calls, which on a host
	 with the usual `ulimit -n 1024' is a server that stops after about a
	 thousand requests -- and stops for good, because the accept loop dies
	 with the first EMFILE (NET-1).  Two questions about a name should cost
	 no descriptor at all once they are answered."
	| time size |
	^[time := aFile modificationTime.
	  size := aFile sizeOnDisk.
	  (time isKindOf: Integer)
		ifTrue: [Array with: time with: size]
		ifFalse: [nil]]
		ensure: [aFile close]
]

{ #category : 'private' }
TonelSource class >> packageNameBeside: pathString [
	"The name the package.st in pathString's directory declares, or nil
	 when there is no such file or it is not a Package file."
	| cut packagePath |
	cut := 0.
	1 to: pathString size do: [:i | (pathString at: i) = $/ ifTrue: [cut := i]].
	packagePath := (pathString copyFrom: 1 to: cut), 'package.st'.
	(self fileFor: packagePath) isNil ifTrue: [^nil].
	^[(TonelReader onFile: packagePath) packageName] on: TonelError do: [:e | e return: nil]
]

{ #category : 'private' }
TonelSource class >> prefixFor: pathString categories: aCollection [
	"The `*Package' prefix of the categories an Extension file at
	 pathString owns: the package.st beside it names the package, and
	 failing that the shortest starred category the file used is taken
	 for the prefix.  nil when neither says anything."
	| packageName shortest |
	packageName := self packageNameBeside: pathString.
	packageName notNil ifTrue: [^'*', packageName].
	shortest := nil.
	aCollection do: [:each |
		((TonelWriter isOwnCategory: each) not
			and: [shortest isNil or: [each size < shortest size]])
				ifTrue: [shortest := each]].
	^shortest
]

{ #category : 'private' }
TonelSource class >> entriesFor: aSymbol [
	"With the lock held: the entries registered under a class name, or nil."
	^Registry at: aSymbol ifAbsent: [nil]
]

{ #category : 'loading' }
TonelSource class >> loadFile: pathString [
	"Read the Tonel file into the image and remember where it came from.
	 Answer the class; raise TonelError if there is no such file."

	^Lock critical: [self lockedLoad: pathString]
]

{ #category : 'private' }
TonelSource class >> lockedLoad: pathString [
	"With the lock held: load, and record the file as it was at the load
	 -- under the class it defined or extended, one entry per path."
	| file stamp reader class entries entry |
	file := self fileFor: pathString.
	file isNil ifTrue: [^TonelError new signal: 'no such file: ', pathString].
	stamp := self stampFrom: file.
	reader := TonelReader onFile: pathString.
	class := reader load.
	class isNil ifTrue: [^nil].
	entries := Registry at: class name asSymbol
		ifAbsent: [Registry at: class name asSymbol put: OrderedCollection new].
	entry := entries detect: [:each | each path = pathString] ifNone: [nil].
	entry isNil ifTrue: [
		entry := self new setName: class name asSymbol path: pathString.
		entries add: entry].
	reader fileType = 'Extension'
		ifTrue: [
			entry
				noteType: reader fileType
				prefix: (self prefixFor: pathString categories: reader categoriesRead)
				categories: (reader categoriesRead reject: [:each | TonelWriter isOwnCategory: each])
				comment: reader fileComment]
		ifFalse: [entry noteType: reader fileType prefix: nil categories: nil comment: nil].
	entry noteHeldIn: class.
	entry recordStamp: stamp.
	^class
]

{ #category : 'loading' }
TonelSource class >> reloadIfChanged: aClass [
	"If aClass came from a file, or has an extension file, and any of them
	 has changed since, load those again.  Answer whether one was."

	^Lock critical: [ | entries reloaded |
		reloaded := false.
		entries := self entriesFor: aClass name asSymbol.
		entries notNil ifTrue: [
			entries copy do: [:entry | | file stamp |
				file := self fileFor: entry path.
				file notNil ifTrue: [
					stamp := self stampFrom: file.
					(stamp notNil and: [stamp ~= entry stamp])
						ifTrue: [self lockedLoad: entry path. reloaded := true]]]].
		reloaded]
]

{ #category : 'writing back' }
TonelSource class >> noteChangeIn: aBehavior [
	"aBehavior -- a class or its metaclass -- has a method or definition
	 changed.  If it came from a file, or has extension files, write each
	 of them with what belongs to it.

	 A file that cannot be written faithfully -- see checkChangeTo:source:
	 -- is not written, and the TonelError that says why is signalled once
	 the lock is released, so that whoever handles it, or the notifier
	 that shows it, does not hold every other load and write-back up."

	| class refusal |
	class := aBehavior isMeta ifTrue: [aBehavior soleInstance] ifFalse: [aBehavior].
	refusal := Lock critical: [ | entries answer |
		entries := self entriesFor: class name asSymbol.
		entries notNil ifTrue: [
			entries do: [:entry | | why |
				why := entry writeBack: (Smalltalk at: class name asSymbol).
				answer isNil ifTrue: [answer := why]]].
		answer].
	refusal notNil ifTrue: [^TonelError new signal: refusal]
]

{ #category : 'writing back' }
TonelSource class >> checkChangeTo: aBehaviorOrNil source: aStringOrNil [
	"Raise a TonelError, and change nothing, if a change to aBehaviorOrNil
	 -- a class or its metaclass -- with source aStringOrNil could not be
	 written back to the files it came from as the image will hold it:
	 one of them has changed on disk since it was loaded or last written
	 (Bugs5 FILES-4), or the source holds a line feed, which no Tonel
	 file can carry through a reload (Bugs5 FILES-5), or one of them
	 cannot be written -- it may only be read, or its directory is gone
	 (Bugs6 COMP-13).  The Browser's hooks send this BEFORE they compile,
	 remove or redefine anything; a class that came from no file passes."

	| class refusal |
	(aBehaviorOrNil isKindOf: Behavior) ifFalse: [^self].
	class := aBehaviorOrNil isMeta
		ifTrue: [aBehaviorOrNil soleInstance]
		ifFalse: [aBehaviorOrNil].
	refusal := Lock critical: [ | entries answer |
		entries := self entriesFor: class name asSymbol.
		entries notNil ifTrue: [
			(aStringOrNil notNil
				and: [aStringOrNil asString includes: (Character value: 10)])
					ifTrue: [answer := TonelWriter lineFeedRefusal: 'the source given for ', class name].
			entries do: [:entry |
				answer isNil ifTrue: [answer := entry changedOnDiskRefusal].
				answer isNil ifTrue: [answer := entry unwritableRefusal]]].
		answer].
	refusal notNil ifTrue: [^TonelError new signal: refusal]
]

{ #category : 'accessing' }
TonelSource class >> pathOf: aClass [
	"Where aClass came from, or nil: its Class file, or failing that the
	 first of its extension files."
	^Lock critical: [ | entries |
		entries := self entriesFor: aClass name asSymbol.
		entries isNil ifTrue: [nil] ifFalse: [
			(entries detect: [:each | each isExtension not] ifNone: [entries first]) path]]
]

{ #category : 'accessing' }
TonelSource class >> pathsOf: aClass [
	"Every file aClass is tracked from, Class file and extensions alike."
	^Lock critical: [ | entries |
		entries := self entriesFor: aClass name asSymbol.
		entries isNil ifTrue: [#()] ifFalse: [(entries collect: [:each | each path]) asArray]]
]

{ #category : 'accessing' }
TonelSource class >> isTracked: aClass [
	^(self pathOf: aClass) notNil
]

{ #category : 'accessing' }
TonelSource class >> forget: aClass [
	"Stop tracking aClass, every file of it; the files are left as they are."
	Lock critical: [Registry removeKey: aClass name asSymbol ifAbsent: [nil]]
]

{ #category : 'accessing' }
TonelSource class >> trackedClasses [
	"The classes, as they are now: a name is looked up when asked for."
	^Lock critical: [
		(Registry keys asSortedCollection collect: [:name | Smalltalk at: name ifAbsent: [nil]])
			reject: [:c | c isNil]]
]

{ #category : 'private' }
TonelSource >> setName: aSymbol path: pathString [
	name := aSymbol.
	path := pathString
]

{ #category : 'private' }
TonelSource >> noteType: aString prefix: prefixOrNil categories: aCollectionOrNil comment: commentOrNil [
	"What the file was: 'Class' or 'Extension', and for an extension the
	 prefix of the categories it owns, the starred categories it was read
	 with, which it owns too, and the comment at the top of it."
	type := aString.
	prefix := prefixOrNil.
	categories := aCollectionOrNil isNil ifTrue: [nil] ifFalse: [aCollectionOrNil asSet].
	comment := commentOrNil
]

{ #category : 'private' }
TonelSource >> owns: category [
	"Whether a method in category belongs in my file.  A Class file owns
	 the class's own categories, those that do not begin with a star, as
	 TonelWriter>>writeClass: writes them.  An Extension file owns the
	 starred categories it was read with, and my prefix and the tags under
	 it (Bugs07 COMP-1; see the class comment)."
	self isExtension ifFalse: [^TonelWriter isOwnCategory: category].
	^(categories notNil and: [categories includes: category])
		or: [prefix notNil and: [TonelWriter category: category isUnder: prefix]]
]

{ #category : 'private' }
TonelSource >> noteHeldIn: aClass [
	"Remember the methods my file holds, as the class has them just after
	 the file was read: every method in a category I own, but the
	 bootstrap's synthesized new, which no file holds.  A Class file as
	 well as an Extension file (Bugs07 X-19).  See lostRefusalWriting:."
	| writer |
	writer := TonelWriter new.
	held := Set new.
	(Array with: aClass class with: aClass) do: [:side |
		side organization categories do: [:category |
			(self owns: category asString) ifTrue: [
				(side organization listAtCategoryNamed: category) do: [:selector |
					(writer isSynthesized: selector of: side)
						ifFalse: [held add: (self keyFor: selector meta: side isMeta)]]]]]
]

{ #category : 'private' }
TonelSource >> keyFor: selector meta: aBoolean [
	"How held names a method: its selector, with `class ' in front of a
	 class-side one.  A String and not the method or the class, because a
	 redefinition makes a new class object and the name outlives it."
	^aBoolean ifTrue: ['class ', selector] ifFalse: [selector asString]
]

{ #category : 'writing back' }
TonelSource >> lostRefusalWriting: pairs of: aClass [
	"nil, or the reason my file must not be written with only pairs -- the
	 behavior and selector of each method the writer wrote -- in it: a
	 method the file held at its last load or write that the class still
	 has, and that the writer left out because its category is not one
	 the file owns, nor one that another file of the class owns (Bugs07
	 COMP-1 for an Extension file, X-19 for a Class file).  A method the
	 class no longer has was removed, and leaving it out is the point; one
	 moved into the class's own categories, or another package's, is
	 written by that file.

	 Checked before the file is written, after the change it reports: the
	 Browser's hooks ask before they compile, but what they ask with is a
	 source and not a category, so the new category is first seen here.
	 The file is left as it was, which keeps every method it held.  Sent
	 with the lock held, by noteChangeIn: through writeBack:."
	| writtenKeys siblings lost |
	held isNil ifTrue: [^nil].
	writtenKeys := (pairs collect: [:pair |
		self keyFor: (pair at: 2) meta: (pair at: 1) isMeta]) asSet.
	siblings := (TonelSource entriesFor: name) ifNil: [#()].
	lost := (held reject: [:key | | meta side selector category |
		meta := key size > 6 and: [(key copyFrom: 1 to: 6) = 'class '].
		side := meta ifTrue: [aClass class] ifFalse: [aClass].
		selector := (meta ifTrue: [key copyFrom: 7 to: key size] ifFalse: [key]) asSymbol.
		(writtenKeys includes: key)
			or: [(side includesSelector: selector) not
			or: [category := (side organization categoryOfElement: selector) asString.
				siblings anySatisfy: [:each | each ~~ self and: [each owns: category]]]]]) asSortedCollection.
	lost isEmpty ifTrue: [^nil].
	^path, ' holds ', (lost inject: '' into: [:all :each | | method |
			method := (each size > 6 and: [(each copyFrom: 1 to: 6) = 'class '])
				ifTrue: [name, ' class>>', (each copyFrom: 7 to: each size)]
				ifFalse: [name, '>>', each].
			all isEmpty ifTrue: [method] ifFalse: [all, ', ', method]]),
		(lost size = 1
			ifTrue: [', which is now in a category the file does not hold, so writing ']
			ifFalse: [', which are now in categories the file does not hold, so writing ']),
		name, ' to it would leave ', (lost size = 1 ifTrue: ['it'] ifFalse: ['them']),
		' out, and nothing was written.',
		'  Put ', (lost size = 1 ifTrue: ['it'] ifFalse: ['them']),
		(self isExtension
			ifTrue: [' back in one of the file''s categories -- ',
				((categories isNil ifTrue: [#()] ifFalse: [categories asSortedCollection asArray])
					copyWith: prefix) printString, ' --']
			ifFalse: [' back in one of the class''s own categories, whose names have no star,']),
		' or send TonelSource forget: ', name, ' to stop writing the class to it.'
]

{ #category : 'private' }
TonelSource >> recordStamp: anArrayOrNil [
	modificationTime := anArrayOrNil isNil ifTrue: [nil] ifFalse: [anArrayOrNil at: 1].
	size := anArrayOrNil isNil ifTrue: [nil] ifFalse: [anArrayOrNil at: 2]
]

{ #category : 'writing back' }
TonelSource >> writeBack: aClass [
	"Write my file from aClass: the class's own methods to a Class file,
	 the methods under my prefix to an Extension file -- and nothing at
	 all for an extension file whose prefix could not be found, since a
	 file that owns no category has nothing that can be written to it
	 without writing something else's.  Then record the file as written,
	 so that the write is not read back as a change.

	 Answer nil, or -- having written nothing -- the reason the file was
	 not written: it has changed on disk since it was loaded or last
	 written, and writing would throw that change away (Bugs5 FILES-4);
	 or it cannot be written at all (Bugs6 COMP-13); or TonelWriter
	 refused the text (Bugs5 FILES-5); or the file would lose a method it
	 held (Bugs07 COMP-1, X-19); or the text would not read back as
	 the methods it was written from (Bugs6 COMP-14).  The text is built,
	 and read back, before the file is opened, so a refusal leaves the
	 file as it was.

	 THE READ-BACK IS WHAT STANDS BETWEEN A WRITER FAULT AND THE DISK.  A
	 file this system has written and cannot read is the worst thing a
	 write-back can do: the class is gone at the next load, the server
	 cannot reload its service, and the text the file held is gone with
	 it -- the stamp check above cannot help, because the write is the
	 system's own.  One accept of any method of a class with a selector
	 holding an underscore did exactly that (Bugs6 COMP-14), and two
	 earlier writer faults (Bugs5 COMP-3, FILES-12) would have been caught
	 the same way.  It costs compiling the class's methods once more,
	 without installing them, which an accept can afford."
	| file why text writer |
	(self isExtension and: [prefix isNil]) ifTrue: [^nil].
	why := self changedOnDiskRefusal.
	why notNil ifTrue: [^why].
	why := self unwritableRefusal.
	why notNil ifTrue: [^why].
	writer := TonelWriter new.
	text := [self isExtension
			ifTrue: [
				writer
					writeExtensionOf: aClass
					selecting: [:category | self owns: category]
					comment: comment;
					contents]
			ifFalse: [writer writeClass: aClass; contents]]
		on: TonelError do: [:e | why := e messageText. e return: nil].
	why notNil ifTrue: [^why].
	why := self lostRefusalWriting: writer written of: aClass.
	why notNil ifTrue: [^why].
	why := TonelReader refusalReading: text named: path holding: writer written.
	why notNil ifTrue: [^'the file would not read back as written, so it was not written: ', why].
	TonelWriter writeText: text toFile: path.
	held := (writer written collect: [:pair |
		self keyFor: (pair at: 2) meta: (pair at: 1) isMeta]) asSet.
	file := TonelSource fileFor: path.
	file notNil ifTrue: [self recordStamp: (TonelSource stampFrom: file)].
	^nil
]

{ #category : 'writing back' }
TonelSource >> changedOnDiskRefusal [
	"nil if my file is as it was when it was last loaded or written, or
	 is not there at all -- a file somebody deleted has nothing on disk
	 to lose, and writing puts it back.  Otherwise the reason a write
	 would be refused (Bugs5 FILES-4)."
	| file stamp |
	(self isExtension and: [prefix isNil]) ifTrue: [^nil].
	modificationTime isNil ifTrue: [^nil].
	file := TonelSource fileFor: path.
	file isNil ifTrue: [^nil].
	stamp := TonelSource stampFrom: file.
	(stamp isNil or: [stamp = self stamp]) ifTrue: [^nil].
	^path, ' has changed on disk since it was loaded, and writing ', name,
		' to it would throw that change away, so nothing was changed.',
		'  Load it again first -- TonelSource loadFile: ', path printString,
		' -- or send TonelSource forget: ', name,
		' to stop writing the class to it.'
]

{ #category : 'writing back' }
TonelSource >> unwritableRefusal [
	"nil if my file could be written now, or the reason it could not: the
	 directory in front of its name is gone, or the file is there and may
	 only be read.  A file that is not there is written back whole, as
	 changedOnDiskRefusal says; a directory that is not there cannot be.

	 Asked BEFORE an accept installs anything, as the stamp is (Bugs5
	 FILES-4).  An accept on a class whose file could not be written
	 installed the method and then raised from the write, so the Browser
	 showed a method the file did not have -- and the next load of the
	 file, a server's restart or its reload once the file was touched,
	 took the method out again as one the file did not hold (Bugs6
	 COMP-13).  The file is opened to ask, the way a stream would open
	 it, and closed again: the question costs no descriptor."
	| cut dir file readOnly |
	(self isExtension and: [prefix isNil]) ifTrue: [^nil].
	cut := 0.
	1 to: path size do: [:i | (path at: i) = $/ ifTrue: [cut := i]].
	cut > 1 ifTrue: [
		dir := path copyFrom: 1 to: cut - 1.
		(Disk isDirectoryNamed: dir) ifFalse: [
			^'the directory ', dir, ' is not there, so ', name,
				' cannot be written to ', path, ' and nothing was changed.',
				'  Put the directory back, or send TonelSource forget: ', name,
				' to stop writing the class to it.']].
	file := TonelSource fileFor: path.
	file isNil ifTrue: [^nil].
	readOnly := [[file open. file isReadOnly] on: Error do: [:e | e return: true]]
		ensure: [file close].
	readOnly ifFalse: [^nil].
	^path, ' may only be read, so ', name,
		' cannot be written to it and nothing was changed.',
		'  Make the file writable, or send TonelSource forget: ', name,
		' to stop writing the class to it.'
]

{ #category : 'accessing' }
TonelSource >> stamp [
	^Array with: modificationTime with: size
]

{ #category : 'accessing' }
TonelSource >> path [
	^path
]

{ #category : 'accessing' }
TonelSource >> name [
	^name
]

{ #category : 'accessing' }
TonelSource >> isExtension [
	^type = 'Extension'
]

{ #category : 'accessing' }
TonelSource >> prefix [
	"The `*Package' prefix of the categories an extension file owns; nil
	 for a Class file."
	^prefix
]
