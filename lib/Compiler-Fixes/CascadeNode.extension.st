"
A cascade the Browser's format prints keeps its parentheses.

1983's CascadeNode answers printOn:indent:precedence: with ParseNode's, which
ignores the precedence and never parenthesises.  A cascade is a whole
expression -- it binds more loosely than any message -- so wherever one is
the receiver or an argument of a message it needs parentheses to stay one,
and without them format rewrote LinkedList>>copy's

	copy addLast: (each shallowCopy nextLink: nil; yourself)

as `copy addLast: (each shallowCopy) nextLink: nil; yourself', which sends
addLast:nextLink: to copy -- and `Array with: (3 + 4; yourself)', which
answers an Array holding 3, as a cascade sent to the class Array.  A brace
array is a cascade as well (see Parser>>braceExpression), so `{a. b} size'
went the same way.  Bugs5 COMP-6.
"
Extension { #name : 'CascadeNode' }

{ #category : '*Compiler-Fixes' }
CascadeNode >> printOn: aStream indent: level precedence: p [
	"In parentheses, always.  printOn:indent:precedence: is asked only of a
	 node that is the receiver or an argument of a message -- a statement,
	 a return and an assignment's value print through printOn:indent: --
	 and in every one of those places a cascade written bare would be
	 read as part of the message around it.  Even at precedence 0, which
	 is how a CascadeNode prints its own receiver: the receiver of
	 `(a b; c) d; e' is itself a cascade."

	aStream nextPut: $(.
	self printOn: aStream indent: level.
	aStream nextPut: $)
]
