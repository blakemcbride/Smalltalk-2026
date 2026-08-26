"
From a request's _class to a class in the image, loading the file the first
time and reloading it when it changes.

`services.Adder' names backend/services/Adder.class.st: the dots are
directory separators, the last word is the class the file must define, and
the file is Tonel.  On the first request for it the file is read into the
image through TonelSource, which remembers where it came from; on every
request after, the file's time and size are compared with what was loaded,
and a changed file is read again before the request is served.  That is
Kiss's microservice property -- edit the file on a running server and the
next call runs the new code -- with the whole class as the unit rather than a
Java class file.

The name is checked before it is a path.  A request is a stranger's, and
`../../etc/passwd' is a stranger's idea of a class name; only letters, digits
and underscores between the dots reach the file system.

The lock is held across a load, so two requests that arrive for a class
nobody has loaded yet load it once.
"
Class {
	#name : 'RestServiceLoader',
	#superclass : 'Object',
	#instVars : [ 'backendDirectory', 'lock' ],
	#category : 'Rest-Server'
}

{ #category : 'instance creation' }
RestServiceLoader class >> on: aDirectoryPath [
	^self new setBackendDirectory: aDirectoryPath
]

{ #category : 'private' }
RestServiceLoader >> setBackendDirectory: aString [
	backendDirectory := (aString size > 1 and: [aString last = $/])
		ifTrue: [aString copyFrom: 1 to: aString size - 1]
		ifFalse: [aString].
	lock := Mutex new
]

{ #category : 'accessing' }
RestServiceLoader >> backendDirectory [
	^backendDirectory
]

{ #category : 'naming' }
RestServiceLoader >> segmentsOf: aClassPath [
	"The words between the dots, or nil if any of them is not a name."
	| segments |
	(aClassPath isNil or: [aClassPath isEmpty]) ifTrue: [^nil].
	segments := aClassPath substrings: '.'.
	segments isEmpty ifTrue: [^nil].
	(segments allSatisfy: [:s |
		s isEmpty not and: [s allSatisfy: [:c | c isLetter or: [c isDigit or: [c = $_]]]]])
			ifFalse: [^nil].
	^segments
]

{ #category : 'naming' }
RestServiceLoader >> pathFor: aClassPath [
	"backend/services/Adder.class.st for `services.Adder', or nil."
	| segments out |
	segments := self segmentsOf: aClassPath.
	segments isNil ifTrue: [^nil].
	out := WriteStream on: (String new: 64).
	out nextPutAll: backendDirectory.
	segments do: [:s | out nextPut: $/; nextPutAll: s].
	out nextPutAll: '.class.st'.
	^out contents
]

{ #category : 'loading' }
RestServiceLoader >> classFor: aClassPath [
	"The class _class names, loaded or reloaded as needed.  Raises
	 RestError when there is no such file, or the file defines some other
	 class."
	| segments className path class |
	segments := self segmentsOf: aClassPath.
	segments isNil ifTrue: [^RestError new signal: 'not a class name: ', aClassPath printString].
	className := segments last asSymbol.
	path := self pathFor: aClassPath.
	^lock critical: [
		class := Smalltalk at: className ifAbsent: [nil].
		(class notNil and: [(TonelSource pathOf: class) = path])
			ifTrue: [
				TonelSource reloadIfChanged: class.
				Smalltalk at: className]
			ifFalse: [
				class := [TonelSource loadFile: path]
					on: TonelError
					do: [:e | RestError new signal: 'No back-end code found for ', aClassPath, ': ', e messageText].
				(class isNil or: [class name asSymbol ~= className])
					ifTrue: [RestError new signal: path, ' does not define ', className].
				class]]
]
