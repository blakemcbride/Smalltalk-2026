"
Everything about one request that a service may want: the documents, the
connection, the user, the uploads, and the two ways out.

This is Kiss's ProcessServlet as the service sees it.  One per request, made
by the dispatcher, handed to the service as its `request' variable, and
forgotten when the response is written.
"
Class {
	#name : 'RestRequest',
	#superclass : 'Object',
	#instVars : [ 'server', 'httpRequest', 'injson', 'outjson', 'db', 'userData', 'binaryData', 'connectionReleased' ],
	#category : 'Rest-Server'
}

{ #category : 'instance creation' }
RestRequest class >> server: aServer httpRequest: anHttpRequest injson: in outjson: out [
	^self new setServer: aServer httpRequest: anHttpRequest injson: in outjson: out
]

{ #category : 'private' }
RestRequest >> setServer: aServer httpRequest: anHttpRequest injson: in outjson: out [
	server := aServer.
	httpRequest := anHttpRequest.
	injson := in.
	outjson := out.
	connectionReleased := false
]

{ #category : 'private' }
RestRequest >> db: aConnection [
	db := aConnection
]

{ #category : 'private' }
RestRequest >> userData: aRestUserData [
	userData := aRestUserData
]

{ #category : 'accessing' }
RestRequest >> server [
	^server
]

{ #category : 'accessing' }
RestRequest >> httpRequest [
	^httpRequest
]

{ #category : 'accessing' }
RestRequest >> injson [
	^injson
]

{ #category : 'accessing' }
RestRequest >> outjson [
	^outjson
]

{ #category : 'accessing' }
RestRequest >> db [
	"The request's database connection, or nil when there is no database
	 or the service released it."
	^db
]

{ #category : 'accessing' }
RestRequest >> userData [
	"The logged-in user, or nil."
	^userData
]

{ #category : 'accessing' }
RestRequest >> environment [
	"The server's environment: what its configuration file said."
	^server environment
]

{ #category : 'accessing' }
RestRequest >> environmentAt: aKey [
	"One setting from the configuration file, or nil: what a service reads
	 its own settings with -- Kiss's MainServlet.getEnvironment(name)."
	^server environmentAt: aKey
]

{ #category : 'accessing' }
RestRequest >> remoteAddress [
	^httpRequest remoteAddress
]

{ #category : 'testing' }
RestRequest >> isLoggedIn [
	^userData notNil
]

{ #category : 'login' }
RestRequest >> requireLogin [
	userData isNil ifTrue: [RestLoginRequiredError new signal: 'login required'].
	^userData
]

{ #category : 'uploads' }
RestRequest >> uploadFileCount [
	"How many files came with the request: parts named _file-0, _file-1..."
	| n |
	n := 0.
	[(httpRequest partNamed: '_file-', n printString) notNil] whileTrue: [n := n + 1].
	^n
]

{ #category : 'uploads' }
RestRequest >> uploadPart: anIndex [
	"The part for upload anIndex, counting from 0; raises if there is none."
	| part |
	part := httpRequest partNamed: '_file-', anIndex printString.
	part isNil ifTrue: [RestUserError new signal: 'no upload number ', anIndex printString].
	^part
]

{ #category : 'uploads' }
RestRequest >> uploadFileName: anIndex [
	^(self uploadPart: anIndex) fileName
]

{ #category : 'uploads' }
RestRequest >> uploadContentType: anIndex [
	^(self uploadPart: anIndex) contentType
]

{ #category : 'uploads' }
RestRequest >> uploadBytes: anIndex [
	"The file's bytes, as a String."
	^(self uploadPart: anIndex) body
]

{ #category : 'uploads' }
RestRequest >> saveUploadFile: anIndex to: aPath [
	"Write upload anIndex to aPath, bytes as they came."
	| dir name stream |
	dir := FileDirectory directoryFromName: aPath setFileName: [:n | name := n].
	stream := dir file: name.
	[stream nextPutAll: (self uploadBytes: anIndex); shorten] ensure: [stream close]
]

{ #category : 'binary' }
RestRequest >> returnBinary: aString [
	"Send these bytes after the JSON reply, as application/octet-stream:
	 what a file download is."
	binaryData := aString
]

{ #category : 'binary' }
RestRequest >> binaryData [
	^binaryData
]

{ #category : 'binary' }
RestRequest >> isBinaryReturn [
	^binaryData notNil
]

{ #category : 'database' }
RestRequest >> closeConnection: success [
	"Finish with the database before the method returns: commit (true) or
	 roll back (false), and hand the connection back to the pool.  The
	 dispatcher then commits nothing."
	db isNil ifTrue: [^self].
	success ifTrue: [db commit] ifFalse: [db rollback].
	server releaseConnection: db.
	db := nil.
	connectionReleased := true
]

{ #category : 'database' }
RestRequest >> closeConnection [
	"Hand the connection back without committing or rolling back."
	db isNil ifTrue: [^self].
	server releaseConnection: db.
	db := nil.
	connectionReleased := true
]

{ #category : 'database' }
RestRequest >> connectionReleased [
	^connectionReleased
]
