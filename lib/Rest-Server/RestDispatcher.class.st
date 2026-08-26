"
The /rest handler: Kiss's protocol, request in and reply out.

THE WIRE, unchanged from Kiss so that its front end -- Server.js -- works as
it is.  A request is a POST to /rest carrying a JSON document with three names
of the framework's and the rest the service's:

    _class    the service, as `services.Adder'
    _method   the method
    _uuid     the session, from a Login

or a multipart form with the same names as fields and files as _file-0,
_file-1... -- how a browser sends an upload.  The reply is always HTTP 200 and
always JSON, and its own names are

    _Success        true or false
    _ErrorCode      0; 2 for `log in first'; -1 or the service's own otherwise
    _ErrorMessage   when _Success is false
    _BootId         a uuid made when the server started, so a front end can
                    tell that the server it is talking to has restarted

with the service's own names beside them.  A service that asked for a binary
reply gets application/octet-stream: the JSON, one byte 3, and the bytes.

THREE METHODS ARE THE FRAMEWORK'S, on the empty class: LoginRequired answers
whether this server wants a login; Login takes username and password and
answers uuid; Logout ends the session.  Login and the periodic check that a
session is still good are the application's, in backend/Login.class.st:

    Login class >> login: username password: password outjson: out request: r
        ''answer a RestUserData from request server userCache newUser:..., or nil''
    Login class >> checkLogin: aRestUserData request: r
        ''answer whether the user is still allowed in''

ONE TRANSACTION PER REQUEST.  A connection is taken from the pool before the
service runs and committed after it returns; anything the service raises
rolls it back.  A service may closeConnection: itself, and then nothing more
is done to it.

WHAT IS LOGGED.  A RestUserError is the user's news and is not logged; a
RestLogError is a line; a RestServerError and every other Error is a line
naming the class, the method and the fault.  Nothing about a fault reaches
the client but the message.

Kiss's ProcessServlet has three faults this does not repeat: a request
without _class hung the client for ever (here it is an error reply); a
method registered as allowed without authentication could never match
(here the name is matched as it was registered); and a call that failed the
login check when no login was required ran the service anyway (here it
runs, on purpose, without a user).
"
Class {
	#name : 'RestDispatcher',
	#superclass : 'Object',
	#instVars : [ 'server' ],
	#category : 'Rest-Server'
}

{ #category : 'instance creation' }
RestDispatcher class >> on: aRestServer [
	^self new setServer: aRestServer
]

{ #category : 'private' }
RestDispatcher >> setServer: aRestServer [
	server := aRestServer
]

{ #category : 'handling' }
RestDispatcher >> handle: anHttpRequest [
	"The HttpResponse for one request.  Every failure is a JSON reply."
	| injson outjson |
	anHttpRequest method = 'POST'
		ifFalse: [^HttpResponse error: 405 reason: 'POST to /rest'].
	outjson := JSONObject new.
	injson := [self injsonFrom: anHttpRequest]
		on: JSONError
		do: [:e | ^self failure: 'Invalid JSON format' code: -1 in: outjson].
	^self dispatch: injson into: outjson from: anHttpRequest
]

{ #category : 'private - envelope' }
RestDispatcher >> injsonFrom: anHttpRequest [
	"The request document: the JSON body, or the fields of a multipart form
	 decoded the way Kiss's Server.js encodes them -- strings prefixed with
	 S, and the rest as true, false, null or a number."
	| injson |
	anHttpRequest isMultipart ifFalse: [
		^anHttpRequest body isEmpty
			ifTrue: [JSONObject new]
			ifFalse: [JSONObject fromString: anHttpRequest body]].
	injson := JSONObject new.
	anHttpRequest formParameters keysAndValuesDo: [:name :value |
		injson at: name put: (self decodeFormValue: value named: name)].
	^injson
]

{ #category : 'private - envelope' }
RestDispatcher >> decodeFormValue: aString named: aName [
	(aName isEmpty not and: [aName first = $_]) ifTrue: [^aString].
	aString isEmpty ifTrue: [^aString].
	aString first = $S ifTrue: [^aString copyFrom: 2 to: aString size].
	aString = 'true' ifTrue: [^true].
	aString = 'false' ifTrue: [^false].
	aString = 'null' ifTrue: [^nil].
	aString first = ${ ifTrue: [^[JSONObject fromString: aString] on: JSONError do: [:e | aString]].
	aString first = $[ ifTrue: [^[JSONArray fromString: aString] on: JSONError do: [:e | aString]].
	^[aString asNumber] on: Error do: [:e | aString]
]

{ #category : 'private' }
RestDispatcher >> dispatch: injson into: outjson from: anHttpRequest [
	| className methodName request user |
	className := injson at: '_class'.
	methodName := injson at: '_method'.
	(className isNil or: [(className isKindOf: String) not])
		ifTrue: [^self failure: 'Missing _class' code: -1 in: outjson].
	(methodName isNil or: [(methodName isKindOf: String) not])
		ifTrue: [^self failure: 'Missing _method' code: -1 in: outjson].
	request := RestRequest server: server httpRequest: anHttpRequest injson: injson outjson: outjson.
	className isEmpty ifTrue: [^self core: methodName request: request].
	"Authentication."
	user := server userCache findUser: (injson at: '_uuid').
	(server requiresAuthentication and: [(server allowsWithoutAuthentication: className method: methodName) not])
		ifTrue: [
			[self checkLogin: user request: request]
				on: RestError
				do: [:e | ^self failure: e messageText code: e errorCode in: outjson]].
	user notNil ifTrue: [user touch].
	request userData: user.
	^self serve: className method: methodName request: request
]

{ #category : 'private - core' }
RestDispatcher >> core: methodName request: request [
	"The framework's own three methods."
	| outjson injson |
	outjson := request outjson.
	injson := request injson.
	methodName = 'LoginRequired' ifTrue: [
		outjson at: 'LoginRequired' put: server requiresAuthentication.
		^self success: request].
	methodName = 'Login' ifTrue: [
		^[ | user |
			user := self login: (injson at: 'username') password: (injson at: 'password') request: request.
			outjson at: 'uuid' put: user uuid.
			self success: request]
			on: RestError
			do: [:e | self failure: e messageText code: e errorCode in: outjson]].
	methodName = 'Logout' ifTrue: [
		^[ | user |
			user := server userCache findUser: (injson at: '_uuid').
			self checkLogin: user request: request.
			server userCache removeUser: user uuid.
			outjson at: 'success' put: true.
			self success: request]
			on: RestError
			do: [:e | self failure: e messageText code: e errorCode in: outjson]].
	^self failure: 'Incorrect internal method call.' code: -1 in: outjson
]

{ #category : 'private - login' }
RestDispatcher >> login: username password: password request: request [
	"A RestUserData, through the application's Login class when a login is
	 required and straight into the cache when it is not."
	| user |
	server requiresAuthentication ifFalse: [
		^server userCache newUser: username password: password userId: nil].
	user := self withConnection: request do: [
		(server loginClass) login: username password: password outjson: request outjson request: request].
	user isNil ifTrue: [RestLogError new signal: 'Invalid login.'].
	^user
]

{ #category : 'private - login' }
RestDispatcher >> checkLogin: user request: request [
	"Raise unless the user is logged in and, if long enough has passed,
	 the application still agrees; Kiss re-asks after two minutes."
	user isNil ifTrue: [
		RestLoginRequiredError new signal: 'You have been logged out due to inactivity. Please log in again.'].
	(server requiresAuthentication and: [user secondsSinceAccess > 120]) ifTrue: [ | good |
		good := self withConnection: request do: [
			(server loginClass) checkLogin: user request: request].
		good == true ifFalse: [
			server userCache removeUser: user uuid.
			RestLoginRequiredError new signal: 'Invalid login.']].
	user touch
]

{ #category : 'private - login' }
RestDispatcher >> withConnection: request do: aBlock [
	"aBlock with a pooled connection in request db, committed after it
	 answers and rolled back if it raises; the connection goes back to the
	 pool either way."
	| db result committed |
	server hasDatabase ifFalse: [^aBlock value].
	db := server acquireConnection.
	request db: db.
	committed := false.
	^[result := aBlock value.
	  db commit.
	  committed := true.
	  result]
		ensure: [
			committed ifFalse: [[db rollback] on: Error do: [:e | nil]].
			request db: nil.
			server releaseConnection: db]
]

{ #category : 'private - serving' }
RestDispatcher >> serve: className method: methodName request: request [
	"Load the class, make the service, send the method inside a
	 transaction.  The connection goes back to the pool on every path but
	 the one where the service already sent it back itself."
	| outjson db |
	outjson := request outjson.
	^[[ | class selector service |
	   class := server loader classFor: className.
	   selector := self selectorFor: methodName on: class.
	   service := class new.
	   db := server hasDatabase ifTrue: [server acquireConnection] ifFalse: [nil].
	   request db: db.
	   service setInjson: request injson outjson: outjson db: db request: request.
	   [service perform: selector.
	    (db notNil and: [request connectionReleased not]) ifTrue: [db commit]]
		ifCurtailed: [
			(db notNil and: [request connectionReleased not])
				ifTrue: [[db rollback] on: Error do: [:e | nil]]].
	   self success: request]
		on: Error
		do: [:e |
			self log: e in: className method: methodName.
			self failure: e messageText
				code: ((e isKindOf: RestError) ifTrue: [e errorCode] ifFalse: [-1])
				in: outjson]]
		ensure: [
			(db notNil and: [request connectionReleased not])
				ifTrue: [server releaseConnection: db]]
]

{ #category : 'private - serving' }
RestDispatcher >> selectorFor: methodName on: aClass [
	"The unary selector methodName names, defined in a subclass of
	 RestService: a request cannot reach Object's methods, or this
	 package's conveniences, or anything with arguments."
	| selector implementor |
	(methodName isEmpty or: [methodName includes: $:])
		ifTrue: [RestError new signal: 'No such method: ', methodName].
	selector := methodName asSymbol.
	implementor := aClass whichClassIncludesSelector: selector.
	(implementor notNil
	 and: [implementor ~~ RestService
	 and: [(implementor inheritsFrom: RestService)]])
		ifFalse: [RestError new signal: 'No such method: ', aClass name, '>>', methodName].
	^selector
]

{ #category : 'private - serving' }
RestDispatcher >> log: anError in: className method: methodName [
	| level |
	level := (anError isKindOf: RestError) ifTrue: [anError logLevel] ifFalse: [#error].
	level == #none ifTrue: [^self].
	level == #warn
		ifTrue: [RestLog warn: className, '.', methodName, ': ', anError messageText]
		ifFalse: [RestLog error: className, '.', methodName, ': ', anError class name, ': ', anError messageText]
]

{ #category : 'private - replies' }
RestDispatcher >> success: request [
	| outjson |
	outjson := request outjson.
	outjson at: '_Success' put: true.
	outjson at: '_ErrorCode' put: 0.
	outjson at: '_BootId' put: server bootId.
	request isBinaryReturn ifTrue: [
		^HttpResponse bytes: outjson asJsonString, (String with: (Character value: 3)), request binaryData
			contentType: 'application/octet-stream'].
	^HttpResponse json: outjson asJsonString
]

{ #category : 'private - replies' }
RestDispatcher >> failure: aMessage code: anInteger in: outjson [
	outjson at: '_Success' put: false.
	outjson at: '_ErrorCode' put: anInteger.
	outjson at: '_ErrorMessage' put: (aMessage ifNil: ['unspecified']).
	anInteger = 2 ifTrue: [outjson at: '_BootId' put: server bootId].
	^HttpResponse json: outjson asJsonString
]
