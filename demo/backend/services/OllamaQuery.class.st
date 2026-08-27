"
The Ollama screen's service: is the local Ollama up, what models has it,
and what does one of them say.  Kiss's OllamaQuery.groovy.

The screen keeps the conversation itself and sends the whole of it back
as the prompt each time, so this side is stateless, as every service here
is.  A model can take a long time; the wait parks this request's process
and no worker, and is bounded by `OllamaTimeoutSeconds' in server.json
when it is there -- Kiss reads the same name from its ini -- and by five
minutes otherwise.  `OllamaUrl' in server.json points at a server other
than http://localhost:11434.

Kiss's servlet.setTimeout(0) has no counterpart: this server does not
time out a request that is being served, only a connection that is idle
between requests.
"
Class {
	#name : 'OllamaQuery',
	#superclass : 'RestService',
	#category : 'Web-Demo'
}

{ #category : 'private' }
OllamaQuery >> ollama [
	| ollama timeout serverUrl |
	ollama := Ollama new.
	serverUrl := request environmentAt: 'OllamaUrl'.
	serverUrl notNil ifTrue: [ollama url: serverUrl].
	timeout := request environmentAt: 'OllamaTimeoutSeconds'.
	(timeout isKindOf: Integer) ifTrue: [ollama timeoutSeconds: timeout].
	^ollama
]

{ #category : 'services' }
OllamaQuery >> isOllamaUp [
	outjson at: 'isOllamaUp' put: self ollama isUp
]

{ #category : 'services' }
OllamaQuery >> listModels [
	outjson at: 'models' put: (JSONArray withAll: self ollama models)
]

{ #category : 'services' }
OllamaQuery >> ask [
	| answer |
	answer := (self ollama model: (injson stringAt: 'model'); yourself) send: (injson stringAt: 'prompt').
	outjson at: 'textResponse' put: answer;
		at: 'htmlResponse' put: (Ollama toHtml: answer)
]
