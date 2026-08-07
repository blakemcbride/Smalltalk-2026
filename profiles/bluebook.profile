"
The 1983 system, exactly as it has always been built.

This is the profile the byte-for-byte gate is checked against: it must
produce the same image as -manifest sources/MANIFEST did before profiles
existed, which is why it names the manifest rather than the directory --
the manifest's order is part of what makes the image what it is.
"
Profile {
	#name      : 'bluebook',
	#manifests : [ '../sources/MANIFEST' ]
}
