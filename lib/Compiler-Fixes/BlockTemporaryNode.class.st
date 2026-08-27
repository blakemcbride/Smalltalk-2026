"
The nil that starts a block temporary.

`[:each | | v | ... ]' declares v, and in this dialect a block has no frame
of its own -- Chapter 27's BlockContext shares its home's -- so v is a
method temporary like any other and the compiler allocates it a slot.  What
it does NOT get for free is being nil on each entry: a method's temporaries
are nil because the context is fresh, and a block entered a second time
inside a do: would otherwise still see what the first pass left there.

So the parser puts one of me at the head of the block's statements per
declared name, and I am an assignment of nil, which is exactly what the C
bootstrap compiler emits for the same source (`push nil; store; pop', see
the note beside block temporaries in src/compiler/compiler.c).  Being a
class of my own rather than a plain AssignmentNode is what lets BlockNode
print the block back as a DECLARATION instead of as an assignment of nil to
a name nothing declares -- see BlockNode>>printArgumentsOn:indent: in this
package.
"
Class {
	#name : 'BlockTemporaryNode',
	#superclass : 'AssignmentNode',
	#category : 'Compiler-Fixes'
}

{ #category : 'accessing' }
BlockTemporaryNode >> variable [
	"The variable node I declare."
	^variable
]
