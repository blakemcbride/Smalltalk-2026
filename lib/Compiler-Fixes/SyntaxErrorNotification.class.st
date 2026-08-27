"
The image's compiler could not read the text, said as an Error.

1983 says it with a WINDOW.  Compiler>>notify:at: with no requestor called
SyntaxError class>>errorInClass:withCode:errorString:, which opens a
StandardSystemView on the offending text and then does

	Processor activeProcess suspend

so that the person reading it can fix the code and carry on.  That is a
good design for a machine with one process and a screen, and it is fatal
without one: headless there is no window to dismiss, so the process is
suspended for good.  Under -eval that costs the doIt; under -serve the VM
reports `every process is blocked; nothing can run' and the image exits.
`on: Error do:' caught neither, because nothing was ever signalled.

`Compiler evaluate:' is the documented workspace entry point, and any path
that compiles text it did not write -- a Tonel service reloaded on a
running REST server, a doIt arriving over JSON-RPC -- reached it on the
first typo.  So the compiler now signals this, an ordinary Error like any
other: a handler can catch it, and with no handler Error>>defaultAction
reports it, unwinds, and lets the Parser's own failBlock answer.

The window is not lost.  An unhandled Error opens the 1983 notifier when
there is a screen to open it on, which is the same decision
lib/Kernel-Exceptions/Error.class.st already argues for every other error
in the system.

I carry the position the compiler complained at and the text with the
complaint spliced in at that point -- which is what the 1983 view showed --
because a handler logging a failed compile wants both.
"
Class {
	#name : 'SyntaxErrorNotification',
	#superclass : 'Error',
	#instVars : [ 'errorCode', 'errorPosition' ],
	#category : 'Compiler-Fixes'
}

{ #category : 'accessing' }
SyntaxErrorNotification >> errorCode [
	"The source, with the complaint spliced in where it was raised."
	^errorCode
]

{ #category : 'accessing' }
SyntaxErrorNotification >> errorCode: aString [
	errorCode := aString
]

{ #category : 'accessing' }
SyntaxErrorNotification >> errorPosition [
	"Where in the source the complaint was raised, one-based."
	^errorPosition
]

{ #category : 'accessing' }
SyntaxErrorNotification >> errorPosition: anInteger [
	errorPosition := anInteger
]
