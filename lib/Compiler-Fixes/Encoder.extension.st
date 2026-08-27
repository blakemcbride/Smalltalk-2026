Extension { #name : 'Encoder' }

{ #category : '*Compiler-Fixes' }
Encoder >> pushBlockTemp: name [
	"Give name a temporary slot of its own for the length of a block.

	 A slot of its OWN, and that is the difference from autoBind:, which is
	 what a block ARGUMENT uses: an argument of the same name as an
	 enclosing temporary shares that temporary's slot, a 1983 artifact the
	 class library depends on.  A DECLARATION means a new variable, so this
	 always calls newTemp: and remembers whatever binding it displaced.

	 nTemps grows, which is right: MethodNode>>generate: takes the frame
	 size from `encoder maxTemp', and a block temporary needs room in the
	 home context exactly as a method temporary does.  The C bootstrap
	 compiler keeps a max_names for the same reason and the note there says
	 what happens when the frame is undercounted -- the working stack starts
	 on top of a block's slots and the block overwrites it.

	 Answers what popBlockTemp: needs to undo this."
	| displaced node |
	displaced := scopeTable at: name ifAbsent: [nil].
	node := self newTemp: name.
	scopeTable at: name put: node.
	^Association key: node value: displaced
]

{ #category : '*Compiler-Fixes' }
Encoder >> popBlockTemp: aBinding [
	"Put the name back the way pushBlockTemp: found it.

	 Without this the scope table is a flat dictionary of names and a block
	 temporary would outlive its block: a declaration that SHADOWED a method
	 temporary would leave every later mention of that name pointing at the
	 block's slot, which is a wrong answer rather than an error, and one
	 that no test looking at the block itself could see.

	 A binding whose displaced value is the node ITSELF puts the node back
	 where it already is, which is how autoBindBlockArgument: says `leave
	 this one alone' without needing a second kind of answer."
	aBinding value == nil
		ifTrue: [scopeTable removeKey: aBinding key key ifAbsent: []]
		ifFalse: [scopeTable at: aBinding key key put: aBinding value]
]

{ #category : '*Compiler-Fixes' }
Encoder >> bindTemp: name [
	"Declare a temporary, which may SHADOW anything that is not itself a
	 temporary.

	 1983 refuses outright if the name is already in the scope table, and
	 the scope table holds the instance variables too -- so a method could
	 not declare a temporary named like one of its class's instance
	 variables.  That is not a hypothetical restriction here: SocketTest has
	 instance variables `client' and `accepted' and a test method declaring
	 temporaries of both names, and EVERY class-side method with a temporary
	 called `name' hit it, because a metaclass's instances are classes and
	 `name' is one of Class's own instance variables -- TonelWriter
	 class>>write:toFile:, TonelSource class>>fileFor:, TonelReader
	 class>>loadFile: and RestServer class>>fromConfigFile: all failed to
	 re-parse for that one reason.  All of them were compiled INTO the image
	 by the C bootstrap compiler, which shadows.

	 A duplicate TEMPORARY is still refused, because that is a typo rather
	 than a scope: `| a a |' cannot mean anything.

	 The shadow lasts as long as the scope table, which a Compiler makes
	 fresh per method, so nothing outlives the compile."
	| existing |
	existing := scopeTable at: name ifAbsent: [nil].
	(existing notNil and: [existing isTemp])
		ifTrue: [^self notify: 'Name is already defined'].
	^self reallyBindTemp: name
]

{ #category : '*Compiler-Fixes' }
Encoder >> autoBindBlockArgument: name [
	"Bind a block argument, shadowing an instance variable or a global.

	 1983's autoBind: refuses both -- `Name already used in this class' and
	 `Name already used in a Pool or Global' -- and the image contains a
	 method that trips the second: BlockContext>>hasMethodReturn scans with
	 `[:byte | ...]' and `byte' is a global.  The C compiler shadows, and so
	 must this or the image cannot re-parse its own source.  A name found in
	 a pool takes a slot of its own and gives the pool variable back at the
	 end of the block, which is what makes the shadow local rather than a
	 change to what the rest of the method means by that name.

	 Where the name is free or already a temporary, this is EXACTLY 1983's
	 autoBind: and deliberately so.  A block argument sharing the slot of an
	 enclosing temporary of the same name is a 1983 artifact the class
	 library depends on, and it is also what keeps two sibling blocks that
	 both say `[:i | ...]' inside one frame -- a method may have at most 31
	 temporaries, and giving every block argument a slot of its own would
	 spend that ceiling on methods that used to fit.  Answering an
	 Association from the node to ITSELF is what makes popBlockTemp: leave
	 such a binding exactly where it is."
	| existing node |
	existing := scopeTable at: name ifAbsent: [nil].
	existing notNil ifTrue: [
		existing isTemp ifFalse: [^self pushBlockTemp: name].
		"1983 shares the enclosing temporary's slot and leaves it in place."
		^Association key: existing value: existing].
	(self lookupInPools: name ifFound: [:assoc | assoc])
		ifTrue: [^self pushBlockTemp: name].
	node := self reallyBindTemp: name.
	^Association key: node value: node
]
