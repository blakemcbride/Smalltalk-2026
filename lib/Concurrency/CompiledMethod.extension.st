"
TempNameCache, read once.

CompiledMethod keeps the temporary names of the last method asked for them
in one class variable, an Association of the method and its names.  1983's
setTempNamesIfCached: read that variable three times -- is it nil, is its
key me, what is its value -- and another worker caching ITS method's names
between the second read and the third handed the first worker the wrong
names for the right method.  The debugger would then have labelled a
method's temporaries with another method's.  One read into a temporary,
and the Association is immutable once written, so what was read stays
consistent however many workers write after.
"
Extension { #name : 'CompiledMethod' }

{ #category : '*Concurrency' }
CompiledMethod >> setTempNamesIfCached: aBlock [
	"If my temporary names are the ones cached, hand them to aBlock."
	| cache |
	cache := TempNameCache.
	cache == nil ifTrue: [^self].
	cache key == self ifTrue: [aBlock value: cache value]
]
