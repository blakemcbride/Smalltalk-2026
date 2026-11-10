"
One connection, from accept to close: the body of the process the server
forks for it.

Reads a request, asks the server for a response, writes it, and goes round
again while the client keeps the connection open -- waiting for the next
request no longer than the server's keep-alive time.  Every way a request can
fail ends in a response or a clean close, never in an error escaping the
process: an HttpError is the response it names; any other Error inside a
handler is a 500, with the fault written to standard error where somebody
can read it and nothing about it in the body where a stranger could; a peer
that vanishes mid-request, or a socket that fails, is a close.

This process is what `one request on one core' means.  It runs on whichever
worker was idle when it was forked, and while it waits for bytes it costs
that worker nothing.
"
Class {
	#name : 'HttpConnection',
	#superclass : 'Object',
	#instVars : [ 'socket', 'stream', 'server', 'served', 'busy' ],
	#category : 'JSON-RPC-Server'
}

{ #category : 'instance creation' }
HttpConnection class >> on: aSocket server: aServer [
	^self new setSocket: aSocket server: aServer
]

{ #category : 'private' }
HttpConnection >> setSocket: aSocket server: aServer [
	socket := aSocket.
	server := aServer.
	stream := SocketStream on: aSocket.
	served := 0.
	busy := false
]

{ #category : 'accessing' }
HttpConnection >> served [
	"How many requests this connection has answered."
	^served
]

{ #category : 'running' }
HttpConnection >> run [
	"Serve requests until the client stops sending them or something fails.

	 The close-out is an ensure:, so that a connection whose process is
	 ended some other way still gives back its place in the server's two
	 counts: one left behind in the requests in flight would hold a
	 stopping server for its whole grace (Bugs07 NET-5)."
	[[[[self serveOne] whileTrue: []]
		on: TimedOut
		do: [:e | nil]]			"the keep-alive wait ran out: the ordinary end of a connection"
		on: Error
		do: [:e |
			"A peer that went away is the other ordinary end; anything else
			 is worth a line."
			e class == NetError
				ifFalse: [server log: 'connection: ', e class name, ': ', e messageText]]]
		ensure: [
			[stream close] on: Error do: [:e | nil].
			self beIdle.
			server connectionClosed: self]
]

{ #category : 'private' }
HttpConnection >> beBusy [
	"The first byte of a request is here: it is in flight until this
	 connection is idle again or closed."
	busy ifFalse: [busy := true. server requestBegun]
]

{ #category : 'private' }
HttpConnection >> beIdle [
	busy ifTrue: [busy := false. server requestEnded]
]

{ #category : 'running' }
HttpConnection >> serveOne [
	"Read one request and answer it.  Answer whether the connection is to
	 carry another."
	| request response keepAlive |
	"Waiting for the next request is bounded by the keep-alive time, one
	 read at a time.  Reading it, once it has begun, is bounded as a WHOLE,
	 or a client sending a byte every few seconds is never done (Bugs5
	 NET-1); and writing the reply is bounded by how long the client may
	 go without taking any of it."
	stream readTimeout: server keepAliveMilliseconds.
	stream deadlineIn: nil.
	stream writeTimeout: server writeMilliseconds.
	stream atEnd ifTrue: [^false].
	self beBusy.
	stream deadlineIn: server requestMilliseconds.
	request := [HttpRequest readFrom: stream maxBodyBytes: server maxRequestBytes]
		on: HttpError
		do: [:e |
			self write: (HttpResponse error: e status reason: e messageText) keepAlive: false.
			^false].
	request isNil ifTrue: [^false].
	stream deadlineIn: nil.
	request remoteAddress: ([socket peerAddress] on: NetError do: [:e | nil]).
	"A server that has stopped answers this request and no more: the reply
	 says Connection: close and the connection ends after it, so a stopping
	 server's requests in flight fall to none instead of being renewed by a
	 kept-alive client for as long as it keeps sending (Bugs07 NET-5)."
	keepAlive := request isKeepAlive and: [server isRunning].
	response := [server handle: request]
		on: HttpError
		do: [:e | HttpResponse error: e status reason: e messageText].
	"A HEAD is a GET without the bytes, and the response for one is built
	 exactly as the GET's is and then sent without its body -- so the
	 Content-Length it carries is the length the GET would have.  This is
	 the one place that knows the method and the response at once, which is
	 why it is done here rather than in each handler; the static handler
	 used to answer an empty body for a HEAD and the reply then said
	 `Content-Length: 0', which is a lie to the one client that asks a HEAD
	 to find out how big something is (Bugs3 B57, lower)."
	request method = 'HEAD' ifTrue: [response omitBody].
	served := served + 1.
	self write: response keepAlive: keepAlive.
	"Answered and kept alive: idle, until the next request's first byte.
	 Answered and ending: in flight until run has closed the socket."
	keepAlive ifTrue: [self beIdle].
	^keepAlive
]

{ #category : 'private' }
HttpConnection >> write: aResponse keepAlive: aBoolean [
	server standingHeadersDo: [:name :value |
		(aResponse headerAt: name) isNil ifTrue: [aResponse headerAt: name put: value]].
	aResponse writeOn: stream keepAlive: aBoolean.
	stream flush
]
