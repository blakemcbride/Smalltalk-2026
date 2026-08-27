"
Kiss's MyScript.groovy: a class under backend/scripts that a service can
call, loaded the first time it is asked for and reloaded when its file
changes, so that it too can be changed on the running system.

    (request server loader classFor: 'scripts.MyScript') myMethod: 3 with: 4      107

The loader names it by its path, as it names a service; the difference
between a script and a service is only that nothing on the wire can reach
this one, because it is not a RestService.
"
Class {
	#name : 'MyScript',
	#superclass : 'Object',
	#category : 'Web-Demo'
}

{ #category : 'scripts' }
MyScript class >> myMethod: a with: b [
	^a + b + 100
]
