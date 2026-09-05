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
	^self decode: aString plusIsSpace: true
]

{ #category : 'percent encoding' }
HttpCodec class >> percentDecodePath: aString [
	"As percentDecode:, but `+' stays a plus: it is only a space in a
	 form, and this is a path."
	^self decode: aString plusIsSpace: false
]

{ #category : 'private' }
HttpCodec class >> decode: aString plusIsSpace: aBoolean [
	"The one decoder both of the above are, so that a change to what an
	 escape means cannot be made in one of them and forgotten in the other.

	 A MALFORMED ESCAPE IS REFUSED, with 400, rather than passed through as
	 the characters that spell it.  Passing it through was the older
	 behaviour and it is indefensible for the same reason `01' is not JSON:
	 `%zz' means nothing, every server in the world decides for itself what
	 to do with it, and a proxy that decides differently from this one is
	 how a path gets past a filter here and arrives somewhere else as a
	 different path.  Refusing is the one answer no two parties can disagree
	 about.  A `%' at the end of the string, and a `%' with one character
	 after it, are the same fault and are refused the same way.

	 The refusal names the position and not the characters: this string
	 came off the wire and the message goes back on it, and there is no
	 reason to reflect a stranger's bytes even into a plain-text body."
	| out i c |
	out := WriteStream on: (String new: aString size).
	i := 1.
	[i <= aString size] whileTrue: [
		c := aString at: i.
		c = $%
			ifTrue: [ | value |
				i + 2 <= aString size ifFalse: [
					HttpError status: 400
						reason: 'a percent escape at character ', i printString,
							' has fewer than two digits after it'].
				value := self hexValueOf: (aString copyFrom: i + 1 to: i + 2).
				value isNil ifTrue: [
					HttpError status: 400
						reason: 'the percent escape at character ', i printString,
							' is not two hexadecimal digits'].
				out nextPut: (Character value: value).
				i := i + 3]
			ifFalse: [
				out nextPut: ((aBoolean and: [c = $+]) ifTrue: [$ ] ifFalse: [c]).
				i := i + 1]].
	^out contents
]

{ #category : 'private' }
HttpCodec class >> hexValueOf: twoChars [
	"The value of two hexadecimal digits, or nil when they are not two.

	 asUppercase BEFORE digitValue, and Bugs3 B52 is what happens without
	 it: 1983's Character>>digitValue answers the value only for $0 to $9
	 and $A to $Z, and -1 for every lowercase letter.  The older code here
	 guarded acceptance with `c asUppercase between: $A and: $F' and then
	 took the value from the character as written, so every lowercase
	 escape decoded to something else -- `%2f' answered 31 rather than 47,
	 so an ordinary lowercase URL 404'd -- and `%0a', whose first digit is
	 a $0 and whose second is a lowercase letter, computed 0 * 16 + -1 and
	 handed Character value: -1 a subscript out of bounds.  That is not an
	 HttpError, so it escaped the request parser, and the connection was
	 dropped with no response at all.  The JSON parser's nextHex4
	 documents the same trap and uppercases first.

	 digitValue answers 10 to 35 for $A to $Z, so the range test is on the
	 VALUE and not on the character: anything above 15 is a letter past $F
	 and anything below zero is not a digit at all."
	| value |
	value := 0.
	twoChars do: [:c | | d |
		d := c asUppercase digitValue.
		(d < 0 or: [d > 15]) ifTrue: [^nil].
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
	"Now, as RFC 7231 writes it: `Sun, 06 Nov 1994 08:49:37 GMT'.

	 In GMT, because the string says GMT and RFC 7231 allows nothing else.
	 This asked `Date today' and `Time now', which are corrected for the
	 host's time zone, and stamped the answer GMT -- so a server in
	 Chicago sent every response five hours in the past under a label
	 saying it had not.  (Before Bugs4 CHRON-3 those two answered
	 California whatever the host, so the header was wrong by a different
	 constant and not by the host's.)  Time utcSeconds is the same instant
	 with nothing added to it."
	| utc |
	utc := Time utcSeconds.
	^self
		httpDateFor: (Date fromDays: utc // 86400)
		at: (Time fromSeconds: utc \\ 86400)
]

{ #category : 'dates' }
HttpCodec class >> httpDateFor: aDate at: aTime [
	| out |
	out := WriteStream on: (String new: 32).
	"The day's own NAME, cut to three letters, and not `Days at: aDate
	 weekdayIndex'.  weekdayIndex counts from Monday in 1983 and from
	 Sunday in Pharo's Date, which supersedes it in profiles/pharo-time --
	 so the same table named the right day in one profile and the day
	 before in the other, and every Date header this server sent from a
	 pharo-time build was one day out in its weekday and right in its
	 date.  weekday answers `Friday' in both."
	out nextPutAll: (aDate weekday copyFrom: 1 to: 3); nextPutAll: ', '.
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
