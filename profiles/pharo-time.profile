"
Chronology: the second real turn of the ratchet, and a much larger bite
than the first.

Twenty-eight classes, and unlike Announcements-Core it touches the outside
world -- DateAndTime now has to ask somebody what time it is -- so this is
also the first Pharo package whose gaps will include VM services rather
than only missing protocol.

Date and Time exist in 1983 too and are superseded rather than excluded:
Pharo's are richer and are what the rest of the package is built on.  The
1983 image still sends them `Time millisecondClockValue',
`Time millisecondsToRun:', `Time now', `Date today' and `Date newDay:', so
whatever of that Pharo does not provide has to be added on our side rather
than by editing the import.

VirtualMachine.extension.st is excluded: it extends Pharo's own VM
reflection class, which we do not have and which describes a VM that is
not this one.  Of the five extension files in the package it is the only
one whose class is missing; the other four extend Integer, Number, String
and BlockClosure, which is where `3 seconds' and `1 day' come from.
"
Profile {
	#name     : 'pharo-time',
	#requires : [ 'st2026' ],
	#dialect  : 'closures',
	#exclude   : [ 'ManifestSystemTime', 'ManifestSystemTimeTests',
	               'VirtualMachine', 'ExceptionTest', 'ExceptionTester' ],
	#supersede : [ 'Date', 'Time' ],
	#packages : [ '../pharo/System-Time', '../lib/Chronology-Compat',
	              '../pharo/System-Time-Tests' ]
}
