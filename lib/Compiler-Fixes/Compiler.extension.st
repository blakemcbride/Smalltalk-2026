Extension { #name : 'Compiler' }

{ #category : '*Compiler-Fixes' }
Compiler >> notify: aString at: position [
	"Report a syntax error at that character position.

	 With a requestor -- a Browser's code pane, a Workspace -- the complaint
	 goes into the text where it happened and the person fixes it.  That
	 arm is 1983's and is unchanged.

	 Without one, 1983 opened a StandardSystemView and then did
	 `Processor activeProcess suspend', which is fatal headless: there is no
	 window to dismiss, so the process never resumes.  `Compiler evaluate:
	 ''#[1 2 3]''' printed `every process is blocked; nothing can run' and
	 the image exited, and `on: Error do:' around it changed nothing because
	 nothing was signalled.  That is one typo taking down a server that
	 reloads a service file, and one bad doIt taking down every other
	 worker's work with it.

	 It signals now.  See SyntaxErrorNotification for the whole argument;
	 the short version is that an unhandled Error still opens the same
	 notifier when there is a screen for it, and a handled one behaves like
	 every other error in the system.  Either way Parser>>notify:at: then
	 runs `self abort', which is the failBlock, so the compile answers what
	 the caller's ifFail: said it should."

	Cursor normal show.
	requestor == nil
		ifFalse: [^requestor insertAndSelect: aString at: (position max: 1)].
	^SyntaxErrorNotification new
		errorPosition: position;
		errorCode: (self sourceWithNotification: aString at: position);
		signal: aString
]

{ #category : '*Compiler-Fixes' }
Compiler >> sourceWithNotification: aString at: position [
	"The source being compiled with the complaint spliced in at position.

	 What the 1983 view displayed, kept because a handler that logs a failed
	 compile wants to see where.  The bounds are checked here and were not
	 there: a complaint at the very end of the text -- which `offEnd:' is
	 for -- indexes one past the last character, and copyReplaceFrom:to:with:
	 raises rather than appending."
	| source |
	source := sourceStream == nil ifTrue: [''] ifFalse: [sourceStream contents].
	(position between: 1 and: source size + 1) ifFalse: [^source].
	^source copyReplaceFrom: position to: position - 1 with: aString
]

{ #category : '*Compiler-Fixes' }
Compiler class >> primCompile: source class: aBehavior instVarNames: names classAssociation: anAssociation noPattern: aBoolean [
	"Compile source with the C compiler -- the one that built this image.

	 Answers an Array of six: the CompiledMethod or nil, its selector, the
	 error message, the line, the character position, and the name of the
	 undeclared variable if that is what went wrong.  It does not fail for
	 a compile error, because a compile error is an ordinary outcome; it
	 fails only if the source is not a String.

	 THE SYMBOL LOCK MUST BE HELD.  compileSource:for:noPattern: below is
	 the way in."

	<primitive: 228>
	^self primitiveFailed
]

{ #category : '*Compiler-Fixes' }
Compiler class >> compileSource: code for: aBehavior noPattern: aBoolean [
	"With nobody to ask about an undeclared name, so one is declared."
	^self
		compileSource: code
		for: aBehavior
		noPattern: aBoolean
		declaringUndeclared: true
]

{ #category : '*Compiler-Fixes' }
Compiler class >> compileSource: code for: aBehavior noPattern: aBoolean declaringUndeclared: mayDeclare [
	"The C compiler, with everything it needs from the image and the Symbol
	 lock held.

	 The instance variable names come from allInstVarNames rather than
	 being gathered in C, so what is in scope has ONE definition -- the
	 image's -- and the two sides cannot come to differ about it.

	 UNDECLARED NAMES are the one place this has to do more than call, and
	 the caller decides which way.  1983's Encoder puts the question to the
	 requestor's EDITOR when there is one -- which is how TonelReader
	 refuses a name a file never declares, by name -- and with no editor it
	 puts the name in Undeclared bound to nil and carries on, which is what
	 makes a forward reference compile.  The C compiler reports it and
	 stops either way, so mayDeclare says which of the two this caller
	 wants: false hands the refusal back for reportFailure:of:to: to give
	 the requestor, true declares the name and compiles again.

	 The retry loop is bounded by the number of distinct names, and a name
	 already declared ends it -- otherwise a variable that cannot be
	 declared for some other reason would spin."

	| source answer name declared |
	source := self sourceTextOf: code.
	declared := Set new.
	[true] whileTrue: [
		answer := LibraryLocks holdingSymbols: [
			self
				primCompile: source
				class: aBehavior
				instVarNames: (aBehavior isNil
					ifTrue: [Array new: 0]
					ifFalse: [aBehavior allInstVarNames asArray])
				classAssociation: (aBehavior isNil
					ifTrue: [nil]
					ifFalse: [self associationFor: aBehavior])
				noPattern: aBoolean].
		(answer at: 1) notNil ifTrue: [^answer].
		name := answer at: 6.
		(mayDeclare not or: [name isNil or: [declared includes: name]])
			ifTrue: [^answer].
		declared add: name.
		Transcript show: ' (' , name , ' is Undeclared) '.
		Undeclared at: name asSymbol put: nil]
]

{ #category : '*Compiler-Fixes' }
Compiler class >> associationFor: aBehavior [
	"An Association whose value is that class, for a super send.

	 Encoder>>associationFor: word for word: a super send reads its lookup
	 class from the method's last literal, and a metaclass has no name in
	 Smalltalk, so it gets a keyless Association.  All the interpreter reads
	 is the value."

	| name |
	name := Smalltalk keyAtValue: aBehavior ifAbsent: [nil].
	name isNil ifTrue: [^Association new value: aBehavior].
	^Smalltalk associationAt: name
]

{ #category : '*Compiler-Fixes' }
Compiler class >> put: bytes intoTrailerOf: aMethod [
	"The three bytes at the end of a CompiledMethod that say where its
	 source is.

	 MethodNode>>generate: wrote these, and the C compiler leaves room for
	 them and zeroes them, so this is the same three stores at the same
	 three places."

	bytes isNil ifTrue: [^aMethod].
	1 to: 3 do: [:i | aMethod at: aMethod size - 3 + i put: (bytes at: i)].
	^aMethod
]

{ #category : '*Compiler-Fixes' }
Compiler class >> reportFailure: answer of: code to: requestor [
	"Tell whoever asked that the compile failed, where Compiler>>notify:at:
	 would have.

	 The same two arms, because the requestor protocol has to stay what it
	 was: an editor is sent insertAndSelect:at: and puts the complaint into
	 the text at that position -- TonelReader implements it too -- and with
	 nobody to tell, a SyntaxErrorNotification is signalled.  What is new is
	 that the position is a real one: the C compiler answers the character
	 the token started at, so the selection lands on the offending token
	 rather than at the start of the method."

	| message position source |
	message := answer at: 3.
	position := (answer at: 5) max: 1.
	Cursor normal show.
	requestor isNil ifFalse: [^requestor insertAndSelect: message at: position].
	source := self sourceTextOf: code.
	^SyntaxErrorNotification new
		errorPosition: position;
		errorCode: ((position between: 1 and: source size + 1)
			ifTrue: [source copyReplaceFrom: position to: position - 1 with: message]
			ifFalse: [source]);
		signal: message
]

{ #category : '*Compiler-Fixes' }
Compiler class >> sourceTextOf: textOrStream [
	"The text, whatever it arrived as.

	 compile: and evaluate: take a String, a Text, or a stream on either,
	 because 1983's from:class:context:notifying: took all three."
	textOrStream isString ifTrue: [^textOrStream].
	(textOrStream respondsTo: #contents)
		ifTrue: [^textOrStream contents asString].
	^textOrStream asString
]

{ #category : '*Compiler-Fixes' }
Compiler >> evaluate: textOrStream in: aContext to: receiver notifying: aRequestor ifFail: failBlock [
	"Compile the text as a doIt and run it on receiver.

	 The C compiler, for the same reason Behavior>>compile:notifying:
	 trailer:ifFail: uses it: a doIt evaluated in a workspace should mean
	 what the same text means in a method, and it did not -- blocks were
	 Chapter 27's, so `((1 to: 3) collect: [:i | [i]]) collect: [:b | b
	 value]' answered (3 3 3) here and (1 2 3) in a compiled method.

	 A doIt is a method BODY: temporaries, several statements, and the last
	 one's value as the answer.  That is what noPattern: says, and it is
	 what compile_statements already meant by `inside a block'.

	 THE DEBUGGER'S CASE IS NOT ROUTED and cannot be.  Evaluating in a
	 CONTEXT means the text can name that context's temporaries, which
	 1983's Encoder arranges by binding homeContext and mapping each name to
	 a tempAt: send.  The C compiler has no notion of a context, so when
	 aContext is not nil this falls back to the compiler that does.  The
	 fallback is the 1983 one in every respect, blocks included, which is
	 the honest cost of the exception and is written down rather than
	 discovered.

	 Running the method with primitive 188 rather than installing it under
	 #DoIt is lib/Concurrency's fix and is kept: one slot in one method
	 dictionary shared by every evaluation in the image is a race in three
	 steps."

	| answer method |
	aContext == nil ifFalse: [
		^super
			evaluate: textOrStream
			in: aContext
			to: receiver
			notifying: aRequestor
			ifFail: failBlock].
	Cursor execute show.
	class := receiver class.
	answer := Compiler
		compileSource: (Compiler sourceTextOf: textOrStream)
		for: class
		noPattern: true
		declaringUndeclared: aRequestor isNil.
	method := answer at: 1.
	method isNil ifTrue: [
		Compiler reportFailure: answer of: textOrStream to: aRequestor.
		^failBlock value].
	Cursor normal show.
	^receiver withArgs: #() executeMethod: method
]
