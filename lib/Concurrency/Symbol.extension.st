"
Interning, serialized.

Symbol class>>intern: is a read-modify-write on USTable: look the string
up, and if it is not there make a Symbol and put it in its bucket.  Two
workers interning the same new string both looked, both missed, and both
made one -- 132 of 600 symbols disagreed across two workers, 1,170 of
2,400 across eight.  A Symbol that is not identical to itself is the worst
kind of wrong: `#foo == 'foo' asSymbol' answers false, perform: with a
selector made at run time does not understand, and a Dictionary keyed by
Symbols cannot find what it holds.

intern: now holds LibraryLocks' Symbol lock around the 1983 body, which is
basicIntern: below.  The re-check is inside the lock, which is
the whole point: the second worker, once it gets the lock, finds the
first's Symbol.  Reads (hasInterned:ifTrue:, sent by asSymbol before
anything else) stay unlocked: a bucket is replaced by a copy, never edited
in place, so a reader sees the old bucket or the new one.

rehash rebuilds the table, so it holds the lock too, and interns with
basicIntern: because the lock is not re-entrant.

The table also GROWS here.  Serialising interning made it correct;
1983's fixed 512 buckets made it quadratic, and a program that interns
symbols at run time -- a parser, a compiler, dynamic selectors -- slowed
down as the image filled up.  See growTable, and Bugs4 COLL-1.
"
Extension { #name : 'Symbol' }

{ #category : '*Concurrency' }
Symbol class >> intern: aString [
	"Answer a unique Symbol whose characters are those of aString."
	^LibraryLocks holdingSymbols: [self basicIntern: aString]
]

{ #category : '*Concurrency' }
Symbol class >> basicIntern: aString [
	"1983's intern:, to be run with the Symbol lock held, plus the one
	 line that keeps it from being quadratic.

	 A bucket is an Array replaced by a copy one longer, so interning a new
	 name costs the LENGTH OF ITS BUCKET, and the bucket count was fixed:
	 512, chosen in C where the table is seeded (src/boot/bootstrap.c) and
	 never changed afterwards by anything.  A fresh image already holds
	 five and a half thousand symbols, so the buckets start eleven long,
	 and every symbol a running program interns makes every later one
	 dearer.  Ten thousand new symbols took 0.85 seconds and forty thousand
	 took 10.3 -- twelve times the work for four times the symbols, which
	 is quadratic with the ordinary constant (Bugs4 COLL-1).  A parser
	 turning keys into Symbols, a compiler compiling methods, and
	 classify:under: on every category name all pay it.

	 So the table grows.  growTable does the deciding -- it is the one that
	 counts and that knows whether growing would help; what has to be O(1)
	 is deciding whether to ASK it, and the length of the bucket just
	 written is the one measurement already in hand.  Twelve is the number,
	 and it is a threshold on the LONGEST bucket rather than on an average
	 nothing here can compute cheaply: with a hash that spreads, a table
	 whose buckets average one or two never has a bucket of twelve, and one
	 whose buckets average four or five soon does.  So the walk happens
	 when the table is loaded and essentially never when it is not.

	 The test is `= 12' and not `>= 12', because a bucket is replaced by a
	 copy exactly one longer and therefore passes through twelve exactly
	 once.  Asking on every insert into an ALREADY long bucket would turn a
	 run of hash collisions -- twelve names that happen to land together,
	 or are made to -- into a walk of the whole table per intern, for a
	 table growTable would rightly refuse to grow."
	| sym index bucket |
	self hasInterned: aString ifTrue: [:existing | ^existing].
	sym := (aString isMemberOf: Symbol)
		ifTrue: [aString]
		ifFalse: [(Symbol new: aString size) string: aString].
	index := aString stringhash \\ USTable size + 1.
	bucket := USTable at: index.
	USTable at: index put: (bucket copyWith: sym).
	bucket size = 12 ifTrue: [self growTable].
	^sym
]

{ #category : '*Concurrency' }
Symbol class >> growTable [
	"Give USTable enough buckets for the symbols it holds, and rehash into
	 them.  To be run with the Symbol lock held.

	 Called from basicIntern: when a bucket has just reached twelve.  That
	 is a hint and not a measurement, so the first thing done here is to
	 COUNT -- the walk over the old buckets is the same walk the rehash
	 needs, so counting is free -- and the new
	 size is the smallest power of two that is at least the count and at
	 least the size the table already has.  A table that is already roomy
	 is therefore left exactly as it was, which is the answer when a long
	 bucket was collisions rather than load; the table never shrinks, and
	 it never grows past one bucket per symbol.

	 Never shrinking is not tidiness.  Symbol class>>hasInterned:ifTrue:
	 reads USTable without the lock -- the fast path of asSymbol, taken by
	 every worker -- and it reads the table into a temporary and then
	 indexes it, so a table replaced under it is a table it looks the name
	 up in, misses, and re-checks under the lock.  A miss costs a lock; an
	 index past the end of a SMALLER table would be an error.

	 The C side reads the table through its Association and takes the
	 modulus from the Array's own length, so a grow is invisible to it;
	 see live_symbol_table in src/boot/bootstrap.c.  It matters, because
	 the compiler interns selectors through that path and a C side using
	 the old bucket count would make a second Symbol with the same
	 characters -- and a selector that is not identical to itself cannot
	 be sent.

	 The buckets are refilled with an Array per bucket built once from an
	 OrderedCollection rather than by copyWith: per symbol, which is the
	 quadratic this method exists to end.

	 A newly seeded image is the one case where the table can already be
	 over its threshold before a single symbol is interned by the image
	 itself: the C fills 512 buckets with five and a half thousand
	 symbols, eleven deep.  Dozens of those buckets sit at exactly twelve,
	 so the first handful of interns finds one and the table grows to
	 8192 once and for all."
	| old count size new lists index |
	old := USTable.
	count := 0.
	1 to: old size do: [:i | count := count + (old at: i) size].
	size := old size.
	[size < count] whileTrue: [size := size * 2].
	size = old size ifTrue: [^self].
	lists := Array new: size.
	1 to: size do: [:i | lists at: i put: (OrderedCollection new: 4)].
	1 to: old size do: [:i |
		(old at: i) do: [:sym |
			index := sym stringhash \\ size + 1.
			(lists at: index) addLast: sym]].
	new := Array new: size.
	1 to: size do: [:i | new at: i put: (lists at: i) asArray].
	USTable := new
]

{ #category : '*Concurrency' }
Symbol class >> hasInterned: aString ifTrue: symBlock [
	"Answer false when that string has not been interned; otherwise hand
	 the Symbol to symBlock and answer true.

	 1983's body, with the table read ONCE into a temporary.  It wrote
	 `USTable at: aString stringhash \\ USTable size + 1', two separate
	 reads of a class variable that another worker can replace between
	 them -- this is the unlocked read every asSymbol takes, and
	 growTable replaces USTable wholesale (Bugs4 COLL-1).  Reading twice
	 would still have been safe, because the table only ever grows and an
	 index computed from a smaller size is in range of a larger table, but
	 it would have been safe for a reason a reader has to reconstruct.
	 One read is safe for a reason there is nothing to reconstruct about:
	 the size and the slot come from the same Array.

	 A bucket is replaced by a copy and never edited in place, so a reader
	 sees the old bucket or the new one and never a torn one; that is what
	 makes this method need no lock."
	| ascii table bucket |
	aString size = 1 ifTrue: [
		(ascii := (aString at: 1) asciiValue) < 128 ifTrue: [
			symBlock value: (SingleCharSymbols at: ascii + 1).
			^true]].
	table := USTable.
	bucket := table at: aString stringhash \\ table size + 1.
	1 to: bucket size do: [:i |
		((bucket at: i) == nil) ifFalse: [
			aString size = (bucket at: i) size ifTrue: [
				aString = (bucket at: i) ifTrue: [
					symBlock value: (bucket at: i).
					^true]]]].
	^false
]

{ #category : '*Concurrency' }
Symbol class >> rehash [
	"Rebuild the hash table that holds all the unique Symbols."
	LibraryLocks holdingSymbols: [
		USTable := USTable collect: [:each | Array new: 0].
		Symbol allInstancesDo: [:sym | self basicIntern: sym]]
]
