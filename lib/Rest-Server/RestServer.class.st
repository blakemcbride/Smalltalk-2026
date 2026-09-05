"
A Kiss-style JSON-RPC server: an HttpServer with the /rest dispatcher on it,
the services under a back-end directory, the front end under a document
root, a session cache, and a pool of database connections.

    RestServer new
        port: 8080;
        backendDirectory: 'backend';
        documentRoot: 'frontend';
        database: 'DSN=shop;UID=app;PWD=secret';
        start

or, for a server image,

    ./st80 -bootstrap -profile profiles/st2026.profile -startup 'RestServer serve' -o server.im
    ./st80 -serve server.im -workers 8 server.json

where server.json holds the same settings by name:

    { ""port"": 8080, ""backendDirectory"": ""backend"", ""documentRoot"": ""frontend"",
      ""database"": ""DSN=shop;UID=app;PWD=secret"", ""requireAuthentication"": true,
      ""userInactiveSeconds"": 3600, ""maxSessions"": 10000,
      ""connectionPoolSize"": 12,
      ""allowWithoutAuthentication"": [ ""services.Catalog:list"" ] }

WHAT IS SHARED, AND HOW.  Kiss's MainServlet keeps all of this in static
fields, some of them plain HashSets read by every request; here each is an
object with a lock, or immutable after start: the environment and the
allowed-without-login set take a Mutex on every access, the user cache and
the loader have their own, and the pool is a SharedQueue.  bootId is written
once at start and read for ever after.

Authentication is required when the configuration says so, and otherwise
when there is a database -- Kiss's rule.  The application's Login class does
the actual checking; see RestDispatcher.

STARTUP.  If <backendDirectory>/Init.class.st exists, it is the application's
startup -- Kiss's KissInit.groovy -- and start sends it two messages on the
way up:

    Init class >> init: aRestServer
        before anything is opened: the place to register a logout handler,
        read the environment, allow a method without authentication
    Init class >> init2: aDbConnectionOrNil
        once the pool is open and before the listener is, with a connection
        of its own in a transaction that is committed when the method
        returns and rolled back when it raises; nil when there is no
        database, as Kiss passes null

Either raising stops the start: the pool is closed again, nothing listens,
and the error reaches whoever sent start.  An application that could not
initialise must not be taking requests.  Without the file nothing happens,
and a class that defines only one of the two is sent only that one.
"
Class {
	#name : 'RestServer',
	#superclass : 'Object',
	#instVars : [ 'port', 'bindAddress', 'backendDirectory', 'documentRoot', 'connectionString', 'requireAuthentication', 'userInactiveSeconds', 'maxSessions', 'keepAliveSeconds', 'maxRequestBytes', 'poolSize', 'environment', 'environmentLock', 'allowed', 'allowedLock', 'bootId', 'httpServer', 'loader', 'userCache', 'pool', 'name' ],
	#category : 'Rest-Server'
}

{ #category : 'serving' }
RestServer class >> serve [
	"What a server image's startup runs: build a server from the file named
	 by the first command-line argument -- server.json by default -- start
	 it, and stay up until the operating system asks the image to stop."
	| path server |
	path := Smalltalk arguments isEmpty ifTrue: ['server.json'] ifFalse: [Smalltalk arguments first].
	"A start that fails -- no configuration, a database that cannot be
	 reached, the application's Init raising -- is logged and ends the
	 startup process, which -serve reports as `the image stopped on its
	 own' with exit code 1.  Handled here on purpose: an Error nobody
	 handles in a headless image is printed and resumed, and a server
	 that could not initialise would go on to listen."
	server := [self fromConfigFile: path]
		on: Error
		do: [:e | RestLog error: 'cannot read ', path, ': ', e messageText. ^self].
	[server start]
		on: Error
		do: [:e | RestLog error: 'cannot start: ', e messageText. ^self].
	[Socket stopRequested] whileFalse: [(Delay forMilliseconds: 250) wait].
	server stop.
	RestLog info: 'stopping as asked'.
	Smalltalk quitPrimitive
]

{ #category : 'serving' }
RestServer class >> fromConfigFile: aPath [
	"A server configured from a JSON file."
	| dir name stream text |
	dir := FileDirectory directoryFromName: aPath setFileName: [:n | name := n].
	(dir includesKey: name) ifFalse: [RestError new signal: 'no configuration file: ', aPath].
	stream := (dir findKey: name) asFileStream.
	text := [stream contentsOfEntireFile] ensure: [stream close].
	^self fromConfig: (JSONObject fromString: text)
]

{ #category : 'serving' }
RestServer class >> fromConfig: aJsonObject [
	"A server configured from a JSONObject; see the class comment for the
	 names.  Every name goes into the environment as well, so a service can
	 read its own settings from the same file."
	| server |
	server := self new.
	(aJsonObject at: 'port') notNil ifTrue: [server port: (aJsonObject integerAt: 'port')].
	(aJsonObject at: 'bindAddress') notNil ifTrue: [server bindAddress: (aJsonObject stringAt: 'bindAddress')].
	(aJsonObject at: 'backendDirectory') notNil ifTrue: [server backendDirectory: (aJsonObject stringAt: 'backendDirectory')].
	(aJsonObject at: 'documentRoot') notNil ifTrue: [server documentRoot: (aJsonObject stringAt: 'documentRoot')].
	(aJsonObject at: 'database') notNil ifTrue: [server database: (aJsonObject stringAt: 'database')].
	(aJsonObject at: 'requireAuthentication') notNil ifTrue: [server requireAuthentication: (aJsonObject booleanAt: 'requireAuthentication')].
	(aJsonObject at: 'userInactiveSeconds') notNil ifTrue: [server userInactiveSeconds: (aJsonObject integerAt: 'userInactiveSeconds')].
	(aJsonObject at: 'maxSessions') notNil ifTrue: [server maxSessions: (aJsonObject integerAt: 'maxSessions')].
	(aJsonObject at: 'keepAliveSeconds') notNil ifTrue: [server keepAliveSeconds: (aJsonObject numberAt: 'keepAliveSeconds')].
	(aJsonObject at: 'maxRequestBytes') notNil ifTrue: [server maxRequestBytes: (aJsonObject integerAt: 'maxRequestBytes')].
	(aJsonObject at: 'connectionPoolSize') notNil ifTrue: [server connectionPoolSize: (aJsonObject integerAt: 'connectionPoolSize')].
	(aJsonObject at: 'allowWithoutAuthentication') notNil ifTrue: [
		(aJsonObject arrayAt: 'allowWithoutAuthentication') do: [:entry | | colon |
			colon := entry indexOf: $:.
			colon > 1 ifTrue: [
				server allowWithoutAuthentication: (entry copyFrom: 1 to: colon - 1)
					method: (entry copyFrom: colon + 1 to: entry size)]]].
	aJsonObject keysAndValuesDo: [:k :v | server environmentAt: k put: v].
	^server
]

{ #category : 'initialization' }
RestServer >> initialize [
	port := 8080.
	backendDirectory := 'backend'.
	requireAuthentication := nil.
	userInactiveSeconds := 3600.
	maxSessions := 10000.
	keepAliveSeconds := 15.
	maxRequestBytes := 64 * 1024 * 1024.
	poolSize := 8.
	environment := Dictionary new.
	environmentLock := Mutex new.
	allowed := Set new.
	allowedLock := Mutex new.
	bootId := RestUuid new.
	name := 'rest'
]

{ #category : 'configuration' }
RestServer >> port: anInteger [
	port := anInteger
]

{ #category : 'configuration' }
RestServer >> bindAddress: aStringOrNil [
	bindAddress := aStringOrNil
]

{ #category : 'configuration' }
RestServer >> backendDirectory: aPath [
	"Where the services are: _class `services.Adder' is
	 <backendDirectory>/services/Adder.class.st."
	backendDirectory := aPath
]

{ #category : 'configuration' }
RestServer >> documentRoot: aPathOrNil [
	"Where the front end's files are, served for every path but /rest."
	documentRoot := aPathOrNil
]

{ #category : 'configuration' }
RestServer >> database: aConnectionStringOrNil [
	"An ODBC connection string; every request gets a connection from a
	 pool opened on it, in a transaction of its own."
	connectionString := aConnectionStringOrNil
]

{ #category : 'configuration' }
RestServer >> requireAuthentication: aBoolean [
	requireAuthentication := aBoolean
]

{ #category : 'configuration' }
RestServer >> userInactiveSeconds: anInteger [
	userInactiveSeconds := anInteger
]

{ #category : 'configuration' }
RestServer >> maxSessions: anInteger [
	"How many logged-in sessions may be held at once.  Ten thousand unless
	 told otherwise; see RestUserCache, which refuses a login past it rather
	 than growing without a bound a stranger controls."
	maxSessions := anInteger
]

{ #category : 'configuration' }
RestServer >> keepAliveSeconds: aNumber [
	keepAliveSeconds := aNumber
]

{ #category : 'configuration' }
RestServer >> maxRequestBytes: anInteger [
	maxRequestBytes := anInteger
]

{ #category : 'configuration' }
RestServer >> connectionPoolSize: anInteger [
	poolSize := anInteger
]

{ #category : 'configuration' }
RestServer >> name: aString [
	name := aString
]

{ #category : 'configuration' }
RestServer >> allowWithoutAuthentication: aClassPath method: aMethodName [
	"Let this method be called with no login even when logins are required
	 -- a public catalogue, a sign-up form."
	allowedLock critical: [allowed add: aClassPath, ':', aMethodName]
]

{ #category : 'configuration' }
RestServer >> logoutHandler: aBlock [
	"Evaluated with the RestUserData of every user logged out or purged."
	self userCache logoutHandler: aBlock
]

{ #category : 'accessing' }
RestServer >> allowsWithoutAuthentication: aClassPath method: aMethodName [
	^allowedLock critical: [allowed includes: aClassPath, ':', aMethodName]
]

{ #category : 'accessing' }
RestServer >> requiresAuthentication [
	"What the configuration said; with nothing said, whether there is a
	 database -- Kiss's rule."
	^requireAuthentication ifNil: [connectionString notNil]
]

{ #category : 'accessing' }
RestServer >> hasDatabase [
	^connectionString notNil
]

{ #category : 'accessing' }
RestServer >> bootId [
	^bootId
]

{ #category : 'accessing' }
RestServer >> port [
	^httpServer isNil ifTrue: [port] ifFalse: [httpServer port]
]

{ #category : 'accessing' }
RestServer >> backendDirectory [
	^backendDirectory
]

{ #category : 'accessing' }
RestServer >> userCache [
	^userCache ifNil: [userCache := RestUserCache new
		inactiveSeconds: userInactiveSeconds;
		maxUsers: maxSessions;
		yourself]
]

{ #category : 'accessing' }
RestServer >> loader [
	^loader ifNil: [loader := RestServiceLoader on: backendDirectory]
]

{ #category : 'accessing' }
RestServer >> httpServer [
	^httpServer
]

{ #category : 'accessing' }
RestServer >> isRunning [
	^httpServer notNil and: [httpServer isRunning]
]

{ #category : 'environment' }
RestServer >> environment [
	"A copy of the settings, as a Dictionary."
	^environmentLock critical: [environment copy]
]

{ #category : 'environment' }
RestServer >> environmentAt: aKey [
	^environmentLock critical: [environment at: aKey ifAbsent: [nil]]
]

{ #category : 'environment' }
RestServer >> environmentAt: aKey put: aValue [
	^environmentLock critical: [environment at: aKey put: aValue]
]

{ #category : 'login' }
RestServer >> loginClass [
	"The application's Login class, from <backendDirectory>/Login.class.st."
	^self loader classFor: 'Login'
]

{ #category : 'startup' }
RestServer >> initClass [
	"The application's Init class, from <backendDirectory>/Init.class.st,
	 or nil when there is no such file.  See the class comment, STARTUP."
	^self loader classFor: 'Init' ifAbsent: [nil]
]

{ #category : 'database' }
RestServer >> acquireConnection [
	^pool acquire
]

{ #category : 'database' }
RestServer >> releaseConnection: aConnection [
	pool release: aConnection
]

{ #category : 'database' }
RestServer >> withConnectionDo: aBlock [
	"aBlock with a pooled connection, committed after it answers and rolled
	 back if it raises, and back in the pool either way; with nil when there
	 is no database.  What init2: runs inside."
	| db result committed |
	connectionString isNil ifTrue: [^aBlock value: nil].
	db := self acquireConnection.
	committed := false.
	^[result := aBlock value: db.
	  db commit.
	  committed := true.
	  result]
		ensure: [
			committed ifFalse: [[db rollback] on: Error do: [:e | nil]].
			self releaseConnection: db]
]

{ #category : 'database' }
RestServer >> closePool [
	pool notNil ifTrue: [pool closeAll. pool := nil]
]

{ #category : 'starting and stopping' }
RestServer >> start [
	"Open the database pool, then listen.  A database that cannot be
	 reached stops the start rather than the first request; so does the
	 application's Init class raising from init: or init2: (see the class
	 comment, STARTUP), and then the pool is closed again, because a start
	 that did not finish must leave nothing open."
	| initClass initialised |
	self isRunning ifTrue: [^self].
	self userCache; loader.
	initClass := self initClass.
	(initClass notNil and: [initClass respondsTo: #init:])
		ifTrue: [initClass init: self].
	connectionString notNil ifTrue: [
		pool := RestConnectionPool on: connectionString size: poolSize.
		pool open].
	(initClass notNil and: [initClass respondsTo: #init2:])
		ifTrue: [
			initialised := false.
			[self withConnectionDo: [:db | initClass init2: db].
			 initialised := true]
				ensure: [initialised ifFalse: [self closePool]]].
	httpServer := HttpServer new
		port: port;
		bindAddress: bindAddress;
		documentRoot: documentRoot;
		keepAliveSeconds: keepAliveSeconds;
		maxRequestBytes: maxRequestBytes;
		name: name;
		yourself.
	httpServer at: '/rest' put: (RestDispatcher on: self).
	httpServer start.
	userCache startPurging.
	RestLog info: name, ': serving ', backendDirectory, ' on port ', self port printString,
		(documentRoot isNil ifTrue: [''] ifFalse: [', files from ', documentRoot]),
		(connectionString isNil ifTrue: [', no database'] ifFalse: [', ', poolSize printString, ' database connections'])
]

{ #category : 'starting and stopping' }
RestServer >> stop [
	self isRunning ifFalse: [^self].
	userCache stopPurging.
	httpServer stop.
	self closePool
]
