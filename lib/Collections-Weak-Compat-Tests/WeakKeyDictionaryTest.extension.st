"
One of Pharo's weak-dictionary tests, written for this scheduler.

WeakKeyDictionaryTest>>testClearing collects, then asserts on the next line
that the dictionary is still full -- `keys are gone but not yet finalized'
-- and on the line after that that it is empty.  Both are true only if
#mourn is sent to a thousand associations in the gap between two
consecutive sends.  On Cog it is: the interrupt check falls on a method
return, so `dict size' has already answered 1001 when the finalization
process takes over.  Here SCHED_check_process_switch runs once per
BYTECODE, so the finalization process, which outranks the test, gets the
processor before `size' is sent and answers 1.  Nothing about the
finalization is wrong and nothing about the test is wrong; they disagree
about where a preemption lands, which is not what a weak dictionary
promises.  What it promises -- the keys are gone once collected, and the
size follows once they are mourned -- is what this version asserts.

Loaded after pharo/Collections-Weak-Tests, so that it is the test.
"
Extension { #name : 'WeakKeyDictionaryTest' }

{ #category : '*Collections-Weak-Compat-Tests' }
WeakKeyDictionaryTest >> testClearing [
	"Pharo's, less the assertion about the instant between the collection
	 and the finalization; see the comment above."
	keys := nil.
	dict at: self put: nil.
	self assert: dict size equals: self size + 1.

	Smalltalk garbageCollect.

	"The keys are gone."
	self assert: dict keys size equals: 1.

	"And, mourned, the size is adjusted.  The finalization process runs
	 above this one and has done so by now; the yield is for a scheduler
	 that would not have let it."
	Processor yield.
	self assert: dict size equals: 1
]
