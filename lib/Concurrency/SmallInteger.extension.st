"
SmallInteger>>printOn:base: with its digits on the stack, not in a class
variable.

1983's version writes each digit into Digitbuffer -- one Array of 32, a
class variable, shared by every SmallInteger there is -- and then reads
them back out in reverse.  That is correct when `concurrent' means green
processes interleaved by one interpreter that never preempts a process by
another of the same priority, which is what the Blue Book promises and
what doc/CONCURRENCY.md deliberately breaks.  Here, eight workers printing
integers at once all write into the same thirty-two slots and each reads
back whichever digits landed last.

What that looked like, because it looked like nothing to do with printing:
`i printString = i printString' on eight workers was true 1,501 times in
16,000; a JSON document written while other workers were writing their own
came back with ""38"" where ""30"" had been sent and 100 as 00; and the
diagnostics printed to explain it were themselves wrong, since they
printed integers to say so.  It was found by ruling everything else out --
the object table never handed an index out twice, no worker ever read a
freed object, ThreadSanitizer saw nothing, because a class variable read
and written by Smalltalk code is not a race any C tool can see.

Sixty-four slots rather than thirty-two.  The 1983 buffer was `big enough
for 32-bit systems' and this system's SmallIntegers are 63 bits, so
`(2 raisedTo: 60) printStringRadix: 2' needed sixty-one and indexed past
the end -- a second fault in the same line, and one that did not need a
second worker to reach.

Kept as close to the 1983 method as the change allows, so that the two can
be read side by side: the same loop, the same digit order, the buffer
merely made a temporary.  A String rather than an Array, holding the
Characters themselves, because that is what is written out and it saves
looking each one up twice.
"
Extension { #name : 'SmallInteger' }

{ #category : '*Concurrency' }
SmallInteger >> printOn: aStream base: b [
	| digits i x |
	(x := self) < 0 ifTrue: [
		aStream nextPut: $-.
		x := self negated].
	digits := String new: 64.
	i := 0.
	[x >= b] whileTrue: [
		digits at: (i := i + 1) put: (Character digitValue: x \\ b).
		x := x // b].
	digits at: (i := i + 1) put: (Character digitValue: x).
	[i > 0] whileTrue: [
		aStream nextPut: (digits at: i).
		i := i - 1]
]
