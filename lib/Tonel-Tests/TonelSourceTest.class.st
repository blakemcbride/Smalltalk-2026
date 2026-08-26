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
	#instVars : [ 'path' ],
	#category : 'Tonel-Tests'
}

{ #category : 'running' }
TonelSourceTest >> setUp [
	path := 'TonelTestSample.class.st'
]

{ #category : 'running' }
TonelSourceTest >> tearDown [
	(Smalltalk includesKey: #TonelTestSample) ifTrue: [
		TonelSource forget: (Smalltalk at: #TonelTestSample).
		(Smalltalk at: #TonelTestSample) removeFromSystem].
	(Disk includesKey: path) ifTrue: [Disk removeKey: path]
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
