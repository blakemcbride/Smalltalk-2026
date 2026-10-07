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
	"1983's compile:classified:notifying: -- compile, log the source,
	 classify -- with the source logged BEFORE the method is installed
	 rather than after.  The layer below the write-back hook.

	 1983 compiled and installed in one step and wrote the source second,
	 which was safe while writing the source could not fail.  It can: past
	 four megabytes the changes file's offset no longer fits a method's
	 trailer, and putSource: raises.  Raising second left the method in
	 its class anyway, answering when sent and raising `subscript is out
	 of bounds' when asked for its source, with the source appended to a
	 file nothing pointed into (Bugs5 FILES-3).  Compiled without
	 installing, the method is installed only once its source has a
	 place, and putSource: refuses before it writes; a refusal leaves the
	 class and the file as they were.  The change-set entry and the
	 working cursor are 1983's and are kept."

	| selector method |
	Cursor execute showWhile: [
		self compileOnly: code
			notifying: requestor
			trailer: #(0 0 0)
			ifFail: [^nil]
			into: [:sel :m | selector := sel. method := m]].
	method
		putSource: code asString
		class: self
		category: heading
		inFile: 2.
	(methodDict includesKey: selector)
		ifTrue: [Smalltalk changes changeSelector: selector class: self]
		ifFalse: [Smalltalk changes addSelector: selector class: self].
	self basicInstall: method selector: selector classified: heading.
	^selector
]

{ #category : '*Tonel' }
ClassDescription >> basicInstall: aCompiledMethod selector: aSymbol source: code classified: heading [
	"The second half of basicCompile:classified:notifying:, for a method
	 compiled earlier and not yet installed: log the source, install,
	 classify.  The layer below the write-back hook.

	 TonelReader compiles every method in a file before it installs one,
	 so that a file with a mistake in its seventh method leaves a class
	 with six of neither the old set nor the new (Bugs3 B63); this is what
	 a single method is installed with.  The source goes first, for the
	 reason basicCompile:classified:notifying: gives."

	aCompiledMethod
		putSource: code asString
		class: self
		category: heading
		inFile: 2.
	self basicInstall: aCompiledMethod selector: aSymbol classified: heading
]

{ #category : '*Tonel' }
ClassDescription >> basicInstall: aCompiledMethod selector: aSymbol classified: heading [
	"Install a method whose source has already been logged, and classify
	 it.  TonelReader logs every method's source in a file before it
	 installs any of them, so that a source file that fills up halfway
	 through installs none (Bugs5 FILES-3)."

	self addSelector: aSymbol withMethod: aCompiledMethod.
	self organization classify: aSymbol under: heading
]

{ #category : '*Tonel' }
ClassDescription >> compile: code classified: heading notifying: requestor [
	"Compile and install code under heading, as 1983 did -- and if this class
	 came from a Tonel file, write the file.  Asked first whether the file
	 can be written as the class will be, and refused before anything
	 changes if not: see TonelSource>>checkChangeTo:source:."

	| selector |
	TonelSource checkChangeTo: self source: code.
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
	 file, write the file, having asked first whether it can be."

	| answer |
	TonelSource checkChangeTo: self source: nil.
	answer := self basicRemoveSelector: aSymbol.
	TonelSource noteChangeIn: self.
	^answer
]
