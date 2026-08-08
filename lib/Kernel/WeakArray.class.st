"
An Array whose elements do not keep their contents alive.

The collector does not follow a weak object's INDEXED fields, so anything
reached only through one of them is collected and the slot is set to nil.
The named fields at the front stay strong, which is the Squeak arrangement
and the useful one: a WeakArray's own instance variables are not the point
of it.

This is what a registry of things-that-might-go is built on -- caches,
dependents, and Pharo's WeakRegistry.  What it is NOT is an ephemeron: an
ephemeron's key is weak while its value stays strong for as long as the key
lives, and deciding that needs the marker to run to a fixed point rather
than once.  That is a different collector, not a different flag, and the
loader says so by name when a package asks for one.
"
Class {
	#name : 'WeakArray',
	#superclass : 'ArrayedCollection',
	#type : 'weak',
	#category : 'Kernel-Closures'
}

{ #category : 'accessing' }
WeakArray >> at: index [
	<primitive: 60>
	^self primitiveFailed
]

{ #category : 'accessing' }
WeakArray >> at: index put: value [
	<primitive: 61>
	^self primitiveFailed
]

{ #category : 'accessing' }
WeakArray >> size [
	<primitive: 62>
	^self primitiveFailed
]

{ #category : 'testing' }
WeakArray >> livingCount [
	"How many of my slots still hold something."
	| n |
	n := 0.
	1 to: self size do: [:i | (self at: i) isNil ifFalse: [n := n + 1]].
	^n
]
