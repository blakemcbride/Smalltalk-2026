"
The Blue Book system plus our own library.

lib/ is where this system diverges from 1983.  Nothing in sources/ is ever
edited for BEHAVIOUR; every difference is a file here, so 'how far have we
drifted' has a mechanical answer.

The one thing sources/ has been edited for is spelling.  Fifty-seven places
wrote 1983's assignment arrow glued to the name before it -- `runs_ runs
copyWith: 1' -- which 1983's parser reads as an assignment and the closures
lexer reads as an identifier ending in an underscore, so those methods
could not be recompiled from the source the system itself shows.  They are
written `runs _ runs copyWith: 1' now.  Whitespace only, in both dialects,
and no method's meaning moved.

SharedQueue is excluded from the 1983 sources because lib/Concurrency
replaces it.  The original guards an OrderedCollection with two Semaphores,
which is correct when 'concurrent' means green processes interleaved by one
interpreter and wrong when eight native threads are inside it at once.
This is the substitution ratchet: one class, one provider, named here.

lib/Compiler-Fixes teaches the image's own compiler the four constructs the
C bootstrap compiler in src/compiler/ already accepts -- block temporaries,
a pragma before the temporaries, #[1 2 3] and {1. 2. 3} -- without which 86
of the image's own 6,843 methods could not be re-parsed by the image that
holds them.  It also stops a syntax error with no requestor from opening a
window and suspending the process, which headless is a hang.

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
	#packages : [ '../lib/Kernel', '../lib/Kernel-Exceptions', '../lib/Kernel-Protocol', '../lib/Kernel-Methods', '../lib/Kernel-Pragmas', '../lib/Kernel-Reshape', '../lib/Collections-Protocol', '../lib/Streams-Protocol',
	              '../lib/Strings-Protocol', '../lib/System', '../lib/Concurrency', '../lib/Concurrency-Tests', '../lib/Kernel-Finalization', '../lib/SUnit', '../lib/SUnit-Tests',
	              '../lib/Graphics-Fixes', '../lib/Scope-Fixes', '../lib/Compiler-Fixes', '../lib/Files-Fixes',
	              '../lib/Scroll-Wheel', '../lib/Keyboard-Map', '../lib/Clipboard', '../lib/Clipboard-Tests', '../lib/Browser-Sorting', '../lib/Confirm-Nag', '../lib/Library-Tests',
	              '../lib/Database', '../lib/Database-Tests',
	              '../lib/Json', '../lib/Json-Tests',
	              '../lib/Crypto', '../lib/Crypto-Tests',
	              '../lib/Network', '../lib/Network-Tests',
	              '../lib/Tonel', '../lib/Tonel-Tests',
	              '../lib/JSON-RPC-Server', '../lib/JSON-RPC-Server-Tests',
	              '../lib/HTTP-Client', '../lib/HTTP-Client-Tests',
	              '../lib/LLM', '../lib/LLM-Tests',
	              '../lib/Rest-Server', '../lib/Rest-Server-Tests',
	              '../lib/Web-Demo-Tests',
	              '../lib/Probe' ]
}
