# Post-Blue-Book language extensions

A survey of what the Squeak/Pharo lineage added to the Smalltalk-80 grammar,
and what only looks added.

It was filed while the compiler was fresh, because the question of where the
1983 grammar stops is exactly the question the compiler had to answer. It was
deferred until the nine phases of `PLAN.md` were complete. **They are, and
Phase A of `PLAN-PHARO.md` has now implemented five of the six** — the
prerequisite for compiling Pharo source at all.

Where the six candidates stand, measured rather than assumed:

| Extension | Today |
|---|---|
| Dynamic arrays `{ a. b }` | **Implemented.** The elements are compiled in order and bytecode 138 (push new Array, elements off the stack -- Squeak's, and the interpreter's already) builds the Array from them; up to 127 elements, no literal per element |
| General pragmas `<foo: 1>` | **Implemented**, several per method, all literal argument kinds. Kept in the literal frame as an `AdditionalMethodState`, which `CompiledMethod>>pragmas` reads back |
| Block-local temporaries `[:x \| \| t \| ...]` | **Implemented.** Each gets a frame slot and is nilled at every activation |
| Byte arrays `#[1 2 3]` | **Implemented**, including nested inside `#(...)` |
| Named primitives `<primitive: 'p' module: 'M'>` | **Implemented** as Squeak's primitive 117 with the descriptor as literal 0. The VM does not yet dispatch it |
| Scaled decimals `1.23s2` | **Not implemented, deliberately.** See below |
| `nil`, `true` and `false` inside `#( )` | **The objects, in the closure dialect.** A seventh, found later; see below |

The first four were clean parse errors before, which is what an absent feature
should look like, and is why adding them could take no meaning away from
anything that compiled: the 1983 library still compiles 4,521 of 4,521 methods,
and `trace2` is still byte-exact.

**And the same four in the image's own compiler**, which is a separate answer
to the same question and was `no' for a long time after this one was `yes'.
1983's Parser is what the Browser and `Compiler evaluate:` use, and it read
none of them -- so 86 of a bootstrapped image's own 6,843 methods could not be
re-parsed by the image holding them, which meant the Browser could not edit
the system's own source and a Tonel file that loaded at bootstrap might not
load at run time. `lib/Compiler-Fixes` closes that: block temporaries with a
scope that ends at the block, a pragma on either side of the temporaries,
`#[1 2 3]` in the Scanner, `{1. 2. 3}` as a cascade the Parser builds, binary
selectors longer than two characters, and a temporary that shadows an instance
variable or a global. The census is 0 of 6,875 now.

**And the image no longer WRITES a second dialect either.** Reading was half
of it: the image's own compiler emitted Chapter 27's `BlockContext` where this
one emits closures, so the same text meant different things depending on which
had compiled it — and a Tonel service file reloaded on a running server is
exactly the case where both do. `Behavior>>compile:notifying:trailer:ifFail:`
reaches this compiler through primitive 228 now, and
`tests/unit/test_image.c`'s self-hosting check compares the bytecodes a
compile inside the image produces against the bytecodes a direct call
produces, byte for byte. The 1983 parser is still what pretty-prints,
decompiles and answers `parseSelector:`; if it drifts, a *tool* complains
about a method that compiles, which is visible where a semantic difference was
not.

The scaled decimal is the one that already compiles, and it was recorded here
as arguably a bug on the grounds that a Blue Book compiler ought to reject
`1.23s2` outright. Measured, that turns out to be wrong, and the reason is why
it is the one left alone:

```
$ ./st80 -bootstrap -manifest sources/MANIFEST -eval '^3factorial'
6
```

A unary selector needs no space before it, so `3factorial` is `3 factorial`,
and `1.23s2` is `1.23 s2` by exactly the same rule. There is no `s` suffix in
the Blue Book number grammar, so `s2` is an ordinary unary selector and Float
does not implement it. Answering `nil` after a doesNotUnderstand is what this
system does with every unimplemented unary send.

So the compiler is right and the note was wrong. What it costs is that adding
scaled decimals is not a free extension the way `{`, `#[` and `<foo:>` were:
those were all syntax errors, so recognising them could break nothing, whereas
`1.23s2` already means something. It is the only one of the six with an
existing meaning to take away, there is no `ScaledDecimal` class in the 1983
library for it to answer, and Pharo code needing exact decimals is rare — so it
stays out. Should it ever go in, it must require the `s` to be followed by
digits and then a non-alphanumeric, so that `1.23some` remains a unary send.

## The seventh: `nil`, `true` and `false` inside a literal array

Not one of the six, because it is not a new construct -- `#(nil)` has always
parsed. It is a question about what the three bare words in it MEAN, and the
answer changed after 1983.

1983 interns every bare word in a literal array, those three included, so
`#(nil true false)` is three Symbols. ANSI specifies the objects, and Squeak,
Pharo and VisualWorks all answer the objects. The failure is invisible from
inside, because `#nil printString` is `'nil'`:

```smalltalk
(#(nil) at: 1) class name      "Symbol"
#(1 nil 2) copyWithout: nil    "(1 nil 2 ) -- nothing removed"
#(1 nil 2) indexOf: nil        "0"
#(nil) = (Array with: nil)     "false"
```

An array written to hold a hole held a symbol instead, and every test of it
answered the wrong way round with nothing printed to say so. `{nil. true.
false}` -- the brace form -- was already correct, which is what made the
literal form a trap rather than a documented limitation; and the compiler was
inconsistent with itself, because `pragma_literal` in the same file already
special-cased the same three words, so `<foo: nil>` got `nil` while `#(nil)`
got `#nil`.

**So the closure dialect answers the objects, and the Blue Book dialect does
not.** The split is not a hedge. Under the Blue Book this compiler is
compiling 1983's own library, where
`ClassDescription>>subclassOf:oldClass:instanceVariableNames:...` builds
`#(self super thisContext true false nil) asSet` and means the six NAMES; and
`trace2` checks this compiler's literal frames against the ones Xerox shipped,
which a changed literal would break. Every dialect-dependent decision in this
lexer and compiler has the same shape -- the underscore, the length of a binary
selector -- and for the same reason: post-1983 Smalltalk spent a character
1983 had already spent, and only the caller knows which spending is meant.

Everything in `lib/` and everything a user writes is compiled in the closure
dialect, so the ANSI reading is what a program of this system's own gets.

## What the implementation cost, and one thing it removed

The pragma work is worth recording because it made the lexer *smaller*.
`<primitive: 60>` used to be scanned into a single token by reading raw
characters, which recognised the one pragma the Blue Book has and could not be
generalised without teaching the lexer the grammar. That case is now deleted:
`<` is an ordinary binary selector again, and pragmas are recognised by the
parser, which knows the single position they may appear in and can speculate
and rewind like everything else here. A method with no temporaries whose first
statement is `a < 3 ifTrue: [...]` still compiles to a send, because a
speculative parse that never reaches a closing `>` gives the token back.

The one new hazard is that binary selectors are greedy up to two characters, so
a closing `>` immediately followed by another binary character arrives as one
token and the pragma is not recognised. Pragmas end lines in practice, and all
4,521 library methods still compile, but the limit is real rather than
theoretical.

The survey follows as written.

---

Your impression is correct, but the number of genuine **syntax extensions in Squeak is quite small**. Most of what makes modern Smalltalk feel richer comes from additional classes and methods rather than changes to the grammar.

Compared with the original Smalltalk-80 language described in the Blue Book, the important Squeak-family extensions are these.

## 1. Dynamic array construction

Modern Squeak-derived dialects support curly-brace arrays:

```smalltalk
{ expression1. expression2. expression3 }
```

For example:

```smalltalk
{ 1. 2 + 3. Date today }
```

This evaluates the expressions at runtime and produces:

```smalltalk
#(1 5 aDate)
```

Conceptually, it is shorthand for something like:

```smalltalk
Array
    with: 1
    with: 2 + 3
    with: Date today
```

This differs from the traditional literal-array syntax:

```smalltalk
#(1 2 3)
```

A `#(...)` array must contain compile-time literals. A `{...}` array may contain arbitrary expressions evaluated at runtime. Curly-brace arrays are probably the most visible true language extension in Squeak and Pharo. ([Pharo Books][1])

---

## 2. Generalized pragmas

Smalltalk-80 had special syntax for declaring VM primitives, such as:

```smalltalk
<primitive: 70>
```

Squeak generalized that notation into **pragmas**, which are annotations attached to compiled methods:

```smalltalk
<foo>
<foo: 123>
<category: #testing>
```

A realistic example might be:

```smalltalk
exampleMethod
    <author: 'Blake'>
    <deprecated: 'Use #newMethod instead'>
    ^42
```

The compiler stores these as `Pragma` objects associated with the compiled method. Frameworks can inspect them for tests, menus, serialization, foreign-function interfaces, deprecation notices, and other purposes.

Therefore, primitive declarations were not entirely new syntax, but allowing arbitrary annotations within `<...>` was a genuine generalization of the original construct. ([Pharo Books][1])

---

## 3. Block-local temporary variables

Original Smalltalk-80 blocks had arguments:

```smalltalk
[:x | x + 1]
```

Modern Squeak-family Smalltalks also allow temporary variables local to a block:

```smalltalk
[:x |
    | result |
    result := x * 2.
    result + 1
]
```

For a block with no arguments, the form is:

```smalltalk
[
    | temporary |
    temporary := 10.
    temporary squared
]
```

For a block with arguments and temporaries, there are effectively two vertical-bar separators:

```smalltalk
[:x :y |
    | sum |
    sum := x + y.
    sum squared
]
```

These block-local declarations are part of the parser and are not merely a library feature. Modern Pharo documentation explicitly describes blocks as taking arguments and containing temporary variables. ([Pharo Books][1])

---

## 4. Additional literal forms

Squeak and related dialects added literal syntax for certain specialized objects.

### Byte arrays

A common Squeak-family form is:

```smalltalk
#[1 2 3 255]
```

This creates a literal `ByteArray`, rather than a regular `Array`.

Compare:

```smalltalk
#(1 2 3)       "Array"
#[1 2 3]       "ByteArray"
```

This is a lexical and syntactic extension because the compiler directly recognizes the `#[...]` notation.

Some descriptions use the older-looking form:

```smalltalk
#[1 2 3]
```

while historical or dialect-specific material may show variants. It is not as universally portable across Smalltalk dialects as `#(...)`.

### Scaled decimals

Squeak-family systems commonly recognize scaled-decimal literals such as:

```smalltalk
1.23s2
```

The `s2` specifies a decimal scale of two places. This produces a `ScaledDecimal`, intended for exact decimal arithmetic rather than a binary floating-point number.

Examples:

```smalltalk
12.34s2
100s2
0.125s3
```

This is another genuine compiler-recognized literal syntax, although support and exact behavior vary among modern dialects.

---

## 5. Extended primitive declarations

Squeak supports richer primitive declarations than the original numeric primitive form. For example:

```smalltalk
<primitive: 'primitiveName' module: 'ModuleName'>
```

This allows a method to invoke a named primitive supplied by a VM plugin:

```smalltalk
somePrimitiveOperation
    <primitive: 'primitiveDoSomething' module: 'MyPlugin'>
    self primitiveFailed
```

This is largely an application of the generalized pragma grammar, but it is still a syntax-level extension compared with the original numeric form:

```smalltalk
<primitive: 123>
```

Squeak’s plugin architecture makes extensive use of named primitive declarations. ([Squeak Wiki][2])

---

## Things that may look like extensions but are not

Several frequently mentioned differences are better described as notation changes, semantic changes, or library additions.

### `:=` for assignment

Modern code uses:

```smalltalk
x := 10
```

Original Smalltalk-80 commonly displayed a left-arrow glyph:

```smalltalk
x ← 10
```

Internally, historical systems sometimes represented that glyph using an underscore:

```smalltalk
x _ 10
```

Thus, `:=` is mostly an ASCII-portable replacement for the old assignment glyph, not a significant new language construct.

Similarly:

```smalltalk
^result
```

is the ASCII representation of the original upward-arrow return glyph:

```smalltalk
↑result
```

### Full lexical closures

Modern Squeak has much better block-closure semantics than early Smalltalk implementations. Blocks correctly capture their surrounding lexical environment and may remain alive after the defining method returns.

That is a major semantic and implementation improvement, but the basic syntax:

```smalltalk
[:x | x + offset]
```

already existed.

### Exception handling

Code such as:

```smalltalk
[
    riskyOperation
] on: Error do: [:exception |
    exception return
]
```

looks like a language construct, but it is ordinary block and message-send syntax. Exception handling is implemented by classes and methods.

### Conditionals and loops

Likewise:

```smalltalk
condition ifTrue: [...] ifFalse: [...]
collection do: [:each | ...]
[condition] whileTrue: [...]
```

are not parser additions. They are ordinary messages sent to booleans, collections, and blocks.

### Cascades

```smalltalk
object
    firstMessage;
    secondMessage;
    thirdMessage
```

Cascades were already part of Smalltalk-80.

### Multiple assignment

Code such as:

```smalltalk
a := b := 10
```

is simply nested assignment and was already supported.

---

## The practical summary

For Squeak specifically, the notable syntax-level additions over classic Smalltalk-80 are principally:

1. **Dynamic arrays:** `{ expression. expression }`
2. **General pragmas:** `<annotation: value>`
3. **Block-local temporaries**
4. **Additional literal forms**, especially `#[...]` byte arrays and scaled decimals
5. **Named and richer primitive specifications**, built on pragma syntax

Everything else remains remarkably close to Smalltalk-80. Unary, binary and keyword messages, blocks, cascades, assignments, returns, literal arrays, symbols, characters and method syntax are fundamentally the same. The Blue Book’s core grammar is still readily recognizable in Squeak today. ([Stephane Ducasse][3])

One qualification is important: **Squeak, Pharo, Cuis, VisualWorks, GNU Smalltalk and GemStone do not all have exactly the same extensions**. Curly-brace arrays and pragmas are strongly associated with the Squeak/Pharo lineage, while other dialects have their own literal forms, namespaces, binding syntax or compiler directives.

[1]: https://books.pharo.org/pharo-by-example9/pdf/2022-03-26-index.pdf?utm_source=chatgpt.com "Pharo 9 by Example"
[2]: https://wiki.squeak.org/squeak/850?utm_source=chatgpt.com "Specifications For Named Primitives"
[3]: https://stephane.ducasse.free.fr/FreeBooks/BlueBook/Bluebook.pdf?utm_source=chatgpt.com "Smalltalk-80: the language and its implementation"
