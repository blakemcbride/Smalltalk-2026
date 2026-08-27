"
The small encodings HTTP is made of: percent-encoding, query strings, MIME
types by file extension, and the date format.

Class-side and stateless, on purpose: every request on every worker uses
these at once, and a method with no state to share is one with nothing to
lock.  The MIME table is a class variable filled in by the class initializer,
eagerly, and read only.
"
Class {
	#name : 'HttpCodec',
	#superclass : 'Object',
	#classVars : [ 'MimeTypes', 'Days', 'Months' ],
	#category : 'JSON-RPC-Server'
}

{ #category : 'class initialization' }
HttpCodec class >> initialize [
	"HttpCodec initialize"
	| table |
	MimeTypes := Dictionary new.
	table := #('html' 'text/html' 'htm' 'text/html' 'css' 'text/css' 'js' 'application/javascript'
	  'mjs' 'application/javascript' 'json' 'application/json' 'txt' 'text/plain'
	  'xml' 'application/xml' 'csv' 'text/csv' 'png' 'image/png' 'jpg' 'image/jpeg'
	  'jpeg' 'image/jpeg' 'gif' 'image/gif' 'svg' 'image/svg+xml' 'ico' 'image/x-icon'
	  'webp' 'image/webp' 'pdf' 'application/pdf' 'woff' 'font/woff' 'woff2' 'font/woff2'
	  'ttf' 'font/ttf' 'map' 'application/json' 'wasm' 'application/wasm'
	  'zip' 'application/zip' 'mp3' 'audio/mpeg' 'mp4' 'video/mp4').
	1 to: table size by: 2 do: [:i | MimeTypes at: (table at: i) put: (table at: i + 1)].
	Days := #('Mon' 'Tue' 'Wed' 'Thu' 'Fri' 'Sat' 'Sun').
	Months := #('Jan' 'Feb' 'Mar' 'Apr' 'May' 'Jun' 'Jul' 'Aug' 'Sep' 'Oct' 'Nov' 'Dec')
]

{ #category : 'mime types' }
HttpCodec class >> mimeTypeForName: aFileName [
	"The content type for a file name, by its extension; octet-stream when
	 the extension is not one this knows."
	| dot ext |
	dot := 0.
	1 to: aFileName size do: [:i | (aFileName at: i) = $. ifTrue: [dot := i]].
	dot = 0 ifTrue: [^'application/octet-stream'].
	ext := (aFileName copyFrom: dot + 1 to: aFileName size) asLowercase.
	^MimeTypes at: ext ifAbsent: ['application/octet-stream']
]

{ #category : 'percent encoding' }
HttpCodec class >> percentDecode: aString [
	"`%41' is A and `+' is a space, as in a query string or a form."
	| out i c |
	out := WriteStream on: (String new: aString size).
	i := 1.
	[i <= aString size] whileTrue: [
		c := aString at: i.
		(c = $% and: [i + 2 <= aString size])
			ifTrue: [ | value |
				value := self hexValueOf: (aString copyFrom: i + 1 to: i + 2).
				value isNil
					ifTrue: [out nextPut: c. i := i + 1]
					ifFalse: [out nextPut: (Character value: value). i := i + 3]]
			ifFalse: [
				out nextPut: (c = $+ ifTrue: [$ ] ifFalse: [c]).
				i := i + 1]].
	^out contents
]

{ #category : 'percent encoding' }
HttpCodec class >> percentDecodePath: aString [
	"As percentDecode:, but `+' stays a plus: it is only a space in a
	 form, and this is a path."
	| out i c |
	out := WriteStream on: (String new: aString size).
	i := 1.
	[i <= aString size] whileTrue: [
		c := aString at: i.
		(c = $% and: [i + 2 <= aString size])
			ifTrue: [ | value |
				value := self hexValueOf: (aString copyFrom: i + 1 to: i + 2).
				value isNil
					ifTrue: [out nextPut: c. i := i + 1]
					ifFalse: [out nextPut: (Character value: value). i := i + 3]]
			ifFalse: [out nextPut: c. i := i + 1]].
	^out contents
]

{ #category : 'private' }
HttpCodec class >> hexValueOf: twoChars [
	| value |
	value := 0.
	twoChars do: [:c | | d |
		d := c digitValue.
		(c isDigit or: [(c asUppercase between: $A and: $F)]) ifFalse: [^nil].
		value := value * 16 + d].
	^value
]

{ #category : 'query strings' }
HttpCodec class >> parseQuery: aString [
	"`a=1&b=two+words' as a Dictionary of decoded names and values.  A name
	 without `=' has the value empty; a repeated name keeps the last."
	| dict |
	dict := Dictionary new.
	aString isEmpty ifTrue: [^dict].
	(aString substrings: '&') do: [:pair | | eq |
		eq := pair indexOf: $=.
		eq = 0
			ifTrue: [dict at: (self percentDecode: pair) put: '']
			ifFalse: [dict at: (self percentDecode: (pair copyFrom: 1 to: eq - 1))
					put: (self percentDecode: (pair copyFrom: eq + 1 to: pair size))]].
	^dict
]

{ #category : 'dates' }
HttpCodec class >> httpDate [
	"Now, as RFC 7231 writes it: `Sun, 06 Nov 1994 08:49:37 GMT'."
	^self httpDateFor: Date today at: Time now
]

{ #category : 'dates' }
HttpCodec class >> httpDateFor: aDate at: aTime [
	| out |
	out := WriteStream on: (String new: 32).
	out nextPutAll: (Days at: aDate weekdayIndex); nextPutAll: ', '.
	out nextPutAll: (aDate dayOfMonth printString padLeftTo: 2 with: $0); space.
	out nextPutAll: (Months at: aDate monthIndex); space.
	out nextPutAll: aDate year printString; space.
	out nextPutAll: (aTime hours printString padLeftTo: 2 with: $0); nextPut: $:.
	out nextPutAll: (aTime minutes printString padLeftTo: 2 with: $0); nextPut: $:.
	out nextPutAll: (aTime seconds printString padLeftTo: 2 with: $0).
	out nextPutAll: ' GMT'.
	^out contents
]

{ #category : 'reasons' }
HttpCodec class >> reasonFor: status [
	status = 200 ifTrue: [^'OK'].
	status = 201 ifTrue: [^'Created'].
	status = 204 ifTrue: [^'No Content'].
	status = 301 ifTrue: [^'Moved Permanently'].
	status = 302 ifTrue: [^'Found'].
	status = 304 ifTrue: [^'Not Modified'].
	status = 400 ifTrue: [^'Bad Request'].
	status = 401 ifTrue: [^'Unauthorized'].
	status = 403 ifTrue: [^'Forbidden'].
	status = 404 ifTrue: [^'Not Found'].
	status = 405 ifTrue: [^'Method Not Allowed'].
	status = 408 ifTrue: [^'Request Timeout'].
	status = 411 ifTrue: [^'Length Required'].
	status = 413 ifTrue: [^'Payload Too Large'].
	status = 414 ifTrue: [^'URI Too Long'].
	status = 415 ifTrue: [^'Unsupported Media Type'].
	status = 431 ifTrue: [^'Request Header Fields Too Large'].
	status = 500 ifTrue: [^'Internal Server Error'].
	status = 501 ifTrue: [^'Not Implemented'].
	status = 503 ifTrue: [^'Service Unavailable'].
	^'Unknown'
]

{ #category : 'origins' }
HttpCodec class >> hostOf: aString [
	"The host an Origin or Host header names, lowercased: what is left of
	 `http://Example.com:8000/x' and of `example.com:8001' once the
	 scheme, the port and any path are gone.  This is the whole of the
	 server's rule for another origin (HttpServer, ORIGINS): the two
	 headers' hosts, compared.

	 An IPv6 literal keeps its brackets, so `[::1]' is one host and `::1'
	 is not mistaken for a name with ports in it.  A value with no host --
	 `null', which a sandboxed page sends -- answers itself, and compares
	 equal to no real host, which is the answer wanted.  Nil for nil."
	| start end i host colon |
	aString isNil ifTrue: [^nil].
	start := 1.
	i := 1.
	[i + 2 <= aString size and: [start = 1]] whileTrue: [
		(aString copyFrom: i to: i + 2) = '://' ifTrue: [start := i + 3].
		i := i + 1].
	end := aString size.
	i := start.
	[i <= aString size and: [i <= end]] whileTrue: [
		('/?#' includes: (aString at: i)) ifTrue: [end := i - 1].
		i := i + 1].
	start > end ifTrue: [^''].
	host := aString copyFrom: start to: end.
	host first = $[
		ifTrue: [
			colon := host indexOf: $].
			colon > 0 ifTrue: [host := host copyFrom: 1 to: colon]]
		ifFalse: [
			colon := 0.
			1 to: host size do: [:k | (host at: k) = $: ifTrue: [colon := k]].
			(colon > 0 and: [(host copyFrom: colon + 1 to: host size) allSatisfy: [:c | c isDigit]])
				ifTrue: [host := host copyFrom: 1 to: colon - 1]].
	^host asLowercase
]
