"
The File Upload screen's service.  The screen sends a multipart form --
the files as _file-0, _file-1..., anything else as fields -- through
Server.fileUploadSend, and the request answers uploadFileCount,
uploadFileName:, uploadBytes: and saveUploadFile:to:.

Kiss's FileUpload.groovy opens the first file's stream and closes it
again; this one answers how many files came, and the first one's name and
size, and writes a line to the log, so that something visible happens.
Keeping the file is one more line: request saveUploadFile: 0 to: aPath.
"
Class {
	#name : 'FileUpload',
	#superclass : 'RestService',
	#category : 'Web-Demo'
}

{ #category : 'services' }
FileUpload >> upload [
	| n |
	n := request uploadFileCount.
	outjson at: 'count' put: n.
	n > 0 ifTrue: [
		outjson at: 'name' put: (request uploadFileName: 0);
			at: 'size' put: (request uploadBytes: 0) size.
		"request saveUploadFile: 0 to: 'uploads/', (request uploadFileName: 0)"].
	RestLog info: 'demo: upload of ', n printString, ' file',
		(n = 1 ifTrue: [''] ifFalse: ['s']),
		(n > 0
			ifTrue: [' -- the first ', (request uploadFileName: 0), ', ', (request uploadBytes: 0) size printString, ' bytes']
			ifFalse: [''])
]
