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
