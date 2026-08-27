"
The demo's startup: Kiss's KissInit.groovy.  RestServer>>start sends init:
before the database pool is opened and init2: once it is (see the STARTUP
paragraph of RestServer's class comment), and this is where an application
does the things Kiss did in those two methods.

init: keeps the server, and is the place for the two things Kiss's file
shows in comments -- a method allowed without a login, and a handler run
whenever a user is logged out.  Both are left as comments here too, so that
the demo asks for a login on every screen, as Kiss's does.

init2: is the reason the demo needs no separate database step.  Kiss ships
DB.sqlite ready-made; here the SQLite ODBC driver makes an empty
demo/DB.sqlite on the first connection, and this method, finding no
`users' table in it, runs demo/schema.sqlite -- Kiss's own schema file,
with its one user, renamed smalltalk, whose password is `password' --
statement by
statement.  On every later start the table is there and nothing happens.
The whole of it is one transaction: a schema that fails half way is rolled
back and the server does not start, which is what a start that could not
initialise should do.

The one class variable, Server, is written once by init: and read by
init2: a moment later on the same process, before anything listens.  It is
the only way init2: -- which is given the connection and nothing else,
as Kiss's init2(db) is -- can reach the environment for the schema's path.
"
Class {
	#name : 'Init',
	#superclass : 'Object',
	#classVars : [ 'Server' ],
	#category : 'Web-Demo'
}

{ #category : 'startup' }
Init class >> init: aServer [
	"Configure the system: before the database is opened."
	Server := aServer.
	"A method that may be called without a login:"
	"aServer allowWithoutAuthentication: 'services.MyService' method: 'addNumbers'."
	"Something to run whenever any user logs out or is purged -- closing
	 what was opened for them, stamping a table, removing their temporary
	 files:"
	"aServer logoutHandler: [:user | ...]."
]

{ #category : 'startup' }
Init class >> init2: aConnectionOrNil [
	"Once the database is open but before the application is running: make
	 the demo's tables the first time.  With no database, nothing."
	aConnectionOrNil isNil ifTrue: [^self].
	(aConnectionOrNil tableExists: 'users') ifTrue: [^self].
	self schemaStatements do: [:each | aConnectionOrNil executeImmediate: each].
	RestLog info: 'demo: made the tables from ', self schemaPath
]

{ #category : 'schema' }
Init class >> schemaPath [
	"server.json's `schema', or the file beside the demo."
	^(Server environmentAt: 'schema') ifNil: ['demo/schema.sqlite']
]

{ #category : 'schema' }
Init class >> schemaStatements [
	"The statements in the schema file: comment lines dropped, the rest
	 split at semicolons.  Enough for a schema file; not an SQL parser."
	| dir fileName stream text joined lineEnds |
	"fileName and not name: this is a class-side method, and name is the
	 class's own instance variable, which the compiler will not let a
	 temporary shadow."
	dir := FileDirectory directoryFromName: self schemaPath setFileName: [:n | fileName := n].
	(dir includesKey: fileName) ifFalse: [^self error: 'no schema file: ', self schemaPath].
	stream := (dir findKey: fileName) asFileStream.
	text := [stream contentsOfEntireFile] ensure: [stream close].
	lineEnds := String with: (Character value: 13) with: (Character value: 10).
	joined := WriteStream on: (String new: text size).
	(text substrings: lineEnds) do: [:line |
		(line trimmed startsWith: '--') ifFalse: [joined nextPutAll: line; nextPut: Character space]].
	^((joined contents substrings: ';') collect: [:each | each trimmed])
		reject: [:each | each isEmpty]
]
