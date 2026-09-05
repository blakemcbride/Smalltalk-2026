"
Weak references and the classes built on them, on the substituted collections.

Collections-Weak was the first real Pharo source this system loaded, and it was
chosen because it exercises the part of Phase F with the most machinery behind
it: weak references, which need a collector that does not follow weak slots and
does nil them when what they pointed at goes away.

It comes with Pharo's own tests, which is the whole point.  A class that loads
proves the loader; a class that passes the tests its authors wrote proves the
system underneath it.  32 of 32 pass.  It was 12 of 32 for a long time, and
every one of the twenty failures had the same shape: this profile was running
Pharo's weak collections on classes that were not the ones they are written
against.

  Pharo's WeakSet is a Set subclass and reaches straight into Pharo's Set --
  its `array', its `tally', scanFor: and atNewIndex:put:.  This profile did not
  load Pharo's Set, so it got 1983's, which keeps its elements in its own
  indexed part and has none of that.  `anObject hash \\ array size + 1' with no
  array is the `division by 0' that eighteen tests reported.  So the
  Collections-Unordered substitution belongs here too, and with it the rest of
  what that substitution needs.

  WeakOrderedCollectionTest builds a Time and sends it `seconds:', which is
  Pharo's Time and not 1983's, so System-Time comes with it.

  WeakArray was EXCLUDED rather than superseded, and #exclude drops a class
  wherever it comes from -- so both providers went and WeakArray was simply
  absent.  Every WeakSet in this profile had a nil where its storage should be.
  It is superseded now, which is the mechanism that means `use the other one'.

  WeakOrderedCollection is superseded by lib/Collections-Weak-Compat, and that
  one is not a dependency but a difference: Pharo's is one method,
  `arrayType ^WeakArray', and it works because Pharo's OrderedCollection keeps
  its elements in a separate storage array.  1983's has no storage array -- the
  elements are in its own indexed part -- so there is nothing to give a type to
  and the collection ITSELF has to be weak.  See the class comment.

  And the collector marked every slot of a context rather than only those below
  its stack pointer, so an object dropped by the very frame that made the weak
  reference stayed alive in a stale stack slot.  Two WeakSet tests measured
  exactly that.  Contexts are marked precisely now.

The four Dictionary tests are here now, and so is the package their superclass
lives in.  They were excluded because DictionaryTest is in
Collections-Unordered-Tests, and loading it runs pharo-collections' tests a
second time -- which is the price, and the reason it is worth paying is that
until Bugs4 nothing in this profile touched a WEAK KEY.  WeakKeyDictionary and
WeakIdentityKeyDictionary were not weak at all: their keys were held strongly
and never reclaimed, and the profile passed 524 of 524 without noticing,
because the only tests that would have said so were the four excluded ones.
An exclusion that hides the one thing a profile is for is worse than a slow
suite.

Two of those tests are the proof of the repair.  WeakIdentityKeyDictionaryTest
>>testNoNils says a dead key leaves no nil behind in the keys, and
>>testFinalizeValuesWhenLastChainContinuesAtFront builds three objects whose
hashes collide at the end of the table, drops the first, and asks that the
other two still be findable -- which is the case a removal that does not fix
the collision chain gets wrong.

Collections-Abstract-Tests comes with Collections-Unordered-Tests because
DictionaryTest roots on CollectionRootTest, and it brings one thing this
profile did not have before: WeakSetTest asks for TIterateTest and had been
built WITHOUT it, which the loader said every time and nobody read.  With the
trait, twelve of its tests ran for the first time and all twelve failed --
Collection had select:thenCollect: and collect:thenSelect: and none of
collect:thenDo:, select:thenDo:, reject:thenDo:, reject:thenCollect: or
sumNumbers:.  They are in lib/Collections-Protocol now.

One test fails and is left failing, deliberately, because it is a statement
about a scheduler rather than about weak keys.  WeakKeyDictionaryTest
>>testClearing collects, then asserts on the next line that the dictionary is
still full -- "keys are gone but not yet finalized" -- and on the line after
that that it is empty.  Both are true only if #mourn is sent to a thousand
associations in the gap between two consecutive sends.  On Cog it is: the
interrupt check falls on a method return, so `dict size' has already answered
1001 when the finalization process takes over.  Here SCHED_check_process_switch
runs once per BYTECODE, so the finalization process gets the processor before
`size' is sent and answers 1.  Nothing about the fix is wrong and nothing about
the test is wrong; they disagree about where a preemption lands.  The other
1035 pass.
"
Profile {
	#name     : 'pharo-weak',
	#requires : [ 'st2026' ],
	#dialect  : 'closures',
	#exclude  : [ 'ManifestCollectionsUnordered', 'HashTableSizesTest',
	              'ManifestSystemTime', 'VirtualMachine',
	              'OrderedDictionary', 'OrderedIdentityDictionary',
	              'KeyedTree', 'KeyedTreeTest' ],
	#supersede : [ 'Set', 'Dictionary', 'IdentityDictionary', 'IdentitySet',
	               'Bag', 'MethodDictionary', 'WeakArray', 'Date', 'Time',
	               'WeakOrderedCollection' ],
	#packages : [ '../pharo/Collections-Unordered',
	              '../pharo/Collections-Support',
	              '../pharo/Kernel-CodeModel-MethodDictionary',
	              '../lib/Collections-Compat',
	              '../pharo/System-Time',
	              '../lib/Chronology-Compat',
	              '../pharo/Collections-Weak',
	              '../lib/Collections-Weak-Compat',
	              '../pharo/Collections-Abstract-Tests',
	              '../pharo/Collections-Unordered-Tests',
	              '../pharo/Collections-Weak-Tests' ]
}
