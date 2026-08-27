"
The clipboard where these tests run, which is a bootstrap with no window:
the primitive says so and every answer is the `nothing there' one, which
is what leaves the editor with its own buffer.  The line-end translation,
which is the one piece of logic in the package, is checked on text.  The
real clipboard is checked by hand: copy in a workspace, paste in a
terminal, and back.
"
Class {
	#name : 'ClipboardTest',
	#superclass : 'TestCase',
	#category : 'Clipboard-Tests'
}

{ #category : 'tests' }
ClipboardTest >> testNoWindowMeansNothingThere [
	Clipboard isAvailable ifTrue: [^self].
	self assert: Clipboard text isNil.
	self deny: (Clipboard text: 'anything')
]

{ #category : 'tests' }
ClipboardTest >> testLineEndsComingIn [
	| cr lf |
	cr := String with: (Character value: 13).
	lf := String with: (Character value: 10).
	self assert: (Clipboard fromOutside: 'one', lf, 'two') equals: 'one', cr, 'two'.
	self assert: (Clipboard fromOutside: 'one', cr, lf, 'two') equals: 'one', cr, 'two'.
	self assert: (Clipboard fromOutside: 'one', cr, 'two') equals: 'one', cr, 'two'.
	self assert: (Clipboard fromOutside: lf, lf) equals: cr, cr.
	self assert: (Clipboard fromOutside: '') equals: ''.
	self assert: (Clipboard fromOutside: 'plain') equals: 'plain'
]

{ #category : 'tests' }
ClipboardTest >> testLineEndsGoingOut [
	| cr lf |
	cr := String with: (Character value: 13).
	lf := String with: (Character value: 10).
	self assert: (Clipboard toOutside: 'one', cr, 'two') equals: 'one', lf, 'two'.
	self assert: (Clipboard toOutside: 'plain') equals: 'plain'.
	self assert: (Clipboard toOutside: ('a', cr) asText) equals: 'a', lf
]

{ #category : 'tests' }
ClipboardTest >> testAWrongArgumentIsNotAnError [
	"The primitive fails and the fallback answers nil: not available."
	self assert: (Clipboard primCommand: 2 with: 42) isNil.
	self assert: (Clipboard primCommand: 'x' with: nil) isNil
]
