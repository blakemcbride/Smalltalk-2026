"
The binary readers answer nil at the end of the stream, as next does.

1983's nextNumber: and nextWord read bytes with `self next' and did
arithmetic on what came back, and past the end that is nil: `setToEnd;
nextNumber: 2' died with `a UndefinedObject is not a Number', and nextWord
answered false (Bugs6 FILES-10).  A reader that runs out of bytes answers
nil, which every caller can test and which next has always answered.
"
Extension { #name : 'ExternalStream' }

{ #category : '*Files-Fixes' }
ExternalStream >> nextNumber: n [
	"Answer the next n bytes as a positive Integer or
	 LargePositiveInteger, or nil when fewer than n are left."
	| s byte |
	n <= 2
		ifTrue: [
			s := 0.
			n timesRepeat: [
				byte := self next.
				byte isNil ifTrue: [^nil].
				s := s * 256 + byte].
			^s].
	s := LargePositiveInteger new: n.
	1 to: n do: [:i |
		byte := self next.
		byte isNil ifTrue: [^nil].
		s at: n + 1 - i put: byte].
	"reverse order of significance"
	^s truncated
]

{ #category : '*Files-Fixes' }
ExternalStream >> nextWord [
	"Answer the next two bytes from the receiver as an Integer, or nil
	 when there are not two."
	| high low |
	high := self next.
	high isNil ifTrue: [^nil].
	low := self next.
	low isNil ifTrue: [^nil].
	^(high asInteger bitShift: 8) + low asInteger
]
