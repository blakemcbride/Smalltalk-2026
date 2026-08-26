"
One part of a multipart/form-data body: a form field, or an uploaded file.

A field has a name and a body and no file name; an upload has all three and a
content type.  The body is a String of bytes either way -- a String in this
image is bytes -- and a file's bytes are not looked at.
"
Class {
	#name : 'HttpPart',
	#superclass : 'Object',
	#instVars : [ 'name', 'fileName', 'contentType', 'body' ],
	#category : 'JSON-RPC-Server'
}

{ #category : 'instance creation' }
HttpPart class >> name: aString fileName: fileNameOrNil contentType: typeOrNil body: bytes [
	^self new setName: aString fileName: fileNameOrNil contentType: typeOrNil body: bytes
]

{ #category : 'private' }
HttpPart >> setName: aString fileName: fileNameOrNil contentType: typeOrNil body: bytes [
	name := aString.
	fileName := fileNameOrNil.
	contentType := typeOrNil.
	body := bytes
]

{ #category : 'accessing' }
HttpPart >> name [
	^name
]

{ #category : 'accessing' }
HttpPart >> fileName [
	"The name the client gave the file, or nil for a field."
	^fileName
]

{ #category : 'accessing' }
HttpPart >> contentType [
	^contentType
]

{ #category : 'accessing' }
HttpPart >> body [
	^body
]

{ #category : 'testing' }
HttpPart >> isFile [
	^fileName notNil
]

{ #category : 'printing' }
HttpPart >> printOn: aStream [
	aStream nextPutAll: 'HttpPart '; nextPutAll: name printString.
	fileName notNil ifTrue: [aStream nextPutAll: ' file '; nextPutAll: fileName printString].
	aStream nextPutAll: ' ('; print: body size; nextPutAll: ' bytes)'
]
