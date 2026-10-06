"
What line ending this host writes, and a file opened to write it; and the
end of a file, met the way a ReadStream meets the end of its collection.

A FileStream and a ReadStream disagreed about everything past the end
(Bugs3 B51): next: past it raised where the other answered short, skip:
backwards raised where the other clamped, and position: past it was taken
by one and refused by the other.  Each of the three is answered below in
the ReadStream's way, with one deliberate difference kept for a stream
that can write: positioning a writer past its end is how a file grows.

And a stream that was only read leaves its file as it found it.  1983's
writing gave a stream with no mode the default readWriteShorten the first
time anybody asked, and close asks; so once Shorten was made a real bit (see
File class>>initialize), opening an old file, reading part of it and closing
it cut the file off at the read position (Bugs5 FILES-1).  writing and close
are answered below so that only a write sets the mode, which nextPut: and
nextPutAll: already do for themselves.
"
Extension { #name : 'FileStream' }

{ #category : '*Files-Fixes' }
FileStream class >> nativeLineEnd [
	"The line ending native to the machine this is running on: a line feed
	 everywhere except Windows, which takes a carriage return and a line
	 feed.

	 A primitive because the image has no other way to find out, and asked
	 at RUN time rather than baked in at bootstrap, so that an image built
	 on one host and opened on another answers for the host it is on."

	<primitive: 254>
	^String with: (Character value: 10)
]

{ #category : '*Files-Fixes' }
FileStream >> next: anInteger [
	"The next anInteger characters, or as many as are left.

	 1983's own loop, with its end handled as a ReadStream handles it.
	 The original allocated the answer at full size and, on reaching the
	 end of the file, filled the rest with `self next' -- which is nil
	 past the end -- so asking a three-byte file for ten characters failed
	 inside at:put: with `Strings only store Characters', where
	 `(ReadStream on: 'abc') next: 10' answers 'abc' (Bugs3 B51).  A short
	 answer is one every caller can check; the error was one none could.

	 The rest is kept as written because it is the fast path: whole runs
	 are moved with replaceFrom:to:with:startingAt:, and the page is
	 turned by atEnd, which reads the next one when this one is used up."
	| newCollection howManyRead increment |
	newCollection := collection species new: anInteger.
	howManyRead := 0.
	[howManyRead < anInteger] whileTrue: [
		self atEnd ifTrue: [^newCollection copyFrom: 1 to: howManyRead].
		increment := (readLimit - position) min: (anInteger - howManyRead).
		newCollection
			replaceFrom: howManyRead + 1
			to: (howManyRead := howManyRead + increment)
			with: collection
			startingAt: position + 1.
		position := position + increment].
	^newCollection
]

{ #category : '*Files-Fixes' }
FileStream >> skip: anInteger [
	"Move anInteger characters along, clamped at the beginning -- and at
	 the end too when I am read-only.

	 1983's went through positionPage:character:, which for a target
	 before the beginning asked for page zero and raised `cannot skip -5'
	 where a ReadStream clamps to 0 (lib/Streams-Protocol); a parser that
	 steps back over what it has looked at should not have to know which
	 kind of stream it was handed (Bugs3 B51).  Forward past the end is
	 clamped only when I cannot write: for a writer, a position past the
	 end is how a file is extended, and 1983 means it -- see
	 File>>readOrAdd:."
	| target |
	anInteger = 0 ifTrue: [^self].
	target := (self position + anInteger) max: 0.
	self isReadOnly ifTrue: [target := target min: self size].
	^self position: target
]

{ #category : '*Files-Fixes' }
FileStream >> position: anInteger [
	"Set my position, refusing one before the beginning, and one past the
	 end when I am read-only.

	 1983 accepted both and failed later, in the page arithmetic, with
	 `cannot read page 0' or `cannot read page 1' -- the page's number,
	 which the caller never mentioned.  A ReadStream refuses with
	 positionError, and this is the same refusal (Bugs3 B51).  A writer
	 is different: positioning it past its end is how a file is extended,
	 and the default mode IS a writer -- FileStream>>writing makes a
	 stream that was never told a mode readWriteShorten -- which is why
	 `(FileStream oldFileNamed: f) position: 99' is still accepted where
	 `readOnly; position: 99' is refused."
	anInteger < 0 ifTrue: [^self positionError].
	(self isReadOnly and: [anInteger > self size]) ifTrue: [^self positionError].
	^self position: anInteger size: 1
]

{ #category : '*Files-Fixes' }
FileStream >> isReadOnly [
	"Whether I have been told readOnly and not since told otherwise.

	 Asked of rwmode directly rather than as `self writing not', because
	 writing answers true for a stream whose mode was never set -- it may
	 still be written -- and that is not the question here."
	^rwmode notNil and: [(rwmode bitAnd: Write) = 0]
]

{ #category : '*Files-Fixes' }
FileStream >> writing [
	"Whether I may be written.  A stream that was never told a mode may.

	 Without setting the mode.  1983's version made readWriteShorten the
	 mode of any stream that asked, and close asks; once Shorten was a
	 real bit, `(FileStream oldFileNamed: f) next: 5; close' left f five
	 bytes long (Bugs5 FILES-1).  nextPut: and nextPutAll: set the default
	 mode themselves before they write, so a stream that has written has a
	 mode and one that has not does not."
	rwmode == nil ifTrue: [^true].
	^(rwmode bitAnd: Write) = Write
]

{ #category : '*Files-Fixes' }
FileStream >> close [
	"Set my file's status to closed -- flushing, or shortening it to my
	 position if my mode says so, only if I was written.

	 A stream with no mode was never written (see writing), so there is
	 nothing to flush and nothing to cut off.  Otherwise this is 1983's
	 close: a write through the default mode still shortens the file to
	 where the writing stopped, which is what saving a shorter text over
	 a longer file needs."
	closed ifFalse: [
		(rwmode notNil and: [self writing]) ifTrue: [
			(rwmode bitAnd: Shorten) = Shorten
				ifTrue: [self shorten]
				ifFalse: [self flush]].
		closed := true.
		readLimit := writeLimit := 0.
		self file close.
		FileDirectory removeExternalReference: self]
]
