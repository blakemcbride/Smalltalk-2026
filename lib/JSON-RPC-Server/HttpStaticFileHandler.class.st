"
Files under a document root, served as they are -- what Tomcat's default
servlet did for a Kiss front end: index.html, the kiss/*.js the page loads,
its images.

Only GET and HEAD.  The path is the request's, decoded, with two refusals
before anything is opened: a segment of `..' -- there is no legitimate reason
for a client to ask for a file above the root, and one that asks is looking
for /etc/passwd -- and a path that is not a path at all.  A directory answers
its index.html.  Anything else that is not a file is 404, including what the
file system refuses to open, because `forbidden' would tell a stranger that
the name exists.

The content type comes from the extension.  The bytes are read whole and sent
whole; a front end's largest file is a few hundred kilobytes of JavaScript.
"
Class {
	#name : 'HttpStaticFileHandler',
	#superclass : 'Object',
	#instVars : [ 'documentRoot' ],
	#category : 'JSON-RPC-Server'
}

{ #category : 'instance creation' }
HttpStaticFileHandler class >> on: aDirectoryPath [
	^self new setDocumentRoot: aDirectoryPath
]

{ #category : 'private' }
HttpStaticFileHandler >> setDocumentRoot: aString [
	"Without a trailing slash, so that root, '/', name joins cleanly."
	documentRoot := (aString size > 1 and: [aString last = $/])
		ifTrue: [aString copyFrom: 1 to: aString size - 1]
		ifFalse: [aString]
]

{ #category : 'accessing' }
HttpStaticFileHandler >> documentRoot [
	^documentRoot
]

{ #category : 'handling' }
HttpStaticFileHandler >> handle: aRequest [
	| relative full |
	(aRequest method = 'GET' or: [aRequest method = 'HEAD'])
		ifFalse: [^HttpResponse error: 405 reason: 'only GET and HEAD here'].
	relative := self safeRelativePathFor: aRequest path.
	relative isNil ifTrue: [^HttpResponse error: 403 reason: 'forbidden'].
	full := relative isEmpty ifTrue: [documentRoot] ifFalse: [documentRoot, '/', relative].
	(self isDirectory: full) ifTrue: [full := full, '/index.html'].
	(self isFile: full) ifFalse: [^HttpResponse notFound: 'not found: ', aRequest path].
	^HttpResponse
		bytes: (aRequest method = 'HEAD' ifTrue: [''] ifFalse: [self contentsOf: full])
		contentType: (HttpCodec mimeTypeForName: full)
]

{ #category : 'private' }
HttpStaticFileHandler >> safeRelativePathFor: aPath [
	"The path relative to the root, or nil if it must not be served.

	 The escape this refuses is `..', and the way it refuses it is by
	 splitting the DECODED path on its separators and looking at each
	 segment -- decoded first, so `%2F' is a real slash by the time the
	 split happens and cannot smuggle a segment past.

	 A BACKSLASH is a separator too, on Windows, and this method has no
	 platform test in it -- nor should it, since a path arrives from the
	 network and the machine that serves it is not the machine that wrote
	 it.  Splitting on `/' alone left `..\..\..\windows\win.ini' as ONE
	 segment, which is not `..', so it passed the filter and reached fopen
	 intact.  Windows.md says the platform is supported.

	 A segment holding a backslash is therefore refused outright rather than
	 split on it.  Refusing is the choice with no wrong answer in it:
	 splitting would turn a POSIX file legitimately named `a\b' into the
	 path a/b and serve a DIFFERENT file, where refusing only declines to
	 serve a name nobody should be asking for over HTTP.  A NUL is refused
	 the same way and for the same reason -- it truncates the name inside
	 the C library."
	| segments out |
	(aPath isEmpty or: [aPath first ~= $/]) ifTrue: [^nil].
	segments := (aPath substrings: '/') reject: [:s | s isEmpty or: [s = '.']].
	(segments anySatisfy: [:s |
		s = '..'
			or: [(s includes: $\)
			or: [s includes: (Character value: 0)]]]) ifTrue: [^nil].
	out := WriteStream on: (String new: aPath size).
	segments do: [:s | out nextPutAll: s] separatedBy: [out nextPut: $/].
	^out contents
]

{ #category : 'private' }
HttpStaticFileHandler >> isDirectory: aPath [
	^Disk isDirectoryNamed: aPath
]

{ #category : 'private' }
HttpStaticFileHandler >> isFile: aPath [
	| dir name |
	(self isDirectory: aPath) ifTrue: [^false].
	dir := FileDirectory directoryFromName: aPath setFileName: [:n | name := n].
	^[dir includesKey: name] on: Error do: [:e | false]
]

{ #category : 'private' }
HttpStaticFileHandler >> contentsOf: aPath [
	| dir name stream |
	dir := FileDirectory directoryFromName: aPath setFileName: [:n | name := n].
	stream := (dir findKey: name) asFileStream.
	^[stream contentsOfEntireFile] ensure: [stream close]
]
