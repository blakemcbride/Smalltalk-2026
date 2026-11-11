"
The change list's `file out', written as every other file out is.
"
Extension { #name : 'ChangeList' }

{ #category : '*Files-Fixes' }
ChangeList >> fileOutNamed: aFileName [
	"Write the changes I show, as fileOutOn: writes them, to the file of
	 that name in Disk, through FileDirectory>>replaceTextFile:using: --
	 so the file is the new one whole, or, when a change cannot be
	 written, an older file of the name exactly as it was and no stream
	 left open (Bugs07 X-20).  What ChangeListController>>fileOut does
	 with the name it asks for."

	^Disk replaceTextFile: aFileName using: [:aFile | self fileOutOn: aFile]
]
