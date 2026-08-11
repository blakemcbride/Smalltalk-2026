"
The 1983 system, our library, and one package imported from Pharo.

Collections-Weak is the first real Pharo source this system loads, and it was
chosen because it exercises the part of Phase F with the most machinery behind
it: weak references, which need a collector that does not follow weak slots and
does nil them when what they pointed at goes away.

It comes with Pharo's own tests, which is the whole point.  A class that loads
proves the loader; a class that passes the tests its authors wrote proves the
system underneath it.

WeakArray is excluded from lib/, not from pharo/: this profile is here to run
PHARO's version, and two definitions of one class is an error rather than a
merge.

The four Dictionary tests are excluded because they inherit from DictionaryTest,
which lives in Collections-Unordered-Tests and is not imported.  That is a
dependency and not a failure; importing it is the next ratchet turn rather than
something to fake with a stub.
"
Profile {
	#name     : 'pharo-weak',
	#requires : [ 'st2026' ],
	#dialect  : 'closures',
	#exclude  : [ 'WeakArray',
	              'WeakKeyDictionaryTest', 'WeakValueDictionaryTest',
	              'WeakIdentityKeyDictionaryTest',
	              'WeakIdentityValueDictionaryTest' ],
	#packages : [ '../pharo/Collections-Weak', '../pharo/Collections-Weak-Tests' ]
}
