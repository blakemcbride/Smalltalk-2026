"
Evaluating an expression without installing it.

1983's Compiler evaluates by compiling the text into a method named DoIt,
installing that in the receiver's class, sending it, and removing it.
That is one slot in one method dictionary shared by every evaluation in
the image, and with real workers evaluating at once it is a race in three
steps: eight workers evaluating `3 + 4' lost 138 of 800 answers, each one a
worker whose DoIt another had already removed, and a nil from the
doesNotUnderstand that followed.

The method is now run from the compiler's hand, with primitive 188 --
withArgs:executeMethod: -- and nothing shared is touched.  Everything else
is 1983's: the parse, the code generation, the cursor, the failBlock.
"
Extension { #name : 'Compiler' }

{ #category : '*Concurrency' }
Compiler >> evaluate: textOrStream in: aContext to: receiver notifying: aRequestor ifFail: failBlock [
	"Compile the text into a method and run it on receiver, in aContext if
	 there is one, so that the text can refer to receiver's instance
	 variables and aContext's temporaries.  aRequestor, if not nil, is sent
	 notify:at: before an attempt is abandoned; failBlock's value is then
	 answered."
	| methodNode method value |
	Cursor execute show.
	class := (aContext == nil ifTrue: [receiver] ifFalse: [aContext receiver]) class.
	self from: textOrStream class: class context: aContext notifying: aRequestor.
	methodNode := self translate: sourceStream noPattern: true ifFail: [^failBlock value].
	method := methodNode generate: #(0 0 0).
	value := aContext == nil
		ifTrue: [receiver withArgs: #() executeMethod: method]
		ifFalse: [receiver withArgs: (Array with: aContext) executeMethod: method].
	Cursor normal show.
	^value
]
