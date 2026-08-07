"
An extension: methods added to a class this package did not define.

The chunk format cannot say this at all, and the loader needs nothing for
it -- an extension file emits no class definition and a few methods naming
a class from somewhere else.
"
Extension { #name : 'Object' }

{ #category : '*Probe-Core' }
Object >> asProbeLength [
	^Length of: self
]

{ #category : '*Probe-Core' }
Object class >> probePackageName [
	^'Probe'
]
