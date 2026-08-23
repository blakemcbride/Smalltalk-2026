"
File out a protocol or a single method in the host's line ending.

Both of these are the 1983 methods with `Disk file:' replaced by
`Disk textFile:'.  See TextFileStream for why.
"
Extension { #name : 'ClassDescription' }

{ #category : '*Files-Fixes' }
ClassDescription >> fileOutCategory: aString [
	"Create a file whose name is the name of the receiver with -.st- as the
	 extension, and file a description of the receiver's category aString
	 onto it."

	| fileName fileStream |
	fileName := Disk checkName: self name , '-' , aString , '.st' fixErrors: true.
	fileStream := Disk textFile: fileName.
	fileStream timeStamp.
	self fileOutCategory: aString on: fileStream moveSource: false toFile: 0.
	fileStream shorten; close
]

{ #category : '*Files-Fixes' }
ClassDescription >> fileOutMessage: aString fileName: fileName [
	"Create a local file named fileName and file a description of the
	 receiver's message aString onto it."

	| fileStream |
	fileStream := Disk textFile: fileName.
	fileStream timeStamp.
	self fileOutMessage: aString on: fileStream moveSource: false toFile: 0.
	fileStream close
]
