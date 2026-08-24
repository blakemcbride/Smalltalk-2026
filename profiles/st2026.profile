"
The Blue Book system plus our own library.

lib/ is where this system diverges from 1983.  Nothing in sources/ is ever
edited; every difference is a file here, so 'how far have we drifted' has a
mechanical answer.

SharedQueue is excluded from the 1983 sources because lib/Concurrency
replaces it.  The original guards an OrderedCollection with two Semaphores,
which is correct when 'concurrent' means green processes interleaved by one
interpreter and wrong when eight native threads are inside it at once.
This is the substitution ratchet: one class, one provider, named here.

lib/Database comes after lib/Concurrency because it uses Mutex, and after
lib/Collections-Protocol because it uses keysAndValuesDo: and asByteArray.
It loads whether or not this build has ODBC: without one, Odbc class>>isAvailable
answers false and every attempt to connect raises with a sentence saying so.
Compiling it out instead would turn `no database on this machine' into a
doesNotUnderstand on a class that does not exist.
"
Profile {
	#name     : 'st2026',
	#requires : [ 'bluebook' ],
	#dialect  : 'closures',
	#supersede : [ 'SharedQueue' ],
	#packages : [ '../lib/Kernel', '../lib/Kernel-Exceptions', '../lib/Kernel-Protocol', '../lib/Kernel-Methods', '../lib/Kernel-Pragmas', '../lib/Collections-Protocol', '../lib/Streams-Protocol',
	              '../lib/Strings-Protocol', '../lib/System', '../lib/Concurrency', '../lib/Concurrency-Tests', '../lib/SUnit', '../lib/SUnit-Tests',
	              '../lib/Graphics-Fixes', '../lib/Scope-Fixes', '../lib/Files-Fixes',
	              '../lib/Scroll-Wheel', '../lib/Cursor-Keys', '../lib/Browser-Sorting', '../lib/Confirm-Nag', '../lib/Library-Tests',
	              '../lib/Database', '../lib/Database-Tests',
	              '../lib/Probe' ]
}
