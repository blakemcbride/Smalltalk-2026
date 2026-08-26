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
NetError that ends it; connections in flight finish their request and close.
"
Class {
	#name : 'HttpServer',
	#superclass : 'Object',
	#instVars : [ 'port', 'bindAddress', 'listener', 'acceptProcess', 'handlers', 'handlersLock', 'staticHandler', 'maxRequestBytes', 'keepAliveMilliseconds', 'standingHeaders', 'connections', 'connectionsLock', 'running', 'name' ],
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
	maxRequestBytes := 64 * 1024 * 1024.
	keepAliveMilliseconds := 15000.
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
	running ifTrue: [^self].
	listener := ServerSocket listenOn: port address: bindAddress backlog: 128.
	running := true.
	acceptProcess := [self acceptLoop] fork.
	self log: 'listening on port ', self port printString
]

{ #category : 'starting and stopping' }
HttpServer >> stop [
	"Stop accepting.  Connections in flight finish what they are doing."
	running ifFalse: [^self].
	running := false.
	listener close.
	listener := nil.
	self log: 'stopped'
]

{ #category : 'private' }
HttpServer >> acceptLoop [
	"Accept until the listener is closed, which accept reports as NetError."
	[[running] whileTrue: [self serve: listener accept]]
		on: NetError
		do: [:e | running := false]
]

{ #category : 'private' }
HttpServer >> serve: aSocket [
	"One connection, in a process of its own.  The socket is this method's
	 argument and therefore this activation's -- see the class comment."
	connectionsLock critical: [connections := connections + 1].
	[(HttpConnection on: aSocket server: self) run] fork
]

{ #category : 'private' }
HttpServer >> connectionClosed: aConnection [
	connectionsLock critical: [connections := connections - 1]
]

{ #category : 'handling' }
HttpServer >> handle: aRequest [
	"The response for aRequest: from the handler at its path, else the
	 static files, else 404.  A handler's own Error is a 500 here."
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
	"To standard error: a server run has no Transcript window."
	self reportOnStandardError: name, ': ', aString
]
