"
The two browser panes that were in creation order are in alphabetical order.

Of the browser's four list panes only two were ever out of alphabetical
order, and the reason is one method in Categorizer.  classify:under: walks
the elements of a category and inserts the new one where it belongs, so the
classes in a system category and the selectors in a protocol come out sorted
and have since 1983.  Categories themselves are appended:
addCategory:before: puts a new one at the end unless somebody names the one
it goes in front of.  That is why the system category list reads
Kernel-Objects, Kernel-Classes, ... Interface-Browser, and why a class's
protocols read initialization, accessing, ... private.

That order is deliberate, and it is also the order a file out is written in
-- fileOutOn: walks `self organization categories' and writes a
`methodsFor:' chunk for each -- so it stays where it is.  What changes is
what the two panes display: the list is sorted on its way to the view and
nothing else is touched, which means an existing file out is byte for byte
what it was.

Sorting only the display is safe because a SelectionInListView finds its
highlighted row by matching what the model answers against the list rather
than by index -- `itemList findFirst: [:x | x = item]' in list: -- so the
selection follows the row it is on rather than the position it was at.

classList is left alone.  Its ordinary answer is already sorted, and its
other one, the list a spawned hierarchy browser shows, is in superclass
order on purpose.
"
Extension { #name : 'Browser' }

{ #category : '*Browser-Sorting' }
Browser >> categoryList [
	"The system categories, alphabetically.  1983 answers `organization
	 categories', which is the order they were created in."

	^organization categories asSortedCollection asArray
]

{ #category : '*Browser-Sorting' }
Browser >> protocolList [
	"The selected class's protocols, alphabetically.  1983's method with the
	 list sorted on the way out."

	className == nil ifTrue: [^nil].
	^self selectedClass organization categories asSortedCollection asArray
]
