Extension { #name : 'Behavior' }

{ #category : '*Kernel-Reshape' }
Behavior >> primMigrate: oldInstances to: newInstances permuting: aMap oldNamed: oldInstSize [
	"Copy each of oldInstances into the matching one of newInstances and
	 exchange their identities, all of it with every worker parked.

	 aMap is one entry per named field of the NEW shape, holding the index
	 of the field it comes from in the old shape or 0 for one the old shape
	 did not have; oldInstSize is how many named fields that old shape had,
	 which is where the indexed fields start in the objects being read.

	 ALL THREE MUST BE ARRAYS.  This reads their slots directly, so a
	 collection that keeps its elements behind named instance variables --
	 an OrderedCollection, which is what allInstances and allInstVarNames
	 both answer -- offers firstIndex as its first element and is refused.

	 Answers the receiver, or nil if it refused -- an instance that cannot
	 change identity, or arguments that are not three Arrays and a count.
	 Nil is not an error: updateInstancesFrom: then does the same work the
	 1983 way, which is what this image did before there was a primitive.

	 THE NEW INSTANCES ARE MADE OUT HERE, and that is not an oversight.
	 Allocating inside a safepoint can want a collection, a collection wants
	 a safepoint of its own, and safepoints do not nest: it would deadlock
	 rather than run slowly.  What has to be indivisible is the copy and the
	 swap, and only those are in there."

	<primitive: 234>
	^nil
]
