"
Pharo's deprecation hooks, which the ported methods send before they work.

String>>asDuration, asDateAndTime, Number>>asDuration and so
DateAndTime>>+ all open with `self deprecated: ... transformWith: ...',
Pharo's rewrite-rule hook for its tools; nothing here implemented it, so
the four never reached their last line and `aDateAndTime + 60' was a
doesNotUnderstand (Bugs6 KERNA-30).  The rule is for a tool this system
has not got; the method just goes on and does its work.
"
Extension { #name : 'Object' }

{ #category : '*Chronology-Compat' }
Object >> deprecated: aString [
	"Nothing to do here: see the comment above."
]

{ #category : '*Chronology-Compat' }
Object >> deprecated: aString transformWith: aRewriteRule [
	"Nothing to do here: see the comment above."
]
