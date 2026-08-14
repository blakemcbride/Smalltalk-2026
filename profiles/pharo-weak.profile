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

The four Dictionary tests are still excluded: they inherit from DictionaryTest,
which lives in Collections-Unordered-Tests, and loading that here would run all
469 of pharo-collections' tests a second time.  The classes are loaded; only
the test package is not.
"
Profile {
	#name     : 'pharo-weak',
	#requires : [ 'st2026' ],
	#dialect  : 'closures',
	#exclude  : [ 'ManifestCollectionsUnordered', 'HashTableSizesTest',
	              'ManifestSystemTime', 'VirtualMachine',
	              'OrderedDictionary', 'OrderedIdentityDictionary',
	              'KeyedTree', 'KeyedTreeTest',
	              'WeakKeyDictionaryTest', 'WeakValueDictionaryTest',
	              'WeakIdentityKeyDictionaryTest',
	              'WeakIdentityValueDictionaryTest' ],
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
	              '../pharo/Collections-Weak-Tests' ]
}
