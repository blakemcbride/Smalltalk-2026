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
"
Profile {
	#name     : 'st2026',
	#requires : [ 'bluebook' ],
	#dialect  : 'closures',
	#supersede : [ 'SharedQueue' ],
	#packages : [ '../lib/Kernel', '../lib/Kernel-Exceptions', '../lib/Kernel-Protocol', '../lib/Kernel-Methods', '../lib/Kernel-Pragmas', '../lib/Collections-Protocol', '../lib/Streams-Protocol',
	              '../lib/Strings-Protocol', '../lib/System', '../lib/Concurrency', '../lib/Concurrency-Tests', '../lib/SUnit', '../lib/SUnit-Tests',
	              '../lib/Graphics-Fixes', '../lib/Library-Tests',
	              '../lib/Probe' ]
}
