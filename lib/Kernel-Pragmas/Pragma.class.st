"
An annotation on a method, and what a framework reads to find out what a
method is FOR.

    <test>                        a keyword and no arguments
    <deprecated: 'use #other'>    a keyword and one
    <shared: #serialize>          the parallel-safety audit's own

The Blue Book has one pragma, <primitive: N>, and treats it as syntax
rather than as an object.  Squeak generalised the notation; making the
result readable from Smalltalk is what turns it from a comment the compiler
throws away into something a program can act on.
"
Class {
	#name : 'Pragma',
	#superclass : 'Object',
	#instVars : [ 'keyword', 'arguments', 'method' ],
	#category : 'Kernel-Pragmas'
}

{ #category : 'instance creation' }
Pragma class >> from: aSpec method: aMethod [
	"aSpec is #(keyword arg1 arg2 ...), which is how the compiler stores it."
	| args |
	args := Array new: aSpec size - 1.
	2 to: aSpec size do: [:i | args at: i - 1 put: (aSpec at: i)].
	^self new setKeyword: (aSpec at: 1) arguments: args method: aMethod
]

{ #category : 'private' }
Pragma >> setKeyword: aSymbol arguments: anArray method: aMethod [
	keyword := aSymbol.
	arguments := anArray.
	method := aMethod
]

{ #category : 'accessing' }
Pragma >> keyword [
	^keyword
]

{ #category : 'accessing' }
Pragma >> selector [
	"Pharo's name for the keyword."
	^keyword
]

{ #category : 'accessing' }
Pragma >> arguments [
	^arguments
]

{ #category : 'accessing' }
Pragma >> argumentAt: anIndex [
	^arguments at: anIndex
]

{ #category : 'accessing' }
Pragma >> method [
	^method
]

{ #category : 'testing' }
Pragma >> hasKeyword: aSymbol [
	^keyword = aSymbol
]

{ #category : 'printing' }
Pragma >> printOn: aStream [
	aStream nextPut: $<.
	aStream nextPutAll: keyword.
	arguments do: [:each | aStream space. each printOn: aStream].
	aStream nextPut: $>
]
