"
Collections-Unordered: the first Tier 2 substitution, and the one the plan
warns about.

Announcements and Chronology were Tier 1 -- they added classes the 1983
library had never heard of, so nothing could be broken by their arrival.
This replaces HashedCollection, Set, Dictionary and their kin, which the
whole 1983 library and the C bootstrap both stand on.  Three things make it
a different kind of turn:

The shapes differ.  1983's Set is `Collection variableSubclass:' and keeps
its elements in the INDEXED part; Pharo's keeps them in an `array' instance
variable.  1983's Dictionary is a Set subclass holding Associations in the
indexed part; Pharo's is a HashedCollection subclass holding them in array.
Anything that reaches inside -- and the C bootstrap does, at four sites --
is reaching for a different place afterwards.

The hierarchy differs.  1983 runs HashedCollection-less: Collection -> Set ->
Dictionary -> IdentityDictionary -> MethodDictionary.  Pharo has
HashedCollection under Collection, with Set and Dictionary as siblings.
SystemDictionary and LiteralDictionary are 1983 classes that inherit from
Dictionary and are frozen, so they inherit whatever this profile provides.

MethodDictionary is the exception that makes the turn thinkable at all.  The
VM reads method dictionaries from C -- tally at instance variable 0, the
value array at 1, keys in the indexed part from word 2 -- and Pharo's
MethodDictionary has exactly that shape, for exactly the reason 1983 did:
one array of selectors and one of methods, rather than thousands of
Associations.  So the one class the interpreter cannot afford to have
change is the one that does not.

HashTableSizesTest is excluded because lib/Collections-Compat replaces the
table it tests with a literal -- the search that builds it does not finish
under this interpreter.  KeyedTreeTest is excluded because KeyedTree is,
and a test whose subject is absent reports ten failures that say nothing.

The package's own tests are NOT loaded here.  They root on
CollectionRootTest, which lives in Collections-Abstract-Tests, and pulling
that in is the next package rather than part of this one.  What this profile
proves is that the st2026 suites pass against Pharo's collections; what it
does not yet prove is that Pharo's collections pass their own.
"
Profile {
	#name     : 'pharo-collections',
	#requires : [ 'st2026' ],
	#dialect  : 'closures',
	#exclude   : [ 'ManifestCollectionsUnordered', 'HashTableSizesTest',
	               'OrderedDictionary', 'OrderedIdentityDictionary',
	               'KeyedTree', 'KeyedTreeTest' ],
	#supersede : [ 'Set', 'Dictionary', 'IdentityDictionary', 'IdentitySet',
	               'Bag', 'MethodDictionary' ],
	#packages : [ '../pharo/Collections-Unordered',
	              '../pharo/Kernel-CodeModel-MethodDictionary',
	              '../lib/Collections-Compat',
	              '../pharo/Collections-Abstract-Tests',
	              '../pharo/Collections-Unordered-Tests' ]
}
