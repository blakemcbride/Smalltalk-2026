"
The two instance shapes the Blue Book's definition messages cannot ask for,
and the record of a class's trait composition.

subclass:, variableSubclass:, variableByteSubclass: and
variableWordSubclass: are the four 1983 spellings, and between them they
can say indexable, bytes and words.  The loader reads two more shapes out of
a Tonel header -- `#type : 'weak'' and `#type : 'ephemeron'' -- and
builds them in C; until these two messages existed nothing in the image
could build one, so TonelReader made a weak class from a weak file as an
ordinary variable class and said nothing (Bugs3 B61).  Each is the 1983
message for the strong shape with the extra bit set on the class it
answers; Behavior>>beWeak says what that does and does not change.

traitComposition is the other half of the same finding.  A class that
names a trait in its header gets the trait's methods compiled into it at
load time, in `*trait:TName' categories, and nothing kept the header's
words -- so TonelWriter could neither write the `#traits' line back nor
know which methods were the trait's to leave out, and Greeter came back
from its own write-back owning `greeting'.  The bootstrap now records each
composition in TraitCompositions, a Dictionary from class name to the
text as the header had it, and this is how a class asks.

Keyed by NAME, as TonelSource is, because a redefinition makes a new class
object and the trait is a fact about the definition rather than about one
object.  Written once, by the bootstrap, before any worker exists; read
without a lock afterwards, which lib/Concurrency's LibraryLocks explains is
safe for a Dictionary nobody writes to.  A class removed from the system
leaves its entry behind, which is harmless: a name that is not a class
answers nothing here, and one redefined without the trait through the
Browser is a case the Browser's class template does not offer.

The global is named here, not looked up through `Smalltalk at:', and that is
load-bearing: the bootstrap fills a global by storing into the binding the
compiler made for the name, and a name no method mentions has no binding in
Smalltalk to store into -- Disk, Sensor and Undeclared are the same
arrangement, each declared by the methods that use it and filled afterwards
by src/boot/bootstrap.c.  Nil until the bootstrap has filled it, and in a
profile without traits the Dictionary is empty rather than absent.
"
Extension { #name : 'Class' }

{ #category : '*Kernel-Protocol' }
Class >> weakSubclass: t instanceVariableNames: f classVariableNames: d poolDictionaries: s category: cat [
	"A subclass whose indexed fields the collector does not follow: a
	 variable pointer subclass with the weak bit set.  What
	 `#type : 'weak'' in a Tonel header means, and what WeakArray is."
	| newClass |
	newClass := self variableSubclass: t instanceVariableNames: f
		classVariableNames: d poolDictionaries: s category: cat.
	newClass beWeak.
	^newClass
]

{ #category : '*Kernel-Protocol' }
Class >> ephemeronSubclass: t instanceVariableNames: f classVariableNames: d poolDictionaries: s category: cat [
	"A subclass whose first instance variable is a weakly held key and
	 whose every field is strong as long as that key is reachable some
	 other way.  Not indexable: what `#type : 'ephemeron'' means."
	| newClass |
	newClass := self subclass: t instanceVariableNames: f
		classVariableNames: d poolDictionaries: s category: cat.
	newClass beEphemeron.
	^newClass
]

{ #category : '*Kernel-Protocol' }
Class >> traitComposition [
	"The trait composition my definition named -- `TGreeting', or
	 `TA + (TB - {#x})' -- as the header wrote it; nil when it named none.
	 Recorded by the bootstrap in TraitCompositions when it flattened the
	 traits into me; see the comment on this file."
	TraitCompositions isNil ifTrue: [^nil].
	^TraitCompositions at: self name asSymbol ifAbsent: [nil]
]
