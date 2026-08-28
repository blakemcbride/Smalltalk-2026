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
"
Class {
	#name : 'TonelSource',
	#superclass : 'Object',
	#instVars : [ 'name', 'path', 'modificationTime', 'size', 'type', 'prefix', 'comment' ],
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
	entry
		noteType: reader fileType
		prefix: (reader fileType = 'Extension'
			ifTrue: [self prefixFor: pathString categories: reader categoriesRead]
			ifFalse: [nil])
		comment: (reader fileType = 'Extension' ifTrue: [reader fileComment] ifFalse: [nil]).
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
	 of them with what belongs to it."

	| class |
	class := aBehavior isMeta ifTrue: [aBehavior soleInstance] ifFalse: [aBehavior].
	Lock critical: [ | entries |
		entries := self entriesFor: class name asSymbol.
		entries notNil ifTrue: [
			entries do: [:entry | entry writeBack: (Smalltalk at: class name asSymbol)]]]
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
TonelSource >> noteType: aString prefix: prefixOrNil comment: commentOrNil [
	"What the file was: 'Class' or 'Extension', and for an extension the
	 prefix of the categories it owns and the comment at the top of it."
	type := aString.
	prefix := prefixOrNil.
	comment := commentOrNil
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
	 so that the write is not read back as a change."
	| file |
	self isExtension
		ifTrue: [
			prefix isNil ifTrue: [^self].
			TonelWriter writeExtensionOf: aClass prefix: prefix comment: comment toFile: path]
		ifFalse: [TonelWriter write: aClass toFile: path].
	file := TonelSource fileFor: path.
	file notNil ifTrue: [self recordStamp: (TonelSource stampFrom: file)]
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
