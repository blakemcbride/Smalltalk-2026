"
Reshaping a class while other workers are using its instances.

Adding an instance variable to a class does not edit the objects that
already exist: ClassDescription>>updateInstancesFrom: makes a new object of
the new shape for each of them, copies the fields across, and sends
become:.  1983 wrote that as four steps because in 1983 nothing could
happen between them.

Eight workers can.  A store into a field that lands after this has READ
that field and before the become: goes into the body the become: is about
to leave behind -- every reference names the identity, the identity now
names the new body, and the value written is in the old one.  The write is
gone, silently, and the object afterwards looks like any other object
holding a value one step stale.  A thousand instances kept to an invariant
by one worker while another reshaped the class thirty times came back with
22 of them broken and not one error raised (Bugs4 REFLECTION-3).

There is no lock to take.  The other worker is running `b := b + 1' in a
method written years before anybody thought about this, and making every
store in the image synchronise with an event that happens twice a year is
the wrong trade by several orders of magnitude.  So the exclusion is the
one the collector already uses -- a safepoint, every worker parked -- and
primitive 234 does the copying and the swapping inside one of them, for
every instance at once.  See src/interp/migrate.c for what that costs and
why the allocation stays out here.

WHAT IS STILL 1983's, deliberately: which instances are migrated.  They are
gathered with allInstances before the safepoint, so an instance another
worker creates while this runs keeps the old class and becomes an instance
of an obsolete one.  That was true before and is not what this fixes;
fixing it means deciding what `the instances of a class' means while the
class is changing, which is a larger question than a lost write.

AND THE FALLBACK IS 1983's.  If the primitive refuses -- some
instance is a context a worker is executing in -- the loop below is the
body this method replaced, with the two collections it walks already made.
A refusal must not be a reshape that does not happen.
"
Extension { #name : 'ClassDescription' }

{ #category : '*Kernel-Reshape' }
ClassDescription >> updateInstancesFrom: oldClass [
	"Recreate any existing instances of the argument, oldClass, as
	 instances of the receiver, which is a newly changed class.  Permute
	 variables as necessary.

	 1983's, with the copy and the become: moved inside one safepoint.  See
	 this package's comment.

	 THE TWO asArrays ARE NOT DECORATION.  allInstances and allInstVarNames
	 both answer OrderedCollections, and the primitive reads raw slots, so
	 an OrderedCollection hands it firstIndex where it expects the first
	 instance.  It refuses that -- correctly -- and a refusal is a SILENT
	 fall through to the loop below, which is the very race this method
	 exists to close.  Written without them, all thirty reshapes of a
	 thirty-round test took the 1983 path and the fix measured as no fix at
	 all.  (`news' needs none: collect: on an Array answers an Array.)"

	| oldInstVarNames map variable instSize olds news |
	oldClass someInstance == nil ifTrue: [^self].	"no instances to convert"
	oldInstVarNames := oldClass allInstVarNames.
	map := (self allInstVarNames
		collect: [:instVarName | oldInstVarNames indexOf: instVarName]) asArray.
	variable := self isVariable.
	instSize := self instSize.
	olds := oldClass allInstances asArray.
	news := olds collect: [:old |
		variable
			ifTrue: [self basicNew: old basicSize]
			ifFalse: [self basicNew]].
	(self
		primMigrate: olds
		to: news
		permuting: map
		oldNamed: oldClass instSize) isNil ifFalse: [^self].

	"1983's own loop, for the cases the primitive refuses."
	olds with: news do: [:old :new |
		1 to: instSize do:
			[:offset |
			(map at: offset) > 0
				ifTrue: [new instVarAt: offset put: (old instVarAt: (map at: offset))]].
		variable
			ifTrue: [1 to: old basicSize do:
						[:offset | new basicAt: offset put: (old basicAt: offset)]].
		old become: new]
]
