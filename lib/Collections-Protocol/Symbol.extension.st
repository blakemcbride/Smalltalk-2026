"
Symbol as an announcement identifier.

Pharo lets a Symbol stand in for an Announcement class -- `announcer
when: #FOO send: #bar to: nil' -- and the matching test for that lives on
Symbol itself, in Collections-Strings, a package we will not import because
Symbol is ours.  So it belongs here.

It was found the way these things should be found: AnnouncerTest's one
remaining FAILURE was still failing after the class it appeared to need
had been added, and printing the selector the test's own handler caught --
rather than comparing it -- named this in one line.  The test expects
#bar not to be understood; it was catching #handlesAnnouncement: instead.
"
Extension { #name : 'Symbol' }

{ #category : '*Collections-Protocol' }
Symbol >> handlesAnnouncement: anAnnouncement [
	"Only the identical symbol.  #FOO subscribes to #FOO and to nothing
	 else."
	^anAnnouncement == self
]

{ #category : '*Collections-Protocol' }
Symbol >> prepareForDelivery [
	"Nothing to prepare.  An Announcement gets a chance to fill itself in
	 before subscribers see it; a Symbol standing in for one has nothing
	 to fill in, and Pharo's own comment on this method says only 'somehow
	 symbol can be announcement'."
]
