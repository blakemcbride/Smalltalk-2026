"
A tool a model may call: a name, a description the model reads to decide
when, a JSON schema for the arguments it should send, and a block that
does it.

    LLMTool name: 'weather'
        description: 'The current weather in a city'
        parameters: (JSONObject fromString: schemaText)     type object, a string property `city'
        do: [:args | Weather in: (args stringAt: 'city')]

The block gets the arguments as a JSONObject and answers what the model
should read: a String as it is, a JSONObject or JSONArray as JSON, and
anything else printed.  A tool that raises raises out of the ask: the
caller's tool, the caller's error.  The schema is the service's business
to interpret and this file's to carry: every service takes JSON Schema for
it, under a name of its own, which the LLM subclasses know.

name:description:do: is the tool with no arguments -- a schema of an empty
object -- for `what time is it'.
"
Class {
	#name : 'LLMTool',
	#superclass : 'Object',
	#instVars : [ 'name', 'description', 'parameters', 'block' ],
	#category : 'LLM'
}

{ #category : 'instance creation' }
LLMTool class >> name: aName description: aDescription parameters: aJSONObject do: aBlock [
	^self new setName: aName description: aDescription parameters: aJSONObject do: aBlock
]

{ #category : 'instance creation' }
LLMTool class >> name: aName description: aDescription do: aBlock [
	"A tool that takes no arguments."
	^self name: aName description: aDescription
		parameters: (JSONObject new at: 'type' put: 'object'; at: 'properties' put: JSONObject new; yourself)
		do: aBlock
]

{ #category : 'instance creation' }
LLMTool class >> missing: aName [
	"What runs when a model asks for a tool nobody gave it: an answer that
	 says so, which the model reads and gets over."
	^self name: aName description: '' do: [:args | 'no such tool: ', aName]
]

{ #category : 'private' }
LLMTool >> setName: aName description: aDescription parameters: aJSONObject do: aBlock [
	name := aName.
	description := aDescription.
	parameters := aJSONObject.
	block := aBlock
]

{ #category : 'accessing' }
LLMTool >> name [
	^name
]

{ #category : 'accessing' }
LLMTool >> description [
	^description
]

{ #category : 'accessing' }
LLMTool >> parameters [
	^parameters
]

{ #category : 'running' }
LLMTool >> run: aJSONObject [
	"The tool's answer to these arguments, as the text the model reads."
	| value |
	value := block value: aJSONObject.
	value isNil ifTrue: [^''].
	value isString ifTrue: [^value].
	((value isKindOf: JSONObject) or: [value isKindOf: JSONArray]) ifTrue: [^value asJsonString].
	^value printString
]

{ #category : 'printing' }
LLMTool >> printOn: aStream [
	aStream nextPutAll: 'LLMTool '; nextPutAll: name
]
