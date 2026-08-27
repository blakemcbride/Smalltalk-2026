"
OpenRouter: one key and one API -- OpenAI's chat completions shape -- in
front of many services' models, named `provider/model'.

    router := OpenRouter apiKey: (Smalltalk environmentAt: 'OPENROUTER_API_KEY') model: 'anthropic/claude-sonnet-4'.
    router fallbackModels: #('openai/gpt-4o' 'meta-llama/llama-3.1-70b-instruct').
    router siteUrl: 'https://example.com'; siteName: 'My Application'.
    router send: 'Why is the sky blue?'

Kiss's org.kissweb.llm.OpenRouter, which is its OpenAI with three
additions, and so is this: fallbackModels: are sent as the `models' list
the gateway tries in order when the first fails; siteUrl: and siteName:
are the `HTTP-Referer' and `X-Title' headers the gateway credits requests
to, optional; and reasoningEffort: goes under `reasoning', which is the
gateway's one spelling for every service's reasoning control.  The
gateway sends comment lines in a stream to keep the connection warm, which
the OpenAI reader already ignores.
"
Class {
	#name : 'OpenRouter',
	#superclass : 'OpenAI',
	#instVars : [ 'fallbackModels', 'siteUrl', 'siteName' ],
	#category : 'LLM'
}

{ #category : 'defaults' }
OpenRouter class >> defaultUrl [
	^'https://openrouter.ai/api'
]

{ #category : 'configuration' }
OpenRouter >> fallbackModels: anArray [
	"Models to try in order when this one is unavailable; nil for none."
	fallbackModels := anArray
]

{ #category : 'configuration' }
OpenRouter >> siteUrl: aString [
	"Your application's URL, for the gateway's attribution; nil omits it."
	siteUrl := aString
]

{ #category : 'configuration' }
OpenRouter >> siteName: aString [
	siteName := aString
]

{ #category : 'accessing' }
OpenRouter >> fallbackModels [
	^fallbackModels
]

{ #category : 'accessing' }
OpenRouter >> siteUrl [
	^siteUrl
]

{ #category : 'accessing' }
OpenRouter >> siteName [
	^siteName
]

{ #category : 'the wire' }
OpenRouter >> headers [
	| all |
	all := super headers asOrderedCollection.
	(siteUrl notNil and: [siteUrl notEmpty]) ifTrue: [all add: 'HTTP-Referer' -> siteUrl].
	(siteName notNil and: [siteName notEmpty]) ifTrue: [all add: 'X-Title' -> siteName].
	^all asArray
]

{ #category : 'the wire' }
OpenRouter >> requestBodyFor: messages tools: toolsOrNil stream: aBoolean [
	| body |
	body := super requestBodyFor: messages tools: toolsOrNil stream: aBoolean.
	(fallbackModels notNil and: [fallbackModels notEmpty]) ifTrue: [
		body at: 'models' put: (JSONArray withAll: fallbackModels)].
	reasoningEffort notNil ifTrue: [
		body removeKey: 'reasoning_effort'.
		body at: 'reasoning' put: (JSONObject new at: 'effort' put: reasoningEffort; yourself)].
	^body
]
