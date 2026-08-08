"
Restarting a frame counted its arguments twice.

MethodContext>>restart resets a frame so the Debugger can run it again --
it is what the debugger's 'restart' button does, and what
restartWith: does after a method under debug is recompiled.  It set

	stackp := method numArgs + method numTemps

and numTemps ALREADY counts the arguments.  BlockClosure>>on:do: takes two
arguments and declares one temporary, and answers 3 for numTemps, not 1.
So a restarted frame came back believing it had two more values below its
stack than it did, and the method then read whatever was in those slots.

The class contradicts itself about this, which is what makes it findable:
setSender:receiver:method:arguments: -- twenty lines further down, and the
method that CREATES a context -- says plainly

	stackp := method numTemps

and that one agrees with the interpreter, which sets a new frame's stack
pointer to the header's temporary count and nothing else.
"
Extension { #name : 'MethodContext' }

{ #category : '*Kernel-Methods-Fixes' }
MethodContext >> restart [
	"Reinitialize the receiver so that it is in the state it was at its
	 creation."
	pc := method initialPC.
	stackp := method numTemps
]
