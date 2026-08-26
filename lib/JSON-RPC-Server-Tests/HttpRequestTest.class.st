"
The request parser, on text: no socket, every refusal, and a multipart body
taken apart.

Over a third of these are requests the parser must refuse -- a body with no
declared length, a body over the limit, a header line that never ends -- for
the reason lib/Json-Tests gives: a refusal is invisible to every test of a
good request, and a reader that quietly accepted all of them would pass the
rest of this suite.
"
Class {
	#name : 'HttpRequestTest',
	#superclass : 'TestCase',
	#category : 'JSON-RPC-Server-Tests'
}

{ #category : 'private' }
HttpRequestTest >> crlf [
	^String with: (Character value: 13) with: (Character value: 10)
]

{ #category : 'private' }
HttpRequestTest >> request: lines body: aBody [
	"Lines joined with CRLF, a blank line, and the body."
	| out |
	out := WriteStream on: (String new: 256).
	lines do: [:l | out nextPutAll: l; nextPutAll: self crlf].
	out nextPutAll: self crlf.
	out nextPutAll: aBody.
	^out contents
]

{ #category : 'tests' }
HttpRequestTest >> testAGet [
	| r |
	r := HttpRequest fromString: (self request: #('GET /index.html?a=1&b=two+words HTTP/1.1' 'Host: here' 'User-Agent: test') body: '').
	self assert: r method equals: 'GET'.
	self assert: r path equals: '/index.html'.
	self assert: r queryString equals: 'a=1&b=two+words'.
	self assert: r version equals: 'HTTP/1.1'.
	self assert: (r headerAt: 'host') equals: 'here'.
	self assert: (r headerAt: 'HOST') equals: 'here'.
	self assert: (r headerAt: 'User-Agent') equals: 'test'.
	self assert: (r headerAt: 'nothing') isNil.
	self assert: r body equals: ''.
	self assert: (r parameterAt: 'a') equals: '1'.
	self assert: (r parameterAt: 'b') equals: 'two words'.
	self assert: r isKeepAlive
]

{ #category : 'tests' }
HttpRequestTest >> testAPost [
	| r |
	r := HttpRequest fromString: (self request: #('POST /rest HTTP/1.1' 'Content-Type: application/json' 'Content-Length: 13') body: '{"num1": 3}  ').
	self assert: r method equals: 'POST'.
	self assert: r isJson.
	self assert: r contentType equals: 'application/json'.
	self assert: r body equals: '{"num1": 3}  '.
	self deny: r isMultipart
]

{ #category : 'tests' }
HttpRequestTest >> testAPercentEncodedPath [
	| r |
	r := HttpRequest fromString: (self request: #('GET /a%20b/c+d.html HTTP/1.1') body: '').
	self assert: r path equals: '/a b/c+d.html'
]

{ #category : 'tests' }
HttpRequestTest >> testKeepAlive [
	self assert: (HttpRequest fromString: (self request: #('GET / HTTP/1.1') body: '')) isKeepAlive.
	self deny: (HttpRequest fromString: (self request: #('GET / HTTP/1.1' 'Connection: close') body: '')) isKeepAlive.
	self deny: (HttpRequest fromString: (self request: #('GET / HTTP/1.0') body: '')) isKeepAlive.
	self assert: (HttpRequest fromString: (self request: #('GET / HTTP/1.0' 'Connection: Keep-Alive') body: '')) isKeepAlive
]

{ #category : 'tests' }
HttpRequestTest >> testEndOfStreamBeforeARequestIsNil [
	self assert: (HttpRequest fromString: '') isNil.
	self assert: (HttpRequest fromString: self crlf) isNil
]

{ #category : 'tests' }
HttpRequestTest >> testRefusals [
	self should: [HttpRequest fromString: (self request: #('not a request line') body: '')] raise: HttpError.
	self should: [HttpRequest fromString: (self request: #('GET / FTP/9') body: '')] raise: HttpError.
	self should: [HttpRequest fromString: (self request: #('GET / HTTP/1.1' 'no colon here') body: '')] raise: HttpError.
	self should: [HttpRequest fromString: (self request: #('POST / HTTP/1.1' 'Transfer-Encoding: chunked') body: '5', self crlf, 'hello')] raise: HttpError.
	self should: [HttpRequest fromString: (self request: #('POST / HTTP/1.1' 'Content-Length: abc') body: '')] raise: HttpError.
	self should: [HttpRequest fromString: (self request: #('POST / HTTP/1.1' 'Content-Length: 100') body: 'short')] raise: HttpError.
	self should: [HttpRequest fromString: 'GET / HTTP/1.1', self crlf, 'Host: here'] raise: HttpError
]

{ #category : 'tests' }
HttpRequestTest >> testRefusalsCarryTheirStatus [
	[HttpRequest fromString: (self request: #('POST / HTTP/1.1' 'Transfer-Encoding: chunked') body: '').
	 self assert: false]
		on: HttpError do: [:e | self assert: e status equals: 411].
	[HttpRequest fromString: (self request: #('POST / HTTP/1.1' 'Content-Length: 2000') body: (String new: 2000 withAll: $x)) maxBodyBytes: 1000.
	 self assert: false]
		on: HttpError do: [:e | self assert: e status equals: 413].
	[HttpRequest fromString: (self request: #('nope') body: '').
	 self assert: false]
		on: HttpError do: [:e | self assert: e status equals: 400]
]

{ #category : 'tests' }
HttpRequestTest >> testABodyAtTheLimitIsAccepted [
	| r |
	r := HttpRequest fromString: (self request: #('POST / HTTP/1.1' 'Content-Length: 1000') body: (String new: 1000 withAll: $x)) maxBodyBytes: 1000.
	self assert: r body size equals: 1000
]

{ #category : 'tests' }
HttpRequestTest >> testTooManyHeadersAreRefused [
	| lines |
	lines := OrderedCollection with: 'GET / HTTP/1.1'.
	1 to: 101 do: [:i | lines add: 'X-H', i printString, ': v'].
	self should: [HttpRequest fromString: (self request: lines body: '')] raise: HttpError
]

{ #category : 'tests' }
HttpRequestTest >> testALineTooLongIsRefused [
	self should: [HttpRequest fromString: (self request: (Array with: 'GET /', (String new: 9000 withAll: $a), ' HTTP/1.1') body: '')] raise: HttpError
]

{ #category : 'private' }
HttpRequestTest >> multipartBody [
	"Two files and three fields, as a browser sends them, with the
	 'S'-prefixed scalars Kiss's Server.js uses."
	| b crlf |
	crlf := self crlf.
	b := '----WebKitFormBoundaryTEST'.
	^'--', b, crlf, 'Content-Disposition: form-data; name="_class"', crlf, crlf, 'services.FileUpload', crlf,
	 '--', b, crlf, 'Content-Disposition: form-data; name="_method"', crlf, crlf, 'upload', crlf,
	 '--', b, crlf, 'Content-Disposition: form-data; name="note"', crlf, crlf, 'Sa note', crlf,
	 '--', b, crlf, 'Content-Disposition: form-data; name="_file-0"; filename="one.txt"', crlf, 'Content-Type: text/plain', crlf, crlf, 'first file', crlf, 'two lines', crlf,
	 '--', b, crlf, 'Content-Disposition: form-data; name="_file-1"; filename="two.bin"', crlf, 'Content-Type: application/octet-stream', crlf, crlf, (String with: (Character value: 0) with: (Character value: 255) with: $]), crlf,
	 '--', b, '--', crlf
]

{ #category : 'tests' }
HttpRequestTest >> testAMultipartBody [
	| body r parts |
	body := self multipartBody.
	r := HttpRequest fromString: (self request: (Array with: 'POST /rest HTTP/1.1'
		with: 'Content-Type: multipart/form-data; boundary=----WebKitFormBoundaryTEST'
		with: 'Content-Length: ', body size printString) body: body).
	self assert: r isMultipart.
	self assert: (r contentTypeParameterAt: 'boundary') equals: '----WebKitFormBoundaryTEST'.
	parts := r parts.
	self assert: parts size equals: 5.
	self assert: (parts at: 1) name equals: '_class'.
	self assert: (parts at: 1) body equals: 'services.FileUpload'.
	self deny: (parts at: 1) isFile.
	self assert: (parts at: 4) name equals: '_file-0'.
	self assert: (parts at: 4) fileName equals: 'one.txt'.
	self assert: (parts at: 4) contentType equals: 'text/plain'.
	self assert: (parts at: 4) body equals: 'first file', self crlf, 'two lines'.
	self assert: (parts at: 5) fileName equals: 'two.bin'.
	self assert: (parts at: 5) body size equals: 3.
	self assert: ((parts at: 5) body at: 2) asciiValue equals: 255.
	self assert: (r formParameters at: 'note') equals: 'Sa note'.
	self assert: (r formParameters includesKey: '_file-0') not.
	self assert: (r partNamed: '_method') body equals: 'upload'.
	self assert: (r partNamed: 'missing') isNil
]

{ #category : 'tests' }
HttpRequestTest >> testAMultipartBodyWithoutABoundaryIsRefused [
	| r |
	r := HttpRequest fromString: (self request: #('POST /rest HTTP/1.1' 'Content-Type: multipart/form-data' 'Content-Length: 4') body: 'abcd').
	self should: [r parts] raise: HttpError
]

{ #category : 'tests' }
HttpRequestTest >> testCodec [
	self assert: (HttpCodec percentDecode: 'a%20b+c%2Fd') equals: 'a b c/d'.
	self assert: (HttpCodec percentDecode: '100%') equals: '100%'.
	self assert: (HttpCodec percentDecodePath: 'a+b%41') equals: 'a+bA'.
	self assert: ((HttpCodec parseQuery: 'x=1&y=%7B%7D&flag') at: 'y') equals: '{}'.
	self assert: ((HttpCodec parseQuery: 'x=1&y=%7B%7D&flag') at: 'flag') equals: ''.
	self assert: (HttpCodec mimeTypeForName: 'kiss/Server.js') equals: 'application/javascript'.
	self assert: (HttpCodec mimeTypeForName: 'a.PNG') equals: 'image/png'.
	self assert: (HttpCodec mimeTypeForName: 'noext') equals: 'application/octet-stream'.
	self assert: (HttpCodec reasonFor: 404) equals: 'Not Found'.
	self assert: HttpCodec httpDate size equals: 29.
	self assert: (HttpCodec httpDate endsWith: ' GMT')
]
