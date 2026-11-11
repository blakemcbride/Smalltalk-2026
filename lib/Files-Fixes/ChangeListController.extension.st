"
The change list's `file out', written as every other file out is.
"
Extension { #name : 'ChangeListController' }

{ #category : '*Files-Fixes' }
ChangeListController >> fileOut [
	"Ask for a file name, and write the changes the list shows to it.

	 1983 opened the file itself with `Disk file:' and wrote the changes
	 into it, so a file out that stopped partway -- a change whose text
	 could no longer be read -- left the pages written so far in front of
	 whatever an older file of the name held past them, and the stream
	 open: what the class, category and protocol file outs did until they
	 were given FileDirectory>>replaceTextFile:using: (Bugs07 COMP-2).
	 This one was left out of that, and goes through it now, by
	 ChangeList>>fileOutNamed: (Bugs07 X-20); the file is written with
	 the host's line ending, as theirs are, and the change list's own
	 `file in' reads either.  The prompt, the empty answer that does
	 nothing and the check of the name are 1983's getFile:withPrompt:do:."

	| fileName |
	self controlTerminate.
	fileName := FillInTheBlank request: 'type name of file for writing' initialAnswer: ''.
	(fileName isNil or: [fileName isEmpty]) ifFalse: [
		(Disk isLegalFileName: fileName)
			ifTrue: [Cursor wait showWhile: [model fileOutNamed: fileName]]
			ifFalse: [Transcript cr; show: 'You specified an illegal file name.']].
	self controlInitialize
]
