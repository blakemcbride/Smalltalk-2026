"
What line ending this host writes, and a file opened to write it.
"
Extension { #name : 'FileStream' }

{ #category : '*Files-Fixes' }
FileStream class >> nativeLineEnd [
	"The line ending native to the machine this is running on: a line feed
	 everywhere except Windows, which takes a carriage return and a line
	 feed.

	 A primitive because the image has no other way to find out, and asked
	 at RUN time rather than baked in at bootstrap, so that an image built
	 on one host and opened on another answers for the host it is on."

	<primitive: 254>
	^String with: (Character value: 10)
]
