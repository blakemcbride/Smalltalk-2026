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
	#instVars : [ 'socket', 'stream', 'server', 'served' ],
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
	served := 0
]

{ #category : 'accessing' }
HttpConnection >> served [
	"How many requests this connection has answered."
	^served
]

{ #category : 'running' }
HttpConnection >> run [
	"Serve requests until the client stops sending them or something fails."
	[[[self serveOne] whileTrue: []]
		on: TimedOut
		do: [:e | nil]]			"the keep-alive wait ran out: the ordinary end of a connection"
		on: Error
		do: [:e |
			"A peer that went away is the other ordinary end; anything else
			 is worth a line."
			e class == NetError
				ifFalse: [server log: 'connection: ', e class name, ': ', e messageText]].
	[stream close] on: Error do: [:e | nil].
	server connectionClosed: self
]

{ #category : 'running' }
HttpConnection >> serveOne [
	"Read one request and answer it.  Answer whether the connection is to
	 carry another."
	| request response keepAlive |
	stream readTimeout: server keepAliveMilliseconds.
	request := [HttpRequest readFrom: stream maxBodyBytes: server maxRequestBytes]
		on: HttpError
		do: [:e |
			self write: (HttpResponse error: e status reason: e messageText) keepAlive: false.
			^false].
	request isNil ifTrue: [^false].
	request remoteAddress: ([socket peerAddress] on: NetError do: [:e | nil]).
	keepAlive := request isKeepAlive.
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
	^keepAlive
]

{ #category : 'private' }
HttpConnection >> write: aResponse keepAlive: aBoolean [
	server standingHeadersDo: [:name :value |
		(aResponse headerAt: name) isNil ifTrue: [aResponse headerAt: name put: value]].
	aResponse writeOn: stream keepAlive: aBoolean.
	stream flush
]
