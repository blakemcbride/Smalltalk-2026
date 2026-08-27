"
The image's own compiler reads what the C bootstrap compiler wrote.

Walking every class and metaclass of a bootstrapped image, taking each
method's source and handing it back to `Compiler new parse:in:notifying:',
86 of 6,843 methods failed to parse -- ProcessorScheduler>>yield,
String>>hash, Object>>dependents, TonelReader>>load,
RestDispatcher>>serve:method:request:, Socket>>startTls:timeout: among
them.  Every one of them had been compiled INTO that image by the C
compiler in src/compiler/ minutes earlier.

That is not a curiosity.  It means the System Browser cannot edit the
system's own source -- open Crypto class>>hex:, change nothing, accept, and
the requestor is told `syntax error' (or, with no requestor, the image
hangs; see Compiler>>notify:at: in this package) -- and it means a Tonel
file that loaded at bootstrap may not load at run time, which is the REST
server's headline property: edit a service file on a running server and the
next call runs the new code.

Two constructs accounted for all 86, and two more were accepted by the C
compiler and refused here without lib/ happening to use them:

  [:i | | t | ... ]      block temporaries.  Parser>>statements:innerBlock:
                         never looked for a declaration.
  <primitive: n> before  Parser>>method:context: read the temporaries
  | temps |              first and the pragma second, which is 1983's order
                         and the reverse of Tonel's and Pharo's.
  #[1 2 3]               a byte array literal.
  {1. 2. 3}              a dynamic array.

All four are here.  Nothing in sources/ is edited; the four methods that
had to change are overridden whole, and each says what changed.
"
Extension { #name : 'Parser' }

{ #category : '*Compiler-Fixes' }
Parser >> method: doit context: ctxt [
	"pattern [ <primitive: n> ] [ | temporaries | ] [ <primitive: n> ] block

	 1983 accepts the pragma only AFTER the temporaries.  Tonel writes it
	 before, which is Pharo's shape and the shape lib/ is written in --
	 String>>hash and Float>>truncated both read

		hash
			<primitive: 223>
			| h |

	 -- and the C bootstrap compiler takes it either way.  So this asks for
	 pragmas on BOTH sides of the temporaries and takes whichever answers;
	 pragmasInto: consumes nothing when the next token is not a `<', so
	 asking twice costs one comparison.

	 It also accepts pragmas that are not `primitive:', which 1983 has no
	 notion of and Squeak generalised the notation for: `<test>',
	 `<deprecated: ''use #normal''>'.  They are kept rather than discarded
	 -- see rememberPragmas: -- because the C compiler keeps them and a
	 method recompiled in the Browser must not quietly lose what it
	 declared."

	| sap blk prim temps messageComment methodNode specs |
	sap := self pattern: doit inContext: ctxt.
	"sap={selector, arguments, precedence}"
	(sap at: 2) do: [:argNode | argNode isArg: true].
	specs := OrderedCollection new.
	prim := 0.
	doit ifFalse: [prim := self pragmasInto: specs].
	temps := self temporaries.
	messageComment := currentComment.
	currentComment := nil.
	doit ifFalse: [
		| after |
		after := self pragmasInto: specs.
		after = 0 ifFalse: [prim := after]].
	self rememberPragmas: specs.
	self statements: #() innerBlock: doit.
	blk := parseNode.
	doit
		ifTrue: [blk returnLast]
		ifFalse: [blk returnSelfIfNoOther].
	hereType == #doIt ifFalse: [^self expected: 'Nothing more'].
	methodNode := MethodNode new comment: messageComment.
	^methodNode
		selector: (sap at: 1)
		arguments: (sap at: 2)
		precedence: (sap at: 3)
		temporaries: temps
		block: blk
		encoder: encoder
		primitive: prim
]

{ #category : '*Compiler-Fixes' }
Parser >> blockExpression [
	"[ {:var} [ | temporaries | ] ( | statements) ] => BlockNode

	 1983's, plus the temporaries.  There is no ambiguity to resolve here,
	 unlike in the C compiler, which has to read ahead and rewind: by the
	 time this looks, a block's ARGUMENT bar has already been eaten, so
	 `[:a | b | c]' -- the binary send `b | c' -- is sitting on the word b
	 and never enters blockTemporaries at all.  A bar in this position can
	 only open a declaration.

	 The declarations are attached AFTER the statements are parsed, because
	 they become statements themselves -- see BlockTemporaryNode -- and
	 they leave scope immediately afterwards, which is what stops a
	 declaration that shadows a method temporary from outliving its block."

	| argNodes argBindings tempBindings |
	argNodes := OrderedCollection new.
	argBindings := OrderedCollection new.
	[self match: #colon "gather any arguments"]
		whileTrue: [
			argBindings addLast: (encoder autoBindBlockArgument: self argumentName).
			argNodes addLast: argBindings last key].
	(argNodes size > 0 & (hereType ~~ #rightBracket) and: [(self match: #verticalBar) not])
		ifTrue: [^self expected: 'Vertical bar'].
	tempBindings := self blockTemporaries.
	self statements: argNodes innerBlock: true.
	tempBindings isEmpty ifFalse: [
		parseNode declareTemporaries: (tempBindings collect: [:each | each key])].
	tempBindings do: [:each | encoder popBlockTemp: each].
	argBindings do: [:each | encoder popBlockTemp: each].
	(self match: #rightBracket)
		ifFalse: [^self expected: 'Period or right bracket']
]

{ #category : '*Compiler-Fixes' }
Parser >> blockTemporaries [
	"[ '|' (variable)* '|' ] at the head of a block, or nothing.

	 Deliberately not Parser>>temporaries, for two reasons.  That one sets
	 lastTempMark, which is where pasteTemp: inserts a declaration the
	 Browser offers to write for an undeclared name -- and the METHOD's bar
	 is where that belongs, not a block's.  And it binds with bindTemp:,
	 which refuses a name already in scope; a block declaration is allowed
	 to shadow one, so it binds with pushBlockTemp: and hands back what is
	 needed to unbind."

	| bindings |
	(self match: #verticalBar) ifFalse: [^#()].
	(self match: #verticalBar) ifTrue: [^#()].	"empty: | |"
	bindings := OrderedCollection new.
	[hereType == #word] whileTrue: [
		bindings addLast: (encoder pushBlockTemp: self advance)].
	(self match: #verticalBar) ifTrue: [^bindings].
	^self expected: 'Vertical bar'
]

{ #category : '*Compiler-Fixes' }
Parser >> primaryExpression [
	"1983's, plus { expression. expression } -- a dynamic array.

	 #[1 2 3] needs nothing here: the Scanner builds it into a literal, so
	 it arrives as #literal like #(1 2 3) does.  A brace array cannot,
	 because its elements are EXPRESSIONS evaluated when the method runs,
	 not literals read when it is compiled."

	hereType == #word
		ifTrue: [
			parseNode := encoder encodeVariable: self advance.
			^true].
	hereType == #leftBracket
		ifTrue: [
			self advance.
			self blockExpression.
			^true].
	hereType == #leftBrace
		ifTrue: [
			self advance.
			self braceExpression.
			^true].
	hereType == #leftParenthesis
		ifTrue: [
			self advance.
			self expression ifFalse: [^self expected: 'expression'].
			(self match: #rightParenthesis) ifFalse: [^self expected: 'right parenthesis'].
			^true].
	(hereType == #string or: [hereType == #number or: [hereType == #literal]])
		ifTrue: [
			parseNode := encoder encodeLiteral: self advance.
			^true].
	(here == #- and: [tokenType == #number])
		ifTrue: [
			self advance.
			parseNode := encoder encodeLiteral: self advance negated.
			^true].
	^false
]

{ #category : '*Compiler-Fixes' }
Parser >> braceExpression [
	"{ expression. expression } => a CascadeNode building an Array.

	 The opening brace has been consumed.  There is no bytecode for this and
	 no need for one: `{a. b}' is exactly

		(Array new: 2) at: 1 put: a; at: 2 put: b; yourself

	 and a cascade already compiles to `receiver, dup, send, pop, ...',
	 which is the sequence the C compiler emits by hand.  Building it as a
	 parse tree rather than as bytecode is also what makes the size pass and
	 the source map work without knowing this construct exists.

	 The trailing yourself is what answers the Array: at:put: answers the
	 value it stored.  An empty {} is `(Array new: 0) yourself', which is
	 the cascade with one message and no dup."

	| elements receiver messages message |
	elements := OrderedCollection new.
	[hereType == #rightBrace] whileFalse: [
		self expression ifFalse: [^self expected: 'expression'].
		elements addLast: parseNode.
		hereType == #rightBrace ifFalse: [
			(self match: #period) ifFalse: [^self expected: 'Period or right brace']]].
	(self match: #rightBrace) ifFalse: [^self expected: 'Right brace'].
	receiver := MessageNode new
		receiver: (encoder encodeVariable: 'Array')
		selector: #new:
		arguments: (Array with: (encoder encodeLiteral: elements size))
		precedence: 3
		from: encoder.
	messages := OrderedCollection new.
	1 to: elements size do: [:index |
		message := MessageNode new
			receiver: receiver
			selector: #at:put:
			arguments: (Array
				with: (encoder encodeLiteral: index)
				with: (elements at: index))
			precedence: 3
			from: encoder.
		message cascadeReceiver.
		messages addLast: message].
	message := MessageNode new
		receiver: receiver
		selector: #yourself
		arguments: #()
		precedence: 1
		from: encoder.
	message cascadeReceiver.
	messages addLast: message.
	parseNode := CascadeNode new receiver: receiver messages: messages.
	^true
]

{ #category : '*Compiler-Fixes' }
Parser >> pragmasInto: aCollection [
	"Read every < ... > annotation that is here, answering the primitive
	 number among them, or zero.

	 Everything else is added to aCollection as an Array whose first element
	 is the keyword Symbol and whose rest are its arguments -- the shape
	 AdditionalMethodState holds and the same one the C bootstrap compiler
	 builds."
	| primitiveNumber spec |
	primitiveNumber := 0.
	[(spec := self pragma) notNil] whileTrue: [
		(spec size = 2
			and: [(spec at: 1) == #primitive: and: [(spec at: 2) isInteger]])
				ifTrue: [primitiveNumber := spec at: 2]
				ifFalse: [aCollection addLast: spec]].
	^primitiveNumber
]

{ #category : '*Compiler-Fixes' }
Parser >> pragma [
	"One < ... > annotation, or nil if there is not one here.

	 Answers an Array: the keyword Symbol, then one element per argument.
	 A unary pragma -- `<test>' -- is a one-element Array."
	| keyword parts |
	(here == #< and: [hereType == #binary]) ifFalse: [^nil].
	self advance.
	hereType == #word ifTrue: [
		keyword := self advance asSymbol.
		(self matchToken: #>) ifFalse: [^self expected: '>'].
		^Array with: keyword].
	hereType == #keyword ifFalse: [^self expected: 'a pragma keyword'].
	keyword := WriteStream on: (String new: 24).
	parts := OrderedCollection new.
	[hereType == #keyword] whileTrue: [
		keyword nextPutAll: self advance.
		parts addLast: self pragmaArgument].
	(self matchToken: #>) ifFalse: [^self expected: '>'].
	^(Array with: keyword contents asSymbol) , parts
]

{ #category : '*Compiler-Fixes' }
Parser >> pragmaArgument [
	"One argument of a pragma: a literal, and nothing that has to be run.

	 nil, true and false arrive as words rather than as literals, exactly as
	 they do in an expression, so they are named here.  Any other word is
	 taken as a Symbol, which is what Pharo does and what makes
	 `<primitive: 60 error: ec>' -- where ec names a temporary rather than
	 a value -- read without a special case."
	(here == #- and: [tokenType == #number])
		ifTrue: [self advance. ^self advance negated].
	(hereType == #number or: [hereType == #string or: [hereType == #literal]])
		ifTrue: [^self advance].
	hereType == #word ifTrue: [
		here = 'nil' ifTrue: [self advance. ^nil].
		here = 'true' ifTrue: [self advance. ^true].
		here = 'false' ifTrue: [self advance. ^false].
		^self advance asSymbol].
	^self expected: 'a literal pragma argument'
]

{ #category : '*Compiler-Fixes' }
Parser >> rememberPragmas: specs [
	"Put the method's pragmas in its literal frame.

	 As an AdditionalMethodState, which is where CompiledMethod>>pragmas
	 looks for them -- it scans the frame for one by class rather than by
	 position, so putting it in the stream here is all that is needed and
	 MethodNode>>generate: does not have to know it exists.

	 Ahead of whatever Encoder>>literals:nArgs: appends, which is where the
	 class association goes when there is one; a super send takes its lookup
	 class from the last literal and must not find this instead.

	 An image whose profile has no AdditionalMethodState -- bluebook loads
	 sources/ alone -- compiles the method exactly as it did before, without
	 the pragmas, which is better than refusing source it can otherwise
	 read."
	| state |
	specs isEmpty ifTrue: [^self].
	(Smalltalk includesKey: #AdditionalMethodState) ifFalse: [^self].
	state := AdditionalMethodState new pragmaSpecs: specs asArray.
	encoder litIndex: state
]
