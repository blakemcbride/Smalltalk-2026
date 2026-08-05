"Smalltalk-2026 minimal kernel.
Copyright (c) 2026 Blake McBride.  All rights reserved.

Enough of a class library to bootstrap a running image and answer
questions.  The field orders here are not free: the interpreter and the
bootstrap both index these objects directly, so Behavior must begin with
superclass, methodDict and format, and MethodDictionary must begin with
tally and valueArray.  See src/boot/bootstrap.c.

This is our own code, so the image built from it carries no Xerox
provenance.  The 1983 library is the eventual target and loads on top."!

"----------  The root  ----------"!

nil subclass: #Object
    instanceVariableNames: ''
    classVariableNames: ''
    poolDictionaries: ''
    category: 'Kernel-Objects'!

"----------  Classes are objects too  ----------"!

Object subclass: #Behavior
    instanceVariableNames: 'superclass methodDict format subclasses'
    classVariableNames: ''
    poolDictionaries: ''
    category: 'Kernel-Classes'!

Behavior subclass: #ClassDescription
    instanceVariableNames: 'instanceVariables organization'
    classVariableNames: ''
    poolDictionaries: ''
    category: 'Kernel-Classes'!

ClassDescription subclass: #Class
    instanceVariableNames: 'name classPool sharedPools'
    classVariableNames: ''
    poolDictionaries: ''
    category: 'Kernel-Classes'!

ClassDescription subclass: #Metaclass
    instanceVariableNames: 'thisClass'
    classVariableNames: ''
    poolDictionaries: ''
    category: 'Kernel-Classes'!

"----------  The three singletons  ----------"!

Object subclass: #UndefinedObject
    instanceVariableNames: ''
    classVariableNames: ''
    poolDictionaries: ''
    category: 'Kernel-Objects'!

Object subclass: #Boolean
    instanceVariableNames: ''
    classVariableNames: ''
    poolDictionaries: ''
    category: 'Kernel-Objects'!

Boolean subclass: #True
    instanceVariableNames: ''
    classVariableNames: ''
    poolDictionaries: ''
    category: 'Kernel-Objects'!

Boolean subclass: #False
    instanceVariableNames: ''
    classVariableNames: ''
    poolDictionaries: ''
    category: 'Kernel-Objects'!

"----------  Numbers  ----------"!

Object subclass: #Magnitude
    instanceVariableNames: ''
    classVariableNames: ''
    poolDictionaries: ''
    category: 'Kernel-Numbers'!

Magnitude subclass: #Number
    instanceVariableNames: ''
    classVariableNames: ''
    poolDictionaries: ''
    category: 'Kernel-Numbers'!

Number subclass: #Integer
    instanceVariableNames: ''
    classVariableNames: ''
    poolDictionaries: ''
    category: 'Kernel-Numbers'!

Integer subclass: #SmallInteger
    instanceVariableNames: ''
    classVariableNames: ''
    poolDictionaries: ''
    category: 'Kernel-Numbers'!

Integer variableByteSubclass: #LargePositiveInteger
    instanceVariableNames: ''
    classVariableNames: ''
    poolDictionaries: ''
    category: 'Kernel-Numbers'!

Number variableWordSubclass: #Float
    instanceVariableNames: ''
    classVariableNames: ''
    poolDictionaries: ''
    category: 'Kernel-Numbers'!

Magnitude subclass: #Character
    instanceVariableNames: 'value'
    classVariableNames: ''
    poolDictionaries: ''
    category: 'Kernel-Objects'!

"----------  Collections  ----------"!

Object subclass: #Collection
    instanceVariableNames: ''
    classVariableNames: ''
    poolDictionaries: ''
    category: 'Kernel-Collections'!

Collection variableSubclass: #Array
    instanceVariableNames: ''
    classVariableNames: ''
    poolDictionaries: ''
    category: 'Kernel-Collections'!

Collection variableByteSubclass: #String
    instanceVariableNames: ''
    classVariableNames: ''
    poolDictionaries: ''
    category: 'Kernel-Collections'!

String variableByteSubclass: #Symbol
    instanceVariableNames: ''
    classVariableNames: ''
    poolDictionaries: ''
    category: 'Kernel-Collections'!

"The method dictionary's shape is fixed by the interpreter's lookup:
 tally, then the value array, then the selectors in the indexed part."!

Collection variableSubclass: #MethodDictionary
    instanceVariableNames: 'tally valueArray'
    classVariableNames: ''
    poolDictionaries: ''
    category: 'Kernel-Collections'!

Object subclass: #Association
    instanceVariableNames: 'key value'
    classVariableNames: ''
    poolDictionaries: ''
    category: 'Kernel-Collections'!

Object subclass: #SystemDictionary
    instanceVariableNames: 'associations'
    classVariableNames: ''
    poolDictionaries: ''
    category: 'Kernel-System'!

"----------  Objects the interpreter names directly  ----------"!

Object subclass: #Point
    instanceVariableNames: 'x y'
    classVariableNames: ''
    poolDictionaries: ''
    category: 'Kernel-Objects'!

Object subclass: #Message
    instanceVariableNames: 'selector arguments'
    classVariableNames: ''
    poolDictionaries: ''
    category: 'Kernel-Methods'!

Object variableByteSubclass: #CompiledMethod
    instanceVariableNames: ''
    classVariableNames: ''
    poolDictionaries: ''
    category: 'Kernel-Methods'!

Object variableSubclass: #MethodContext
    instanceVariableNames: 'sender instructionPointer stackPointer method unused receiver'
    classVariableNames: ''
    poolDictionaries: ''
    category: 'Kernel-Methods'!

Object variableSubclass: #BlockContext
    instanceVariableNames: 'caller instructionPointer stackPointer blockArgumentCount initialIP home'
    classVariableNames: ''
    poolDictionaries: ''
    category: 'Kernel-Methods'!

Object subclass: #Link
    instanceVariableNames: 'nextLink'
    classVariableNames: ''
    poolDictionaries: ''
    category: 'Kernel-Processes'!

Object subclass: #LinkedList
    instanceVariableNames: 'firstLink lastLink'
    classVariableNames: ''
    poolDictionaries: ''
    category: 'Kernel-Processes'!

LinkedList subclass: #Semaphore
    instanceVariableNames: 'excessSignals'
    classVariableNames: ''
    poolDictionaries: ''
    category: 'Kernel-Processes'!

Link subclass: #Process
    instanceVariableNames: 'suspendedContext priority myList'
    classVariableNames: ''
    poolDictionaries: ''
    category: 'Kernel-Processes'!

Object subclass: #ProcessorScheduler
    instanceVariableNames: 'quiescentProcessLists activeProcess'
    classVariableNames: ''
    poolDictionaries: ''
    category: 'Kernel-Processes'!

Object variableWordSubclass: #DisplayBitmap
    instanceVariableNames: ''
    classVariableNames: ''
    poolDictionaries: ''
    category: 'Kernel-Graphics'!

Object subclass: #Form
    instanceVariableNames: 'bits width height offset'
    classVariableNames: ''
    poolDictionaries: ''
    category: 'Kernel-Graphics'!

Form subclass: #DisplayScreen
    instanceVariableNames: ''
    classVariableNames: ''
    poolDictionaries: ''
    category: 'Kernel-Graphics'!

"==========  Methods  =========="!

!Object methodsFor: 'testing'!
isNil
    ^false!
notNil
    ^true!
yourself
    ^self!
== anObject
    <primitive: 110>
    ^false!
= anObject
    ^self == anObject!
class
    <primitive: 111>
    ^nil!
hash
    ^0! !

!Object methodsFor: 'errors'!
doesNotUnderstand: aMessage
    ^aMessage!
error: aString
    ^aString!
primitiveFailed
    ^nil! !

!Object class methodsFor: 'instance creation'!
new
    <primitive: 70>
    ^self primitiveFailed!
new: size
    <primitive: 71>
    ^self primitiveFailed!
basicNew
    <primitive: 70>
    ^self primitiveFailed! !

!UndefinedObject methodsFor: 'testing'!
isNil
    ^true!
notNil
    ^false! !

!Boolean methodsFor: 'logic'!
& aBoolean
    ^self ifTrue: [aBoolean]!
not
    ^self ifTrue: [false]! !

!True methodsFor: 'logic'!
& aBoolean
    ^aBoolean!
| aBoolean
    ^true!
not
    ^false!
and: aBlock
    ^aBlock value!
or: aBlock
    ^true! !

!False methodsFor: 'logic'!
& aBoolean
    ^false!
| aBoolean
    ^aBoolean!
not
    ^true!
and: aBlock
    ^false!
or: aBlock
    ^aBlock value! !

!Behavior methodsFor: 'accessing'!
superclass
    ^superclass!
methodDictionary
    ^methodDict!
format
    ^format! !

!Class methodsFor: 'accessing'!
name
    ^name! !

!Metaclass methodsFor: 'accessing'!
thisClass
    ^thisClass! !

!Number methodsFor: 'arithmetic'!
+ aNumber
    ^self generality!
- aNumber
    ^self generality!
generality
    ^0! !

!SmallInteger methodsFor: 'arithmetic'!
+ aNumber
    <primitive: 1>
    ^self primitiveFailed!
- aNumber
    <primitive: 2>
    ^self primitiveFailed!
* aNumber
    <primitive: 9>
    ^self primitiveFailed!
// aNumber
    <primitive: 12>
    ^self primitiveFailed!
\\ aNumber
    <primitive: 11>
    ^self primitiveFailed!
quo: aNumber
    <primitive: 13>
    ^self primitiveFailed! !

!SmallInteger methodsFor: 'comparing'!
< aNumber
    <primitive: 3>
    ^self primitiveFailed!
> aNumber
    <primitive: 4>
    ^self primitiveFailed!
<= aNumber
    <primitive: 5>
    ^self primitiveFailed!
>= aNumber
    <primitive: 6>
    ^self primitiveFailed!
= aNumber
    <primitive: 7>
    ^self primitiveFailed!
~= aNumber
    <primitive: 8>
    ^self primitiveFailed!
hash
    ^self! !

!SmallInteger methodsFor: 'converting'!
@ aNumber
    <primitive: 18>
    ^self primitiveFailed!
asCharacter
    ^Character value: self! !

!Character methodsFor: 'accessing'!
value
    ^value!
asInteger
    ^value! !

!Character class methodsFor: 'instance creation'!
value: anInteger
    ^CharacterTable at: anInteger + 1! !

!Collection methodsFor: 'accessing'!
size
    <primitive: 62>
    ^self primitiveFailed!
at: index
    <primitive: 60>
    ^self primitiveFailed!
at: index put: value
    <primitive: 61>
    ^self primitiveFailed!
isEmpty
    ^self size = 0! !

!String methodsFor: 'converting'!
asSymbol
    ^self!
asString
    ^self! !

!Symbol methodsFor: 'converting'!
asString
    ^self!
asSymbol
    ^self! !

!Association methodsFor: 'accessing'!
key
    ^key!
value
    ^value!
key: aKey value: aValue
    key := aKey.
    value := aValue! !

!Point methodsFor: 'accessing'!
x
    ^x!
y
    ^y!
setX: ax setY: ay
    x := ax.
    y := ay! !

!Point class methodsFor: 'instance creation'!
x: ax y: ay
    ^self new setX: ax setY: ay! !

!SystemDictionary methodsFor: 'accessing'!
associations
    ^associations! !
