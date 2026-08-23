"
File out a whole system category in the host's line ending.
"
Extension { #name : 'SystemOrganizer' }

{ #category : '*Files-Fixes' }
SystemOrganizer >> fileOutCategory: category [
	"File out every class in the category.  The 1983 method with
	 `Disk file:' replaced by `Disk textFile:'; see TextFileStream."

	| aFileStream |
	aFileStream := Disk textFile: (category , '.st.') asFileName.
	self fileOutCategory: category on: aFileStream.
	aFileStream shorten; close
]
