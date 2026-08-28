"
The Browser's two paths into a class -- accepting a method and removing one --
with a hook that writes the class back to the Tonel file it came from.

compile:classified:notifying: is what Browser>>acceptMethod:from: sends, and
removeSelector: is what its `remove' does.  Each is 1983's own body, kept as
basicCompile:classified:notifying: and basicRemoveSelector: for TonelReader to
use -- a load must never write -- with the hooked version calling the basic one
and then telling TonelSource.  A class that came from no file is untouched by
the hook: TonelSource looks it up and finds nothing.
"
Extension { #name : 'ClassDescription' }

{ #category : '*Tonel' }
ClassDescription >> basicCompile: code classified: heading notifying: requestor [
	"1983's compile:classified:notifying:, verbatim: compile, log the source,
	 classify.  The layer below the write-back hook."

	| selector |
	selector := self compile: code
			notifying: requestor
			trailer: #(0 0 0 )
			ifFail: [^nil].
	(methodDict at: selector)
		putSource: code asString
		class: self
		category: heading
		inFile: 2.
	self organization classify: selector under: heading.
	^selector
]

{ #category : '*Tonel' }
ClassDescription >> basicInstall: aCompiledMethod selector: aSymbol source: code classified: heading [
	"The second half of basicCompile:classified:notifying:, for a method
	 compiled earlier and not yet installed: install, log the source,
	 classify.  The layer below the write-back hook.

	 TonelReader compiles every method in a file before it installs one,
	 so that a file with a mistake in its seventh method leaves a class
	 with six of neither the old set nor the new (Bugs3 B63); this is what
	 it installs them with, and it is 1983's own three steps with the
	 compile taken out of the front."

	self addSelector: aSymbol withMethod: aCompiledMethod.
	(methodDict at: aSymbol)
		putSource: code asString
		class: self
		category: heading
		inFile: 2.
	self organization classify: aSymbol under: heading
]

{ #category : '*Tonel' }
ClassDescription >> compile: code classified: heading notifying: requestor [
	"Compile and install code under heading, as 1983 did -- and if this class
	 came from a Tonel file, write the file."

	| selector |
	selector := self basicCompile: code classified: heading notifying: requestor.
	selector notNil ifTrue: [TonelSource noteChangeIn: self].
	^selector
]

{ #category : '*Tonel' }
ClassDescription >> basicRemoveSelector: aSymbol [
	"1983's removeSelector:, verbatim.  The layer below the write-back hook."

	(methodDict includesKey: aSymbol) ifFalse: [^nil].
	super removeSelector: aSymbol.
	self organization removeElement: aSymbol.
	Smalltalk changes removeSelector: aSymbol class: self.
	Smalltalk logChange: self name , ' removeSelector: #' , aSymbol
]

{ #category : '*Tonel' }
ClassDescription >> removeSelector: aSymbol [
	"Remove the method, as 1983 did -- and if this class came from a Tonel
	 file, write the file."

	| answer |
	answer := self basicRemoveSelector: aSymbol.
	TonelSource noteChangeIn: self.
	^answer
]
