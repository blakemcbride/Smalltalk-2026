"
A Qdrant vector database, where embeddings are kept for retrieval: the
other half of a RAG, with an LLM's embed: as the first.

    qdrant := Qdrant new.                                   http://localhost:6333
    qdrant collections                                      #('docs' ...)
    qdrant createCollection: 'docs' size: 1536              the vectors' length, cosine distance
    qdrant collection: 'docs'.
    id := qdrant store: (llm embed: text) text: text document: 'manual.pdf' sequence: 3 payload: nil.
    qdrant search: (llm embed: 'a question') limit: 5       a JSONArray of hits: id, score, payload
    qdrant at: id                                           the record: its vector and payload
    qdrant removeAt: id.
    qdrant indexField: 'document_id'.

Kiss's org.kissweb.llm.QdrantClient, method for method, on an instance
with a url: rather than a class-wide one.  A record is a `point': an id
that is a UUID, made here when none is given; the vector; and a payload
carrying the text, the document it came from as document_id, its place in
that document as sequence_number, and whatever else the caller adds.
search:limit:filter: takes Qdrant's own filter JSON.  What the server
answers is answered as it came, as JSON; a reply that is not 2xx is an
LLMError with the server's words.
"
Class {
	#name : 'Qdrant',
	#superclass : 'Object',
	#instVars : [ 'url', 'collection', 'timeoutSeconds' ],
	#category : 'LLM'
}

{ #category : 'instance creation' }
Qdrant class >> on: aCollectionName [
	^self new collection: aCollectionName; yourself
]

{ #category : 'ids' }
Qdrant class >> newId [
	"A version-4 UUID as text, from the system's entropy source."
	| bytes hex out |
	bytes := Socket randomBytes: 16.
	bytes at: 7 put: (Character value: (((bytes at: 7) asInteger bitAnd: 16r0F) bitOr: 16r40)).
	bytes at: 9 put: (Character value: (((bytes at: 9) asInteger bitAnd: 16r3F) bitOr: 16r80)).
	hex := '0123456789abcdef'.
	out := WriteStream on: (String new: 36).
	bytes doWithIndex: [:c :i |
		(#(5 7 9 11) includes: i) ifTrue: [out nextPut: $-].
		out nextPut: (hex at: (c asInteger bitShift: -4) + 1);
			nextPut: (hex at: (c asInteger bitAnd: 15) + 1)].
	^out contents
]

{ #category : 'initialization' }
Qdrant >> initialize [
	url := 'http://localhost:6333'.
	timeoutSeconds := 60
]

{ #category : 'configuration' }
Qdrant >> url: aString [
	| s |
	s := aString.
	[s notEmpty and: [s last = $/]] whileTrue: [s := s copyFrom: 1 to: s size - 1].
	url := s
]

{ #category : 'configuration' }
Qdrant >> collection: aString [
	collection := aString
]

{ #category : 'configuration' }
Qdrant >> timeoutSeconds: anInteger [
	timeoutSeconds := anInteger
]

{ #category : 'accessing' }
Qdrant >> url [
	^url
]

{ #category : 'accessing' }
Qdrant >> collection [
	^collection
]

{ #category : 'collections' }
Qdrant >> collections [
	"The names of the collections the server has."
	| reply |
	reply := self request: 'GET' path: '/collections' body: nil.
	^((reply objectAt: 'result') arrayAt: 'collections') asArray collect: [:each | each stringAt: 'name']
]

{ #category : 'collections' }
Qdrant >> createCollection: aName size: vectorSize [
	"A collection for vectors of that length, compared by cosine distance."
	| vectors body |
	vectors := JSONObject new at: 'size' put: vectorSize; at: 'distance' put: 'Cosine'; yourself.
	body := JSONObject new at: 'vectors' put: vectors; yourself.
	self request: 'PUT' path: '/collections/', aName body: body
]

{ #category : 'collections' }
Qdrant >> deleteCollection: aName [
	self request: 'DELETE' path: '/collections/', aName body: nil
]

{ #category : 'records' }
Qdrant >> store: aVector text: aString document: documentId sequence: anInteger payload: aJSONObjectOrNil [
	"Insert, or update: a new id is made and answered."
	^self store: aVector id: nil text: aString document: documentId sequence: anInteger payload: aJSONObjectOrNil
]

{ #category : 'records' }
Qdrant >> store: aVector id: anIdOrNil text: aString document: documentId sequence: anInteger payload: aJSONObjectOrNil [
	"Insert the record, or update the one with that id; answer the id."
	| id point payload body |
	id := anIdOrNil ifNil: [self class newId].
	payload := JSONObject new
		at: 'text' put: aString;
		at: 'document_id' put: documentId;
		at: 'sequence_number' put: anInteger;
		yourself.
	aJSONObjectOrNil notNil ifTrue: [aJSONObjectOrNil keysAndValuesDo: [:k :v | payload at: k put: v]].
	point := JSONObject new
		at: 'id' put: id;
		at: 'vector' put: (JSONArray withAll: aVector);
		at: 'payload' put: payload;
		yourself.
	body := JSONObject new at: 'points' put: (JSONArray with: point); yourself.
	self request: 'PUT' path: self pointsPath body: body.
	^id
]

{ #category : 'records' }
Qdrant >> search: aVector limit: anInteger [
	"The anInteger records nearest the vector, as the server's JSONArray
	 of hits: each an id, a score and a payload."
	^self search: aVector limit: anInteger filter: nil
]

{ #category : 'records' }
Qdrant >> search: aVector limit: anInteger filter: aJSONObjectOrNil [
	| body reply |
	body := JSONObject new
		at: 'vector' put: (JSONArray withAll: aVector);
		at: 'limit' put: anInteger;
		yourself.
	aJSONObjectOrNil notNil ifTrue: [body at: 'filter' put: aJSONObjectOrNil].
	reply := self request: 'POST' path: self pointsPath, '/search' body: body.
	^reply arrayAt: 'result'
]

{ #category : 'records' }
Qdrant >> at: anId [
	"The record with that id: its id, vector and payload, as JSON."
	^(self request: 'GET' path: self pointsPath, '/', anId body: nil) objectAt: 'result'
]

{ #category : 'records' }
Qdrant >> removeAt: anId [
	| body |
	body := JSONObject new at: 'points' put: (JSONArray with: anId); yourself.
	self request: 'POST' path: self pointsPath, '/delete' body: body
]

{ #category : 'records' }
Qdrant >> indexField: aFieldName [
	"An index on a payload field, so that a filter on it is fast."
	^self indexField: aFieldName schema: nil
]

{ #category : 'records' }
Qdrant >> indexField: aFieldName schema: aSchemaOrNil [
	| body |
	body := JSONObject new at: 'field_name' put: aFieldName; yourself.
	aSchemaOrNil notNil ifTrue: [body at: 'field_schema' put: aSchemaOrNil].
	self request: 'PUT' path: '/collections/', collection, '/index' body: body
]

{ #category : 'private' }
Qdrant >> pointsPath [
	(collection isNil or: [collection isEmpty]) ifTrue: [^LLMError new signal: 'no collection: Qdrant on: aName'].
	^'/collections/', collection, '/points'
]

{ #category : 'private' }
Qdrant >> request: aMethod path: aPath body: aJSONObjectOrNil [
	"The reply's JSON, or an LLMError with the server's words."
	| reply |
	reply := HttpClient new readTimeout: timeoutSeconds * 1000;
		request: aMethod url: url, aPath
		body: (aJSONObjectOrNil ifNil: [nil] ifNotNil: [:b | b asJsonString])
		contentType: (aJSONObjectOrNil ifNil: [nil] ifNotNil: [:b | 'application/json']).
	reply isSuccess ifFalse: [
		^(LLMError new) status: reply status; body: reply body;
			signal: 'Qdrant answered ', reply status printString, ': ', reply body].
	^reply json
]
