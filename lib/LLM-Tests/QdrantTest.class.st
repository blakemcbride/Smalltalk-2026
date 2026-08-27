"
Qdrant's wire, without a Qdrant: the paths and bodies of a collection
made, a record stored, a search, a record read and removed, and an index;
and the ids this makes.
"
Class {
	#name : 'QdrantTest',
	#superclass : 'LLMTestCase',
	#category : 'LLM-Tests'
}

{ #category : 'tests' }
QdrantTest >> testAnId [
	| id |
	id := Qdrant newId.
	self assert: id size equals: 36.
	self assert: (id at: 15) equals: $4.
	#(9 14 19 24) do: [:i | self assert: (id at: i) equals: $-].
	self assert: ('89ab' includes: (id at: 20)).
	self deny: id = Qdrant newId
]

{ #category : 'tests' }
QdrantTest >> testCollections [
	| names |
	names := self serving: (self chunked: '{"result":{"collections":[{"name":"docs"},{"name":"notes"}]},"status":"ok","time":0.0001}')
		do: [:url | (Qdrant new url: url; yourself) collections].
	self assert: names equals: #('docs' 'notes').
	self assert: (self lineAt: 1) equals: 'GET /collections HTTP/1.1'.
	self serving: (Array with: (self chunked: '{"result":true,"status":"ok"}') with: (self chunked: '{"result":true,"status":"ok"}'))
		do: [:url | (Qdrant new url: url; yourself) createCollection: 'docs' size: 3; deleteCollection: 'docs'].
	self assert: (self lineAt: 1) equals: 'PUT /collections/docs HTTP/1.1'.
	self assert: (self bodyAt: 1) asJsonString equals: '{"vectors":{"size":3,"distance":"Cosine"}}'.
	self assert: (self lineAt: 2) equals: 'DELETE /collections/docs HTTP/1.1'
]

{ #category : 'tests' }
QdrantTest >> testStoringAndSearching [
	| qdrant id hits body |
	self serving: (Array
			with: (self chunked: '{"result":{"operation_id":1,"status":"completed"},"status":"ok"}')
			with: (self chunked: '{"result":[{"id":"abc","version":1,"score":0.99,"payload":{"text":"a sentence","document_id":"doc","sequence_number":1}}],"status":"ok"}'))
		do: [:url |
			qdrant := (Qdrant on: 'docs') url: url; yourself.
			id := qdrant store: #(0.1 0.2 0.3) text: 'a sentence' document: 'doc' sequence: 1
				payload: (JSONObject new at: 'author' put: 'ada'; yourself).
			hits := qdrant search: #(0.1 0.2 0.3) limit: 5].
	self assert: id size equals: 36.
	self assert: (self lineAt: 1) equals: 'PUT /collections/docs/points HTTP/1.1'.
	body := ((self bodyAt: 1) arrayAt: 'points') objectAt: 1.
	self assert: (body stringAt: 'id') equals: id.
	self assert: (body arrayAt: 'vector') asArray size equals: 3.
	self assert: ((body objectAt: 'payload') stringAt: 'text') equals: 'a sentence'.
	self assert: ((body objectAt: 'payload') stringAt: 'document_id') equals: 'doc'.
	self assert: ((body objectAt: 'payload') integerAt: 'sequence_number') equals: 1.
	self assert: ((body objectAt: 'payload') stringAt: 'author') equals: 'ada'.
	self assert: (self lineAt: 2) equals: 'POST /collections/docs/points/search HTTP/1.1'.
	self assert: ((self bodyAt: 2) integerAt: 'limit') equals: 5.
	self assert: hits size equals: 1.
	self assert: ((hits objectAt: 1) numberAt: 'score') equals: 0.99
]

{ #category : 'tests' }
QdrantTest >> testReadingRemovingAndIndexing [
	| record |
	self serving: (Array
			with: (self chunked: '{"result":{"id":"abc","payload":{"text":"a sentence"},"vector":[0.1,0.2,0.3]},"status":"ok"}')
			with: (self chunked: '{"result":{"operation_id":2,"status":"completed"},"status":"ok"}')
			with: (self chunked: '{"result":{"operation_id":3,"status":"completed"},"status":"ok"}'))
		do: [:url | | qdrant |
			qdrant := (Qdrant on: 'docs') url: url; yourself.
			record := qdrant at: 'abc'.
			qdrant removeAt: 'abc'.
			qdrant indexField: 'document_id' schema: 'keyword'].
	self assert: (self lineAt: 1) equals: 'GET /collections/docs/points/abc HTTP/1.1'.
	self assert: (record stringAt: 'id') equals: 'abc'.
	self assert: (self lineAt: 2) equals: 'POST /collections/docs/points/delete HTTP/1.1'.
	self assert: (self bodyAt: 2) asJsonString equals: '{"points":["abc"]}'.
	self assert: (self lineAt: 3) equals: 'PUT /collections/docs/index HTTP/1.1'.
	self assert: (self bodyAt: 3) asJsonString equals: '{"field_name":"document_id","field_schema":"keyword"}'
]

{ #category : 'tests' }
QdrantTest >> testAnError [
	| error |
	error := self serving: (self status: 404 body: '{"status":{"error":"Not found: Collection `nothing` doesn''t exist!"},"time":0.0}')
		do: [:url | [(Qdrant on: 'nothing') url: url; at: 'x'. nil] on: LLMError do: [:e | e]].
	self assert: error status equals: 404.
	self assert: (error messageText includesSubstring: 'Qdrant answered 404').
	self should: [Qdrant new search: #(1) limit: 1] raise: LLMError
]
