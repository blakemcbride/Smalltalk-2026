"
TLS against real certificates, which only the internet has: a service
whose certificate the system's store vouches for, read whole and read as
it comes; and three from badssl.com that must be refused -- a certificate
for another name, an expired one, a self-signed one -- each with OpenSSL's
reason in the NetError.  Separate from st2026 because a machine building
this system cannot be assumed to be on the internet, and a suite that
cannot run where it is checked would report a perfect score for ever.

    ./st80 -bootstrap -profile profiles/internet-live.profile -tests
"
Class {
	#name : 'HttpClientLiveTest',
	#superclass : 'TestCase',
	#category : 'HTTP-Client-Live-Tests'
}

{ #category : 'private' }
HttpClientLiveTest >> refusal: aUrl [
	"The NetError an https URL raises, or nil when it did not."
	^[HttpClient new readTimeout: 30000; get: aUrl. nil] on: NetError do: [:e | e]
]

{ #category : 'tests' }
HttpClientLiveTest >> testThisBuildHasTls [
	self assert: Socket isTlsAvailable
]

{ #category : 'tests' }
HttpClientLiveTest >> testAnHttpsReplyIsRead [
	"Anthropic's models endpoint without a key: a 401 with a JSON body,
	 which is a reply and not an error, over a certificate the store
	 vouches for."
	| reply |
	reply := HttpClient new readTimeout: 30000; get: 'https://api.anthropic.com/v1/models'.
	self assert: reply status equals: 401.
	self assert: ((reply headerAt: 'content-type') includesSubstring: 'json').
	self assert: ((reply json objectAt: 'error') stringAt: 'type') equals: 'authentication_error'
]

{ #category : 'tests' }
HttpClientLiveTest >> testAnHttpsReplyIsStreamed [
	| lines |
	lines := HttpClient new readTimeout: 30000; get: 'https://api.anthropic.com/v1/models' do: [:reply | | got |
		got := OrderedCollection new.
		self assert: reply status equals: 401.
		reply bodyStream linesDo: [:line | got add: line].
		got].
	self assert: lines notEmpty.
	self assert: ((lines inject: '' into: [:acc :each | acc, each]) includesSubstring: 'authentication_error')
]

{ #category : 'tests' }
HttpClientLiveTest >> testAnHttpsPage [
	| reply |
	reply := HttpClient new readTimeout: 30000; get: 'https://www.google.com/'.
	self assert: reply status < 400.
	self assert: reply body size > 100
]

{ #category : 'tests' }
HttpClientLiveTest >> testACertificateForAnotherNameIsRefused [
	| error |
	error := self refusal: 'https://wrong.host.badssl.com/'.
	self assert: error notNil.
	self assert: (error messageText includesSubstring: 'TLS').
	self assert: (error messageText asLowercase includesSubstring: 'hostname mismatch')
]

{ #category : 'tests' }
HttpClientLiveTest >> testAnExpiredCertificateIsRefused [
	| error |
	error := self refusal: 'https://expired.badssl.com/'.
	self assert: error notNil.
	self assert: (error messageText asLowercase includesSubstring: 'expired')
]

{ #category : 'tests' }
HttpClientLiveTest >> testASelfSignedCertificateIsRefused [
	| error |
	error := self refusal: 'https://self-signed.badssl.com/'.
	self assert: error notNil.
	self assert: (error messageText asLowercase includesSubstring: 'self')
]
