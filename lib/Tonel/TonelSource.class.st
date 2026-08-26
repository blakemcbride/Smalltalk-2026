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

WHAT IS SHARED.  One Dictionary from class name to entry, for the image, read by
every request and written by the loads; so every access takes the Mutex, the
reads too, for the reason doc/CONCURRENCY.md gives about Set>>grow.  A load
holds the lock for its whole duration: two requests arriving for a class
nobody has loaded yet then load it once, and the second finds it present.
The reader compiles through the layer below the Browser's hooks, so nothing a
load does comes back here wanting the lock it already holds -- which matters,
because Mutex is not re-entrant and would report exactly that.

A write-back records the file's new time and size, so that the next reload
check does not read back what was just written.
"
Class {
	#name : 'TonelSource',
	#superclass : 'Object',
	#instVars : [ 'name', 'path', 'modificationTime', 'size' ],
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
	"The file's time and size, as an Array, or nil when it cannot be asked."
	| time size |
	time := aFile modificationTime.
	size := aFile sizeOnDisk.
	(time isKindOf: Integer) ifFalse: [^nil].
	^Array with: time with: size
]

{ #category : 'loading' }
TonelSource class >> loadFile: pathString [
	"Read the Tonel file into the image and remember where it came from.
	 Answer the class; raise TonelError if there is no such file."

	^Lock critical: [self lockedLoad: pathString]
]

{ #category : 'private' }
TonelSource class >> lockedLoad: pathString [
	"With the lock held: load, and record the file as it was at the load."
	| file stamp class entry |
	file := self fileFor: pathString.
	file isNil ifTrue: [^TonelError new signal: 'no such file: ', pathString].
	stamp := self stampFrom: file.
	class := TonelReader loadFile: pathString.
	class isNil ifTrue: [^nil].
	entry := Registry at: class name asSymbol ifAbsent: [nil].
	entry isNil ifTrue: [
		entry := self new setName: class name asSymbol path: pathString.
		Registry at: class name asSymbol put: entry].
	entry recordStamp: stamp.
	^class
]

{ #category : 'loading' }
TonelSource class >> reloadIfChanged: aClass [
	"If aClass came from a file and the file has changed since, load it
	 again.  Answer whether it was reloaded."

	^Lock critical: [ | entry file stamp |
		entry := Registry at: aClass name asSymbol ifAbsent: [nil].
		entry isNil ifTrue: [false] ifFalse: [
			file := self fileFor: entry path.
			file isNil ifTrue: [false] ifFalse: [
				stamp := self stampFrom: file.
				(stamp notNil and: [stamp ~= entry stamp])
					ifTrue: [self lockedLoad: entry path. true]
					ifFalse: [false]]]]
]

{ #category : 'writing back' }
TonelSource class >> noteChangeIn: aBehavior [
	"aBehavior -- a class or its metaclass -- has a method or definition
	 changed.  If it came from a file, write the file."

	| class |
	class := aBehavior isMeta ifTrue: [aBehavior soleInstance] ifFalse: [aBehavior].
	Lock critical: [ | entry file |
		entry := Registry at: class name asSymbol ifAbsent: [nil].
		entry notNil ifTrue: [
			TonelWriter write: (Smalltalk at: class name asSymbol) toFile: entry path.
			file := self fileFor: entry path.
			file notNil ifTrue: [entry recordStamp: (self stampFrom: file)]]]
]

{ #category : 'accessing' }
TonelSource class >> pathOf: aClass [
	"Where aClass came from, or nil."
	^Lock critical: [ | entry |
		entry := Registry at: aClass name asSymbol ifAbsent: [nil].
		entry isNil ifTrue: [nil] ifFalse: [entry path]]
]

{ #category : 'accessing' }
TonelSource class >> isTracked: aClass [
	^(self pathOf: aClass) notNil
]

{ #category : 'accessing' }
TonelSource class >> forget: aClass [
	"Stop tracking aClass; the file is left as it is."
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
TonelSource >> recordStamp: anArrayOrNil [
	modificationTime := anArrayOrNil isNil ifTrue: [nil] ifFalse: [anArrayOrNil at: 1].
	size := anArrayOrNil isNil ifTrue: [nil] ifFalse: [anArrayOrNil at: 2]
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
