"
The system's clipboard, from the image's side: what the rest of the desktop
has copied, and what this image puts there for the rest of the desktop to
paste.

    Clipboard text            a String, or nil when there is nothing or no window
    Clipboard text: aString   true when it went, false when there is no window
    Clipboard isAvailable     whether there is a window at all

1983's text editor had a clipboard of its own -- CurrentSelection, a class
variable of ParagraphEditor -- and nothing outside the image could see it
or fill it.  That was the whole of the world then.  Now a workspace sits
beside a terminal and a browser, and a selection has to cross: copy and
cut in any text pane put the selection here as well (ParagraphEditor's
copySelection and cut, in this package), and paste takes what is here when
there is anything, so the last thing copied anywhere is what pastes,
whichever side of the window it was copied on.

Line ends are translated: the image ends a line with CR, the rest of the
world with LF (or CR LF), and text going out gets LF while text coming in
gets CR, so a method pasted from an editor compiles and a method copied out
reads as lines.  Nothing else is translated.  The image's strings are
bytes, and the system's clipboard is UTF-8, so a character outside ASCII
pasted in arrives as the bytes of its encoding; a curly quote is three of
them.  Turning those into something the compiler accepts is the author's
job, as it was the reader's.

Under -serve there is no window, and under a headless build no SDL, and
then text answers nil and text: answers false, and the editor is left with
its own buffer -- which is 1983's behaviour, unchanged.  Primitive 210 is
the whole of the VM's side.
"
Class {
	#name : 'Clipboard',
	#superclass : 'Object',
	#category : 'Clipboard'
}

{ #category : 'private' }
Clipboard class >> primCommand: commandInteger with: anObject [
	"The one primitive: 0 is there a window, 1 the text or nil, 2 put a
	 String.  A failed primitive -- an argument of the wrong kind -- is
	 nil, which every sender reads as `not available'."

	<primitive: 210>
	^nil
]

{ #category : 'testing' }
Clipboard class >> isAvailable [
	"Whether there is a window, and so a clipboard behind it."

	^(self primCommand: 0 with: nil) == true
]

{ #category : 'accessing' }
Clipboard class >> text [
	"What is on the system's clipboard, with its line ends made the
	 image's; nil when there is nothing, or no window."

	| outside |
	outside := self primCommand: 1 with: nil.
	outside isNil ifTrue: [^nil].
	^self fromOutside: outside
]

{ #category : 'accessing' }
Clipboard class >> text: aStringOrText [
	"Put the text on the system's clipboard, with its line ends made the
	 world's.  Answer whether it went: false when there is no window."

	^(self primCommand: 2 with: (self toOutside: aStringOrText asString)) == true
]

{ #category : 'translating' }
Clipboard class >> fromOutside: aString [
	"LF, and CR LF, become CR: the image's line end."

	| out cr lf i c |
	cr := Character value: 13.
	lf := Character value: 10.
	out := WriteStream on: (String new: aString size).
	i := 1.
	[i <= aString size] whileTrue: [
		c := aString at: i.
		c = cr
			ifTrue: [
				out nextPut: cr.
				(i < aString size and: [(aString at: i + 1) = lf]) ifTrue: [i := i + 1]]
			ifFalse: [out nextPut: (c = lf ifTrue: [cr] ifFalse: [c])].
		i := i + 1].
	^out contents
]

{ #category : 'translating' }
Clipboard class >> toOutside: aString [
	"CR becomes LF: what every editor and terminal outside expects."

	| cr lf |
	cr := Character value: 13.
	lf := Character value: 10.
	^aString asString collect: [:c | c = cr ifTrue: [lf] ifFalse: [c]]
]
