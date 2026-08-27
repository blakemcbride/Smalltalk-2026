"
1983's Parser, when it meets a selector no method in the image has yet --
`self password: p matches: s' before password:matches: is defined -- asks
its editor's user whether that was meant: a menu, `proceed as is / correct
it / abort'.  The only way past the menu was an editor of nil, which is how
a Compiler with no requestor behaves and how a file loaded by a requestor
that IS something -- TonelReader, reading a service for a server -- did
not.

What that did, before this file: TonelReader answered no `editor' at all,
so the Parser sent it doesNotUnderstand:, and what an unhandled
doesNotUnderstand: answers is not the same everywhere.  In the bootstrap
process it answered nil, the selector was interned, and every test
passed.  In an image written to disk and read back by `st80 -serve' it
answered something else, the Parser took that for an editor, sent it
selectFrom:to:, and TonelReader raised `password: p matches: s is
undeclared' -- for a method that sent a selector defined ten lines later
in the same file.  The demo's Login and Init would not load on the server
and loaded fine in every test.  A fault masked by one runtime's answer to
a message nobody meant to send.

So: an editor that says it has no user gets the selector interned, as the
nil editor always did, and everything else is 1983's method as it was,
because the Browser's user does want the question.
"
Extension { #name : 'Parser' }

{ #category : '*Tonel' }
Parser >> makeNewSymbol: aString startingAt: start [
	| editor index menu sym oldText newText oldStream oldKey |
	Symbol hasInterned: aString ifTrue: [:symbol | ^symbol].
	(editor := self editor) isNil ifTrue: [^aString asSymbol].
	((editor respondsTo: #internsNewSelectors) and: [editor internsNewSelectors])
		ifTrue: [^aString asSymbol].
	editor selectFrom: start to: self endOfLastToken.
	Cursor normal show.
	index := (menu := ActionMenu
			labels: 'proceed as is\correct it\abort' withCRs
			selectors: #(proceed correct abort))
				startUp: #anyButton
				withHeading: ' ' , (aString contractTo: 20) , ' is a new message '.
	index = 0 ifTrue: [^self notify: ''].
	(menu selectorAt: index) == #proceed ifTrue: [^aString asSymbol].
	(menu selectorAt: index) == #correct ifTrue:
		[sym := Symbol correctMessage: aString.
		sym == false ifTrue: [self notify: 'Couldn''t correct'. self abort].
		sym == nil ifTrue: [self abort].
		oldText := editor selection.
		sym isKeyword
			ifTrue:
				[newText := oldText.
				oldStream := ReadStream on: aString.
				sym keywords do:
					[:newKey | oldKey := (oldStream upTo: $:) , ':'.
					oldKey = newKey ifFalse:
						[newText := newText copyReplaceAll: oldKey asText with: newKey asText]]]
			ifFalse:
				[newText := oldText copyReplaceAll: aString asText with: sym asText].
		self replaceEditSelectionWith: newText.
		^sym].
	self abort
]
