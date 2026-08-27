"
Something this package was asked to do with bytes it could not: Base64
text with a character outside the alphabet, an iteration count no caller
means, a primitive called with the wrong kind of argument.

One class and not a hierarchy, as NetError and JSONError are one.  The
distinction that matters to a caller is not between kinds of failure but
between a failure and a false: PasswordHash>>verify:against: never raises
for a stored value in the wrong shape -- a wrong shape is a wrong password,
and answers false -- so what does raise here is a fault in the caller's
code, which is what an Error is for.
"
Class {
	#name : 'CryptoError',
	#superclass : 'Error',
	#category : 'Crypto'
}

{ #category : 'accessing' }
CryptoError >> description [
	^'A cryptographic operation failed'
]
