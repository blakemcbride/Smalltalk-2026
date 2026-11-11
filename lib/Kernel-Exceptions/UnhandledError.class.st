"
An Error that no handler took, offered once more before it is reported.

Error>>defaultAction signals one of these, carrying the error, before it
does anything else, so a handler for UnhandledError takes exactly the
errors that no other handler takes: an outer `on: Error do:' still sees an
error first, and only what would otherwise have been reported reaches
here.  Pharo's contract, and Pharo code is written against it -- its
Announcer delivers each announcement inside

    [action cull: anAnnouncement cull: announcer]
        on: UnhandledError fork: [:ex | ex pass]

so that a subscriber that fails is reported from a process of its own and
the subscribers after it are still told.  Without this class that guard
was nil and isolated nothing (Bugs07 KERNB-7).

Not resumable, as in Pharo: the error it carries is the thing that failed,
and there is no value to carry on with from here.  A handler returns from
its on:do:, or passes, and an UnhandledError nobody takes is the error
unhandled after all -- its default action is the Error's own report.
"
Class {
	#name : 'UnhandledError',
	#superclass : 'Exception',
	#instVars : [ 'exception' ],
	#category : 'Kernel-Exceptions'
}

{ #category : 'signalling' }
UnhandledError class >> signalForException: anError [
	"Pharo's way to raise one."
	^self new exception: anError; signal
]

{ #category : 'accessing' }
UnhandledError >> exception [
	"The error that nobody handled."
	^exception
]

{ #category : 'accessing' }
UnhandledError >> exception: anError [
	exception := anError
]

{ #category : 'accessing' }
UnhandledError >> description [
	^exception isNil
		ifTrue: ['An unhandled error']
		ifFalse: [exception messageText]
]

{ #category : 'handling' }
UnhandledError >> defaultAction [
	"Nobody took me, or the handler that did passed me on: the error is
	 unhandled, and gets what any unhandled Error gets.

	 Unless it was signalled on another stack.  BlockClosure>>on:fork:
	 runs its handler on a new process with a copy of me that names no
	 frame, and `ex pass' there -- what the Announcer's guard does -- finds
	 nothing further out and comes here.  The error's own frames belong to
	 the process that raised it, which has gone on by now: a debugger
	 opened on them, or a walk that unwound them, would be working on
	 another process's stack while it runs.  So on another stack the error
	 is counted and reported, protected the way unhandledErrorDefaultAction
	 protects its report, and that is all; the process the handler runs on
	 ends when the handler does."
	| ctx |
	exception isNil ifTrue: [
		^self reportOnStandardError:
			'UnhandledError: signalled with no error, and nobody took it'].
	"Pharo raises one for an Error only; anything else carried here has
	 its own default action, and the Error-only messages below would be
	 a doesNotUnderstand inside a default action."
	(exception isKindOf: Error) ifFalse: [^exception defaultAction].
	ctx := thisContext.
	[ctx isNil or: [ctx == exception signalContext]]
		whileFalse: [ctx := ctx sender].
	ctx isNil ifFalse: [^exception unhandledErrorDefaultAction].
	exception noteUnhandled.
	[exception reportAsText]
		on: Error
		do: [:inner | exception reportOnStandardError:
				'an error was raised that could not describe itself'].
	^nil
]
