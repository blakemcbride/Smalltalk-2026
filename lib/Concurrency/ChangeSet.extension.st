"
The system change set, written under a lock.

Smalltalk changes is one ChangeSet for the whole image: two Dictionaries
keyed by class name, a Set of removed names and an OrderedCollection of
doIts, each a find-the-slot-then-write that grows by become:.  Every
class definition tells it addClass:, every compile addSelector:class:,
every removal removeSelector:class: -- from whichever worker is doing the
defining.  It is the third thing Metaclass>>name:inEnvironment:subclassOf:
... writes with no lock after declaring the global (Bugs6 KERNB-1), and a
new class lost from classChanges is a class that the next file-out of the
changes silently leaves out.

Each writer below is 1983's body under LibraryLocks' Classes lock -- the
lock class definition already takes for the organizer and the subclass
lists, so that one definition's three writes are ordered against another
definition's three.  The private atClass:add: and atSelector:class:put:
take it for the public methods that only delegate to them; the public
methods that read and then write, or write two things, take it
themselves.  The pure readers that walk the dictionaries take it too.
The file-out methods do not: they read the sources, under the Sources
lock, and nothing that holds this lock may wait for that one.

Assignment is written := where 1983 wrote _ and nothing else is changed.
"
Extension { #name : 'ChangeSet' }

{ #category : '*Concurrency' }
ChangeSet >> addClass: class [
	"Include indication that a new class was created."
	LibraryLocks holdingClasses: [
		classRemoves remove: class name ifAbsent: [].
		self atClass: class add: #add]
]

{ #category : '*Concurrency' }
ChangeSet >> addDoIt: smalltalkExpression [
	"When filing out changes, append this string to the file."

	LibraryLocks holdingClasses: [specialDoIts add: smalltalkExpression]
]

{ #category : '*Concurrency' }
ChangeSet >> changedClasses [
	"Answer a collection of the changed or edited classes, not including removed classes."

	| classes |
	classes := Set new.
	LibraryLocks holdingClasses: [
		methodChanges keys do: [:className | classes add: (self classNamed: className)].
		classChanges keys do: [:className | classes add: (self classNamed: className)]].
	^classes
]

{ #category : '*Concurrency' }
ChangeSet >> removeClass: class [
	"Include indication that a class has been forgotten."

	| name |
	LibraryLocks holdingClasses: [
		(self isNew: class)
			ifTrue: [self removeClassChanges: class] 	"only remember old classes"
			ifFalse: [
				(self atClass: class includes: #rename) 	"remember as old name"
					ifTrue: [name := self oldNameFor: class]
					ifFalse: [name := class name].
				self removeClassChanges: class.
				classRemoves add: name]]
]

{ #category : '*Concurrency' }
ChangeSet >> removeClassChanges: class [
	"Remove all memory of changes associated with this class and its metaclass."

	LibraryLocks holdingClasses: [
		classChanges removeKey: class name ifAbsent: [].
		methodChanges removeKey: class name ifAbsent: [].
		classChanges removeKey: class class name ifAbsent: [].
		methodChanges removeKey: class class name ifAbsent: [].
		classRemoves remove: class name ifAbsent: []]
]

{ #category : '*Concurrency' }
ChangeSet >> renameClass: class as: newName [
	"Include indication that a class has been renamed."

	| value |
	LibraryLocks holdingClasses: [
		(self atClass: class includes: #rename) ifFalse:
			[self atClass: class add: 'oldName: ', class name. 	"only original name matters"
			self atClass: class add: #rename].
	 	"copy changes using new name (metaclass too)"
		(Array with: classChanges with: methodChanges) do:
			[:changes |
			(value := changes at: class name ifAbsent: [nil]) == nil ifFalse:
				[changes at: newName put: value.
				changes removeKey: class name].
			(value := changes at: class class name ifAbsent: [nil]) == nil ifFalse:
				[changes at: (newName, ' class') put: value.
				changes removeKey: class class name]]]
]

{ #category : '*Concurrency' }
ChangeSet >> asSortedCollection [
	"Answer a new instance of SortedCollection whose elements are Strings describing the changes represented by the receiver."

	| summary |
	summary := SortedCollection new.
	LibraryLocks holdingClasses: [
		classChanges associationsDo:
			[:clAssoc |
			clAssoc value do:
				[:changeType | summary add: clAssoc key, ' - ', changeType]].
		methodChanges associationsDo:
			[:clAssoc |
			clAssoc value associationsDo:
				[:mAssoc | summary add: clAssoc key, ' ', mAssoc key, ' - ', mAssoc value]].
		classRemoves do:
			[:name | summary add: name, ' - ', 'remove'].
		reorganizeSystem ifTrue: [summary add: 'Reorganize System'].
		specialDoIts isEmpty not ifTrue:
			[summary add: 'SpecialDoIts - (', specialDoIts size printString, ')']].
	^summary
]

{ #category : '*Concurrency' }
ChangeSet >> changeSelector: selector class: class [
	"Include indication that a method has been edited."

	LibraryLocks holdingClasses: [
		(self atSelector: selector class: class) = #add
			ifFalse: [self atSelector: selector class: class put: #change]]
			"Don't forget a method is new just because it's been changed"
]

{ #category : '*Concurrency' }
ChangeSet >> changedMessageList [
	"Answer an array that identifies the methods that have been changed.
	Each entry is a String containing the class name and method selector."

	| messageList |
	messageList := SortedCollection new.
	LibraryLocks holdingClasses: [
		methodChanges associationsDo:
			[:clAssoc |
			clAssoc value associationsDo:
				[:mAssoc |
				mAssoc value = #remove ifFalse:
					[messageList add: clAssoc key asString, ' ' , mAssoc key]]]].
	^messageList asArray
]

{ #category : '*Concurrency' }
ChangeSet >> removeSelector: selector class: class [
	"Include indication that a method has been forgotten."

	LibraryLocks holdingClasses: [
		(self atSelector: selector class: class) = #add
			ifTrue: [self removeSelectorChanges: selector
						class: class]					"Forgot a new method, no-op"
			ifFalse: [self atSelector: selector
						class: class
						put: #remove]]
]

{ #category : '*Concurrency' }
ChangeSet >> removeSelectorChanges: selector class: class [
	"Remove all memory of changes associated with the argument,
	selector, in this class."

	| dictionary |
	LibraryLocks holdingClasses: [
		dictionary := methodChanges at: class name ifAbsent: [nil].
		dictionary == nil ifFalse: [
			dictionary removeKey: selector ifAbsent: [].
			dictionary isEmpty ifTrue: [methodChanges removeKey: class name]]]
]

{ #category : '*Concurrency' }
ChangeSet >> atClass: class add: changeType [
	LibraryLocks holdingClasses: [
		(self isNew: class) ifFalse: 	"new classes don't matter"
			[(classChanges at: class name
					ifAbsent: [classChanges at: class name put:
						(Set with: changeType)])
				add: changeType]]
]

{ #category : '*Concurrency' }
ChangeSet >> atSelector: selector class: class put: changeType [
	| name dict |
	LibraryLocks holdingClasses: [
		(self isNew: class) ifFalse: [	"Don't keep method changes for new classes"
			name := class name.
			(methodChanges at: name
				ifAbsent:
					[dict := IdentityDictionary new.
					methodChanges at: name put: dict.
					dict])
				at: selector put: changeType]]
]
