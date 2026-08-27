"
Smalltalk arguments: the words after `st80 -serve <image> [-workers n]';
Smalltalk environmentAt: an environment variable's value, or nil.
"
Extension { #name : 'SystemDictionary' }

{ #category : '*Network' }
SystemDictionary >> environmentAt: aName [
	"The environment variable's value, or nil: where an API key belongs.

	    Smalltalk environmentAt: 'ANTHROPIC_API_KEY'"

	^Socket environmentAt: aName
]

{ #category : '*Network' }
SystemDictionary >> arguments [
	"The command-line words meant for the image, as an Array of Strings.
	 Empty in every run mode but -serve, which is the only one that takes
	 them; a server reads the name of its configuration file here."

	^Socket arguments
]
