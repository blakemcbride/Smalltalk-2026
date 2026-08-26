"
An HTTP/1.1 request, read from a stream: the request line, the headers, and a
body of the declared length.

READING IS BOUNDED, because the stream is a stranger's.  A request line or
header line longer than 8,192 bytes is refused (414 or 431), more than a
hundred headers is refused (431), a body larger than the server's limit is
refused before it is read (413), and a body with no declared length --
`Transfer-Encoding: chunked' -- is refused (411) rather than parsed, because
a JSON-RPC client always knows how long its document is and a server that
accepted an unbounded body would be a server that could be filled up.  Each
refusal is an HttpError with its status; the connection turns it into the
response.

Header names are kept lower-cased, so headerAt: 'content-type' finds
`Content-Type:' and `CONTENT-TYPE:' alike.  The body is a String of bytes.

A multipart/form-data body is taken apart on request, into HttpParts, by the
boundary the content type names; formParameters answers the fields among
them as a Dictionary and parts answers everything.  That is the shape a Kiss
front end sends an upload in.

The same parser reads from a SocketStream on a connection and from a
ReadStream on a String, which is how the tests read it without a socket.
"
Class {
	#name : 'HttpRequest',
	#superclass : 'Object',
	#instVars : [ 'method', 'target', 'path', 'queryString', 'version', 'headers', 'body', 'remoteAddress', 'parts', 'query' ],
	#category : 'JSON-RPC-Server'
}

{ #category : 'instance creation' }
HttpRequest class >> readFrom: aStream maxBodyBytes: limit [
	"Read one request.  Answer nil if the stream ended before a request
	 line -- the peer went away between requests, which is not an error."
	^self new readFrom: aStream maxBodyBytes: limit
]

{ #category : 'instance creation' }
HttpRequest class >> fromString: aString [
	"A request parsed from text, for tests; the body limit is generous."
	^self readFrom: (ReadStream on: aString) maxBodyBytes: 16 * 1024 * 1024
]

{ #category : 'instance creation' }
HttpRequest class >> fromString: aString maxBodyBytes: limit [
	^self readFrom: (ReadStream on: aString) maxBodyBytes: limit
]

{ #category : 'private - reading' }
HttpRequest >> nextLineFrom: aStream limit: anInteger status: statusForTooLong [
	"A line up to LF with a CR before it dropped, of at most anInteger
	 bytes; nil at end of stream before any byte."
	| out c n lf cr |
	lf := Character value: 10.
	cr := Character value: 13.
	out := WriteStream on: (String new: 80).
	n := 0.
	[c := aStream next.
	 c isNil ifTrue: [^n = 0 ifTrue: [nil] ifFalse: [out contents]].
	 c = lf] whileFalse: [
		n := n + 1.
		n > anInteger ifTrue: [HttpError status: statusForTooLong reason: 'a line is too long'].
		out nextPut: c].
	n := out contents.
	(n size > 0 and: [n last = cr]) ifTrue: [^n copyFrom: 1 to: n size - 1].
	^n
]

{ #category : 'private - reading' }
HttpRequest >> readFrom: aStream maxBodyBytes: limit [
	| line words count |
	headers := Dictionary new.
	line := self nextLineFrom: aStream limit: 8192 status: 414.
	line isNil ifTrue: [^nil].
	"Tolerate one empty line before the request, which some clients send
	 after a body."
	line isEmpty ifTrue: [
		line := self nextLineFrom: aStream limit: 8192 status: 414.
		line isNil ifTrue: [^nil]].
	words := line substrings: ' '.
	words size = 3 ifFalse: [HttpError status: 400 reason: 'not a request line: ', line].
	method := words first asUppercase.
	target := words at: 2.
	version := words at: 3.
	(version beginsWith: 'HTTP/1.') ifFalse: [HttpError status: 400 reason: 'not HTTP/1: ', version].
	self setTarget: target.
	count := 0.
	[line := self nextLineFrom: aStream limit: 8192 status: 431.
	 line isNil ifTrue: [HttpError status: 400 reason: 'the headers were cut off'].
	 line isEmpty] whileFalse: [ | colon name value |
		count := count + 1.
		count > 100 ifTrue: [HttpError status: 431 reason: 'too many headers'].
		colon := line indexOf: $:.
		colon < 2 ifTrue: [HttpError status: 400 reason: 'not a header: ', line].
		name := (line copyFrom: 1 to: colon - 1) asLowercase.
		value := (line copyFrom: colon + 1 to: line size) trimmed.
		headers at: name put: value].
	self readBodyFrom: aStream maxBodyBytes: limit
]

{ #category : 'private - reading' }
HttpRequest >> readBodyFrom: aStream maxBodyBytes: limit [
	| length text |
	(self headerAt: 'transfer-encoding') notNil
		ifTrue: [HttpError status: 411 reason: 'a body must declare its length'].
	text := self headerAt: 'content-length'.
	text isNil ifTrue: [body := ''. ^self].
	length := text asInteger.
	(length isNil or: [length < 0]) ifTrue: [HttpError status: 400 reason: 'not a length: ', text].
	length > limit ifTrue: [HttpError status: 413 reason: 'the body is larger than ', limit printString, ' bytes'].
	body := aStream next: length.
	body size < length ifTrue: [HttpError status: 400 reason: 'the body was cut off']
]

{ #category : 'private' }
HttpRequest >> setTarget: aString [
	| q |
	target := aString.
	q := aString indexOf: $?.
	q = 0
		ifTrue: [path := HttpCodec percentDecodePath: aString. queryString := '']
		ifFalse: [path := HttpCodec percentDecodePath: (aString copyFrom: 1 to: q - 1).
				  queryString := aString copyFrom: q + 1 to: aString size]
]

{ #category : 'accessing' }
HttpRequest >> method [
	"GET, POST, ... upper-cased."
	^method
]

{ #category : 'accessing' }
HttpRequest >> target [
	"The request target as sent: the path and the query, still encoded."
	^target
]

{ #category : 'accessing' }
HttpRequest >> path [
	"The path, decoded, without the query."
	^path
]

{ #category : 'accessing' }
HttpRequest >> queryString [
	^queryString
]

{ #category : 'accessing' }
HttpRequest >> version [
	^version
]

{ #category : 'accessing' }
HttpRequest >> headers [
	"Every header, by lower-cased name."
	^headers
]

{ #category : 'accessing' }
HttpRequest >> headerAt: aString [
	"The header of that name, whatever its case, or nil."
	^headers at: aString asLowercase ifAbsent: [nil]
]

{ #category : 'accessing' }
HttpRequest >> body [
	^body
]

{ #category : 'accessing' }
HttpRequest >> contentType [
	"The media type without its parameters -- `multipart/form-data' from
	 `multipart/form-data; boundary=...'."
	| full semi |
	full := self headerAt: 'content-type'.
	full isNil ifTrue: [^nil].
	semi := full indexOf: $;.
	^(semi = 0 ifTrue: [full] ifFalse: [full copyFrom: 1 to: semi - 1]) trimmed asLowercase
]

{ #category : 'accessing' }
HttpRequest >> contentTypeParameterAt: aName [
	"A parameter of the content type -- `boundary', `charset' -- or nil."
	| full |
	full := self headerAt: 'content-type'.
	full isNil ifTrue: [^nil].
	(full substrings: ';') do: [:piece | | eq p |
		p := piece trimmed.
		eq := p indexOf: $=.
		(eq > 1 and: [(p copyFrom: 1 to: eq - 1) trimmed asLowercase = aName asLowercase]) ifTrue: [ | v |
			v := (p copyFrom: eq + 1 to: p size) trimmed.
			(v size >= 2 and: [v first = $" and: [v last = $"]]) ifTrue: [v := v copyFrom: 2 to: v size - 1].
			^v]].
	^nil
]

{ #category : 'accessing' }
HttpRequest >> remoteAddress [
	^remoteAddress
]

{ #category : 'accessing' }
HttpRequest >> remoteAddress: aString [
	remoteAddress := aString
]

{ #category : 'accessing' }
HttpRequest >> query [
	"The query string as a Dictionary, decoded."
	^query ifNil: [query := HttpCodec parseQuery: queryString]
]

{ #category : 'accessing' }
HttpRequest >> parameterAt: aName [
	"A query parameter, or nil."
	^self query at: aName ifAbsent: [nil]
]

{ #category : 'testing' }
HttpRequest >> isKeepAlive [
	"Whether the client expects the connection to stay open: HTTP/1.1
	 unless it says close, HTTP/1.0 only if it says keep-alive."
	| connection |
	connection := self headerAt: 'connection'.
	connection := connection isNil ifTrue: [''] ifFalse: [connection asLowercase].
	version = 'HTTP/1.0' ifTrue: [^connection includesSubstring: 'keep-alive'].
	^(connection includesSubstring: 'close') not
]

{ #category : 'testing' }
HttpRequest >> isMultipart [
	^self contentType = 'multipart/form-data'
]

{ #category : 'testing' }
HttpRequest >> isJson [
	^self contentType = 'application/json'
]

{ #category : 'multipart' }
HttpRequest >> parts [
	"The parts of a multipart body, in order; empty for any other body."
	^parts ifNil: [parts := self parseParts]
]

{ #category : 'multipart' }
HttpRequest >> formParameters [
	"The fields of a multipart body -- the parts that are not files -- as a
	 Dictionary from name to value."
	| dict |
	dict := Dictionary new.
	self parts do: [:part | part isFile ifFalse: [dict at: part name put: part body]].
	^dict
]

{ #category : 'multipart' }
HttpRequest >> partNamed: aString [
	^self parts detect: [:p | p name = aString] ifNone: [nil]
]

{ #category : 'private - multipart' }
HttpRequest >> parseParts [
	"multipart/form-data, RFC 7578: `--boundary' CRLF headers CRLF CRLF
	 body CRLF ... `--boundary--'.  The body of a part ends at the CRLF
	 before the next boundary line."
	| boundary delimiter out position |
	self isMultipart ifFalse: [^#()].
	boundary := self contentTypeParameterAt: 'boundary'.
	boundary isNil ifTrue: [HttpError status: 400 reason: 'a multipart body names no boundary'].
	delimiter := '--', boundary.
	out := OrderedCollection new.
	position := body indexOfSubstring: delimiter startingAt: 1.
	position = 0 ifTrue: [^out].
	position := position + delimiter size.
	[ | next headerEnd partHeaders partBody |
	 "After a delimiter: `--' ends the body, otherwise CRLF then headers."
	 (position + 1 <= body size and: [(body copyFrom: position to: position + 1) = '--']) ifTrue: [^out].
	 position := self skipLineEndIn: body from: position.
	 headerEnd := self indexOfBlankLineIn: body from: position.
	 headerEnd = 0 ifTrue: [HttpError status: 400 reason: 'a part has no headers'].
	 partHeaders := self parsePartHeaders: (body copyFrom: position to: headerEnd - 1).
	 position := headerEnd.
	 position := self skipLineEndIn: body from: position.
	 position := self skipLineEndIn: body from: position.
	 next := body indexOfSubstring: delimiter startingAt: position.
	 next = 0 ifTrue: [HttpError status: 400 reason: 'a part is not closed'].
	 partBody := body copyFrom: position to: (self trimLineEndBefore: next in: body) - 1.
	 out add: (HttpPart
		name: (partHeaders at: 'name' ifAbsent: [''])
		fileName: (partHeaders at: 'filename' ifAbsent: [nil])
		contentType: (partHeaders at: 'content-type' ifAbsent: [nil])
		body: partBody).
	 position := next + delimiter size.
	 true] whileTrue
]

{ #category : 'private - multipart' }
HttpRequest >> skipLineEndIn: aString from: i [
	"Past one CRLF or LF at i, if there is one."
	| j |
	j := i.
	(j <= aString size and: [(aString at: j) = (Character value: 13)]) ifTrue: [j := j + 1].
	(j <= aString size and: [(aString at: j) = (Character value: 10)]) ifTrue: [j := j + 1].
	^j
]

{ #category : 'private - multipart' }
HttpRequest >> trimLineEndBefore: i in: aString [
	"The index of the CRLF (or LF) just before i, or i."
	| j |
	j := i.
	(j > 1 and: [(aString at: j - 1) = (Character value: 10)]) ifTrue: [j := j - 1].
	(j > 1 and: [(aString at: j - 1) = (Character value: 13)]) ifTrue: [j := j - 1].
	^j
]

{ #category : 'private - multipart' }
HttpRequest >> indexOfBlankLineIn: aString from: i [
	"The index of the line end that begins a blank line at or after i: the
	 LF of the first CRLF CRLF (or LF LF), or 0."
	| j lf cr |
	lf := Character value: 10.
	cr := Character value: 13.
	j := i.
	[j <= aString size] whileTrue: [
		(aString at: j) = lf ifTrue: [ | k |
			k := j + 1.
			(k <= aString size and: [(aString at: k) = cr]) ifTrue: [k := k + 1].
			(k <= aString size and: [(aString at: k) = lf]) ifTrue: [
				^(j > 1 and: [(aString at: j - 1) = cr]) ifTrue: [j - 1] ifFalse: [j]]].
		j := j + 1].
	^0
]

{ #category : 'private - multipart' }
HttpRequest >> parsePartHeaders: aString [
	"The headers of one part, as a Dictionary: the content-disposition's
	 name and filename parameters flattened in beside content-type."
	| dict lf |
	dict := Dictionary new.
	lf := Character value: 10.
	(aString substrings: (String with: lf)) do: [:rawLine | | line colon name value |
		line := rawLine trimmed.
		colon := line indexOf: $:.
		colon > 1 ifTrue: [
			name := (line copyFrom: 1 to: colon - 1) trimmed asLowercase.
			value := (line copyFrom: colon + 1 to: line size) trimmed.
			name = 'content-disposition'
				ifTrue: [
					(value substrings: ';') do: [:piece | | p eq |
						p := piece trimmed.
						eq := p indexOf: $=.
						eq > 1 ifTrue: [ | v |
							v := (p copyFrom: eq + 1 to: p size) trimmed.
							(v size >= 2 and: [v first = $" and: [v last = $"]])
								ifTrue: [v := v copyFrom: 2 to: v size - 1].
							dict at: (p copyFrom: 1 to: eq - 1) trimmed asLowercase put: v]]]
				ifFalse: [dict at: name put: value]]].
	^dict
]

{ #category : 'printing' }
HttpRequest >> printOn: aStream [
	aStream nextPutAll: 'HttpRequest '; nextPutAll: (method ifNil: ['?']); space; nextPutAll: (target ifNil: ['?'])
]
