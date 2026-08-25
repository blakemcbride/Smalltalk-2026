"
What every object answers when it is about to become JSON.

asJsonValue is the one place the conversion happens, and it is a message
rather than a switch inside JSONWriter for two reasons.  A class this
package has never heard of can join in by implementing it -- which is how a
Person becomes a document without this package knowing what a Person is --
and, more importantly, a Rectangle put into a JSONArray is refused at that
put, naming the Rectangle, instead of at the moment the whole document is
written, where the only thing that can be said is that something somewhere
in the tree was not writable.  In a web application those two moments are a
request apart.

The conversions are the obvious ones.  nil, true, false, a String, an
Integer, a Fraction and a Float are JSON values already and answer
themselves.  A Symbol answers its String and a Character answers a String
of one, because JSON has neither and both are text to everybody who reads
it.  A Dictionary becomes a JSONObject and every other Collection becomes a
JSONArray.  Everything else raises, which is the point.

asJsonString is the reverse and is on Object so that anything at all can be
asked for its JSON: `#(1 2 3) asJsonString' is '[1,2,3]' without a
JSONArray having to be built by hand first.

String has BOTH, and they are not opposites, which is worth stating once
where somebody will read it.  `''hello'' asJsonString' is the six
characters that spell hello in quotes, because a String is a JSON value and
that is what it looks like written down.  `''[1,2]'' asJson' is a JSONArray,
because there a String is a DOCUMENT being read.  One goes out, one comes
in, and neither undoes the other.
"
Extension { #name : 'Object' }

{ #category : '*Json' }
Object >> asJsonValue [
	"Nothing general can be done, so nothing is: a class that wants to be
	 written as JSON says how by implementing this, and the report names
	 the class that did not."

	^JSONError signal: self class name article, ' ', self class name,
		' cannot be written as JSON'
]

{ #category : '*Json' }
Object >> asJsonString [
	"Me as compact JSON text."

	^JSONWriter write: self
]

{ #category : '*Json' }
Object >> asJsonStringIndent: anInteger [
	"Me as JSON text, one member to a line, that many spaces to a level."

	^JSONWriter write: self indent: anInteger
]

{ #category : '*Json' }
Object >> writeJsonOn: aStream [
	"Me as compact JSON text, written straight onto that stream.

	 For the document too large to want a String of first, which is the
	 case that made JSONWriter take a stream rather than build one."

	^JSONWriter write: self on: aStream
]

{ #category : '*Json' }
UndefinedObject >> asJsonValue [
	"nil is JSON's null.  There is no separate null object in this package
	 and there does not need to be: a Smalltalk Dictionary stores nil as a
	 value perfectly well, so `{""a"":null}' round trips as one name whose
	 value is nil, and includesKey: still answers true for it.  org.json
	 needs its NULL singleton because a Java HashMap cannot tell a null
	 value from an absent key without one."

	^self
]

{ #category : '*Json' }
Boolean >> asJsonValue [
	^self
]

{ #category : '*Json' }
Number >> asJsonValue [
	"Every Number in this image is a JSON number except the ones that are
	 not finite, and those are caught by the writer -- there is nothing to
	 be done about them here, since answering something else would put a
	 value in the document that the caller did not write."

	^self
]

{ #category : '*Json' }
String >> asJsonValue [
	^self
]

{ #category : '*Json' }
String >> asJson [
	"The JSON value this text spells: an object, an array, a number, a
	 string, a boolean or nil.

	 Not the opposite of asJsonString; see the comment at the top of this
	 file."

	^JSONParser parse: self
]

{ #category : '*Json' }
Symbol >> asJsonValue [
	"A Symbol is a String here, so this is only about what comes back OUT.
	 Left as a Symbol it would be written correctly and then read back as a
	 String, so that a document round tripped through this system would stop
	 comparing equal to itself under ==.  Converting once, on the way in,
	 is cheaper than explaining that."

	^self asString
]

{ #category : '*Json' }
Character >> asJsonValue [
	"JSON has no character type, so a Character is a String of one -- which
	 is what org.json's put(String, char) does, and what anybody who wrote
	 one meant."

	^self asString
]

{ #category : '*Json' }
Collection >> asJsonValue [
	"Any collection is an array.  A Set and a Bag included, in whatever
	 order they enumerate: JSON has no unordered array, so an order has to
	 be chosen and theirs is the only one available."

	^JSONArray withAll: self
]

{ #category : '*Json' }
Dictionary >> asJsonValue [
	"A Dictionary is an object and not an array of its values, which is what
	 it would become from Collection>>asJsonValue above -- do: over a
	 Dictionary answers values, so a Dictionary of three names would have
	 become a three-element array with the names thrown away, in hash
	 order, silently."

	^JSONObject withAll: self
]
