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

MethodContext>>blockReturnTop is also here, for the simulator (see
ContextPart.extension.st beside this file).  The end of a block is
bytecode 125, and 1983 simulated it only in BlockContext, the one place a
block ran.  A closure runs in a MethodContext, so stepping off the end of
any block in the Debugger sent a message no MethodContext understood
(Bugs07 GUI-5).  A block's end returns its value to whoever sent it value
-- the sender, not the home method's sender -- as the interpreter's 125
does.

And restarting a BLOCK's frame ran the method instead (Bugs07 GUI-6).
With closures a block runs in a MethodContext of its own: method is the
home method, because the block's bytecodes are inline in it, and field
five -- receiverMap to 1983, closureOrNil to the VM -- holds the
BlockClosure.  restart read only method, so it put pc at the METHOD's first
bytecode and the stack pointer at the method's temporary count, and the
block's frame, resumed, ran the whole method from the top with the block's
arguments sitting where the method's temporaries belong: `log add:
#method' sent to the 1 that was the block's argument.  A closure's frame
starts where the interpreter's ST_activate_closure starts it -- pc at the
closure's startpc, the arguments and then the copied values as the first
temporaries, the stack pointer just above them -- so restart puts it back
there, re-storing the copied values from the closure (a block never writes
them; a temporary it writes lives in a vector instead).  Everything above
that the block's own prologue makes again: its temp vector and a nil for
each local.  Pharo's privRefresh is the same.

A method's frame is started again with its temporaries nil, as a new
activation has them (Bugs07 X-27).  1983 reset only pc and the stack
pointer, so whatever the first run had left in a temporary was still there
when the second began: a method that tests a temporary for nil before
setting it -- a lazy initialisation, a `found isNil ifTrue:' -- saw the
first run's value, and `restart' in the Debugger did not run the method
the method would run if it were called again.  The arguments are kept, as
they always were: they are the pattern being redone.  Pharo's privRefresh
does the same, and so does primitive 251, which restarts an on:do: frame
for an exception's retry.

restartWith: a new method cannot be done to a block's frame at all: the
new method's blocks need not start where the old one's did, or exist.  It
refuses; the Debugger, which is what sends it after an accept, reverts to
the home method's frame first (Debugger.extension.st beside this file).

tempNames names a frame's slots after what the C compiler put in them
(Bugs07 X-27).  1983's asks the image's own Encoder, which lays every
block's names into the home method's frame the Blue Book way, so the
Debugger's context inspector named a closure's frame with the HOME
METHOD's temporaries -- the block's argument under the method's first
temporary's name -- and in a method's frame the Array holding the
temporaries its blocks share was shown, and bound in the code pane, under
the name of the first temporary declared.  Nothing in a CompiledMethod
records which slot of which frame holds which name; the compiler that laid
the frames out does, and primitive 228 now hands that description back,
one line per scope.  So tempNames compiles the method's source again and,
when the bytes come out the same as the ones the frame runs, reads its own
scope's line: a closure's frame names its own arguments, the values it
copied and its own temporaries, and a vector's slot is named `[a c]' after
what it holds.  When the bytes differ, or there is no source, it is 1983's
answer, as before.

printOn: says `[] in' before a closure's frame, as 1983's BlockContext
printed itself: otherwise a block's frame and its method's print the same,
and a stack in the Debugger or a walkback shows the method twice with no
way to choose the block.
"
Extension { #name : 'MethodContext' }

{ #category : '*Kernel-Methods-Fixes' }
MethodContext >> restart [
	"Reinitialize the receiver so that it is in the state it was at its
	 creation: at its method's first bytecode with the arguments in
	 place and the temporaries nil or, for a closure's frame, at the
	 block's first bytecode with the block's arguments and copied values
	 in place.  See the comment above."
	| closure numArgs |
	closure := self closure.
	closure isNil ifTrue: [
		pc := method initialPC.
		stackp := method numTemps.
		method numArgs + 1 to: method numTemps do: [:i | self at: i put: nil].
		^self].
	numArgs := closure numArgs.
	pc := closure startpc.
	stackp := numArgs + closure basicSize.
	1 to: closure basicSize do: [:i |
		self at: numArgs + i put: (closure at: i)]
]

{ #category : '*Kernel-Methods-Fixes' }
MethodContext >> restartWith: aCompiledMethod [
	"Reinitialize the receiver as though it had been for a different
	 method.  Used by a Debugger when one of the methods to which it
	 refers is recompiled.  Refused for a closure's frame, whose block the
	 new method need not have; restart its home instead."
	self closure isNil ifFalse: [
		^self error: 'a block''s frame cannot be restarted with a new method; restart the method it was written in'].
	method := aCompiledMethod.
	^self restart
]

{ #category : '*Kernel-Methods-Fixes' }
MethodContext >> printOn: aStream [
	"A closure's frame prints as 1983's BlockContext printed: `[] in'
	 and then its method."
	self closure isNil ifFalse: [aStream nextPutAll: '[] in '].
	super printOn: aStream
]

{ #category : '*Kernel-Methods-Fixes' }
MethodContext >> blockReturnTop [
	"Simulate the end of a closure's body: return the top of the stack to
	 my sender, which is whoever sent the closure value."
	^self return: self pop to: sender
]

{ #category : '*Kernel-Methods-Fixes' }
MethodContext >> tempNames [
	"One name for each slot of my frame, in slot order, from the compiler
	 that laid the frame out: a closure's frame names its own arguments,
	 the values it copied and its own temporaries, and a slot holding the
	 Array of the temporaries a method shares with its blocks is named
	 after what it holds, `[a c]'.  A name met twice in one frame -- two
	 inlined blocks may each declare a `k' -- is told apart as `k(2)',
	 because a ContextInspector finds a slot by its name.  1983's when the
	 C compiler cannot say.  See the comment above."
	| words names |
	words := self frameSlotWords.
	words isNil ifTrue: [^super tempNames].
	names := OrderedCollection new.
	words do: [:word | | base label n |
		base := word collect: [:ch | ch = $, ifTrue: [$ ] ifFalse: [ch]].
		label := base.
		n := 1.
		[names includes: label] whileTrue: [
			n := n + 1.
			label := base , '(' , n printString , ')'].
		names add: label].
	^names
]

{ #category : '*Kernel-Methods-Fixes' }
MethodContext >> frameSlotWords [
	"What the C compiler says each slot of my frame holds -- a name, or
	 `[a,c]' for a vector -- or nil when that cannot be had for the bytes
	 this frame runs: no source, a source that does not compile to them,
	 the Blue Book dialect (CompiledMethod>>recompiledFrom:in:).  The
	 answer of primitive 228 has a line per scope, the method's first and
	 then each block's in the order their closures are made
	 (describe_frames in src/compiler/compiler.c), so a closure's line is
	 found by counting the closures made before its first bytecode."
	| source answer text lines start closure scope count scanner words |
	source := [self sourceCode] on: Error do: [:e | e return: nil].
	answer := method recompiledFrom: source in: self mclass.
	answer isNil ifTrue: [^nil].
	text := answer at: 7.
	lines := OrderedCollection new.
	start := 1.
	1 to: text size do: [:i |
		(text at: i) = Character lf ifTrue: [
			lines add: (text copyFrom: start to: i - 1).
			start := i + 1]].
	lines add: (text copyFrom: start to: text size).
	closure := self closure.
	scope := 0.
	count := 0.
	scanner := InstructionStream new method: method pc: method initialPC.
	scanner scanFor: [:byte |
		byte = 143 ifTrue: [
			count := count + 1.
			(closure notNil and: [scanner pc < closure startpc])
				ifTrue: [scope := scope + 1]].
		false].
	lines size = (count + 1) ifFalse: [^nil].
	words := (lines at: scope + 1) substrings.
	closure isNil
		ifTrue: [words size = method numTemps ifFalse: [^nil]]
		ifFalse: [words size < (closure numArgs + closure basicSize) ifTrue: [^nil]].
	^words
]
