# Post-Blue-Book language extensions

A survey of what the Squeak/Pharo lineage added to the Smalltalk-80 grammar,
and what only looks added.  **Deferred: this is for discussion once the nine
phases of `PLAN.md` are complete.**  Nothing here is a commitment, and none of
it belongs in the system before the Blue Book language it extends is finished.

It is filed now because the compiler is fresh and the question of where the
1983 grammar stops is exactly the question the compiler had to answer.

Where the five candidate extensions stand in this system today, measured
rather than assumed:

| Extension | Today |
|---|---|
| Dynamic arrays `{ a. b }` | Not supported — the lexer rejects `{` |
| General pragmas `<foo: 1>` | Only `<primitive: N>` is recognised |
| Block-local temporaries `[:x \| \| t \| ...]` | Not supported — parse error |
| Byte arrays `#[1 2 3]` | Not supported — `#` must begin a symbol |
| Scaled decimals `1.23s2` | Not supported, and **fails silently**: it lexes as `1.23` then a unary send `s2`, so it answers nil instead of erroring |
| Named primitives `<primitive: 'p' module: 'M'>` | Not supported |

The silent failure is the only one of these that is arguably a bug rather than
an absent feature, since a Blue Book compiler should reject `1.23s2` outright.

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
