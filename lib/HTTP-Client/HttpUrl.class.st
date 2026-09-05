"
The parts of an http URL a client needs: the scheme, the host, the port,
and the rest -- path and query as one string, which is what goes on the
request line.

    (HttpUrl fromString: 'http://localhost:11434/api/tags') host    'localhost'
    ... port    11434
    ... path    '/api/tags'
    ... hostHeader    'localhost:11434'

No more than that: no user and password, no fragment, no percent-decoding
-- the path is sent as it was given, which is what a caller who wrote it
wants.  A URL with no `://', or no host, is refused with a NetError, and so
is one holding a space or a control character: see setFromString:.
isSecure says whether the scheme is https, which the client answers with
TLS when the build has it and refuses by name when it does not.
"
Class {
	#name : 'HttpUrl',
	#superclass : 'Object',
	#instVars : [ 'scheme', 'host', 'port', 'path' ],
	#category : 'HTTP-Client'
}

{ #category : 'instance creation' }
HttpUrl class >> fromString: aString [
	^self new setFromString: aString
]

{ #category : 'private' }
HttpUrl >> setFromString: aString [
	"A URL holds no control character and no space, and this is where that
	 is settled rather than at each of the three places a part of it goes on
	 the wire.  The request line is `METHOD target HTTP/1.1' and the Host
	 header is the authority: a CR or an LF anywhere in the text writes
	 headers of the caller's stranger's choosing, and a space in the target
	 writes a second request line (Bugs4 NET-3).  A URL that really wants
	 one of those characters spells it percent-escaped, which is what the
	 escapes are for; nothing here decodes, so an escape stays an escape all
	 the way to the wire."
	| i rest slash hostPort colon close portText |
	(aString anySatisfy: [:c | c asInteger <= 32 or: [c asInteger = 127]])
		ifTrue: [^NetError new signal:
			'a URL with a space or a control character in it: ', aString printString].
	i := 0.
	1 to: aString size - 2 do: [:k |
		(i = 0 and: [(aString copyFrom: k to: k + 2) = '://']) ifTrue: [i := k]].
	i = 0 ifTrue: [^NetError new signal: 'not a URL: ', aString printString].
	scheme := (aString copyFrom: 1 to: i - 1) asLowercase.
	rest := aString copyFrom: i + 3 to: aString size.
	slash := rest indexOf: $/.
	hostPort := slash = 0 ifTrue: [rest] ifFalse: [rest copyFrom: 1 to: slash - 1].
	path := slash = 0 ifTrue: ['/'] ifFalse: [rest copyFrom: slash to: rest size].
	portText := nil.
	(hostPort notEmpty and: [hostPort first = $[])
		ifTrue: [
			"an IPv6 literal: the host is the brackets and what is in them"
			close := hostPort indexOf: $].
			close = 0 ifTrue: [^NetError new signal: 'not a URL: ', aString printString].
			host := hostPort copyFrom: 1 to: close.
			close < hostPort size ifTrue: [
				(hostPort at: close + 1) = $: ifFalse: [^NetError new signal: 'not a URL: ', aString printString].
				portText := hostPort copyFrom: close + 2 to: hostPort size]]
		ifFalse: [
			colon := hostPort indexOf: $:.
			colon = 0
				ifTrue: [host := hostPort]
				ifFalse: [
					host := hostPort copyFrom: 1 to: colon - 1.
					portText := hostPort copyFrom: colon + 1 to: hostPort size]].
	host isEmpty ifTrue: [^NetError new signal: 'a URL with no host: ', aString printString].
	portText isNil
		ifTrue: [port := scheme = 'https' ifTrue: [443] ifFalse: [80]]
		ifFalse: [
			port := self portFrom: portText.
			port isNil ifTrue: [^NetError new signal: 'not a port: ', aString printString]]
]

{ #category : 'private' }
HttpUrl >> portFrom: aString [
	"The port text as an Integer, or nil for text that is not all digits:
	 asNumber answers 0 for `notaport', which is a port nobody meant."
	(aString isEmpty or: [(aString allSatisfy: [:c | c isDigit]) not]) ifTrue: [^nil].
	^aString asNumber
]

{ #category : 'accessing' }
HttpUrl >> scheme [
	^scheme
]

{ #category : 'accessing' }
HttpUrl >> host [
	^host
]

{ #category : 'accessing' }
HttpUrl >> port [
	^port
]

{ #category : 'accessing' }
HttpUrl >> path [
	"The request target: the path and the query, as given; '/' when none."
	^path
]

{ #category : 'accessing' }
HttpUrl >> hostHeader [
	"What the Host header carries: the port only when it is not the
	 scheme's own."
	^(port = (scheme = 'https' ifTrue: [443] ifFalse: [80]))
		ifTrue: [host]
		ifFalse: [host, ':', port printString]
]

{ #category : 'testing' }
HttpUrl >> isSecure [
	^scheme = 'https'
]

{ #category : 'printing' }
HttpUrl >> printOn: aStream [
	aStream nextPutAll: scheme; nextPutAll: '://'; nextPutAll: self hostHeader; nextPutAll: path
]
