"
display: on the Transcript, which is not a Stream.

TextCollector is a StringHolder -- a model with an entry stream inside it --
and only borrows the stream vocabulary it needs: show:, print:, nextPutAll:,
cr, tab.  So Stream>>display: in this package does not reach it, and the
manual's `Transcript display: 42; cr' was a doesNotUnderstand on exactly the
object it was written for (Bugs3 B64).  The same body as Stream's, through
nextPutAll:, which lib/Concurrency holds the Transcript's lock around; the
display form is made before the lock is taken, since displayString is the
argument's own code.
"
Extension { #name : 'TextCollector' }

{ #category : '*Streams-Protocol' }
TextCollector >> display: anObject [
	"Append anObject's display form to the text."
	self nextPutAll: anObject displayString
]
