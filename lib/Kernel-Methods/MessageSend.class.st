"
A message that has not been sent yet: a receiver, a selector, and the
arguments to go with them.

Pharo uses this wherever a callback has to outlive the expression that set
it up -- Announcer>>when:send:to: builds one per subscription -- and the
1983 system has no equivalent, having used blocks and dependents for the
same jobs.

It is worth knowing how its absence showed up, because it was not as a
missing method.  AnnouncerTest>>testSymbolIdentifier wraps its whole body
in an `on: MessageNotUnderstood do:' handler and then asks whether the
selector that was not understood was the one it expected.  With
MessageSend undeclared, `MessageSend receiver: x selector: y' is nil
receiving receiver:selector: -- a doesNotUnderstand, which that handler
caught and compared, so a missing class arrived as a WRONG ANSWER rather
than as an error.  Four other tests reported it honestly.
"
Class {
	#name : 'MessageSend',
	#superclass : 'Object',
	#instVars : [ 'receiver', 'selector', 'arguments' ],
	#category : 'Kernel-Methods'
}

{ #category : 'instance creation' }
MessageSend class >> receiver: anObject selector: aSymbol [
	^self receiver: anObject selector: aSymbol arguments: #()
]

{ #category : 'instance creation' }
MessageSend class >> receiver: anObject selector: aSymbol argument: anArgument [
	^self receiver: anObject selector: aSymbol
		  arguments: (Array with: anArgument)
]

{ #category : 'instance creation' }
MessageSend class >> receiver: anObject selector: aSymbol arguments: anArray [
	| send |
	send := self new.
	send setReceiver: anObject selector: aSymbol arguments: anArray.
	^send
]

{ #category : 'private' }
MessageSend >> setReceiver: anObject selector: aSymbol arguments: anArray [
	receiver := anObject.
	selector := aSymbol.
	arguments := anArray
]

{ #category : 'accessing' }
MessageSend >> receiver [ ^receiver ]

{ #category : 'accessing' }
MessageSend >> receiver: anObject [ receiver := anObject ]

{ #category : 'accessing' }
MessageSend >> selector [ ^selector ]

{ #category : 'accessing' }
MessageSend >> selector: aSymbol [ selector := aSymbol ]

{ #category : 'accessing' }
MessageSend >> arguments [ ^arguments ]

{ #category : 'accessing' }
MessageSend >> arguments: anArray [ arguments := anArray ]

{ #category : 'accessing' }
MessageSend >> numArgs [
	"How many arguments the SELECTOR takes -- not how many are stored.

	 Those are different numbers and the difference is the whole point.
	 `when: #FOO send: #bar to: nil' stores no arguments at all, and the
	 announcement is supplied later by cull:.  Answering the stored count
	 made cull: send #value to a one-argument selector and the VM said so:
	 '0 arguments sent to a method expecting 1'.  Which killed the whole
	 evaluation, so a suite answered nil while every test in it passed
	 when run alone."
	^selector isNil ifTrue: [0] ifFalse: [selector numArgs]
]

{ #category : 'evaluating' }
MessageSend >> value [
	"Send it, with whatever arguments it was built with."
	^receiver perform: selector
		withArguments: (arguments isNil ifTrue: [#()] ifFalse: [arguments])
]

{ #category : 'evaluating' }
MessageSend >> value: anArgument [
	"Send it with one argument, as a one-argument block would be
	 evaluated.  Announcements does this: a subscription is asked to
	 value: the announcement, whether it is a block or one of these."
	^self numArgs = 0
		ifTrue: [self value]
		ifFalse: [receiver perform: selector withArguments:
					(Array with: anArgument)]
]

{ #category : 'evaluating' }
MessageSend >> value: firstArgument value: secondArgument [
	^self numArgs = 2
		ifTrue: [receiver perform: selector withArguments:
					(Array with: firstArgument with: secondArgument)]
		ifFalse: [self value: firstArgument]
]

{ #category : 'evaluating' }
MessageSend >> valueWithArguments: anArray [
	^receiver perform: selector withArguments: anArray
]

{ #category : 'testing' }
MessageSend >> isMessageSend [ ^true ]

{ #category : 'printing' }
MessageSend >> printOn: aStream [
	aStream nextPutAll: 'a MessageSend('.
	selector printOn: aStream.
	aStream nextPut: $)
]



{ #category : 'evaluating' }
MessageSend >> cull: anArgument [
	^self numArgs = 0 ifTrue: [self value] ifFalse: [self value: anArgument]
]

{ #category : 'evaluating' }
MessageSend >> cull: firstArgument cull: secondArgument [
	self numArgs = 0 ifTrue: [^self value].
	self numArgs = 1 ifTrue: [^self value: firstArgument].
	^self value: firstArgument value: secondArgument
]
