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

FRAMING IS STRICT, because the documented way to run this server is behind a
reverse proxy and two parsers that disagree about where one message ends and
the next begins is the whole of request smuggling.  Lines end with CR LF and
a bare line feed is 400; a field name with whitespace in it is 400 rather
than trimmed; a second Content-Length is 400 rather than the last one
winning; a Content-Length that is not all digits is 400 rather than as much
of it as asInteger understood; and Transfer-Encoding together with
Content-Length is 400 rather than one of them being preferred.  Each of
those was accepted here, and each was a request whose bytes a proxy in front
could have divided differently (Bugs3 B56).

THE HOST, AND THE REST OF RFC 7230'S MUSTS.  An HTTP/1.1 request with no
Host header is 400, and so is a request of any version with two: RFC 7230
section 5.4 requires both refusals, and with two Hosts a Dictionary kept
the last, which is the one HttpServer>>allowsOrigin: compares the Origin
against, while a proxy in front may have routed by the first -- two
parsers disagreeing about which host the request is for.  A target in
absolute form, `POST http://host:8001/rest HTTP/1.1', is read as the path
and query after its authority, as section 5.3.2 says a server must, and
the authority is then the request's Host, as RFC 9112 says an origin
server must take it.  Any other header that comes twice is one value, the
lines joined by `, ' in order (section 3.2.2), so `Connection: close' on
one line and `keep-alive' on the next is a request that asked to close --
the last line no longer overwrites the first (Bugs07 NET-12).

A multipart/form-data body is taken apart on request, into HttpParts, by the
boundary the content type names; formParameters answers the fields among
them as a Dictionary and parts answers everything.  That is the shape a Kiss
front end sends an upload in.

The same parser reads from a SocketStream on a connection and from a
ReadStream on a String, which is how the tests read it without a socket.

EXPECT: 100-CONTINUE.  A client with a large body may send its headers
alone and wait to be told to go on: curl does for every body over a
megabyte, and so do libcurl's users, PHP's Guzzle among them.  RFC 7231
says the server MUST then either answer at once with a final status or
send the interim `100 Continue'.  This parser did neither -- it went
straight to reading a body the client was holding back, and curl waited a
second before sending it anyway, while a client with no such fallback
waited for its own timeout (Bugs07 NET-9).  Whoever reads from a
connection passes readFrom:maxBodyBytes:continue: a block that writes the
interim response, and it is run once the headers have been read and the
length accepted, just before the body is read, for an HTTP/1.1 request
with a body that asks for it.  A refusal the headers already decide -- a
413, a 411 -- is the immediate final status instead, as it always was.  An
expectation other than 100-continue is 417, which RFC 7231 allows; an
HTTP/1.0 request's Expect is ignored, which it requires.
"
Class {
	#name : 'HttpRequest',
	#superclass : 'Object',
	#instVars : [ 'method', 'target', 'path', 'queryString', 'version', 'headers', 'body', 'remoteAddress', 'parts', 'query', 'partsByName' ],
	#category : 'JSON-RPC-Server'
}

{ #category : 'instance creation' }
HttpRequest class >> readFrom: aStream maxBodyBytes: limit [
	"Read one request.  Answer nil if the stream ended before a request
	 line -- the peer went away between requests, which is not an error."
	^self new readFrom: aStream maxBodyBytes: limit continue: nil
]

{ #category : 'instance creation' }
HttpRequest class >> readFrom: aStream maxBodyBytes: limit continue: aBlockOrNil [
	"Read one request, evaluating aBlockOrNil -- which writes `100
	 Continue' on the connection -- when the client is holding its body
	 back until it is told to send it.  See the class comment, EXPECT."
	^self new readFrom: aStream maxBodyBytes: limit continue: aBlockOrNil
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
	"A line ended by CR LF, of at most anInteger bytes, without its ending;
	 nil at end of stream before any byte.

	 A BARE LINE FEED IS REFUSED, with 400.  RFC 7230 says a recipient MAY
	 accept one and this one does not, because Bugs3 B56 is about the case
	 where accepting it costs something: the documented way to run this
	 server is behind a reverse proxy, and a proxy in front that recognises
	 only CR LF sees a different set of message boundaries in the same bytes
	 than a server that also recognises LF.  Two parsers that disagree about
	 where a request ends is the whole of request smuggling.  A client that
	 cannot manage a carriage return is not a client this server has, and a
	 refusal that names the fault costs it one round trip.

	 The end of the stream is NOT that fault: a connection that stops
	 mid-line answers what it had, and the caller turns a truncated request
	 into `the headers were cut off' or `not a request line'."
	| out c n line lf cr |
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
	line := out contents.
	(line size > 0 and: [line last = cr]) ifTrue: [^line copyFrom: 1 to: line size - 1].
	HttpError status: 400 reason: 'a line ends with a bare line feed'
]

{ #category : 'private - reading' }
HttpRequest >> readFrom: aStream maxBodyBytes: limit continue: aBlockOrNil [
	| line words count authority |
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
	authority := self setTarget: target.
	count := 0.
	[line := self nextLineFrom: aStream limit: 8192 status: 431.
	 line isNil ifTrue: [HttpError status: 400 reason: 'the headers were cut off'].
	 line isEmpty] whileFalse: [ | colon name value |
		count := count + 1.
		count > 100 ifTrue: [HttpError status: 431 reason: 'too many headers'].
		colon := line indexOf: $:.
		colon < 2 ifTrue: [HttpError status: 400 reason: 'not a header: ', line].
		name := (line copyFrom: 1 to: colon - 1) asLowercase.
		"Bugs3 B56: `Content-Length : 25' -- one space before the colon --
		 was read as a header named `content-length ', which headerAt:
		 never finds, so the length was ignored and the body's bytes were
		 read as the next request on the connection.  That is a smuggled
		 request, and it worked.  RFC 7230 says there is no whitespace
		 between a field name and its colon and that a message with any
		 must be rejected; every character at or below a space, and the
		 DEL above them, is refused here rather than trimmed away, because
		 trimming would make this parser agree with a proxy that trims and
		 disagree with one that rejects."
		(name anySatisfy: [:c | c asInteger <= 32 or: [c asInteger = 127]])
			ifTrue: [HttpError status: 400 reason: 'a header name holds whitespace: ', name printString].
		"Two Content-Lengths are two answers to the question of where the
		 body ends.  A Dictionary kept the last of them, which is one of
		 the two answers a proxy in front might not have kept."
		(name = 'content-length' and: [headers includesKey: name])
			ifTrue: [HttpError status: 400 reason: 'two Content-Length headers'].
		"Two Hosts are two answers to which host the request is for: see
		 the class comment, THE HOST (Bugs07 NET-12)."
		(name = 'host' and: [headers includesKey: name])
			ifTrue: [HttpError status: 400 reason: 'two Host headers'].
		value := (line copyFrom: colon + 1 to: line size) trimmed.
		self addHeader: name value: value].
	self readBodyFrom: aStream maxBodyBytes: limit continue: aBlockOrNil.
	"An absolute-form target names the host itself, and RFC 9112 says an
	 origin server uses that and not the Host header.  Only now, after the
	 body: a request that lacked a Host is refused for it whatever its
	 target says, as RFC 7230 section 5.4 requires."
	authority notNil ifTrue: [headers at: 'host' put: authority]
]

{ #category : 'private - reading' }
HttpRequest >> addHeader: aName value: aString [
	"A header line, by its lower-cased name.  A name seen before has this
	 value joined to what it had, after a comma -- RFC 7230 section 3.2.2's
	 combination, which a list-valued field such as Connection means and
	 which leaves a field that is not one plainly malformed rather than
	 silently the last of its lines (Bugs07 NET-12).  Cookie, the field
	 that may not be repeated in HTTP/1.1 but is by HTTP/2 gateways, is
	 joined with the `; ' its own syntax separates pairs with."
	| previous |
	previous := headers at: aName ifAbsent: [nil].
	headers at: aName put: (previous isNil
		ifTrue: [aString]
		ifFalse: [previous, (aName = 'cookie' ifTrue: ['; '] ifFalse: [', ']), aString])
]

{ #category : 'private - reading' }
HttpRequest >> readBodyFrom: aStream maxBodyBytes: limit continue: aBlockOrNil [
	"The body, of the length the headers declare.

	 THE LENGTH IS ALL DIGITS OR IT IS NOT A LENGTH.  asInteger reads as far
	 as it understands and answers what it had, so `Content-Length: 5, 5' --
	 the shape a proxy produces when it folds two of them into one header --
	 answered 5 and left the second body on the connection (Bugs3 B56).
	 Every character is checked instead, and an empty value is refused too.

	 TRANSFER-ENCODING WITH CONTENT-LENGTH is 400 and not 411.  Either
	 header alone has one meaning; the two together have two, and RFC 7230
	 says the message must be rejected rather than resolved in favour of
	 one of them, precisely because the parser in front may resolve it in
	 favour of the other.  Transfer-Encoding on its own stays 411 -- see the
	 class comment: a chunked body is not read here at all."
	| length text |
	(self headerAt: 'transfer-encoding') notNil ifTrue: [
		(self headerAt: 'content-length') notNil
			ifTrue: [HttpError status: 400 reason: 'both Transfer-Encoding and Content-Length'].
		HttpError status: 411 reason: 'a body must declare its length'].
	text := self headerAt: 'content-length'.
	text isNil ifTrue: [
		self refuseWithoutAHost.
		body := ''.
		^self].
	(text isEmpty or: [text anySatisfy: [:c | c isDigit not]])
		ifTrue: [HttpError status: 400 reason: 'not a length: ', text].
	"A length with more digits than the limit has is over it, and is
	 refused before it is converted (Bugs07 NET-2).  The header line may
	 be 8,192 bytes, and converting 8,150 nines held a worker for 38
	 seconds -- before the 413 it was going to get, before the body, the
	 handler or the session -- while requestSeconds, which bounds the
	 reads, had already stopped counting.  Leading zeros do not count:
	 RFC 7230's length is 1*DIGIT and `007' is seven."
	(self significantDigitsIn: text) > limit printString size
		ifTrue: [HttpError status: 413 reason: 'the body is larger than ', limit printString, ' bytes'].
	length := text asInteger.
	(length isNil or: [length < 0]) ifTrue: [HttpError status: 400 reason: 'not a length: ', text].
	length > limit ifTrue: [HttpError status: 413 reason: 'the body is larger than ', limit printString, ' bytes'].
	self refuseWithoutAHost.
	"The headers are read and the length is accepted: a client waiting to
	 be told to send its body is told now (see the class comment, EXPECT)."
	(self expectsContinue and: [length > 0 and: [aBlockOrNil notNil]])
		ifTrue: [aBlockOrNil value].
	body := aStream next: length.
	body size < length ifTrue: [HttpError status: 400 reason: 'the body was cut off']
]

{ #category : 'private - reading' }
HttpRequest >> refuseWithoutAHost [
	"An HTTP/1.1 request -- any version after 1.0 -- must say which host
	 it is for, and RFC 7230 section 5.4 says the server MUST answer 400
	 when it does not (Bugs07 NET-12).  Asked once the body's framing is
	 settled, so that each framing refusal keeps the status that names
	 it, and before a client holding its body back is told to send it.
	 An empty Host is a Host: the RFC allows one for a target with no
	 authority."
	(version ~= 'HTTP/1.0' and: [(headers includesKey: 'host') not])
		ifTrue: [HttpError status: 400 reason: 'an HTTP/1.1 request must have a Host header']
]

{ #category : 'private - reading' }
HttpRequest >> expectsContinue [
	"Whether the client has said it will send its body only once it is
	 told to -- Expect: 100-continue, in HTTP/1.1.  Any other expectation
	 is 417; an HTTP/1.0 request's Expect is ignored, as RFC 7231 says it
	 must be.  See the class comment, EXPECT."
	| expect |
	expect := self headerAt: 'expect'.
	(expect isNil or: [version = 'HTTP/1.0']) ifTrue: [^false].
	expect asLowercase = '100-continue' ifFalse: [
		HttpError status: 417 reason: 'an expectation this server cannot meet: ', expect].
	^true
]

{ #category : 'private' }
HttpRequest >> significantDigitsIn: aString [
	"How many digits aString has after its leading zeros; aString is all
	 digits.  See readBodyFrom:maxBodyBytes:continue:."
	| first |
	first := aString findFirst: [:c | c ~= $0].
	first = 0 ifTrue: [^0].
	^aString size - first + 1
]

{ #category : 'private' }
HttpRequest >> setTarget: aString [
	"The path and the query of the target; answer the authority of an
	 absolute-form target, or nil.

	 THE ABSOLUTE FORM, `http://host:8001/rest?x=1', is what a client
	 sends a proxy and what RFC 7230 section 5.3.2 says a server MUST
	 accept as well.  It used to be taken whole as the path, which no
	 handler matched: 404, or 405 from the static files (Bugs07 NET-12).
	 The scheme is http or https in any case; the authority runs to the
	 first `/', `?' or `#'; an empty one, or one holding userinfo, which
	 an http URI may not carry, is 400.  Nothing after the authority is
	 the path `/'.  Every other target is the origin form, as before."
	| q rest lower start end authority |
	target := aString.
	rest := aString.
	authority := nil.
	lower := aString asLowercase.
	start := (lower beginsWith: 'http://')
		ifTrue: [8]
		ifFalse: [(lower beginsWith: 'https://') ifTrue: [9] ifFalse: [0]].
	start > 0 ifTrue: [
		end := start.
		[end <= aString size and: [('/?#' includes: (aString at: end)) not]]
			whileTrue: [end := end + 1].
		authority := aString copyFrom: start to: end - 1.
		(authority isEmpty or: [authority includes: $@])
			ifTrue: [HttpError status: 400 reason: 'not a request target: ', aString].
		rest := aString copyFrom: end to: aString size.
		(rest isEmpty or: [rest first ~= $/]) ifTrue: [rest := '/', rest]].
	q := rest indexOf: $?.
	q = 0
		ifTrue: [path := HttpCodec percentDecodePath: rest. queryString := '']
		ifFalse: [path := HttpCodec percentDecodePath: (rest copyFrom: 1 to: q - 1).
				  queryString := rest copyFrom: q + 1 to: rest size].
	^authority
]

{ #category : 'accessing' }
HttpRequest >> method [
	"GET, POST, ... upper-cased."
	^method
]

{ #category : 'accessing' }
HttpRequest >> target [
	"The request target as sent, still encoded: the path and the query,
	 or for the absolute form the whole URI they were taken from."
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
	 unless it says close, HTTP/1.0 only if it says keep-alive -- and in
	 either, close wins over keep-alive, as RFC 7230 section 6.3 orders
	 them.  The header is a list of options, read by its comma-separated
	 items: repeated lines are one list (addHeader:value:), and an HTTP/1.0
	 `keep-alive, close' is a close (Bugs07 NET-12)."
	| connection options |
	connection := self headerAt: 'connection'.
	options := connection isNil
		ifTrue: [#()]
		ifFalse: [(connection asLowercase substrings: ',') collect: [:each | each trimmed]].
	(options includes: 'close') ifTrue: [^false].
	version = 'HTTP/1.0' ifTrue: [^options includes: 'keep-alive'].
	^true
]

{ #category : 'origins' }
HttpRequest >> origin [
	"The Origin header, which a browser sends with a request from a page
	 on another origin; nil otherwise."
	^self headerAt: 'origin'
]

{ #category : 'origins' }
HttpRequest >> hostName [
	"The host of the Host header, without its port, lowercased; nil when
	 there is no Host."
	^HttpCodec hostOf: (self headerAt: 'host')
]

{ #category : 'origins' }
HttpRequest >> isPreflight [
	"Whether this is the OPTIONS a browser sends before a cross-origin
	 request to ask what it may send: an Origin, and the method it means
	 to use."
	^method = 'OPTIONS'
		and: [self origin notNil
		and: [(self headerAt: 'access-control-request-method') notNil]]
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
	"The first part of that name, or nil.

	 BY A DICTIONARY, built once, and not by a walk along the parts each
	 time.  The walk was linear, and RestRequest>>uploadFileCount asks for
	 _file-0, _file-1 ... until one is missing, so n uploads cost n
	 squared over two comparisons: 1,000 files took 6.5 seconds of a
	 worker and 4,000 -- a 299 KB request, well inside every limit --
	 took 81 (Bugs5 NET-15).  The first part of a name wins, as detect:
	 made it win.  One request is one worker's, so the lazy fill needs no
	 lock, as parts' does not."
	partsByName isNil ifTrue: [ | index |
		index := Dictionary new.
		self parts do: [:p |
			(p name notNil and: [(index includesKey: p name) not])
				ifTrue: [index at: p name put: p]].
		partsByName := index].
	^partsByName at: aString ifAbsent: [nil]
]

{ #category : 'private - multipart' }
HttpRequest >> parseParts [
	"multipart/form-data, RFC 7578: `--boundary' CRLF headers CRLF CRLF
	 body CRLF ... `--boundary--'.  The body of a part ends at the CRLF
	 before the next boundary line.

	 THE BOUNDARY IS 1 TO 70 CHARACTERS, which is what RFC 2046 section
	 5.1.1 allows, and a longer one is 400 before a byte of the body has
	 been looked at.  Both the boundary and the body are the stranger's --
	 the boundary bounded only by the 8,192-byte header line, the body only
	 by maxRequestBytes, 64 MB unless the server says otherwise -- and this
	 runs BEFORE ANYTHING IS AUTHENTICATED.  It has to: the session token
	 is a field of the very form it is taking apart, the reason
	 RestDispatcher>>numberFrom:work: gives for its own cap.  So the cost of
	 this scan is the cost of a request from anyone, and it was the body
	 times the boundary: indexOfSubstring:startingAt: compared the whole
	 pattern at every position, and a 300 KB body with a 2,000-character
	 boundary, both well inside the limits, held a worker past the
	 sixty-second mark with no reply, while maxConnections of them would
	 hold every worker (Bugs6 NET-1).  The search is in C now and stops at
	 the first mismatch; the cap on the boundary bounds what one mismatch
	 can cost on top of that, so the parse is linear in the body with a
	 small constant.  An empty boundary is refused with the long ones --
	 `--' alone would match inside any body and means nothing.

	 AND AT MOST 10,000 PARTS, for the same reason: with the search fixed
	 a megabyte of one-byte fields -- 19,000 parts -- still cost five and
	 a half seconds, nearly all of it in the headers each part is given,
	 and a 64 MB body of them would be six minutes of a worker, which is
	 the finding again by another road.  No form has ten thousand fields;
	 Bugs5 NET-15's four thousand files were an audit's, and they pass.
	 The scan stops where the cap is passed, so the work is bounded there
	 too."
	| boundary delimiter out position |
	self isMultipart ifFalse: [^#()].
	boundary := self contentTypeParameterAt: 'boundary'.
	boundary isNil ifTrue: [HttpError status: 400 reason: 'a multipart body names no boundary'].
	(boundary isEmpty or: [boundary size > 70]) ifTrue: [
		HttpError status: 400
			reason: 'a multipart boundary is 1 to 70 characters, not ', boundary size printString].
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
	 out size >= 10000 ifTrue: [HttpError status: 400 reason: 'a multipart body has more than 10000 parts'].
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
