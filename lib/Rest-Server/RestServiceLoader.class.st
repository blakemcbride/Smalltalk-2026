"
From a request's _class to a class in the image, loading the file the first
time and reloading it when it changes.

`services.Adder' names backend/services/Adder.class.st, and so does
`services/Adder': the dots are directory separators, a slash is the same
separator already, the last word is the class the file must define, and
the file is Tonel.  Both spellings because Kiss takes both -- its Java
replaces each dot with a slash and builds a file name, so a slash passes
through -- and because Kiss's own front end sends the slash: every screen
in the demo says `services/Crud', `services/Users', `services/MyService'.
This loader took the dots alone until a browser was pointed at the demo
(2026-08-26) and every screen but the login answered `not a class name':
the tests, the curl in the manual and the smoke runs had all spelled the
name with dots, and nothing had sent what the front end sends.  The demo's
tests spell it with the slash now, for that reason.  On the first request for it the file is read into the
image through TonelSource, which remembers where it came from; on every
request after, the file's time and size are compared with what was loaded,
and a changed file is read again before the request is served.  That is
Kiss's microservice property -- edit the file on a running server and the
next call runs the new code -- with the whole class as the unit rather than a
Java class file.

The name is checked before it is a path.  A request is a stranger's, and
`../../etc/passwd' is a stranger's idea of a class name; only letters, digits
and underscores between the separators reach the file system, and a
separator with nothing between it and the next -- `services//Adder',
`.Adder', `services.Adder/' -- is refused rather than skipped, so no
spelling of a name is a path with an empty or a `..' step in it.

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
	"The words between the dots or slashes, or nil if any of them is not a
	 name.  A dot and a slash are the same separator, as they are to Kiss,
	 whose front end sends `services/Adder' for what the tests here spell
	 `services.Adder' (the class comment says how that was found).
	 substrings: folds a run of separators into one break, which would let
	 `services//Adder' or `/services.Adder' through as a name; the count
	 of separators is held to one fewer than the words so that it does not."
	| segments separators |
	(aClassPath isNil or: [aClassPath isEmpty]) ifTrue: [^nil].
	segments := aClassPath substrings: './'.
	segments isEmpty ifTrue: [^nil].
	separators := (aClassPath select: [:c | c = $. or: [c = $/]]) size.
	separators = (segments size - 1) ifFalse: [^nil].
	(segments allSatisfy: [:s |
		s isEmpty not and: [s allSatisfy: [:c | c isLetter or: [c isDigit or: [c = $_]]]]])
			ifFalse: [^nil].
	^segments
]

{ #category : 'naming' }
RestServiceLoader >> pathFor: aClassPath [
	"backend/services/Adder.class.st for `services.Adder' or `services/Adder', or nil."
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
RestServiceLoader >> classFor: aClassPath ifAbsent: aBlock [
	"The class, as classFor:, or the value of aBlock when there is no file
	 for it.  Only absence goes to the block: a file that exists and will
	 not load still raises, because that is a fault and not an absence.
	 This is how the server asks for the optional Init class."

	| path dir name |
	path := self pathFor: aClassPath.
	path isNil ifTrue: [^aBlock value].
	dir := FileDirectory directoryFromName: path setFileName: [:n | name := n].
	([dir includesKey: name] on: Error do: [:e | false]) ifFalse: [^aBlock value].
	^self classFor: aClassPath
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
