"
What a service extends.  A service is a Tonel file under the back-end
directory --

    backend/services/Adder.class.st

    Class { #name : 'Adder', #superclass : 'RestService', #category : 'services' }
    Adder >> addNumbers [
        outjson at: 'num3' put: (injson integerAt: 'num1') + (injson integerAt: 'num2')
    ]

-- and a request is

    { ""_class"": ""services.Adder"", ""_method"": ""addNumbers"", ""num1"": 3, ""num2"": 4 }

The method named by _method is sent, as it is, to a NEW instance of the
class, with these four instance variables already set:

    injson    the request's JSON document, a JSONObject
    outjson   the reply, an empty JSONObject the method fills in
    db        the request's DbConnection, in a transaction that is committed
              when the method returns and rolled back if it raises; nil when
              the server has no database
    request   the RestRequest: the user, uploads, returnBinary:, and the rest

Kiss hands the same four to a Java method as parameters; Smalltalk hands them
as state, because a fresh instance per request is state that belongs to nobody
else.  Nothing here is shared between requests, and a service that keeps
something in a class variable is choosing to share it and must lock it.

Only a unary method defined in a subclass of this class can be called from
outside.  The conveniences here -- userData, requireLogin, returnBinary: --
are for the service's own use and cannot be named by _method.
"
Class {
	#name : 'RestService',
	#superclass : 'Object',
	#instVars : [ 'injson', 'outjson', 'db', 'request' ],
	#category : 'Rest-Server'
}

{ #category : 'private' }
RestService >> setInjson: in outjson: out db: aConnection request: aRestRequest [
	injson := in.
	outjson := out.
	db := aConnection.
	request := aRestRequest
]

{ #category : 'accessing' }
RestService >> injson [
	^injson
]

{ #category : 'accessing' }
RestService >> outjson [
	^outjson
]

{ #category : 'accessing' }
RestService >> db [
	^db
]

{ #category : 'accessing' }
RestService >> request [
	^request
]

{ #category : 'conveniences' }
RestService >> userData [
	"The logged-in user, or nil."
	^request userData
]

{ #category : 'conveniences' }
RestService >> requireLogin [
	"Raise RestLoginRequiredError unless a user is logged in."
	^request requireLogin
]

{ #category : 'conveniences' }
RestService >> returnBinary: aString [
	"Answer these bytes after the JSON, for a file download."
	^request returnBinary: aString
]
