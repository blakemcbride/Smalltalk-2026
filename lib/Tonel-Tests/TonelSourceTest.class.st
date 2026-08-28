"
The registry, against a real file: loaded on first use, reloaded when the file
changes, and written back when the class changes in the image -- through the
same messages the Browser sends.

The file is made in the directory the tests run from, under a name nothing
else uses, and removed afterwards.
"
Class {
	#name : 'TonelSourceTest',
	#superclass : 'TestCase',
	#instVars : [ 'path', 'extensionPath' ],
	#category : 'Tonel-Tests'
}

{ #category : 'running' }
TonelSourceTest >> setUp [
	path := 'TonelTestSample.class.st'.
	extensionPath := 'TonelTestSmallInteger.extension.st'
]

{ #category : 'running' }
TonelSourceTest >> tearDown [
	(Smalltalk includesKey: #TonelTestSample) ifTrue: [
		TonelSource forget: (Smalltalk at: #TonelTestSample).
		(Smalltalk at: #TonelTestSample) removeFromSystem].
	(Disk includesKey: path) ifTrue: [Disk removeKey: path].
	"The extension tests load a copy of a real file onto SmallInteger and
	 accept one method into it; both are undone through the layer below
	 the write-back hook, so that the undoing writes nothing."
	(SmallInteger includesSelector: #auditProbe) ifTrue: [SmallInteger basicRemoveSelector: #auditProbe].
	TonelSource forget: SmallInteger.
	(Disk includesKey: extensionPath) ifTrue: [Disk removeKey: extensionPath]
]

{ #category : 'private' }
TonelSourceTest >> contentsOf: aPath [
	"The text of a file by path, with every line ending a carriage return
	 so that a file written by the image and one in the tree compare."
	| dir name file stream text |
	dir := FileDirectory directoryFromName: aPath setFileName: [:n | name := n].
	file := dir findKey: name.
	stream := file asFileStream.
	text := [stream contentsOfEntireFile] ensure: [stream close].
	^text copyReplaceAll: (String with: (Character value: 10)) with: (String with: Character cr)
]

{ #category : 'private' }
TonelSourceTest >> count: aSubstring in: aString [
	"How many times aSubstring occurs in aString, without overlap."
	| n at |
	n := 0.
	at := aString indexOfSubCollection: aSubstring startingAt: 1.
	[at > 0] whileTrue: [
		n := n + 1.
		at := aString indexOfSubCollection: aSubstring startingAt: at + aSubstring size].
	^n
]

{ #category : 'private' }
TonelSourceTest >> realExtensionFile [
	"The path of the extension file Bugs3 B59 was found with, relative to
	 the tree the tests run in.  The test fails rather than skips when it
	 is not there: a test that cannot find its subject is not passing."
	| real |
	real := 'lib/Concurrency/SmallInteger.extension.st'.
	self assert: (TonelSource fileFor: real) notNil.
	^real
]

{ #category : 'private' }
TonelSourceTest >> copyTheRealExtensionFile [
	"A copy of Concurrency's SmallInteger extension under a name of the
	 tests' own, in the directory they run from."
	| stream |
	stream := Disk textFile: extensionPath.
	[stream nextPutAll: (self contentsOf: self realExtensionFile); shorten] ensure: [stream close]
]

{ #category : 'private' }
TonelSourceTest >> writeFile: aString [
	| stream |
	stream := Disk textFile: path.
	[stream nextPutAll: aString; shorten] ensure: [stream close]
]

{ #category : 'private' }
TonelSourceTest >> fileContents [
	| stream |
	stream := Disk oldFile: path.
	^[stream contentsOfEntireFile] ensure: [stream close]
]

{ #category : 'private' }
TonelSourceTest >> sampleAnswering: anInteger [
	| cr |
	cr := String with: Character cr.
	^'Class { #name : ''TonelTestSample'', #superclass : ''Object'', #category : ''Tonel-Tests'' }', cr,
		'{ #category : ''accessing'' }', cr,
		'TonelTestSample >> answer [', cr, '	^', anInteger printString, cr, ']', cr
]

{ #category : 'tests' }
TonelSourceTest >> testLoadFromAFile [
	| class |
	self writeFile: (self sampleAnswering: 1).
	class := TonelSource loadFile: path.
	self assert: class name equals: 'TonelTestSample'.
	self assert: class new answer equals: 1.
	self assert: (TonelSource isTracked: class).
	self assert: (TonelSource pathOf: class) equals: path.
	self assert: (TonelSource trackedClasses includes: class)
]

{ #category : 'tests' }
TonelSourceTest >> testAMissingFileRaises [
	self should: [TonelSource loadFile: 'no/such/File.class.st'] raise: TonelError
]

{ #category : 'tests' }
TonelSourceTest >> testReloadWhenTheFileChanges [
	"The size changes with the answer's digits, so the change is seen
	 within the same second."
	| class |
	self writeFile: (self sampleAnswering: 1).
	class := TonelSource loadFile: path.
	self deny: (TonelSource reloadIfChanged: class).
	self writeFile: (self sampleAnswering: 22).
	self assert: (TonelSource reloadIfChanged: class).
	self assert: class new answer equals: 22.
	self deny: (TonelSource reloadIfChanged: class)
]

{ #category : 'tests' }
TonelSourceTest >> testAnAcceptedMethodIsWrittenBack [
	"compile:classified:notifying: is the Browser's accept."
	| class |
	self writeFile: (self sampleAnswering: 1).
	class := TonelSource loadFile: path.
	class compile: 'twice ^self answer * 2' classified: 'accessing' notifying: nil.
	self assert: (self fileContents includesSubstring: 'TonelTestSample >> twice [').
	self assert: (self fileContents includesSubstring: '^self answer * 2').
	"And the write-back is not read back as a change."
	self deny: (TonelSource reloadIfChanged: class)
]

{ #category : 'tests' }
TonelSourceTest >> testARemovedMethodIsWrittenBack [
	| class |
	self writeFile: (self sampleAnswering: 1).
	class := TonelSource loadFile: path.
	class compile: 'twice ^self answer * 2' classified: 'accessing' notifying: nil.
	class removeSelector: #twice.
	self deny: (self fileContents includesSubstring: 'twice').
	self assert: (self fileContents includesSubstring: 'TonelTestSample >> answer [')
]

{ #category : 'tests' }
TonelSourceTest >> testARedefinitionIsWrittenBack [
	| class |
	self writeFile: (self sampleAnswering: 1).
	class := TonelSource loadFile: path.
	Object subclass: #TonelTestSample instanceVariableNames: 'extra' classVariableNames: ''
		poolDictionaries: '' category: 'Tonel-Tests'.
	self assert: (self fileContents includesSubstring: '#instVars : [ ''extra'' ]').
	"The redefinition made a new class object; the registry follows the name."
	class := Smalltalk at: #TonelTestSample.
	self assert: class new answer equals: 1.
	self assert: (TonelSource isTracked: class)
]

{ #category : 'tests' }
TonelSourceTest >> testAnUntrackedClassIsNotWritten [
	"The hook looks the class up and finds nothing; nothing is written."
	| class |
	class := TonelReader loadString: (self sampleAnswering: 5) named: 'text'.
	class compile: 'more ^6' classified: 'accessing' notifying: nil.
	self deny: (Disk includesKey: path).
	self deny: (TonelSource isTracked: class)
]

{ #category : 'tests' }
TonelSourceTest >> testForget [
	| class |
	self writeFile: (self sampleAnswering: 1).
	class := TonelSource loadFile: path.
	TonelSource forget: class.
	self deny: (TonelSource isTracked: class).
	class compile: 'more ^6' classified: 'accessing' notifying: nil.
	self deny: (self fileContents includesSubstring: 'more')
]

{ #category : 'tests' }
TonelSourceTest >> testAnExtensionFileIsWrittenBackAsAnExtension [
	"Bugs3 B59.  A class loaded from an Extension file -- SmallInteger,
	 from Concurrency's -- and then given a method in the Browser: the
	 file is written back as the Extension it was, holding the package's
	 methods and the new one and nothing of SmallInteger's forty-odd
	 others.  It was written back as a Class file holding all of them,
	 which the next bootstrap read as a second definition of the class."
	| text |
	self copyTheRealExtensionFile.
	self assert: (TonelSource loadFile: extensionPath) == SmallInteger.
	self assert: (TonelSource isTracked: SmallInteger).
	SmallInteger compile: 'auditProbe ^1' classified: '*Concurrency' notifying: nil.
	text := self contentsOf: extensionPath.
	self assert: (text includesSubstring: 'Extension { #name : ''SmallInteger'' }').
	self assert: (text includesSubstring: 'SmallInteger >> auditProbe [').
	self assert: (text includesSubstring: 'SmallInteger >> printOn: aStream base: b [').
	self deny: (text includesSubstring: 'Class {').
	"The method header, not the bare selector: printOn:base:'s own comment
	 names printString: as the sender whose base it now refuses."
	self deny: (text includesSubstring: 'SmallInteger >> printString:').
	self deny: (text includesSubstring: '#classVars').
	self assert: (self count: ' >> ' in: text) equals: 2.
	"And the write-back is not read back as a change."
	self deny: (TonelSource reloadIfChanged: SmallInteger)
]

{ #category : 'tests' }
TonelSourceTest >> testAnExtensionFileRoundTripsExactly [
	"Loaded and written back unchanged, the file is the same file, byte
	 for byte: the comment at the top, the one-line header, the method."
	self copyTheRealExtensionFile.
	TonelSource loadFile: extensionPath.
	TonelSource noteChangeIn: SmallInteger.
	self assert: (self contentsOf: extensionPath) equals: (self contentsOf: self realExtensionFile)
]

{ #category : 'tests' }
TonelSourceTest >> testAClassWithAClassFileAndAnExtensionFileWritesEach [
	"Both files of one class tracked: a change writes the class's own
	 methods to the Class file and the package's to the Extension file,
	 and neither gets the other's."
	| class cr extension |
	cr := String with: Character cr.
	self writeFile: (self sampleAnswering: 1).
	class := TonelSource loadFile: path.
	extension := 'TonelTestSampleExtra.extension.st'.
	[ | stream classText extensionText |
	 stream := Disk textFile: extension.
	 [stream nextPutAll: 'Extension { #name : ''TonelTestSample'' }', cr,
		'{ #category : ''*Tonel-Tests-Extra'' }', cr,
		'TonelTestSample >> extra [ ^self answer + 1 ]', cr; shorten] ensure: [stream close].
	 self assert: (TonelSource loadFile: extension) == class.
	 self assert: (TonelSource pathOf: class) equals: path.
	 self assert: (TonelSource pathsOf: class) size equals: 2.
	 class compile: 'twice ^self answer * 2' classified: 'accessing' notifying: nil.
	 class compile: 'thrice ^self answer * 3' classified: '*Tonel-Tests-Extra' notifying: nil.
	 classText := self contentsOf: path.
	 extensionText := self contentsOf: extension.
	 self assert: (classText includesSubstring: 'TonelTestSample >> twice [').
	 self deny: (classText includesSubstring: 'thrice').
	 self deny: (classText includesSubstring: 'extra').
	 self assert: (extensionText includesSubstring: 'Extension { #name : ''TonelTestSample'' }').
	 self assert: (extensionText includesSubstring: 'TonelTestSample >> thrice [').
	 self assert: (extensionText includesSubstring: 'TonelTestSample >> extra [').
	 self deny: (extensionText includesSubstring: 'twice')]
		ensure: [(Disk includesKey: extension) ifTrue: [Disk removeKey: extension]]
]
