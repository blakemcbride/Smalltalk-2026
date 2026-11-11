"
An HTTP/1.1 server: a listener, a process per connection, handlers by path,
and files under a document root for every path no handler claims.

    | server |
    server := HttpServer new port: 8080; documentRoot: 'frontend'.
    server at: '/rest' put: [:request | HttpResponse json: request body].
    server start.

This is what Tomcat was to Kiss: the listener, the thread pool, the request
parser and the static files, with the JSON-RPC protocol living above it in
lib/Rest-Server and knowing nothing about sockets.

ONE PROCESS PER CONNECTION.  The accept loop runs in a process of its own,
and every connection it accepts is handed to a new process -- HttpConnection
is that process's body -- which whichever worker is idle picks up.  That is
the whole of the threading: with the pool running, N connections are served
on N cores, and a connection waiting for its client to send something costs
no worker at all, because the wait is on a Semaphore the VM's network thread
signals.  Under `-bootstrap -tests' the same code runs green, one interpreter
switching between the processes, and the tests pass the same way.

The socket reaches its process AS A METHOD ARGUMENT, in serve:, and not as a
temporary declared inside the loop.  whileTrue: is inlined, so a temporary in
its body is one variable for every iteration, and a process forked in one
iteration that captured it would find itself reading the socket the next
iteration accepted.  The gate on the socket layer found exactly that.

HANDLERS.  at:put: maps a path to a handler -- an object that answers
handle: aRequest with an HttpResponse, or a block taking the request.  The
match is exact.  What matches nothing goes to the static files if there is a
document root, and is 404 otherwise.  The handler table is read on every
request and written by whoever configures the server; a Mutex guards both,
the reads too.

STOPPING.  stop closes the listener, which wakes the accept loop with a
NetError; the loop sees a listener that is gone and ends; connections in
flight finish their request and close, and every connection waiting for its
next request is closed there and then.  A request that arrives on a kept
connection after the stop is not read: the connection ends, as it would at
the end of its keep-alive time.  Without that, a browser that kept its
connection busy -- a page polling more often than the fifteen-second
keep-alive -- went on being served by a server that answered isRunning
false, and after a restart by the old server, with its old configuration
and sessions (Bugs07 NET-13).  The server knows its open connections for
this, and a connection's change from idle to busy is made under the same
lock stop looks at them under: see requestBegun:.  A NetError from an accept whose
listener is still open is NOT the end -- it is one connection lost, logged,
and the loop goes round again after a back-off.  See acceptLoop.

A request is IN FLIGHT from its first byte until its connection either goes
back to waiting for the next one or closes, and the server counts them:
waitForRequestsAtMost: is how a server that has been asked to stop lets
those finish before its image quits (RestServer class>>serve).  An idle
kept-alive connection is not one -- it has nothing to lose -- so a browser
holding its connections open does not keep a stopping server up.

ORIGINS.  A page served from this host on another port -- Kiss's
development layout, the front end on 8000 and this server on 8001 -- is
another origin to the browser, which then sends Origin with every request,
sends a preflight OPTIONS before a POST with a JSON body, and shows the page
the reply only if the response says that origin is allowed.  The rule here
is fixed and not configured: an Origin whose host is the host of the
request's own Host header, port aside and case aside, is allowed -- echoed
back in Access-Control-Allow-Origin, never `*' -- and any other host is
not: its response carries no such header, so the browser withholds it from
the page, and its preflight is 403.  A request with no Origin is served as
it always was, with no such header.  Kiss's index.js builds the back-end
address from the page's own hostname, so the two name the same host by
construction; `localhost' and `127.0.0.1' are different names to this
rule, and a front end that mixes them is refused, as it should be.  The
scheme is not compared: a page on https: calling http: is stopped by the
browser as mixed content before it gets here.  HttpCodec class>>hostOf: is
the comparison, and HttpRequest>>isPreflight the question.
"
Class {
	#name : 'HttpServer',
	#superclass : 'Object',
	#instVars : [ 'port', 'bindAddress', 'listener', 'acceptProcess', 'handlers', 'handlersLock', 'staticHandler', 'maxRequestBytes', 'keepAliveMilliseconds', 'requestMilliseconds', 'writeMilliseconds', 'maxConnections', 'standingHeaders', 'connections', 'connectionsLock', 'running', 'name', 'inFlight', 'live' ],
	#category : 'JSON-RPC-Server'
}

{ #category : 'initialization' }
HttpServer >> initialize [
	port := 8080.
	bindAddress := nil.
	handlers := Dictionary new.
	handlersLock := Mutex new.
	connectionsLock := Mutex new.
	connections := 0.
	inFlight := 0.
	live := IdentitySet new.
	maxRequestBytes := 64 * 1024 * 1024.
	keepAliveMilliseconds := 15000.
	"Bugs5 NET-1: see requestSeconds:, writeSeconds: and maxConnections:."
	requestMilliseconds := 120000.
	writeMilliseconds := 30000.
	maxConnections := self class defaultMaxConnections.
	running := false.
	name := 'http'.
	"The headers Kiss's SecurityHeadersFilter added to every response."
	standingHeaders := OrderedCollection new.
	standingHeaders add: 'X-Content-Type-Options' -> 'nosniff'.
	standingHeaders add: 'X-Frame-Options' -> 'SAMEORIGIN'.
	standingHeaders add: 'Referrer-Policy' -> 'strict-origin-when-cross-origin'
]

{ #category : 'configuration' }
HttpServer >> port: anInteger [
	"The port to listen on; 0 for one the system picks, which port then
	 answers once the server has started."
	port := anInteger
]

{ #category : 'configuration' }
HttpServer >> bindAddress: aStringOrNil [
	"One interface -- '127.0.0.1' for loopback only -- or nil for all."
	bindAddress := aStringOrNil
]

{ #category : 'configuration' }
HttpServer >> documentRoot: aDirectoryPathOrNil [
	"Serve files from here for every path no handler claims."
	staticHandler := aDirectoryPathOrNil isNil ifTrue: [nil] ifFalse: [HttpStaticFileHandler on: aDirectoryPathOrNil]
]

{ #category : 'configuration' }
HttpServer >> maxRequestBytes: anInteger [
	maxRequestBytes := anInteger
]

{ #category : 'configuration' }
HttpServer >> keepAliveSeconds: aNumber [
	"How long a connection may sit idle between requests."
	keepAliveMilliseconds := (aNumber * 1000) rounded
]

{ #category : 'configuration' }
HttpServer >> requestSeconds: aNumber [
	"How long one whole request -- its head and its body -- may take to
	 arrive, once its first byte has.

	 keepAliveSeconds: bounds one wait for data, and a client that sends a
	 byte every few seconds never waits that long: `X-A: ' dripped one
	 character at a time was still being read after thirty seconds, and
	 4,300 such clients filled the socket table so that nothing else could
	 connect (Bugs5 NET-1).  Two minutes by default, which is a 64 MB body
	 at half a megabyte a second."
	requestMilliseconds := (aNumber * 1000) rounded
]

{ #category : 'configuration' }
HttpServer >> requestMilliseconds [
	^requestMilliseconds
]

{ #category : 'configuration' }
HttpServer >> writeSeconds: aNumber [
	"How long a response may wait for the client to take any of it.  A
	 client that asks for a large file and never reads held its connection,
	 its process and the file's bytes for ever (Bugs5 NET-1); one that reads
	 slowly is not cut off, since the clock starts again whenever it takes
	 something."
	writeMilliseconds := (aNumber * 1000) rounded
]

{ #category : 'configuration' }
HttpServer >> writeMilliseconds [
	^writeMilliseconds
]

{ #category : 'configuration' }
HttpServer >> maxConnections: anInteger [
	"How many connections may be open at once; one more is answered 503
	 and closed.  Without a cap, slow clients filled the VM's table of
	 4,096 sockets and the server stopped accepting at all (Bugs5 NET-1).
	 See HttpServer class>>defaultMaxConnections for the default."
	maxConnections := anInteger
]

{ #category : 'configuration' }
HttpServer class >> defaultMaxConnections [
	"1,024, or fewer when the process's descriptor limit would not leave
	 room beside them for everything else the image opens -- database
	 connections, outgoing HTTP, service files, the image's own.

	 1,024 was the whole of it, counted against the VM's table of 4,096
	 sockets, and the limit that binds first is usually the descriptor
	 limit: 1,024 under systemd and in an Ubuntu login shell, 256 on macOS.
	 About a thousand idle connections then used every descriptor while
	 there were still fewer than 1,024 of them, so the 503 never came, a
	 fresh client waited in the backlog with no answer, and each request in
	 flight that needed a descriptor failed with `Too many open files'
	 (Bugs07 NET-3).  The VM now raises the limit where the hard limit
	 allows, which on most hosts leaves this at 1,024; where it cannot, a
	 quarter of the limit, at most 256, is kept for the rest, and a 256
	 limit gives 192 connections.  maxConnections: overrides it, and a
	 connection accepted past the descriptors there are is still answered
	 503 -- see serve:."
	| limit |
	limit := Socket descriptorLimit.
	limit isNil ifTrue: [^1024].
	^1024 min: (limit - (limit // 4 min: 256) max: 16)
]

{ #category : 'configuration' }
HttpServer >> maxConnections [
	^maxConnections
]

{ #category : 'configuration' }
HttpServer >> name: aString [
	"For the log lines."
	name := aString
]

{ #category : 'configuration' }
HttpServer >> standingHeaderAt: aName put: aValue [
	"A header sent with every response unless the handler set its own."
	standingHeaders := standingHeaders reject: [:pair | pair key asLowercase = aName asLowercase].
	standingHeaders add: aName -> aValue
]

{ #category : 'configuration' }
HttpServer >> at: aPath put: aHandler [
	"Serve aPath with aHandler: an object answering handle:, or a block
	 taking the request."
	handlersLock critical: [handlers at: aPath put: aHandler]
]

{ #category : 'configuration' }
HttpServer >> removeHandlerAt: aPath [
	handlersLock critical: [handlers removeKey: aPath ifAbsent: [nil]]
]

{ #category : 'accessing' }
HttpServer >> port [
	"The port actually listened on."
	^listener isNil ifTrue: [port] ifFalse: [listener localPort]
]

{ #category : 'accessing' }
HttpServer >> maxRequestBytes [
	^maxRequestBytes
]

{ #category : 'accessing' }
HttpServer >> keepAliveMilliseconds [
	^keepAliveMilliseconds
]

{ #category : 'accessing' }
HttpServer >> connectionCount [
	"How many connections are open now."
	^connectionsLock critical: [connections]
]

{ #category : 'accessing' }
HttpServer >> requestsInFlight [
	"How many requests are being read, handled or answered now -- the
	 connections that are not idle.  See the class comment, STOPPING."
	^connectionsLock critical: [inFlight]
]

{ #category : 'accessing' }
HttpServer >> isRunning [
	^running
]

{ #category : 'accessing' }
HttpServer >> standingHeadersDo: aBlock [
	standingHeaders do: [:pair | aBlock value: pair key value: pair value]
]

{ #category : 'starting and stopping' }
HttpServer >> start [
	"Listen, and accept in a process of this server's own."
	| limit |
	running ifTrue: [^self].
	listener := ServerSocket listenOn: port address: bindAddress backlog: 128.
	running := true.
	acceptProcess := [self acceptLoop] fork.
	self log: 'listening on port ', self port printString.
	"A maxConnections: the descriptors cannot hold is said once, here,
	 rather than discovered as `Too many open files' in somebody's request
	 (Bugs07 NET-3)."
	limit := Socket descriptorLimit.
	(limit notNil and: [maxConnections >= limit]) ifTrue: [
		self log: 'maxConnections ', maxConnections printString,
			' is more than the ', limit printString,
			' descriptors this process may open; past them a connection is answered 503']
]

{ #category : 'starting and stopping' }
HttpServer >> stop [
	"Stop accepting.  Connections in flight finish what they are doing and
	 close; connections waiting for their next request are closed now.

	 Closing the listener used to be all of it, and a kept-alive
	 connection went on reading and answering requests for as long as its
	 client kept it busy (Bugs07 NET-13).  A connection answers its
	 request with Connection: close once running is false (HttpConnection
	 >>serveOne); one that is idle is closed here, which wakes its
	 process from its wait for the next request.  Which connections are
	 idle is decided under connectionsLock, the lock a connection becomes
	 busy under (requestBegun:), so none is closed in the middle of a
	 request it has begun reading; one whose next request comes after
	 running is false is refused there instead.  Nothing here waits: the
	 requests in flight are waitForRequestsAtMost:'s."
	| idle |
	running ifFalse: [^self].
	running := false.
	listener close.
	listener := nil.
	idle := connectionsLock critical: [(live reject: [:each | each isBusy]) asArray].
	idle do: [:each | each closeSocket].
	self log: 'stopped'
]

{ #category : 'starting and stopping' }
HttpServer >> waitForRequestsAtMost: milliseconds [
	"Wait until no request is in flight, or until milliseconds have passed,
	 and answer how many still are: 0 when every one finished.

	 For after stop (Bugs07 NET-5): the listener is closed, so nothing new
	 arrives, and a request answered from here on ends its connection --
	 see HttpConnection>>serveOne -- and a request that begins after the
	 stop is not read (requestBegun:), so the count only falls (Bugs07
	 NET-13).  Polled rather than signalled:
	 it runs once, at the end of a server's life, for a few seconds."
	| deadline |
	deadline := Time millisecondClockValue + (milliseconds max: 0).
	[self requestsInFlight > 0 and: [Time millisecondClockValue < deadline]]
		whileTrue: [(Delay forMilliseconds: 20) wait].
	^self requestsInFlight
]

{ #category : 'private' }
HttpServer >> acceptLoop [
	"Accept until the listener is closed, which accept reports as NetError.

	 ONE FAILED ACCEPT IS NOT THE END OF THE LOOP, and it used to be.  The
	 whole loop sat inside `on: NetError do: [:e | running := false]', so
	 the FIRST accept that answered an error ended the server's ability to
	 accept anything ever again -- silently, with the listen socket still
	 open, so a client's connect was accepted by the kernel's backlog and
	 then hung for ever rather than being refused, and the process went on
	 looking alive.  Bugs4 NET-1 held 1,024 idle connections, which is
	 exactly the VM's socket table (NET_MAX_SOCKETS), and every request
	 after that timed out -- including every request made after all 1,024
	 had been closed again.  A transient EMFILE from the operating system
	 did the same thing for the same reason, and the descriptor leak in
	 TonelSource>>stampFrom: (NET-2) reached it on ordinary traffic with no
	 attacker at all.

	 So a per-accept failure is one connection lost and a line in the log,
	 and the loop ends only when the listener is really gone -- which is
	 how stop stops it: close wakes this process, and acceptFailed: sees
	 the closed listener and lets the loop end.

	 THE BACK-OFF is what makes retrying safe.  An accept that fails
	 because there is no room for another socket fails again at once, so a
	 bare retry would be a worker spinning at 100% writing a log line per
	 pass -- a denial of service answering a denial of service.  It doubles
	 from 10 ms to a second and is forgotten the moment an accept
	 succeeds, so a server whose table has filled starts serving again
	 within a second of a connection closing and costs nothing while it
	 waits.

	 The socket still reaches its process as serve:'s ARGUMENT and not as
	 this loop's temporary -- see the class comment; whileTrue: is inlined,
	 so `socket' below is one variable for every iteration and would be the
	 next connection by the time a forked process read it."
	| backoff socket |
	backoff := 0.
	[running] whileTrue: [
		socket := [listener isNil ifTrue: [nil] ifFalse: [listener accept]]
			on: NetError
			do: [:e | self acceptFailed: e. nil].
		socket isNil
			ifTrue: [
				running ifTrue: [
					backoff := backoff = 0 ifTrue: [10] ifFalse: [(backoff * 2) min: 1000].
					(Delay forMilliseconds: backoff) wait]]
			ifFalse: [
				backoff := 0.
				self serve: socket]]
]

{ #category : 'private' }
HttpServer >> acceptFailed: anError [
	"One accept that did not answer a socket.  A listener that is gone ends
	 the loop -- that is stop, and the NetError is how close tells this
	 process about it.  Anything else is the network refusing one
	 connection: the peer reset it before it was accepted, or there is no
	 room for another socket right now, neither of which says anything
	 about the next one."
	(running not or: [listener isNil or: [listener isOpen not]])
		ifTrue: [running := false. ^self].
	self log: 'accept failed: ', (anError messageText ifNil: ['no reason given'])
]

{ #category : 'private' }
HttpServer >> serve: aSocket [
	"One connection, in a process of its own.  The socket is this method's
	 argument and therefore this activation's -- see the class comment.

	 Over maxConnections, or over the process's descriptors, it is
	 answered 503 and closed here, before a process is forked for it: the
	 reply is a few dozen bytes into a socket nothing has been written to,
	 so it does not wait, and the write timeout is there in case it would."
	| admitted connection stopped |
	"A connection the VM could accept only in its spare descriptor's room
	 is answered the same way: the process has no descriptor left for the
	 work, and the close gives the spare back (Bugs07 NET-3)."
	admitted := aSocket isOverDescriptorLimit not and: [
		connectionsLock critical: [
			connections < maxConnections
				ifTrue: [connections := connections + 1. true]
				ifFalse: [false]]].
	admitted ifFalse: [
		[[aSocket
			send: 'HTTP/1.1 503 Service Unavailable', self crlf,
				'Content-Length: 0', self crlf,
				'Connection: close', self crlf, self crlf
			timeout: 1000] ensure: [aSocket close]]
			on: Error do: [:e | nil].
		^self].
	"Known to the server before its process starts, so that stop can
	 close it while it waits for its first request (Bugs07 NET-13).  One
	 accepted as stop ran, too late for stop to see, is closed here, so
	 that it does not wait out a keep-alive for a request it will not
	 read; its process still runs, to give its place back."
	connection := HttpConnection on: aSocket server: self.
	stopped := connectionsLock critical: [live add: connection. running not].
	stopped ifTrue: [connection closeSocket].
	[connection run] fork
]

{ #category : 'private' }
HttpServer >> crlf [
	^String with: (Character value: 13) with: (Character value: 10)
]

{ #category : 'private' }
HttpServer >> connectionClosed: aConnection [
	connectionsLock critical: [
		connections := connections - 1.
		live remove: aConnection ifAbsent: [nil]]
]

{ #category : 'private' }
HttpServer >> requestBegun: aConnection [
	"A connection has the first byte of a request: mark it busy and count
	 it in flight, and answer true -- or answer false, marking nothing,
	 when the server has stopped, and the connection ends without reading
	 the request.  Under connectionsLock, which is what stop decides which
	 connections are idle under, so a connection is either busy before
	 stop looks, and finishes its request, or refused here after it
	 (Bugs07 NET-13)."
	^connectionsLock critical: [
		running
			ifTrue: [inFlight := inFlight + 1. aConnection markBusy. true]
			ifFalse: [false]]
]

{ #category : 'private' }
HttpServer >> requestEnded [
	"A connection has answered its request and is idle again, or is closed."
	connectionsLock critical: [inFlight := inFlight - 1]
]

{ #category : 'handling' }
HttpServer >> handle: aRequest [
	"The response for aRequest: a browser's preflight answered here, else
	 the handler at its path, else the static files, else 404 -- and, for
	 a request from a page on this host at another port, the header that
	 lets the browser show the page the reply.  The class comment, ORIGINS."
	^[ | response |
	  aRequest isPreflight
		ifTrue: [self preflightFor: aRequest]
		ifFalse: [
			response := [self dispatch: aRequest]
				on: HttpError
				do: [:e | HttpResponse error: e status reason: e messageText].
			self allow: response for: aRequest]]
		on: Error
		do: [:e |
			"dispatch: already turns a handler's Error into a 500; this
			 catches the two places OUTSIDE it that put request data into a
			 response header -- allow: and preflightFor:, which echo the
			 Origin back.  A header value is refused rather than filtered
			 (Bugs3 B54, HttpResponse>>headerAt:put:), and a refusal here
			 would otherwise leave handle: by an Error the connection does
			 not catch, which is a closed socket and no reply at all."
			e class == HttpError ifTrue: [e pass].
			self log: aRequest printString, ': ', e class name, ': ', e messageText.
			HttpResponse error: 500 reason: 'internal server error']
]

{ #category : 'origins' }
HttpServer >> allowsOrigin: aRequest [
	"Whether the request's Origin is this host on any port: the same host
	 as the Host header's, port aside, case aside."
	| origin host |
	origin := aRequest origin.
	origin isNil ifTrue: [^false].
	host := aRequest hostName.
	(host isNil or: [host isEmpty]) ifTrue: [^false].
	^(HttpCodec hostOf: origin) = host
]

{ #category : 'origins' }
HttpServer >> allow: aResponse for: aRequest [
	"The response, carrying the origin's permission when it has one."
	(self allowsOrigin: aRequest) ifTrue: [
		aResponse headerAt: 'Access-Control-Allow-Origin' put: aRequest origin;
			headerAt: 'Vary' put: 'Origin'].
	^aResponse
]

{ #category : 'origins' }
HttpServer >> preflightFor: aRequest [
	"The answer to a browser asking whether it may send a request from
	 another origin: yes, for this host on any port, with what it may
	 send; 403 for any other host.  No handler sees a preflight."
	| response requested |
	(self allowsOrigin: aRequest)
		ifFalse: [^HttpResponse error: 403 reason: 'origin not allowed: ', aRequest origin].
	requested := aRequest headerAt: 'access-control-request-headers'.
	response := HttpResponse status: 204.
	response headerAt: 'Access-Control-Allow-Origin' put: aRequest origin;
		headerAt: 'Vary' put: 'Origin';
		headerAt: 'Access-Control-Allow-Methods' put: 'POST, GET, OPTIONS';
		headerAt: 'Access-Control-Allow-Headers' put: (requested isNil ifTrue: ['Content-Type'] ifFalse: [requested]);
		headerAt: 'Access-Control-Max-Age' put: '86400'.
	^response
]

{ #category : 'handling' }
HttpServer >> dispatch: aRequest [
	"The response from the handler at the request's path, else the static
	 files, else 404.  A handler's own Error is a 500 here; an HttpError
	 it raised keeps its status."
	| handler |
	handler := handlersLock critical: [handlers at: aRequest path ifAbsent: [nil]].
	handler isNil ifTrue: [
		staticHandler isNil ifTrue: [^HttpResponse notFound: 'not found: ', aRequest path].
		handler := staticHandler].
	^[(handler respondsTo: #handle:)
		ifTrue: [handler handle: aRequest]
		ifFalse: [handler value: aRequest]]
		on: Error
		do: [:e |
			e class == HttpError ifTrue: [e pass].
			self log: aRequest printString, ': ', e class name, ': ', e messageText.
			HttpResponse error: 500 reason: 'internal server error']
]

{ #category : 'logging' }
HttpServer >> log: aString [
	"To standard error: a server run has no Transcript window.  Escaped,
	 because the line quotes the request and its error, which are the
	 client's: see HttpCodec class>>printableForLog: (Bugs5 NET-13)."
	self reportOnStandardError: (HttpCodec printableForLog: name, ': ', aString)
]
