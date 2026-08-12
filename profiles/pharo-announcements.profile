"
The ratchet's first real turn: a Pharo package, byte for byte from
upstream, loaded onto our system.

Announcements-Core is Tier 1 -- no VM contract, nothing that waits on the
object model work of Phase F -- and it brings its own SUnit suite, which is
what makes it a turn of the ratchet rather than a file that happens to
parse.  Progress here is a number: N of M Pharo classes load and pass the
tests their own authors wrote.

ManifestAnnouncementsCore is excluded rather than deleted.  It is Pharo
tooling metadata -- lint rules for the package browser, with
PackageManifest as its superclass and no behaviour -- so it is the
loader's business to skip it, not ours to edit the file.  Every package
imported will carry one.
"
Profile {
	#name     : 'pharo-announcements',
	#requires : [ 'st2026' ],
	#dialect  : 'closures',
	#exclude  : [ 'ManifestAnnouncementsCore',
	              'ManifestAnnouncementsCoreTests' ],
	#packages : [ '../pharo/Announcements-Core',
	              '../pharo/Announcements-Core-Tests' ]
}
