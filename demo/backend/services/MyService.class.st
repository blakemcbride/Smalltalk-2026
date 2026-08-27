"
The RestServices screen's one button, and the SQL Access screen's first
question: two numbers in, their sum out; and whether there is a database.

Kiss has three of these -- MyGroovyService, MyJavaService, MyLispService --
behind three buttons, because the point there is that a service can be
written in any of three languages.  Here every service is Smalltalk, and
three buttons that did the same thing offered a choice that was not one
(Blake, 2026-08-26); so the screen has one button, Call Service, and the
back end has this one file.  The point it makes is the one Crud makes: it
is loaded the first time a request names services.MyService, reloaded when
it changes, and there is no build.  The name is the front end's:
RestServices.js and SQLAccess.js call services/MyService, and so does
this file.
"
Class {
	#name : 'MyService',
	#superclass : 'RestService',
	#category : 'Web-Demo'
}

{ #category : 'services' }
MyService >> addNumbers [
	"num3 is num1 + num2, or 0 when either is missing, as Kiss answers."
	| a b |
	a := injson integerAt: 'num1'.
	b := injson integerAt: 'num2'.
	outjson at: 'num3' put: ((a isNil or: [b isNil]) ifTrue: [0] ifFalse: [a + b])
	"Or, as Kiss's example calls a Groovy script: a class under backend/scripts,
	 loaded on first use and reloaded when its file changes --
	 outjson at: 'num3' put: ((request server loader classFor: 'scripts.MyScript') myMethod: a with: b)"
]

{ #category : 'services' }
MyService >> hasDatabase [
	"What the SQL Access screen asks first."
	outjson at: 'hasDatabase' put: request server hasDatabase
]
