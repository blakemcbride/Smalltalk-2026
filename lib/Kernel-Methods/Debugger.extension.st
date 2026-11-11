"
The Debugger, told a block's frame from its method's.

1983's Debugger knew a block by its class: a frame that was not a
MethodContext was a BlockContext, and before `restart' or an accepted edit
it offered to revert to the block's home, because a BlockContext could not
be started over and a new method could not be put into one.  With closures
a block runs in a MethodContext of its own, so that test let every block's
frame through as a method's, and both operations then ran the HOME
METHOD's bytecodes inside the block's frame (Bugs07 GUI-6): `restart' on
the block's frame in a do-it of `#(1 2 3) collect: [:x | x zork]' ran the
whole do-it again inside itself, and in a real method the method's first
statements ran with the block's arguments where its temporaries belong.

A closure's frame is now told by the closure it holds, and the two
operations part company.

`restart' restarts the selected frame, block or method:
MethodContext>>restart (beside this file) starts a closure's frame again
at the block's first bytecode, so there is nothing to revert.  Only a
BlockContext -- code from 1983's own code generator -- still goes back to
its home.

An accepted edit still has to go to the home, for the reason 1983 gave:
the new method's blocks need not lie where the old one's did, so the frame
that takes the new method is the method's, and the frames above it go.  A
closure's home is found by following outerContext out to the frame with
no closure, and has to be on the stack below the block: a block that has
outlived its method -- a sort block, the bottom of a forked process -- has
no frame of the method left to restart, and the edit is refused with a
notifier rather than put into some other process's frame or a returned
one.

The message a frame is at is selected in its source from a map the C
compiler makes (Bugs07 X-26).  1983's Debugger parsed the source with the
image's own compiler and asked it which pc each message ends at -- but
that compiler lays a method out the Blue Book way, a block as a
blockCopy: send with its body inline and no vectors, and the method that
runs was laid out by the C compiler, which makes a closure in four bytes
and its vectors in a prologue.  Past the first block every pc meant
something else to the two, and the selection landed on the wrong message:
a frame suspended in `Processor activeProcess suspend' inside an
inject:into: block showed `+ e' selected, and the method's frame, waiting
on inject:into:, showed `suspend'.  computeSourceMap now asks the compiler
that made the bytes, through primitive 228, for the end of each send and
each ^ and the characters it came from, and believes the answer only for
a source that compiles to the method's own bytes; otherwise nothing is
selected.  pcRange reads it by its own convention: a frame waiting on a
send is at the send that ends at its pc, and an interrupted top frame,
which has not yet run the instruction at its pc, is at the first send
that ends after it.  1983's also looked its answer up with an index from
indexForInserting:, which counts the SortedCollection's storage, in at:,
which counts its elements -- right only while nothing had been removed
from the front.

`step' goes on from the frame ContextPart>>completeCallee: says the
simulation reached.  Stepping over a send whose callee raised an exception
handled below the selected frame -- or retried, or returned from a block
whose home lies below -- unwinds past that frame; 1983's Debugger then
stepped the frame anyway and went on showing it, a frame that was no
longer on the process's stack, and `proceed' resumed the process there
(Bugs07 X-26).
"
Extension { #name : 'Debugger' }

{ #category : '*Kernel-Methods-Fixes' }
Debugger >> restart [
	"Proceed from the initial state of the currently selected context.
	 The argument is a controller on a view of the receiver.  That view
	 is closed.

	 1983's, except that only a BlockContext is reverted to its home: a
	 closure's frame restarts at its block.  See the comment above."
	self checkContextSelection.
	(context isKindOf: MethodContext)
		ifFalse: [self revertBlock ifFalse: [^self]].
	context restart.
	self resumeProcess
]

{ #category : '*Kernel-Methods-Fixes' }
Debugger >> revertBlock [
	"If the selected context is a block, then revert to its home: the
	 home of a BlockContext, or the frame of the method a closure was
	 written in, which must still be on the stack below it.  Answer
	 whether the selected context is now a method's.  See the comment
	 above."
	| home |
	(context isKindOf: MethodContext)
		ifTrue: [
			context closure isNil ifTrue: [^true].
			home := context homeContext.
			(context hasSender: home) ifFalse: [
				self notify: 'The method this block was written in has already returned,
so there is no frame of it to restart with the new code.
Accept the change in a browser instead.'.
				^false]]
		ifFalse: [home := context home].
	(self confirm:
'I will have to revert to the method from
which this block originated.  Is that OK?')
		ifTrue: [self context: home. ^true]
		ifFalse: [^false]
]

{ #category : '*Kernel-Methods-Fixes' }
Debugger >> computeSourceMap [
	"The map from the selected method's pcs to the characters of its
	 sends and returns, from the compiler that made its bytes: an Array of
	 triples, the index just past the instruction and the first and last
	 character, in the order of the bytes.  Empty when the source shown
	 does not compile to the method's bytes.  See the comment above."
	| answer words map base |
	sourceMap := Array new: 0.
	context isNil ifTrue: [^self].
	answer := context method recompiledFrom: sourceCode in: self selectedClass.
	answer isNil ifTrue: [^self].
	words := (answer at: 8) substrings.
	(words isEmpty or: [words size \\ 3 ~= 0]) ifTrue: [^self].
	base := context method initialPC.
	map := Array new: words size.
	1 to: words size by: 3 do: [:i |
		map at: i put: base + (words at: i) asNumber.
		map at: i + 1 put: (words at: i + 1) asNumber.
		map at: i + 2 put: (words at: i + 2) asNumber].
	sourceMap := map
]

{ #category : '*Kernel-Methods-Fixes' }
Debugger >> pcRange [
	"The characters of the send or return the selected frame is at: the
	 one of its own that ends at its pc, or for an interrupted top frame
	 the first of its own that ends after it; past the last, an empty
	 selection after everything.  Its own: a method's frame never runs
	 the bodies of its blocks, which lie inline among its bytes, and a
	 block's frame runs only its body and not those of the blocks inside
	 it, so the next send in the bytes is not always the frame's next.
	 See the comment above."
	| pc method bodies scanner closure mine innermost end |
	(HighlightPC and: [context ~~ nil]) ifFalse: [^1 to: 0].
	(sourceMap isNil or: [sourceMap isEmpty]) ifTrue: [^1 to: 0].
	pc := context pc.
	pc isNil ifTrue: [^1 to: 0].
	(context == processHandle topContext and: [processHandle interrupted])
		ifTrue: [pc := pc + 1].
	method := context method.
	bodies := OrderedCollection new.
	scanner := InstructionStream new method: method pc: method initialPC.
	scanner scanFor: [:byte | | at |
		byte = 143 ifTrue: [
			at := scanner pc.
			bodies add: (at + 4
				to: at + 3 + ((method at: at + 2) * 256) + (method at: at + 3))].
		false].
	closure := context closure.
	mine := closure isNil
		ifTrue: [nil]
		ifFalse: [bodies detect: [:b | b first = closure startpc] ifNone: [nil]].
	innermost := [:index | | best |
		best := nil.
		bodies do: [:b |
			((index between: b first and: b last)
				and: [best isNil or: [b first > best first]])
					ifTrue: [best := b]].
		best].
	1 to: sourceMap size by: 3 do: [:i |
		((sourceMap at: i) >= pc
			and: [(innermost value: (sourceMap at: i) - 1) == mine])
				ifTrue: [^(sourceMap at: i + 1) to: (sourceMap at: i + 2)]].
	end := 0.
	3 to: sourceMap size by: 3 do: [:i | end := end max: (sourceMap at: i)].
	^end + 1 to: end
]

{ #category : '*Kernel-Methods-Fixes' }
Debugger >> step [
	"1983's, going on from the frame the simulation reached when the step
	 unwound past the selected one.  See the comment above."
	| currentContext reached |
	self checkContextSelection.
	processHandle interrupted ifFalse: [processHandle topContext push: processHandle proceedValue].
	processHandle interrupted: true. "simulation leaves same state as interrupting"
	context == processHandle topContext
		ifTrue:
			[currentContext := context.
			currentContext stepToSendOrReturn.
			currentContext willReturn
				ifTrue:
					[currentContext := currentContext step.
					currentContext stepToSendOrReturn.
					self resetContext: currentContext]
				ifFalse:
					[reached := currentContext completeCallee: currentContext step.
					reached == currentContext
						ifTrue: [self changed: #pc. self updateInspectors]
						ifFalse: [self reachedBy: reached]]]
		ifFalse:
			["Have to complete any stuff called from here"
			reached := context completeCallee: processHandle topContext.
			reached == context
				ifTrue: [self resetContext: context]
				ifFalse: [self reachedBy: reached]]
]

{ #category : '*Kernel-Methods-Fixes' }
Debugger >> reachedBy: aContextOrNil [
	"The simulation of a step ended in aContext, below the frame that was
	 stepped, or (nil) returned past the bottom of the process.  Show the
	 one, or say the other."
	aContextOrNil isNil ifTrue: [
		^self notify: 'The process has run to its end.'].
	aContextOrNil stepToSendOrReturn.
	self resetContext: aContextOrNil
]
