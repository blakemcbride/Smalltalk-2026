"
An index that is not one of the receiver's.

1983 reports this with `self error:', which raises a plain Error carrying a
sentence.  A sentence is fine for a person reading a debugger and useless to
a HANDLER: code that means to catch a bad index and nothing else has to
either catch every Error or match on the text, and both are wrong -- the
first swallows real faults, the second breaks when somebody rewords the
message.

So the index error gets a class of its own.  It stays under Error, so
everything that caught it before still does.
"
Class {
	#name : 'SubscriptOutOfBounds',
	#superclass : 'Error',
	#category : 'Kernel-Exceptions'
}
