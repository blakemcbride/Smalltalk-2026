"
A block argument used outside its block.

Eight methods of the 1983 library are written like this:

    special := Smalltalk hasSpecialSelector: aLiteral ifTrueSetByte: [:byte ].
    ... whichSelectorsReferTo: aLiteral special: special byte: byte

The block is meant to hand `byte' back.  It cannot: its parameter's scope
ends at the bracket, so the `byte' on the next line is a different name
altogether -- an undeclared global, created by the loader the way a real
fileIn creates one, holding nil for ever.  Nothing reports it at run time;
the method simply passes nil where the byte belonged.

That is what the bootstrap's `4 lower-case (probable source bugs)' line has
always been pointing at.  Of the eighty names it counts, seventy-three are
filled in later by design -- Sensor and Transcript and the pool variables do
not exist when the code referring to them is compiled -- and of the seven
that stay nil, three are Xerox Alto disk constants in code that cannot run
here.  These four are the rest: byte, firstByte, secondByte and response.

The correction is the same in every case and is one line: declare a real
temporary and let the block assign to it.  sources/ is never edited, so the
methods are re-stated here in full with that one line changed, and nothing
else touched -- not the underscore-for-assignment, which the closures
dialect reads as an identifier, so those become `:=' of necessity.

The byte only matters when the selector IS special -- the sixteen arithmetic
and the sixteen common ones have their own bytecodes -- so what was broken
is finding the senders of `+', `at:put:', `do:' and their kin.  Which is
exactly the search a Browser runs, and it was quietly answering as though no
method sent any of them.
"
Extension { #name : 'Behavior' }

{ #category : '*Scope-Fixes' }
Behavior >> allCallsOn: aLiteral [
	"Answer a SortedCollection of all the methods that call on aLiteral."

	| aSortedCollection special byte |
	aSortedCollection := SortedCollection new.
	special := Smalltalk hasSpecialSelector: aLiteral ifTrueSetByte: [:b | byte := b].
	(self withAllSuperclasses reverse) , self allSubclasses do:
		[:class |
		(class whichSelectorsReferTo: aLiteral special: special byte: byte) do:
			[:sel | sel ~~ #DoIt ifTrue: [aSortedCollection add: class name , ' ' , sel]].
		(class class whichSelectorsReferTo: aLiteral special: special byte: byte) do:
			[:sel | sel ~~ #DoIt ifTrue: [aSortedCollection add: class class name , ' ' , sel]].
		].
	^aSortedCollection
]

{ #category : '*Scope-Fixes' }
Behavior >> whichSelectorsReferTo: literal [
	"Answer a set of selectors whose methods access the argument as a literal."

	| special byte |
	special := Smalltalk hasSpecialSelector: literal ifTrueSetByte: [:b | byte := b].
	^self whichSelectorsReferTo: literal special: special byte: byte
]
