"
Something the database refused, or something this package asked of it wrongly.

One class and not a hierarchy, on purpose.  The useful distinction is not
between kinds of database error -- a caller almost never wants to handle a
constraint violation and not a deadlock -- but between the message the DRIVER
wrote and anything this image might invent instead.  So the driver's text is
carried through untouched, SQLSTATE and all, and there is nothing else to
know.

If a caller does want to tell one refusal from another, the driver already
told them: the message begins with the SQLSTATE, which is the part that is
the same across every database, and matching on it is more reliable than any
class hierarchy this package could guess at.
"
Class {
	#name : 'DbError',
	#superclass : 'Error',
	#category : 'Database'
}

{ #category : 'accessing' }
DbError >> description [
	^'A database error has occurred'
]
