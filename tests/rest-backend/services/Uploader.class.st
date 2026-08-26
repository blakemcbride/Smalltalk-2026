Class {
	#name : 'Uploader',
	#superclass : 'RestService',
	#category : 'services'
}

{ #category : 'services' }
Uploader >> upload [
	| names sizes |
	names := JSONArray new.
	sizes := JSONArray new.
	0 to: request uploadFileCount - 1 do: [:i |
		names add: (request uploadFileName: i).
		sizes add: (request uploadBytes: i) size].
	outjson at: 'count' put: request uploadFileCount.
	outjson at: 'names' put: names.
	outjson at: 'sizes' put: sizes.
	outjson at: 'note' put: (injson at: 'note').
	outjson at: 'flag' put: (injson at: 'flag').
	outjson at: 'n' put: (injson at: 'n')
]
