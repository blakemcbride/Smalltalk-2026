"
The organizers, read and written under a lock.

A ClassOrganizer is three parallel Arrays -- categoryArray, categoryStops,
elementArray -- and every change to it replaces one, then the next, then
the third, each by a fresh copy stored into the instance variable.  Under
1983's scheduler no other process ran between the three stores.  On a
pool of workers, SystemOrganization is one object that every class
definition writes: eight workers defining fifty classes each lost 205-229
of the 400 names from their category every run, printed about seventy-five
SubscriptOutOfBounds from the organizer's own loops, and one run in six
never finished -- classify:under:'s loop read a nil out of a half-replaced
array, the unhandled Error answered nil, the and: got a non-Boolean,
mustBeBoolean proceeded for truth, and the loop printed NonBooleanReceiver
for ever (Bugs6 KERNB-1).  A class's own organizer, written by every
compile:classified:, is the same object at a smaller scale.

Every method below is 1983's body held under LibraryLocks' Classes lock
(the lock holdingClasses: describes), readers included: a reader between
two of the three replacements indexes one array by the other's stops.  The
lock is a Monitor, so the methods that send one another -- classify:under:
sends categoryOfElement:, removeElement:, addCategory: and removeCategory:
-- take it again on the way in and nothing has to know which entry is the
outermost.

The early returns are rewritten as conditionals, and that is the one
change besides := for _.  A ^ inside the held block would return through
the Monitor's ensure:, and the bootstrap runs classify:under: on a bare
context with no sender -- install_class_organization and
install_system_organization activate it directly, once per class -- from
which a non-local return through an unwind frame has nowhere to go: the
return: primitive fails and the method answers nothing.  Every method here
answers what 1983's did, by the value of its last conditional instead.
"
Extension { #name : 'ClassOrganizer' }

{ #category : '*Concurrency' }
ClassOrganizer >> changeFromString: aString [
	"Parse the string and make this be the receiver's structure.  Categories or elements not found are not affected.  New elements are ignored."

	| scanner oldElements newElements newCategories newStops currentStop anArray |
	LibraryLocks holdingClasses: [
	scanner := Scanner new scanTokens: aString.
	"If nothing was scanned and I had no elements before, then default me"
	(scanner size = 0 and: [elementArray size = 0])
		ifTrue: [self setDefaultList]
		ifFalse: [
	oldElements := elementArray asSet.
	newCategories := Array new: scanner size.
	newStops := Array new: scanner size.
	currentStop := 0.
	newElements := WriteStream on: (Array new: 16).
	1 to: scanner size do:
		[:i |
		anArray := scanner at: i.
		newCategories at: i put: anArray first asSymbol.
		(anArray copyFrom: 2 to: anArray size) asSortedCollection do:
			[:elem |
			(oldElements remove: elem ifAbsent: [nil]) notNil ifTrue:
				[newElements nextPut: elem.
				currentStop := currentStop+1]].
		newStops at: i put: currentStop].

	"Ignore extra elements but don't lose any existing elements!"
	oldElements := oldElements collect:
		[:elem | Array with: (self categoryOfElement: elem) with: elem].
	newElements := newElements contents.
	categoryArray := newCategories.
	categoryStops := newStops.
	elementArray := newElements.
	oldElements do: [:pair | self classify: pair last under: pair first]]]
]

{ #category : '*Concurrency' }
ClassOrganizer >> addCategory: heading before: nextHeading [
	| nextIndex elements |
	"Add a new category named heading.  If nextHeading is specified (not nil)
	and can be found, then INSERT before that entry.  Otherwise ADD it at the end.
	If heading already exists, then MOVE it where it would have gone otherwise."

	LibraryLocks holdingClasses: [
	(categoryArray indexOf: heading) > 0
		ifTrue:  "already there - maybe move"
			["slow but sure;  remove all, then insert all"
			elements := self listAtCategoryNamed: heading.	"Save elements"
			elements do: [:elt | self removeElement: elt].
			self removeCategory: heading.		"Remove old entry"
			self addCategory: heading before: nextHeading.	"Now insert anew"
			self classifyAll: elements under: heading]			"And restore elements"
		ifFalse: [
	nextIndex := categoryArray indexOf: nextHeading
							ifAbsent: [categoryArray size+1].
	categoryArray := categoryArray  "insert before nextIndex"
			copyReplaceFrom: nextIndex
			to: nextIndex-1
			with: (Array with: heading).
	categoryStops := categoryStops
			copyReplaceFrom: nextIndex
			to: nextIndex-1
			with: (Array with: (nextIndex=1
					ifTrue: [0]
					ifFalse: [categoryStops at: nextIndex-1]))]]
]

{ #category : '*Concurrency' }
ClassOrganizer >> categories [
	"Answer an array of categories (names)."

	^LibraryLocks holdingClasses: [
	(categoryArray size = 1
		and: [categoryArray first = Default & (elementArray size = 0)])
		ifTrue: [Array new]
		ifFalse: [categoryArray]]
]

{ #category : '*Concurrency' }
ClassOrganizer >> categories: anArray [
	"Reorder my categories to be in the order of anArray.  If the
	resulting organization does not include all elements, then give an error."
	| newCategories newStops newElements catName list runningTotal |
	LibraryLocks holdingClasses: [
	newCategories := Array new: anArray size.
	newStops := Array new: anArray size.
	newElements := Array new: 0.
	runningTotal := 0.
	1 to: anArray size do:
		[:i |
		catName := (anArray at: i) asSymbol.
		list := self listAtCategoryNamed: catName.
				newElements := newElements, list.
				newCategories at: i put: catName.
				newStops at: i put: (runningTotal := runningTotal + list size)].
	"check to be sure all elements are included"
	(elementArray detect: [:element | (newElements includes: element) not] ifNone: [nil]) notNil
		ifTrue: [self error: 'New categories must match old ones']
		ifFalse: [
	"Everything is good, now update my three arrays."
	categoryArray := newCategories.
	categoryStops := newStops.
	elementArray := newElements]]
]

{ #category : '*Concurrency' }
ClassOrganizer >> categoryOfElement: element [
	"Answer the category associated with the argument, element."

	| index |
	^LibraryLocks holdingClasses: [
	index := self numberOfCategoryOfElement: element.
	index = 0
		ifTrue: [nil]
		ifFalse: [categoryArray at: index]]
]

{ #category : '*Concurrency' }
ClassOrganizer >> removeCategory: cat [
	"Remove the category named, cat.  Create an error if the category has
	any elements in it."

	| index lastStop |
	LibraryLocks holdingClasses: [
	index := categoryArray indexOf: cat ifAbsent: [0].
	index = 0 ifFalse: [
	lastStop :=
		index = 1
			ifTrue: [0]
			ifFalse: [categoryStops at: index - 1].
	(categoryStops at: index) - lastStop > 0
		ifTrue: [self error: 'cannot remove non-empty category']
		ifFalse: [
	categoryArray :=
		(categoryArray copyFrom: 1 to: index - 1)
			, (categoryArray copyFrom: index + 1 to: categoryArray size).
	categoryStops :=
		(categoryStops copyFrom: 1 to: index - 1)
			, (categoryStops copyFrom: index + 1 to: categoryStops size).
	categoryArray size = 0
		ifTrue:
			[categoryArray := Array with: Default.
			categoryStops := Array with: 0]]]]
]

{ #category : '*Concurrency' }
ClassOrganizer >> removeEmptyCategories [
	"Remove empty categories."
	| categoryIndex currentStop keptCategories keptStops |
	LibraryLocks holdingClasses: [
	keptCategories := WriteStream on: (Array new: 16).
	keptStops := WriteStream on: (Array new: 16).
	currentStop := categoryIndex := 0.
	[(categoryIndex := categoryIndex + 1) <= categoryArray size]
		whileTrue:
			[(categoryStops at: categoryIndex) > currentStop
				ifTrue:
					[keptCategories nextPut: (categoryArray at: categoryIndex).
					keptStops nextPut: (currentStop := categoryStops at: categoryIndex)]].
	categoryArray := keptCategories contents.
	categoryStops := keptStops contents.
	categoryArray size = 0
		ifTrue:
			[categoryArray := Array with: Default.
			categoryStops := Array with: 0]]

	"ClassOrganizer allInstancesDo: [:co | co removeEmptyCategories]."
]

{ #category : '*Concurrency' }
ClassOrganizer >> renameCategory: oldName to: newName [
	"Answer the array of elements associated with the name, categoryName."
	| i symbol |
	^LibraryLocks holdingClasses: [
	i := categoryArray indexOf: oldName ifAbsent: [0].
	i = 0
		ifTrue: [false]
		ifFalse: [
			symbol := newName asSymbol.
			(categoryArray indexOf: symbol) = 0
				ifTrue: [categoryArray at: i put: symbol.  true]
				ifFalse: [false  "newName was already there"]]]
]

{ #category : '*Concurrency' }
ClassOrganizer >> classify: element under: heading [
	"Store the argument, element, in the category named heading."

	| catName catIndex elemIndex realHeading |
	LibraryLocks holdingClasses: [
	realHeading := heading asSymbol.

	"Element already there: Default causes no change, and so does a
	 heading that didnt change"
	((catName := self categoryOfElement: element) ~~ nil
	  and: [realHeading = Default or: [realHeading = catName]])
		ifFalse: [
	catName ~~ nil ifTrue: [self removeElement: element].	"remove from old heading if did change"

	(categoryArray indexOf: realHeading) = 0 ifTrue: [self addCategory: realHeading].
	"add realHeading if not there already"

	catIndex := categoryArray indexOf: realHeading.
	elemIndex :=
		catIndex > 1
			ifTrue: [categoryStops at: catIndex - 1]
			ifFalse: [0].
	[(elemIndex := elemIndex + 1) <= (categoryStops at: catIndex)
		and: [element >= (elementArray at: elemIndex)]] whileTrue.

	"elemIndex is now the index for inserting the element. Do the insertion before it."
	elementArray :=
		(elementArray copyFrom: 1 to: elemIndex - 1)
			, (Array with: element)
			, (elementArray copyFrom: elemIndex to: elementArray size).	"insertion"

	"add one to stops for this and later categories"
	catIndex to: categoryArray size do:
		[:i | categoryStops at: i put: (categoryStops at: i) + 1].

	"remove empty default category if any"
	((categoryArray indexOf: Default) > 0
	  and: [(self listAtCategoryNamed: Default) size = 0])
		ifTrue: [self removeCategory: Default]]]
]

{ #category : '*Concurrency' }
ClassOrganizer >> classifyAll: aCollection under: heading [
	"All of them, or none, as far as anyone else can see."
	LibraryLocks holdingClasses: [
		aCollection do:
			[:element | self classify: element under: heading]]
]

{ #category : '*Concurrency' }
ClassOrganizer >> listAtCategoryNamed: categoryName [
	"Answer the array of elements associated with the name, categoryName."

	| i |
	^LibraryLocks holdingClasses: [
	i := categoryArray indexOf: categoryName ifAbsent: [0].
	i = 0
		ifTrue: [Array new]
		ifFalse: [self listAtCategoryNumber: i]]
]

{ #category : '*Concurrency' }
ClassOrganizer >> removeElement: element [
	"Remove the selector, element, from all categories."

	| categoryIndex elementIndex nextStop newElements |
	LibraryLocks holdingClasses: [
	categoryIndex := 1.
	elementIndex := 0.
	nextStop := 0.
	"nextStop keeps track of the stops in the new element array"
	newElements := WriteStream on: (Array new: elementArray size).
	[(elementIndex := elementIndex + 1) <= elementArray size]
		whileTrue:
			[[elementIndex > (categoryStops at: categoryIndex)]
				whileTrue:
					[categoryStops at: categoryIndex put: nextStop.
					categoryIndex := categoryIndex + 1].
			element = (elementArray at: elementIndex)
				ifFalse:
					[nextStop := nextStop + 1.
					newElements nextPut: (elementArray at: elementIndex)]].
	[categoryIndex <= categoryStops size]
		whileTrue:
			[categoryStops at: categoryIndex put: nextStop.
			categoryIndex := categoryIndex + 1].
	elementArray := newElements contents]
]

{ #category : '*Concurrency' }
ClassOrganizer >> printOn: aStream [
	| elementIndex lastStop |
	LibraryLocks holdingClasses: [
	elementIndex := 1.
	lastStop := 1.
	1 to: categoryArray size do:
		[:i |
		aStream nextPut: $(.
		(categoryArray at: i) asString printOn: aStream.
		[elementIndex <= (categoryStops at: i)]
			whileTrue:
				[aStream space.
				(elementArray at: elementIndex) printOn: aStream.
				elementIndex := elementIndex + 1].
		aStream nextPut: $).
		aStream cr]]
]

{ #category : '*Concurrency' }
ClassOrganizer >> listAtCategoryNumber: index [
	"Answer the array of elements stored at the position index."
	| firstIndex lastIndex |
	^LibraryLocks holdingClasses: [
	firstIndex := index = 1
			ifTrue: [1]
			ifFalse: [(categoryStops at: index - 1) + 1].
	lastIndex := categoryStops at: index.
	elementArray copyFrom: firstIndex to: lastIndex]
]

{ #category : '*Concurrency' }
ClassOrganizer >> numberOfCategoryOfElement: element [
	"Answer the index of the category with which the argument, element,
	is associated."

	| categoryIndex elementIndex found |
	^LibraryLocks holdingClasses: [
	categoryIndex := 1.
	elementIndex := 0.
	found := 0.
	[found = 0 and: [(elementIndex := elementIndex + 1) <= elementArray size]]
		whileTrue:
			["point to correct category"
			[elementIndex > (categoryStops at: categoryIndex)]
				whileTrue: [categoryIndex := categoryIndex + 1].
			"see if this is element"
			element = (elementArray at: elementIndex) ifTrue: [found := categoryIndex]].
	found]
]
