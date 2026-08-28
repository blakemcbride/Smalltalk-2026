/*
 *  Copyright (c) 2026 Blake McBride
 *  All rights reserved.
 *
 *  The 1983 class library, bootstrapped and running.
 *
 *  test_library.c proves the library PARSES.  This proves it runs: 226
 *  classes and 4517 methods are compiled into a live object memory and then
 *  asked to compute, with the answers checked.  Nothing of Xerox's is in the
 *  result -- the sources are the MIT ones in sources/ -- and nothing of ours
 *  is either beyond the VM: 3 factorial is answered by Number>>factorial as
 *  Xerox wrote it, not by anything in kernel/Kernel.st.
 *
 *  This is the prerequisite Phase 8 was waiting on.  MVC is 30-odd classes
 *  sitting on top of Collection, Stream, Rectangle and Form, and none of it
 *  could be attempted while the image held only the 36 kernel classes.
 */

#include "st_test.h"

#ifdef ST_OM_MT

#include "om.h"
#include "interp.h"
#include "compiler.h"
#include "bootstrap.h"
#include "profile.h"
#include "census.h"
#include "gfx.h"
#include "st_sched.h"
#include "source.h"
#include "tonel.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <time.h>


/*
 *  The image these tests build is the 1983 library plus the one package
 *  lib/ adds for closures.  Both halves are named so that a count which
 *  moves says WHICH half moved: the Xerox numbers are what they have always
 *  been, and anything else is ours.
 */
#define BLUEBOOK_CLASSES        226
#define BLUEBOOK_METHODS        4521
#define BLUEBOOK_CATEGORIES     41
/*
 *  These moved when this test started building from the profile instead of
 *  from a list of its own: 24 -> 37 classes, 420 -> 519 methods, 6 -> 9
 *  categories.  Nothing was added to lib/ to cause it.  The difference IS
 *  the drift -- the twenty-two files the profile had been loading and this
 *  test had not, among them the whole of lib/Concurrency, ClassTestCase,
 *  MessageSend, SharedPool and the exception classes added this session.
 */
/*
 *  43 -> 58 is lib/Database and lib/Database-Tests: twelve classes for the
 *  database itself and three of tests.  It is the largest single addition
 *  lib/ has had, and every one of the twelve is a class this system did not
 *  previously have any spelling of -- there is nothing in the 1983 image
 *  between an SQL string and a result set.
 */
/*
 *  58 -> 67 is lib/Json and lib/Json-Tests: JSONObject, JSONArray, the
 *  parser, the writer and the one error class they all raise, and four
 *  classes of tests.  The 1983 image has no name for any of it -- JSON is
 *  fifteen years younger than the image is -- so every one of the nine is
 *  new here in the same way lib/Database's twelve were.
 *
 *  67 -> 69 is JSONFixture and DbFixture, which hold the objects
 *  test_parallel_json and test_parallel_db hand to every worker.  They are
 *  classes rather than expressions for the reason ConcurrencyFixture is:
 *  the compiler resolves a global to its Association at COMPILE time, so a
 *  name installed later with `Smalltalk at:put:' is not the name a method
 *  compiled afterwards refers to.
 */
/*
 *  69 -> 71 is LibraryLocks, which holds the locks the 1983 library never
 *  had -- for the Symbol table, Smalltalk, the dependents table, the open
 *  directories and the method dictionaries -- and DelayTest, for the Delay
 *  whose timing process was rewritten to survive a stale timer signal.
 */
/*
 *  71 -> 77 is lib/Network and its tests: Socket, the one class that
 *  knows primitive 208 exists; ServerSocket and SocketStream above it;
 *  NetError, which carries the operating system's own words; and two
 *  classes of tests over the loopback interface.  The 1983 image had no
 *  network -- the Alto talked to the Ethernet through a different machine
 *  -- so all six are new in the way the database's twelve were.
 */
/*
 *  77 -> 84 is lib/Tonel and its tests: TonelReader, which reads Pharo's
 *  package format into a running image the way src/compiler/tonel.c reads
 *  it into a bootstrap; TonelWriter, its inverse, exact on the round trip;
 *  TonelSource, the registry that reloads a class when its file changes
 *  and writes the file when the class does; TonelError; and three classes
 *  of tests.  A server loads its services through these on the first
 *  request for them.
 */
/*
 *  84 -> 95 is lib/JSON-RPC-Server and its tests: what Tomcat was to Kiss.
 *  HttpServer, HttpConnection, HttpRequest, HttpResponse, HttpPart,
 *  HttpStaticFileHandler, HttpCodec and HttpError -- eight -- and three
 *  classes of tests, one of them a client written on the raw socket so
 *  that what is tested is the wire.
 */
/*
 *  95 -> 111 is lib/Rest-Server and its test: Kiss's restServer package as
 *  fifteen classes -- the server, the dispatcher that speaks Kiss's wire
 *  format, the request a service sees, the service superclass, the loader
 *  that reads a service's Tonel file on first use, the session cache and
 *  its user records, the connection pool, five errors, the log and the
 *  uuid -- and RestServerTest, which drives the whole stack over the
 *  loopback interface against tests/rest-backend.
 */
/*
 *  111 -> 118 is lib/Crypto and its tests: what a stored password rests
 *  on.  Crypto, the face of primitive 209 (SHA-256, HMAC, PBKDF2, random
 *  bytes, a constant-time compare, all in src/crypto); Base64; PasswordHash,
 *  Kiss's stored format byte for byte; CryptoError; and three classes of
 *  tests, one of which verifies the hash Kiss's own demo database stores.
 */
/*
 *  118 -> 119 is lib/Web-Demo-Tests: WebDemoTest, which starts the demo
 *  application -- demo/backend and Kiss's front end under demo/frontend --
 *  on a port the system picks and drives it the way a browser does.  The
 *  demo's own classes are not counted: they are Tonel files the server
 *  loads on the first request for them, not packages in the image.
 */
/*
 *  119 -> 125 is lib/HTTP-Client and lib/LLM with their tests: HttpUrl,
 *  HttpClient and HttpClientResponse -- the other half of JSON-RPC-Server,
 *  as far as a program calling a service needs it -- Ollama, a local model
 *  server asked three things through that client, and the two test
 *  classes, whose replies come from a listener of their own on the
 *  loopback interface so that chunked bodies are tested on the wire.
 */
/*
 *  125 -> 127 is lib/Clipboard and its test: the system's clipboard, which
 *  the 1983 editor's copy, cut and paste now fill and read, so that text
 *  crosses between a workspace and the rest of the desktop; and the test
 *  of the one piece of logic in it, the line ends.
 */
/*
 *  127 -> 143 is lib/LLM built out, lib/HTTP-Client's fourth class, and
 *  their tests.  LLM is the abstract model -- the configuration, the HTTP,
 *  the tool loop, the conversation -- with Anthropic, OpenAI, OpenRouter
 *  and Ollama under it, each knowing only its service's wire; LLMTool,
 *  LLMToolCall, LLMConversation and LLMError beside it; Qdrant, where the
 *  embeddings go.  HttpBodyStream reads a reply as it comes, which a
 *  model's streamed answer needs.  And LLMTestCase, the listener that
 *  keeps every request so the tests check the wire, with a test class per
 *  service and one for the conversation.
 *
 *  143 -> 146 with the Bugs1 fixes: SyntaxErrorNotification, so a syntax
 *  error with no requestor signals instead of opening a window and
 *  suspending the process; NonBooleanReceiver, so a conditional given
 *  something that is not a Boolean is an error a handler can name rather
 *  than the end of the VM; BlockTemporaryNode, which is how the image's
 *  own Parser now carries `[:x | | t | ...]'; and OutOfMemory, which the
 *  interpreter raises where it used to print a line and stop.
 *
 *  147 -> 148 with the Bugs2 fixes: RecursionDepthExceeded, which the
 *  interpreter raises when a stack passes ST_MAX_CALL_DEPTH frames.  A
 *  method that does not stop calling itself used to reach five million
 *  frames, twelve gigabytes and the end of the process; it is now an Error
 *  one request can catch.
 *
 *  148 -> 149 with St80FileTest, in lib/Library-Tests: file names and the
 *  File List's pattern pane, which is where a person types a path.
 *
 *  149 -> 153 with the Bugs3 fixes: CorruptMethod, which the interpreter
 *  raises where a literal index, an instruction pointer or a stack pointer
 *  in a context points outside what it belongs to -- it used to trust all
 *  three and segfault; MonitorTest and ProcessTest in lib/Concurrency-Tests,
 *  for a Monitor that is finally reentrant and for terminate, suspend and
 *  signalException: reaching a process another worker is running; and
 *  St80ExceptionTest in lib/Library-Tests, for an ensure: block that used
 *  to run twice and a handler guard that is not a class.
 */
#define LIB_CLASSES             153
/*
 *  This number is a ratchet and is meant to move: lib/ is where every
 *  divergence from the frozen 1983 sources lives, so it grows whenever a
 *  ported package turns out to need protocol 1983 never had.
 *
 *  352 -> 396 is the Chronology suites being made to pass, and they now do:
 *  633 of 633, from 275.  Almost all of it is Collections, Streams and
 *  Integer protocol that Pharo assumes and 1983 does not have a name for --
 *  readStream, second, printOn:base:length:padded:, the bitwise << and & --
 *  plus TimedOut and Process>>signalException:, which is what a timeout
 *  needs to interrupt the process it is watching.
 */
/*
 *  604 with Graphics-Fixes, whose two are Paragraph class>>initialize --
 *  1983's one line, kept -- and repairCaretOffset, which turns the caret's
 *  offset into the hot spot it was always meant to be.
 *
 *  687 with Scroll-Wheel, whose five are the wheel: InputSensor>>wheelDelta
 *  reads the counter the window fills, ScrollController answers it in
 *  controlActivity through scrollByWheel and wheelScrollAmount, and
 *  ParagraphEditor repeats the first of those because it overrides
 *  controlActivity and does not send super -- which is every text view in
 *  the system.
 *
 *  678 with Files-Fixes, which is what it costs to let a directory hold a
 *  directory: directoryFromName:setFileName: splits the designator at its
 *  last separator, isDirectoryNamed: and directoryNamed: answer for a file
 *  system that has them and for 1983's that does not, and a
 *  PosixFileDirectory reads the directoryName 1983 gave it and never used
 *  -- fileNames, initFileName: and pathFor:.  Above that, FileList>>getFile
 *  enters a directory rather than reading it, FileList>>pattern answers
 *  text a text pane can compose, and FileModel>>text is left to files --
 *  translating the line ending on the way in and back again on the way out,
 *  since a Paragraph breaks on the carriage return of 1983 and a file on
 *  disk is as likely to be written in either of the other two.
 *
 *  700 when a FILE OUT was given the same treatment the viewed file already
 *  had.  1983 wrote carriage returns because the Alto did, and nothing now
 *  reads them: a filed-out class opens in an editor and in a diff as one
 *  line thousands of characters long.  TextFileStream is a FileStream that
 *  writes the host's ending instead -- FileStream class>>nativeLineEnd asks
 *  primitive 254 which that is -- reached through File>>asTextFileStream
 *  and FileDirectory>>textFile:, and the four methods that open a file out
 *  now open it that way.  PositionableStream>>nextChunk is the other half:
 *  it turns every ending back into a carriage return as source is read, so
 *  filing in what this system filed out does not fill the browser with
 *  methods that display as a single line.  Thirteen with the `new' the
 *  bootstrap synthesizes for the new class.
 *
 *  702 with lib/Browser-Sorting, which is two methods: the browser's
 *  category and protocol panes were in the order things were made in,
 *  because Categorizer appends a category and sorts only the elements
 *  inside one.
 *
 *  707 with lib/Keyboard-Map, which is five.  The arrow keys reached nothing
 *  at all -- every SDL keycode above 127 was dropped where it arrived -- and
 *  a key that reaches the image without a place in the keyboard map is not
 *  inert either: it decodes to 255, unassigned, which ParagraphEditor types.
 *
 *  708 with lib/Confirm-Nag, which is one: BinaryChoiceController>>
 *  isControlActive.  A confirm insisted on an answer by flashing its view
 *  until the pointer came back, and the same controller drives the View
 *  holding the yes and no switches, so the switches strobed whenever the
 *  pointer was anywhere else in the dialog.
 *
 *  708 -> 1077 is lib/Database and its tests: the ODBC gateway, connection,
 *  command, cursor, record, query builder and join graph, and the 43 tests
 *  that hold them.  A large number for one package, and most of it is the
 *  query builder, which is a code generator and generators are wide.
 */
/*
 *  1077 -> 1337 is lib/Json and its tests: 152 methods of package, 99 of
 *  tests, and the nine `new' the bootstrap synthesizes for the nine new
 *  classes.
 *
 *  94 of the 152 are JSONObject and JSONArray, which looks like a lot for
 *  two containers and is the whole point of the port: 28 of them are the
 *  typed accessors -- stringAt:, numberAt:, integerAt:, floatAt:,
 *  booleanAt:, objectAt: and arrayAt:, each with an ifAbsent: variant, on
 *  both classes.  That breadth is what org.kissweb.json has and what makes
 *  reading a document a line at a time instead of a type test at a time.
 *  The parser and the writer together are 44.
 *
 *  1337 -> 1358 with the locking: JSONObject and JSONArray each gained the
 *  private snapshot their enumerations are built on, DbSchemaGraph gained
 *  the four `basic' methods that do its work with the lock already held,
 *  and the two fixtures gained their accessors.  A guarded class needs a
 *  door and a room, and that is what the extra methods are.  1360 with the
 *  two tests that hold the snapshot rule in place.
 *
 *  1361 is one method REPLACED rather than added: lib/Concurrency's
 *  SmallInteger>>printOn:base:, which puts its digits on the stack where
 *  1983's put them in a class variable shared by every integer in the
 *  image.  A replacement still compiles, so it still counts.
 *
 *  1362 is another replacement: lib/Collections-Protocol's String>>hash,
 *  primitive 223 over every character, where 1983's read three of them
 *  and gave 'key1'..'key200' eleven distinct values.
 *
 *  1369: OrderedCollection>>removeIndex: now takes the index at: takes and
 *  answers the element, where 1983's took an index into the Array behind
 *  the collection and was private in all but name.  Three replaced
 *  (removeIndex:, remove:ifAbsent:, removeAllSuchThat:), two added
 *  (removeAt:, and removeBasicIndex: for the 1983 body), and two tests.
 *
 *  1418: the audit of what the 1983 library shares without a lock, and
 *  what cores did to it (doc/CONCURRENCY.md).  LibraryLocks and its six
 *  methods; Symbol class>>intern: and rehash serialized, with the 1983
 *  body as basicIntern:; Smalltalk's writers and readers under one lock,
 *  with lockedAt:put: for the body; Object's four dependents methods;
 *  CompiledMethod>>setTempNamesIfCached: reading its cache once;
 *  SystemDictionary>>classNames likewise; FileDirectory's five methods on
 *  ExternalReferences; Behavior's three method-dictionary writers;
 *  Compiler>>evaluate:in:to:notifying:ifFail: running the method through
 *  withArgs:executeMethod: instead of installing #DoIt; ProcessorScheduler
 *  activePriority, terminateActive and yield asking THIS worker; Delay's
 *  nine, for a timing process that tolerates a stale signal; and
 *  DelayTest's four.
 *
 *  1430: TextCollector's eleven writers under a Transcript lock, and the
 *  holdingTranscript: that gives it to them -- ThreadSanitizer watched
 *  eight workers write into one String while become: replaced it.
 *
 *  1526: lib/Network.  Socket's thirty-odd -- the primitive, the arm and
 *  wait loop that every read and write is built on, the timed wait,
 *  close that wakes a waiter -- ServerSocket's five, SocketStream's
 *  twenty over lines and counted reads and buffered writes, NetError's
 *  three, SystemDictionary>>arguments, and twenty-eight tests.
 *
 *  1629: lib/Tonel.  TonelReader's thirty, TonelWriter's dozen, TonelSource's
 *  fifteen, the two Browser paths -- compile:classified:notifying: and
 *  removeSelector: -- and the class-definition message, each kept as a
 *  basic... for the reader and hooked for the file; Scanner>>scanToken,
 *  so that the image's own compiler reads `:=' as assignment, which it
 *  never had to before a file written in that spelling was compiled here;
 *  ReadWriteStream>>setToEnd, without which every method compiled in a
 *  test run was logged over the last; PosixFile>>modificationTime; and
 *  twenty-four tests.
 *
 *  1797: lib/JSON-RPC-Server.  HttpRequest's thirty over the request line,
 *  the headers, the body and a multipart body; HttpServer's twenty-five;
 *  HttpResponse's twenty; HttpCodec's ten; the rest smaller; and
 *  twenty-nine tests, more than a third of them requests the parser must
 *  refuse.  SocketStream gained space, tab and cr, which a Stream that
 *  is not a WriteStream does not inherit.
 *
 *  1976: lib/Rest-Server.  RestServer's forty over configuration and the
 *  components; RestDispatcher's fifteen, the protocol; RestRequest's
 *  twenty-five; the loader, the cache, the pool and the user record; the
 *  five errors' handful; and sixteen tests.
 *
 *  2041: lib/Crypto.  Crypto's ten over primitive 209, Base64's seven,
 *  PasswordHash's ten, the error's one, and thirty across the three test
 *  classes -- fifty-eight -- plus the seven `new's the bootstrap
 *  synthesizes, one per class.
 *
 *  2047: the server's startup hook.  RestServer's initClass,
 *  withConnectionDo: and closePool, the loader's classFor:ifAbsent:, and
 *  two tests of an Init class told init: before the listener opens and
 *  init2: before the first request.
 *
 *  2066: lib/Web-Demo-Tests.  WebDemoTest's nine tests and the ten
 *  private methods under them -- a client on the raw socket that GETs a
 *  page and POSTs a request -- and the one `new' the bootstrap adds.
 *
 *  2071: a selector nobody has defined yet is interned, not put to a
 *  menu.  Parser>>makeNewSymbol:startingAt: superseded in lib/Tonel,
 *  TonelReader's editor and internsNewSelectors, and two tests -- one of
 *  which loads a method that sends a selector defined later in its file,
 *  which every test had passed and the server had refused.
 *
 *  2087: another origin.  HttpCodec's hostOf:, HttpRequest's origin,
 *  hostName and isPreflight, HttpServer's dispatch: split out of handle:
 *  and its allowsOrigin:, allow:for: and preflightFor:, and eight in the
 *  tests -- six of them tests, two of them a request written whole.
 *
 *  2170: lib/HTTP-Client and lib/LLM.  HttpUrl's eleven, HttpClient's
 *  eight, HttpClientResponse's fifteen (three ways a body ends), Ollama's
 *  seventeen with the four text substitutions on the class side,
 *  RestRequest's environmentAt: for a service reading its settings, and
 *  twenty-five across the three test classes -- plus the six `new's --
 *  and SocketTest's one for a name with two addresses, which the demo's
 *  Ollama found: `localhost' was ::1 first and the server was on
 *  127.0.0.1, and connectTo:port: now tries every address in turn.
 *
 *  2186: lib/Clipboard.  Clipboard's six over primitive 210 and the two
 *  line-end translations, ParagraphEditor's copySelection, cut and paste
 *  superseded to touch the system's clipboard, four tests, and the two
 *  `new's.
 */
/*
 *  2439: lib/LLM built out, and TLS.  Socket's startTls:, the timed form,
 *  isSecure, isTlsAvailable and environmentAt: over primitive 208's five
 *  new commands, with the two cross-direction waits; Smalltalk
 *  environmentAt:; HttpClient's do: forms, put and delete; HttpBodyStream;
 *  HttpClientResponse's body made lazy.  LLM's forty-odd -- the
 *  configuration, the loop, the HTTP, the images; the four services'
 *  wires; LLMConversation, LLMTool, LLMToolCall, LLMError; Qdrant.  And
 *  the tests: LLMTestCase's listener, thirty-three tests, four of them
 *  in HttpClientTest and SocketTest for the streaming and the TLS.
 *
 *  2439 -> 2515 is the Bugs1 audit worked through: lib/Compiler-Fixes, which
 *  teaches the image's own compiler the four constructs the C bootstrap
 *  compiler already accepted (86 of the image's own methods could not be
 *  re-parsed by the image holding them, and now none cannot); the four
 *  Object accessors and Number>>retry:coercing: rewritten so an unhandled
 *  error RETURNS instead of falling through and looping forever; the
 *  numeric tower's exact truncated, full-precision trig and a hash that
 *  agrees with = across Integer, Float and Fraction; and the small protocol
 *  the audit found missing.
 *
 *  2528 -> 2529 is InputSensor class>>shiftedKeys: the keyboard map 1983
 *  built is the Alto's, and three of its shifted punctuation keys are not
 *  what a keyboard made since answers.
 *
 *  2529 -> 2530 is File class>>initialize: 1983 put 3 in FilePool at #Shorten
 *  and every reader of it asks `rwmode bitAnd: Shorten', so the shorten bit
 *  was never set and no file written through a stream was ever truncated.
 *
 *  2530 -> 2577 is the Bugs2 fixes, and they are worth naming by what they
 *  repair rather than by count.  The hash contract: SequenceableCollection,
 *  Array and Interval hash over every element, so an OrderedCollection or a
 *  ByteArray can be a Set element or a Dictionary key at all, and a
 *  Dictionary keyed by Arrays stops being a linear scan -- 4,000 of them
 *  went from 26 seconds to 82 milliseconds.  LinkedList goes the other way,
 *  to identity =, because a Semaphore is one and its contents are whoever
 *  is waiting on it this instant.  Set, IdentitySet and CollectionElement,
 *  so `aSet add: nil' adds one.  Bag = and hash, and a Bag that refuses a
 *  negative number of occurrences.  ZeroDivide from Float>>/, Integer>>//,
 *  Fraction>>reciprocal and Number>>reciprocal, where only SmallInteger
 *  raised it before.  Object>>halt, halt:, notify:, confirm:, inspect and
 *  basicInspect, which opened a window a headless image can never dismiss.
 *  Time normalising its hours.  Float>>raisedTo: through the machine's pow.
 *  And the Blue Book protocol Bugs2 found missing: Behavior>>compile:,
 *  compile:notifying:, OrderedCollection>>removeFirst:, removeLast:,
 *  add:afterIndex:, String>>trimSeparators, PositionableStream>>collect:
 *  and a skip: that clamps.
 *
 *  2577 -> 2576 is a method REMOVED: lib/Kernel-Protocol's Object extension
 *  defined identityHash twice, and the two were not the same method.  Tonel
 *  loads a file in order, so the later one -- `^self basicIdentityHash' --
 *  had always won; the earlier was `<primitive: 75>' falling back on
 *  `^self hash', which is a VALUE hash for every class lib/ has given one,
 *  and it would have come back the day somebody reordered the file.
 *
 *  2576 -> 2583 is the rest of the keys a keyboard made since the Alto has.
 *  lib/Keyboard-Map had the four arrows; it now has home, end and the delete
 *  that goes forward, and control makes left and right step a word and home
 *  and end address the whole text.  Seven methods: InputSensor>>
 *  keyboardEventPeek, which answers the keystroke undecoded so that the
 *  editor can read the control bit as it was when the key was struck rather
 *  than as the hardware has it now; and six on ParagraphEditor --
 *  editingKey:typeIn:, deleteForward:, indexOfWordStartBefore:,
 *  indexOfWordStartAfter:, indexOfLineStartAt: and indexOfLineEndAt:.
 *  cursorKeyCodes became editingKeyCodes and cursorIndexFor: became
 *  cursorIndexFor:control:, which is a rename each and costs nothing.
 *
 *  2583 -> 2603: seven for the paths a person types into the File List's
 *  pattern pane -- a leading tilde, the dot segments, a directory name
 *  entered rather than matched, and a pattern matched against the local
 *  name it was cut from -- and thirteen in St80FileTest for them.
 *
 *  2603 -> 2609: four for `new' on the File List's list menu -- the menu
 *  itself, which now answers one command when nothing is selected; the
 *  command; where a bare name is taken; and the directory the pattern says
 *  the list is showing -- and two tests for the last two of them.
 *
 *  2609 -> 2613: the two openers and the list pane they now share, so that
 *  the pane is opened with a selector to ask the model which row to
 *  highlight, and a test that it keeps the selection when the rows are
 *  rebuilt.
 *
 *  2613 -> 2622: seven for the mark a directory carries in the list pane --
 *  the mark itself, the rows, the row one name is shown in, the name a row
 *  stands for, the selection reported as a row, and the one question about
 *  a designator that three methods each carried a copy of -- and two tests.
 *
 *  2622 -> 2627: three for the way up, which a directory listing holds so
 *  that going up is a row to select rather than a path to type -- the name
 *  of the place above, putting it in the list, and telling that name from
 *  the rest -- and two tests.
 *
 *  2627 -> 2920 is the Bugs3 fixes, sixty-four findings from a third
 *  outside-in audit, and the count is large because the audit reached
 *  every layer at once.  The hash functions: Float from all sixty-four
 *  bits, LargeInteger over every digit, Date from its seconds, Point and
 *  Rectangle without the cancelling xor, and Set, Dictionary, IdentitySet
 *  and LiteralDictionary mixing a hash before `\\ length' so a table whose
 *  size is a power of two stops putting every round number in one slot --
 *  a Set of 20,000 Floats went from 64 seconds to under one.  Exact
 *  comparison of an Integer or Fraction with a Float (asExactFraction),
 *  LargeInteger>>asFloat rounded once, Float>>rounded without the addition
 *  that rounded first, ln, sqrt and log of an Integer past 1e308, gcd: with
 *  zero, a printString: base refused outside 2..36.  Copies that copy:
 *  Dictionary, Bag and IdentityDictionary postCopy, SortedCollection
 *  refusing the positional adders and keeping its block through every copy,
 *  deepCopy answering self for blocks, contexts, classes, methods and
 *  processes.  Streams over an OrderedCollection, FileStream agreeing with
 *  ReadStream past the end, a file whose stat size is zero read to its end,
 *  Time reading noon as noon, Date refusing what is not a date in one
 *  sentence, a Random with 53 bits and a seed, a stable sort:, and the
 *  smaller protocol Bugs3 found missing -- copyFrom:, space:, tab:,
 *  subStrings:, valuesDo:, valueWithExit, display:, Number readFrom: a
 *  String.  The process protocol: terminate, suspend, resume and
 *  signalException: over primitives 231 and 232 so that they reach a
 *  process another worker holds, terminate running the unwind blocks, a
 *  reentrant Monitor, a Delay validated before it is published, ensure:
 *  disarming itself through runUnwindBlock, and a handler guard that is not
 *  a class raising instead of hanging.  The servers: percent-decoding of
 *  lowercase escapes, a digit cap and a linear conversion in JSONParser,
 *  header values refused with CR or LF in them, Symbol class>>lookup: so an
 *  unknown _class or _method is never interned, the RFC 7230 framing
 *  refusals, every ODBC bind checked, a body cap on the HTTP client, a
 *  realPathNamed: so a symlink cannot leave the doc root, and HEAD
 *  answering GET's length.  Tonel: an extension file written back as an
 *  extension, weak classes and trait compositions round-tripping, a file
 *  with a bad method loading nothing, a BOM skipped, and the 1983 body of
 *  Compiler>>evaluate:in:to:notifying:ifFail: kept under its own selector
 *  so a Debugger's do-it works.  And the tests for all of it: MonitorTest,
 *  ProcessTest, St80ExceptionTest, and additions to nine suites.
 */
#define LIB_METHODS             2920
/*
 *  The extension packages define no CLASSES, and a category is a property
 *  of a class definition, so Kernel-Methods-Fixes and System-Runtime add
 *  none.  Collections-Protocol was in that group until CollectionElement,
 *  the box a Set stores nil in, gave it a class of its own.  Files-Fixes
 *  joined them when TextFileStream gave it one too.
 */
/*
 *  12 -> 14: Database and Database-Tests.  A category is a property of a
 *  class DEFINITION, so a package of pure extensions adds none -- which is
 *  why this number tracks the packages that define classes and not the
 *  packages.
 */
/*
 *  14 -> 16: Json and Json-Tests.  lib/Json is nine classes and one
 *  extension file, and the extension file adds no category for the reason
 *  above -- its methods land in the *Json protocol of classes that already
 *  have categories of their own.
 */
/*
 *  16 -> 18: Network and Network-Tests, four classes and two.  The
 *  extension that gives SystemDictionary its arguments lands in the
 *  *Network protocol of a class that already has a category, as Json's
 *  extension did.
 */
/*
 *  18 -> 20: Tonel and Tonel-Tests.  The Scanner, ClassDescription, Class,
 *  PosixFile and ReadWriteStream extensions land in existing categories.
 */
/*
 *  20 -> 22: JSON-RPC-Server and JSON-RPC-Server-Tests.
 */
/*
 *  22 -> 24: Rest-Server and Rest-Server-Tests.
 */
/*
 *  24 -> 26: Crypto and Crypto-Tests.
 */
/*
 *  26 -> 27: Web-Demo-Tests.  Web-Demo itself is the category of the
 *  demo's classes and reaches the image only when a request loads one.
 */
/*
 *  27 -> 31: HTTP-Client, HTTP-Client-Tests, LLM, LLM-Tests.
 */
/*
 *  31 -> 33: Clipboard and Clipboard-Tests.
 *  33 -> 34: Compiler-Fixes.
 */
#define LIB_CATEGORIES          34
/*
 *  The image this test measures is the one profiles/st2026.profile builds,
 *  and it is built BY that profile rather than by a list kept alongside it.
 *
 *  It used to be a list: sources/MANIFEST read directly, then thirty-odd
 *  lib/ paths written out by hand, with a comment saying they were what the
 *  profile named.  They were not, and the gap grew every time lib/ did --
 *  the profile loaded 82 files and the list had 60.  What that cost is worth
 *  stating plainly, because it is the whole argument for this change: when
 *  lib/ gave Symbol a value-based #=, this test went on asserting that
 *  `#foo = 'foo'` is FALSE, and went on PASSING, for a full round of work
 *  after the system it claims to measure had changed.  A test that builds
 *  its own subject can agree with itself forever.
 *
 *  PROFILE_expand answers the files and their dialects together, which is
 *  the other half: the 1983 chunk files compile as Blue Book and everything
 *  in lib/ as closures, and that split used to be a hand-maintained index
 *  into a hand-maintained array.
 */
#define PROFILE     "profiles/st2026.profile"

static st_names     sources;
static int         *source_dialects;
static int          built;

static int
load_sources(void)
{
    char    error[256];

    if (!PROFILE_expand(PROFILE, &sources, &source_dialects,
                        error, sizeof error)) {
        printf("skipped: %s\n", error);
        return 0;
    }
    return sources.count > 0;
}

static int
build_once(void)
{
    st_bootstrap_result res;

    if (BOOT_build_dialects((const char *const *) sources.items,
                            source_dialects, sources.count, &res) != 0) {
        printf("  bootstrap failed: %s\n", res.error);
        return 0;
    }
    printf("  %u classes, %u methods, %u symbols\n", res.classes_created,
           res.methods_compiled, res.symbols_interned);

    CHECK_EQ_INT(res.classes_created, BLUEBOOK_CLASSES + LIB_CLASSES);
    /*  4517 from the MIT sources, plus the few in kernel/Bootstrap.st.  */
    CHECK_EQ_INT(res.methods_compiled, BLUEBOOK_METHODS + LIB_METHODS);
    /*
     *  One trait, and two of its three methods flattened into Greeter --
     *  the third is #subject, which Greeter defines itself and therefore
     *  keeps.  A trait creates no class, so classes_created does not move.
     */
    CHECK_EQ_INT(res.traits_created, 1);
    CHECK_EQ_INT(res.methods_flattened, 2);
    CHECK_EQ_INT(res.traits_rejected, 0);
    /*
     *  Three: Initialized, TestResult and TestSuite.  Not Subinitialized,
     *  whose chain already has one, and not SelfMade, which wrote its own.
     *  A 1983 class never qualifies -- the chunk files already say what
     *  they mean, and the ~34 that want initialization write the idiom out
     *  by hand.  SUnit is where the mechanism stops being a demonstration:
     *  TestSuite new has to answer a suite with an empty collection in it,
     *  and nothing in SUnit says so.
     */
    /*
     *  Eleven, not three, and the jump is deliberate.
     *
     *  Object>>initialize now exists -- Pharo's convention, needed because
     *  every Pharo class whose initialize begins `super initialize' has to
     *  have something to stop at.  A consequence is that `initialize' is
     *  reachable from EVERY class, so the loader synthesizes
     *  `new ^super new initialize' for every lib/ and pharo/ class rather
     *  than only for the few that defined one themselves.
     *
     *  That is Pharo's object model arriving, not an accident: in Pharo,
     *  `Foo new' initializes.  The 1983 classes are untouched, because the
     *  synthesis only ever applied to ours.
     *
     *  Twenty-two now rather than eleven, because the rule asks whether
     *  the class defines `new' ITSELF rather than whether anything in its
     *  chain does.  Declining because an ancestor had one is what left
     *  every Pharo class subclassing a 1983 collection half-built: 1983
     *  goes new -> new: -> init: and never sends #initialize.
     */
    /*
     *  38, not 23, for the same reason the class count moved: lib/Concurrency
     *  alone brings Mutex, Monitor, Promise, SharedQueue and the fixtures,
     *  and every one of them defines initialize and no class-side new.
     */
    /*
     *  43 -> 57 with lib/Database.  Nearly every class in it defines
     *  initialize and no class-side new, which is the ordinary way to write
     *  one here and is exactly what this synthesis is for.
     */
    /*
     *  57 -> 66 with lib/Json and its tests.  All nine, because not one of
     *  them defines a class-side new: JSONObject, JSONArray, JSONParser and
     *  JSONWriter define initialize and let the loader write the new, and
     *  the error and the four test classes need neither.  68 with the two
     *  parallel fixtures.
     */
    /*
     *  70 -> 76 with lib/Network: all six, for the same reason as Json's
     *  nine -- none defines a class-side new.  Socket and ServerSocket are
     *  made through connectTo:port: and listenOn:, which send new and then
     *  setHandle:, and SocketStream through on:, which sends basicNew, so
     *  the synthesized new is sent and does no harm.
     */
    /*
     *  76 -> 83 with lib/Tonel: all seven of its classes, none defining a
     *  class-side new.
     */
    /*
     *  83 -> 94 with lib/JSON-RPC-Server: all eleven, for the same reason.
     */
    /*
     *  94 -> 109 with lib/Rest-Server: fifteen of its sixteen; RestUuid
     *  defines its own class-side new, which is the whole of what it does.
     */
    /*
     *  125 -> 141 with lib/LLM built out and HttpBodyStream: all sixteen
     *  classes, none defining a class-side new.  The models are made
     *  through model: and apiKey:model:, LLMConversation through on:,
     *  LLMTool through name:description:do:, and each sends the
     *  synthesized new first.
     */
    /*
     *  141 -> 145 with lib/Compiler-Fixes and OutOfMemory:
     *  SyntaxErrorNotification, NonBooleanReceiver, BlockTemporaryNode and
     *  OutOfMemory, none of which defines a class-side new either.
     *
     *  145 -> 146 with RecursionDepthExceeded, which does not either.
     *
     *  146 -> 147 with St80FileTest, which does not either.
     *
     *  147 -> 151 with the Bugs3 fixes: CorruptMethod, MonitorTest,
     *  ProcessTest and St80ExceptionTest, none of which defines one.
     */
    CHECK_EQ_INT(res.news_synthesized, 151);
    built = 1;
    return 1;
}

/*
 *  Evaluate as the driver does: compile the expression as a method body,
 *  stand up a context whose sender is nil, and run.
 */
/*  Which dialect the expressions below are compiled as.  */
static int  test_dialect = ST_DIALECT_BLUE_BOOK;

/*  A sink that only counts, for the TonelWriter check below.  */
typedef struct { unsigned classes; unsigned methods; } tonel_count;

static int
count_class_def(const st_source_class_def *def, void *user)
{
    (void) def;
    ((tonel_count *) user)->classes++;
    return 1;
}

static int
count_method(const char *class_name, int class_side, const char *category,
             const char *source, const char *file, unsigned line, void *user)
{
    (void) class_name; (void) class_side; (void) category; (void) source;
    (void) file; (void) line;
    ((tonel_count *) user)->methods++;
    return 1;
}

static void
fill_compile_context(st_compile_context *ctx)
{
    memset(ctx, 0, sizeof *ctx);
    ctx->intern_symbol      = BOOT_intern_symbol;
    ctx->make_string        = BOOT_make_string;
    ctx->make_float         = BOOT_make_float;
    ctx->make_large_integer = BOOT_make_large_integer;
    ctx->make_large_integer_digits = BOOT_make_large_integer_digits;
    ctx->make_array         = BOOT_make_array;
    ctx->make_byte_array    = BOOT_make_byte_array;
    ctx->make_method_state  = BOOT_make_method_state;
    ctx->make_character     = BOOT_make_character;
    ctx->lookup_global      = BOOT_lookup_global;
    ctx->dialect            = test_dialect;
}

static st_oop
evaluate(const char *expression)
{
    st_compile_context  ctx;
    st_compile_result   res;
    /*
     *  2048, not 1024: the Bugs2 checks below write out a 300-element
     *  literal array and a 300-character string literal, because the fault
     *  they are about was a buffer that dropped what did not fit.
     */
    char                source[2048];
    st_oop              context;

    fill_compile_context(&ctx);

    /*
     *  Collect first.  Every doIt here is unreachable the moment it finishes,
     *  and an expression that runs away allocating would otherwise exhaust
     *  the table and make every LATER expression fail as "out of memory" --
     *  turning one broken thing into twenty misleading ones.
     */
    OM_collect();

    /*
     *  An expression containing a caret is already a method body, temporary
     *  declarations and all, so it is used as written -- the same rule the
     *  driver uses.  Wrapping it in "doIt ^" instead produces "doIt ^| f |",
     *  which is not a sentence in any dialect.
     */
    if (strchr(expression, '^'))
        snprintf(source, sizeof source, "doIt %s", expression);
    else
        snprintf(source, sizeof source, "doIt ^%s", expression);
    if (COMPILE_method(source, &ctx, &res) != 0) {
        printf("  cannot compile \"%s\": %s\n", expression, res.error);
        return ST_OOP_INVALID;
    }
    context = OM_instantiate_pointers(ST_CLASS_METHOD_CONTEXT, 64);
    if (!OM_is_present(context))
        return ST_OOP_INVALID;
    OM_store_pointer(ST_CTX_SENDER, context, ST_NIL);
    OM_store_pointer(ST_CTX_METHOD, context, res.method);
    OM_store_pointer(ST_CTX_RECEIVER, context, ST_NIL);
    OM_store_pointer(ST_CTX_IP, context,
                     OM_int_oop((st_int)
                        (BOOT_method_initial_ip(res.method) + 1)));
    /*  The stack begins above the temporaries, not at zero.  */
    OM_store_pointer(ST_CTX_SP, context,
                     OM_int_oop((st_int) ST_header_temporary_count(
                                    OM_fetch_pointer(0, res.method))));

    memset(&st_vm, 0, sizeof st_vm);
    st_vm.active_context = ST_NIL;
    ST_set_active_context(context);
    st_vm.running      = 1;
    st_vm.return_value = ST_NIL;
    ST_interp_run(20000000);
    if (st_vm.running) {
        printf("  \"%s\" did not finish\n", expression);
        return ST_OOP_INVALID;
    }
    return st_vm.return_value;
}

/*
 *  A doIt this compiler must REFUSE, and the words it must refuse it in.
 *
 *  Half the Bugs2 compiler fixes are about refusing rather than answering:
 *  before them a name too long for the frame tables was silently truncated
 *  to a DIFFERENT name, and the diagnostic that eventually came out named
 *  the assignment rather than the name.  A test that only checked what the
 *  compiler accepts cannot see any of that.
 */
static void
check_refused(const char *expression, const char *wanted)
{
    st_compile_context  ctx;
    st_compile_result   res;
    char                source[2048];

    fill_compile_context(&ctx);
    snprintf(source, sizeof source, "doIt %s", expression);
    memset(&res, 0, sizeof res);
    ++st_test_checks;
    if (COMPILE_method(source, &ctx, &res) == 0) {
        ++st_test_failures;
        printf("  FAIL a %u-character doIt compiled; it should have been "
               "refused with '%s'\n", (unsigned) strlen(source), wanted);
        return;
    }
    if (!strstr(res.error, wanted)) {
        ++st_test_failures;
        printf("  FAIL refused with '%s', want something holding '%s'\n",
               res.error, wanted);
    }
}

static void
check_integer(const char *expression, st_int want)
{
    st_oop  value = evaluate(expression);

    ++st_test_checks;
    if (!OM_is_int(value) || OM_int_value(value) != want) {
        char    text[128];

        ++st_test_failures;
        ST_print_object(value, text, sizeof text);
        printf("  FAIL %s: got %s, want %lld\n", expression, text,
               (long long) want);
    }
}

static void
check_oop(const char *expression, st_oop want, const char *label)
{
    st_oop  value = evaluate(expression);

    ++st_test_checks;
    if (value != want) {
        char    text[128];

        ++st_test_failures;
        ST_print_object(value, text, sizeof text);
        printf("  FAIL %s: got %s, want %s\n", expression, text, label);
    }
}

/*
 *  A String or Symbol answer, compared by its characters.
 */
static void
check_string(const char *expression, const char *want)
{
    st_oop  value = evaluate(expression);
    /*
     *  1024, not 256: the list of every TestCase subclass is compared
     *  here, and at 256 it was silently cut off at its sixteenth name --
     *  a check that had passed for months would then have failed for
     *  ever with a want string no answer could equal.
     */
    char    text[1024];

    ++st_test_checks;
    if (!OM_is_present(value)) {
        ++st_test_failures;
        printf("  FAIL %s: no answer, want '%s'\n", expression, want);
        return;
    }
    OM_string_of(value, text, sizeof text);
    if (strcmp(text, want) != 0) {
        ++st_test_failures;
        printf("  FAIL %s: got '%s', want '%s'\n", expression, text, want);
    }
}

static void
check_boolean(const char *expression, int want)
{
    check_oop(expression, want ? ST_TRUE : ST_FALSE, want ? "true" : "false");
}

static void
check_class(const char *expression, const char *class_name)
{
    st_oop  value = evaluate(expression);

    ++st_test_checks;
    if (!OM_is_present(value)
     || OM_fetch_class(value) != BOOT_global(class_name)) {
        char    text[128];

        ++st_test_failures;
        ST_print_object(value, text, sizeof text);
        printf("  FAIL %s: got %s, want a %s\n", expression, text, class_name);
    }
}

/*
 *  The classes the library is expected to bring, which the 36-class kernel
 *  never had.  MVC needs all of these.
 */
static void
test_classes_present(void)
{
    static const char *const expected[] = {
        "OrderedCollection", "Dictionary", "Set", "Bag", "SortedCollection",
        "Interval", "Symbol", "Stream", "ReadStream", "WriteStream",
        "Rectangle", "Form", "BitBlt", "Pen", "Fraction", "Date", "Time",
        "StandardSystemView", "StandardSystemController", "Browser",
        "Paragraph", "TextStyle", "DisplayScreen", "Debugger", "Inspector"
    };
    unsigned    i;

    for (i = 0; i < sizeof expected / sizeof expected[0]; ++i) {
        st_oop  cls = BOOT_global(expected[i]);

        ++st_test_checks;
        if (!OM_is_present(cls)) {
            ++st_test_failures;
            printf("  FAIL missing class %s\n", expected[i]);
        }
    }
}

/*
 *  Arithmetic answered by the 1983 methods rather than by ours.  factorial,
 *  gcd: and max: are Xerox's code in Number and Integer; nothing in this
 *  project implements them.
 */
static void
test_arithmetic(void)
{
    check_integer("3 + 4", 7);
    check_integer("6 * 7", 42);
    check_integer("3 factorial", 6);
    check_integer("6 factorial", 720);
    check_integer("100 gcd: 75", 25);
    check_integer("3 max: 9", 9);
    check_integer("3 min: 9", 3);
    check_integer("-7 abs", 7);
    check_integer("17 \\\\ 5", 2);
    check_integer("2 raisedTo: 10", 1024);
    check_integer("(1 to: 10) inject: 0 into: [:a :b | a + b]", 55);
    /*  Fractions: exact arithmetic through Fraction>>+ and reduction.  */
    check_integer("(3/4) + (1/4)", 1);
    check_integer("((2/3) + (1/6)) numerator", 5);
    check_integer("((2/3) + (1/6)) denominator", 6);
}

/*
 *  Arithmetic that leaves SmallInteger range.
 *
 *  A primitive is required to FAIL when its result will not fit, because
 *  failing is what runs the Smalltalk body that promotes to a
 *  LargePositiveInteger.  Multiplication did not: it formed a * b in
 *  int64_t, that wrapped, and whenever the wrapped value happened to land
 *  back inside +/-2^62 the primitive answered it and the promotion never
 *  ran.  21 factorial is exactly such a value -- it came out as
 *  -4249290049419214848, negative, a SmallInteger, and wrong by 2^64.
 *
 *  That is the shape worth testing for rather than the number: a primitive
 *  quietly succeeding where it had to fail leaves no trace anywhere, since
 *  failing is the ordinary path and nothing announces not taking it.  So
 *  each check below is an identity that only holds if the promotion
 *  happened, not a comparison against a constant someone could update to
 *  match a wrong answer.
 */
static void
test_integers_larger_than_a_smallinteger(void)
{
    /*
     *  20! fits and 21! does not, so the boundary is crossed here.
     *  check_integer rather than check_class for the small one: a
     *  SmallInteger is a tagged immediate, so it is not "present" as an
     *  object and check_class cannot see it.  Requiring the exact value is
     *  the stronger check anyway.
     */
    check_integer("20 factorial", 2432902008176640000LL);
    check_class("21 factorial", "LargePositiveInteger");
    check_integer("21 factorial // 20 factorial", 21);
    check_integer("21 factorial - 21 factorial", 0);
    check_integer("21 factorial printString size", 20);

    /*  The wrapped product used to be a plausible small number.  */
    check_class("3037000500 * 3037000500", "LargePositiveInteger");
    check_integer("(3037000500 * 3037000500) // 3037000500", 3037000500);

    /*  raisedTo: multiplies, so it wrapped to 0 for anything past 2^63.  */
    check_class("2 raisedTo: 70", "LargePositiveInteger");
    check_integer("(2 raisedTo: 70) // (2 raisedTo: 69)", 2);
    check_integer("(2 raisedTo: 70) printString size", 22);

    /*
     *  Addition never wrapped -- two SmallIntegers always fit int64_t --
     *  but its promotion answered 2^32, because the image builds a
     *  LargePositiveInteger with bitShift: and the primitive refused every
     *  shift of 31 or more.  That bound was inherited from the 16-bit
     *  memory, where it was right.
     */
    check_class("4611686018427387903 + 1", "LargePositiveInteger");
    check_integer("4611686018427387903 + 1 - 1", 4611686018427387903LL);
    check_integer("(1 bitShift: 40)", 1099511627776LL);
    check_integer("(1 bitShift: 40) bitShift: -40", 1);
    check_class("1 bitShift: 62", "LargePositiveInteger");

    /*  And the negative side, which shifts and multiplies differently.  */
    check_integer("(0 - 21 factorial) + 21 factorial", 0);
    check_class("0 - 21 factorial", "LargeNegativeInteger");
}

/*
 *  Evaluating a block twice at once.
 *
 *  A Blue Book BlockContext is the closure and the activation record in one
 *  object, so ST_activate_block used to write the instruction pointer, the
 *  stack pointer and the caller into the very object somebody was holding.
 *  Each activation now gets its own record, which is what these check.
 *
 *  What it does NOT fix, and no amount of copying could: a block's
 *  ARGUMENTS live in the home method's temporary frame, not the block's.
 *  Two activations of one block therefore share the variable, and so do two
 *  different blocks in the same method, which the compiler may give the
 *  same slot.  That is what closures are for and it is Phase D.  The tests
 *  below are written to say which side of that line each case is on.
 */
static void
test_blocks_activate_separately(void)
{
    /*
     *  Recursion where the outer value is already on the stack before the
     *  inner call.  This answered nil until each activation got its own
     *  record; it is the case the copy fixes.
     */
    check_integer("| f | f := [:n | n = 0 ifTrue: [1] "
                  "ifFalse: [n * (f value: n - 1)]]. ^f value: 10",
                  3628800);
    check_integer("| f | f := [:n | n = 0 ifTrue: [0] "
                  "ifFalse: [1 + (f value: n - 1)]]. ^f value: 100", 100);

    /*  A block reached again from inside itself, without arguments.  */
    check_integer("| n b | n := 0. b := [n := n + 1. "
                  "n < 5 ifTrue: [b value]. n]. ^b value", 5);

    /*  Ordinary re-use, which worked before and must keep working.  */
    check_integer("| b | b := [:n | n * 2]. ^(b value: 3) + (b value: 4)", 14);
    check_integer("((1 to: 5) collect: [:i | i * i]) last", 25);
    check_integer("(1 to: 10) inject: 0 into: [:a :b | a + b]", 55);

    /*
     *  And the boundary, asserted rather than left to be discovered.  The
     *  argument of the outer activation is gone after the inner one runs,
     *  because both wrote the same home slot.  When Phase D lands this
     *  answers 7 and the test changes with the behaviour it describes.
     */
    check_integer("| b c | c := [:m | m * 10]. b := [:n | (c value: 99). n]. "
                  "^b value: 7", 99);
}

/*
 *  Closures.
 *
 *  Everything here is compiled in the closure dialect, and every one of
 *  these answers differently -- or not at all -- as a Blue Book block.  A
 *  BlockContext keeps its arguments and temporaries in the HOME method's
 *  frame, so two activations of one block share them; a closure has a frame
 *  of its own and captures what it needs.
 *
 *  The 1983 library and the trace oracle never see any of this: the dialect
 *  is a field in the compile context and defaults to Blue Book.
 */
static void
test_closures(void)
{
    test_dialect = ST_DIALECT_CLOSURES;

    /*  The plain shapes, which must go on working.  */
    check_integer("[3 + 4] value", 7);
    check_integer("[:a :b | a * b] value: 6 value: 7", 42);
    check_integer("(1 to: 5) inject: 0 into: [:a :b | a + b]", 15);
    check_integer("((1 to: 20) collect: [:i | i * i]) last", 400);
    check_integer("[:x | | y | y := x * 3. y + 1] value: 5", 16);
    check_integer("| n | n := 0. [n < 5] whileTrue: [n := n + 1]. ^n", 5);

    /*
     *  Recursion.  This is the case Phase B could not fix and named as
     *  Phase D's: it answered nil, because the second activation of the
     *  block overwrote the first one's argument.
     */
    check_integer("| f | f := [:n | n < 2 ifTrue: [n] "
                  "ifFalse: [(f value: n - 1) + (f value: n - 2)]]. "
                  "^f value: 25", 75025);
    check_integer("| f | f := [:n | n = 0 ifTrue: [1] "
                  "ifFalse: [n * (f value: n - 1)]]. ^f value: 10", 3628800);

    /*
     *  Capture by value, and capture by reference.  A name a block only
     *  reads is copied; one that anything assigns has to be shared, or the
     *  two scopes would stop seeing each other's stores.
     */
    check_integer("| t | t := 5. ^[t + 1] value", 6);
    check_integer("| t | t := 5. [t := t * 2] value. ^t", 10);
    check_integer("| t b | t := 1. b := [t]. t := 99. ^b value", 99);
    check_integer("| s | s := 0. (1 to: 10) do: [:i | s := s + i]. ^s", 55);

    /*
     *  A block outliving the method that made it, and each activation
     *  capturing its own copy.  Neither is possible with a BlockContext:
     *  its home is gone, and its argument slot is shared.
     */
    check_integer("| mk | mk := [:n | [n * 2]]. "
                  "^((mk value: 5) value) + ((mk value: 7) value)", 24);
    check_integer("| c | c := [:n | [:m | n + m]]. ^(c value: 10) value: 5",
                  15);
    check_integer("| a | a := OrderedCollection new. "
                  "(1 to: 3) do: [:i | a add: [i]]. "
                  "^(a collect: [:b | b value]) inject: 0 into: [:x :y | x + y]",
                  6);

    /*  Non-local return, out of a block and out of a nested one.  */
    check_oop("| b | b := [:x | x > 3 ifTrue: [^#big]. #small]. ^b value: 5",
              BOOT_intern_symbol("big", NULL), "#big");
    check_oop("| b | b := [:x | x > 3 ifTrue: [^#big]. #small]. ^b value: 1",
              BOOT_intern_symbol("small", NULL), "#small");
    check_integer("| f | f := [:c | c do: [:e | e > 2 ifTrue: [^e]]. 0]. "
                  "^f value: #(1 2 3 4)", 3);
    check_integer("^(1 to: 10) detect: [:i | i > 6]", 7);

    /*
     *  And the boundary Phase B recorded.  As a Blue Book block this
     *  answers 99, because both blocks are given the same home slot; as
     *  closures each argument is its own.  The assertion changes with the
     *  behaviour it describes, which is what it was written for.
     */
    check_integer("| b c | c := [:m | m * 10]. b := [:n | (c value: 99). n]. "
                  "^b value: 7", 7);

    test_dialect = ST_DIALECT_BLUE_BOOK;
    check_integer("| b c | c := [:m | m * 10]. b := [:n | (c value: 99). n]. "
                  "^b value: 7", 99);
}

/*
 *  Exceptions.
 *
 *  Smalltalk-80 has none.  It has Object>>error:, which opens a debugger,
 *  and the convention of passing a block to run when something is not
 *  found; neither can be caught and neither lets a caller decide.
 *
 *  What makes this implementable is that a marked frame can be found by
 *  walking senders.  on:do: declares <primitive: 199> and ensure: declares
 *  <primitive: 198>; neither number is implemented, an unimplemented
 *  primitive fails, so the Smalltalk body runs and the number is left on
 *  the frame as a LABEL.  D0 asked the shipped 1983 image whether it uses
 *  either for anything real: its highest primitive is 135.
 */
static void
test_exceptions(void)
{
    test_dialect = ST_DIALECT_CLOSURES;

    /*  Catching, and the value of the protected block when nothing is
        signalled.  */
    check_integer("[3 + 4] on: Error do: [:e | 0]", 7);
    check_integer("[Error new signal] on: Error do: [:e | 42]", 42);
    check_integer("[Error new signal] on: Error do: [:e | e return: 5]", 5);

    /*  The class hierarchy decides what a handler catches.  */
    check_oop("[Error new signal] on: Exception do: [:e | true]", ST_TRUE,
              "true");
    check_integer("[[Error new signal] on: ZeroDivide do: [:e | 1]] "
                  "on: Error do: [:e | 2]", 2);
    check_oop("[Warning new signal] on: Error do: [:e | true]", ST_NIL,
              "nil, from the default action");

    /*  The gate the plan names.  */
    check_integer("[1/0] on: ZeroDivide do: [:e | e return: 42]", 42);
    check_integer("[nil foo] on: MessageNotUnderstood do: [:e | 1]", 1);
    check_integer("[nil error: 'x'] on: Error do: [:e | 9]", 9);

    /*
     *  A handler is disabled while it runs, so anything IT signals goes to
     *  the next handler out.  Without that the search that was meant to
     *  move outwards finds the same frame again and goes round for ever --
     *  an exception a handler could not deal with hung the image.
     */
    check_oop("[[Error new signal] on: Error do: [:e | Error new signal. 1]] "
              "on: Error do: [:e2 | true]", ST_TRUE, "true");

    /*  retry, pass and resume.  */
    check_integer("| n | n := 0. ^[n := n + 1. n < 3 ifTrue: [Error new signal]. n] "
                  "on: Error do: [:e | e retry]", 3);
    check_integer("[[Error new signal] on: Error do: [:e | e pass]] "
                  "on: Error do: [:e | 8]", 8);
    check_integer("[Warning new signal] on: Warning do: [:e | e resume: 9]", 9);
    /*  Resuming really goes back to the signal point, not past it.  */
    check_integer("[(Warning new signal) + 1] on: Warning do: [:e | e resume: 9]",
                  10);
    /*
     *  And an exception resumed after its handler returned is refused.  A
     *  returned context still looks well formed -- do_return nils the
     *  fields of the frame it leaves, not of everything that frame called
     *  -- so the test has to be whether it is still reachable, and that is
     *  in the primitive rather than here.
     */
    check_oop("| saved | [Warning new signal] on: Warning do: "
              "[:e | saved := e. e return: 1]. "
              "^[saved resume: 2] on: Error do: [:x | true]", ST_TRUE, "true");
    /*  retry restarts the frame rather than nesting another one.  */
    check_integer("| n | n := 0. ^[n := n + 1. n < 5 ifTrue: [Error new signal]. n] "
                  "on: Error do: [:e | e retry]", 5);

    /*  Ordinary division is unaffected by the ZeroDivide override.  */
    check_integer("7 // 2", 3);
    check_integer("7 \\\\ 2", 1);
    check_integer("(1/2) denominator", 2);

    /*
     *  Unwinding.  These need real methods: in a doIt every ^ targets the
     *  doIt and there is nothing left to look at afterwards.  Unwind
     *  records what happened in what order, which is the whole question.
     */
    check_oop("Unwind reset. ^Unwind normal",
              BOOT_intern_symbol("normal", NULL), "#normal");
    check_integer("Unwind reset. Unwind normal. ^Unwind trace size", 3);
    check_oop("Unwind reset. ^Unwind earlyReturn",
              BOOT_intern_symbol("early", NULL), "#early");
    /*  body and unwound, and NOT the statement after the ensure:.  */
    check_integer("Unwind reset. Unwind earlyReturn. ^Unwind trace size", 2);
    check_oop("Unwind reset. Unwind earlyReturn. ^Unwind trace last",
              BOOT_intern_symbol("unwound", NULL), "#unwound");

    /*  Two ensure: frames between the ^ and its home: both run, inner
        first.  This is the case these implementations usually get wrong. */
    check_integer("Unwind reset. Unwind nested. ^Unwind trace size", 3);
    check_oop("Unwind reset. Unwind nested. ^Unwind trace at: 2",
              BOOT_intern_symbol("inner", NULL), "#inner");
    check_oop("Unwind reset. Unwind nested. ^Unwind trace at: 3",
              BOOT_intern_symbol("outer", NULL), "#outer");

    /*  ifCurtailed: runs only when the receiver does not finish.  */
    check_integer("Unwind reset. Unwind curtailedNormally. ^Unwind trace size",
                  1);
    check_integer("Unwind reset. Unwind curtailedEarly. ^Unwind trace size", 2);

    /*
     *  And a block returning from a method that has already returned.  The
     *  VM sends cannotReturn:, which until this package existed nothing
     *  implemented -- so it stopped quietly and kept the value, which looks
     *  exactly like success.
     */
    check_oop("[Unwind escapingBlock value] on: Error do: [:e | true]",
              ST_TRUE, "true");

    test_dialect = ST_DIALECT_BLUE_BOOK;
}

/*
 *  Pragmas the image can read.
 *
 *  The Blue Book has one, <primitive: N>, and treats it as syntax rather
 *  than as an object.  Squeak generalised the notation; making the result
 *  readable from Smalltalk is what turns it from something the compiler
 *  throws away into something a program can act on -- which is what SUnit's
 *  <test> is for, and what the parallel-safety audit's <shared: #serialize>
 *  will be.
 *
 *  The AdditionalMethodState is found by scanning the literal frame for
 *  one, not at a fixed index: Pharo puts it next to last, and next to last
 *  here is where the Blue Book header extension goes when a method declares
 *  a primitive.
 */
static void
test_pragmas_are_objects(void)
{
    test_dialect = ST_DIALECT_CLOSURES;

    /*  Tonel v3 writes #slots where v1 writes #instVars; for a plain slot
        they say the same thing, and the class behaves the same.  */
    check_integer("(Slotted new left: 3 right: 4) sum", 7);

    /*
     *  A trait is applied by flattening: its source is compiled into the
     *  using class.  Greeter takes TGreeting whole and overrides #subject,
     *  so #greeting comes from the trait and calls the CLASS's version --
     *  which is the property that makes flattening worth doing rather than
     *  copying a CompiledMethod, since a copied method would carry the
     *  trait's own literal frame and instance-variable indices.
     */
    check_string("Greeter new greeting", "hello from a class");
    /*  Class-side trait methods come across too, from #traits alone.  */
    check_string("Greeter defaultGreeting", "hello");
    /*  And the flattened method records where its source lives.  */
    check_string("(Greeter organization categoryOfElement: #greeting)",
                 "*trait:TGreeting");
    /*  A trait creates no class: it is not in the system dictionary.  */
    check_boolean("Smalltalk includesKey: #TGreeting", 0);

    /*
     *  A package class that defines initialize and no new is given the 1983
     *  idiom by the loader, so Pharo-flavoured code allocates the way it
     *  expects to.
     */
    check_integer("Initialized new count", 1);
    /*
     *  And its subclass is NOT given one.  This is the whole reason the rule
     *  is about the chain rather than the class: Initialized class>>new
     *  already sends initialize, so a second new here would send it twice --
     *  the outer super new running this initialize, then the inner one.
     *  2 means once; 3 would mean the bug.
     */
    check_integer("Subinitialized new count", 2);
    /*  A class that writes its own new keeps it, and its initialize with it.  */
    check_oop("SelfMade new count", ST_NIL, "nil");

    /*  A method with no pragmas has none, and costs no literal.  */
    check_integer("(Unwind class compiledMethodAt: #normal) pragmas size", 0);
    /*  Nor does one whose only pragma is a primitive, which is the
        compiler's business rather than the image's.  */
    check_integer("(BlockClosure compiledMethodAt: #on:do:) pragmas size", 0);

    /*  Three of them, keyword and arguments intact.  */
    check_integer("(Unwind class compiledMethodAt: #annotated) pragmas size",
                  3);
    check_oop("(Unwind class compiledMethodAt: #annotated) pragmas first "
              "keyword == #test", ST_TRUE, "true");
    check_oop("((Unwind class compiledMethodAt: #annotated) "
              "pragmaAt: #shared:) argumentAt: 1", 
              BOOT_intern_symbol("serialize", NULL), "#serialize");
    check_integer("((Unwind class compiledMethodAt: #annotated) "
                  "pragmaAt: #deprecated:) arguments first size", 11);
    check_oop("(Unwind class compiledMethodAt: #annotated) hasPragma: #test",
              ST_TRUE, "true");
    check_oop("(Unwind class compiledMethodAt: #annotated) hasPragma: #nope",
              ST_FALSE, "false");
    check_oop("((Unwind class compiledMethodAt: #annotated) pragmaAt: #nope) "
              "isNil", ST_TRUE, "true");

    /*  And the method still runs, which the extra literal must not disturb. */
    check_oop("Unwind annotated", BOOT_intern_symbol("annotated", NULL),
              "#annotated");

    test_dialect = ST_DIALECT_BLUE_BOOK;
}

/*
 *  Weak references, and a collection that can be asked for.
 *
 *  The 1983 library has no garbageCollect anywhere in it -- the image
 *  simply cannot ask -- which is livable until weak references exist and
 *  then is not, because a weak slot is cleared BY a collection and there
 *  was no way to observe the mechanism at all.
 *
 *  Asking for one immediately found something worse.  ST_interp_register is
 *  called from ST_interp_init, which the -run path calls and the doIt path
 *  does not, so the collector walked an interpreter table with nothing in
 *  it and freed the running doIt's own context and method -- and the
 *  interpreter carried on reading bytecodes out of memory that had been
 *  handed back.  It stayed hidden because nothing could request a
 *  collection, and an automatic one only happens when the table fills.
 */
static void
test_weak_references(void)
{
    test_dialect = ST_DIALECT_CLOSURES;

    /*  A collection in the middle of a doIt, which used to be a crash.  */
    check_integer("| w | w := WeakArray new: 3. Smalltalk garbageCollect. "
                  "^w size", 3);
    check_integer("| a | a := Array new: 4. Smalltalk garbageCollect. "
                  "a at: 1 put: 7. ^a at: 1", 7);

    /*
     *  The object is made inside a method that has returned, so nothing but
     *  the weak slot holds it.  Doing it inline would not test anything:
     *  the doIt's own stack slot above its stack pointer still names the
     *  object, and the collector walks every slot of a context rather than
     *  only the live ones.
     */
    check_integer("| w | w := WeakArray new: 3. Unwind fillWeakly: w. "
                  "^w livingCount", 1);
    check_integer("| w | w := WeakArray new: 3. Unwind fillWeakly: w. "
                  "Smalltalk garbageCollect. ^w livingCount", 0);
    /*  And a strong reference elsewhere keeps it.  */
    check_integer("| w a | w := WeakArray new: 3. a := Array new: 1. "
                  "Unwind fillWeakly: w. a at: 1 put: (w at: 1). "
                  "Smalltalk garbageCollect. ^w livingCount", 1);

    /*  The named fields of a weak class stay strong; only indexed ones go. */
    check_oop("(WeakArray new: 2) class == WeakArray", ST_TRUE, "true");

    test_dialect = ST_DIALECT_BLUE_BOOK;
}

/*
 *  A restarted frame counted its arguments twice.
 *
 *  MethodContext>>restart is what the Debugger's restart button does, and
 *  what restartWith: does after a method under debug is recompiled.  It set
 *  the stack pointer to "numArgs + numTemps", and numTemps already counts
 *  the arguments -- so a restarted frame came back believing it had two
 *  more values below its stack than it did, and then read whatever was in
 *  those slots.
 *
 *  The class contradicts itself about it, which is what made it findable:
 *  setSender:receiver:method:arguments:, twenty lines further down and the
 *  method that CREATES a context, says "stackp := method numTemps" -- and
 *  that one agrees with the interpreter, which sets a new frame's stack
 *  pointer to the header's temporary count and nothing else.
 */
static void
test_a_restarted_frame_counts_its_arguments_once(void)
{
    test_dialect = ST_DIALECT_CLOSURES;

    /*  on:do: takes two arguments and declares one temporary.  */
    check_integer("(BlockClosure compiledMethodAt: #on:do:) numArgs", 2);
    check_integer("(BlockClosure compiledMethodAt: #on:do:) numTemps", 3);
    /*  So the sum the old code used was two too many.  */
    check_integer("| m | m := BlockClosure compiledMethodAt: #on:do:. "
                  "^m numArgs + m numTemps", 5);

    /*
     *  A frame with two arguments and one temporary, restarted.  Its stack
     *  pointer is instance variable 3 -- sender, pc, stackp.
     */
    check_integer("| c | c := Unwind contextTakingTwoArgs: 1 and: 2. "
                  "c restart. ^c instVarAt: 3", 3);
    /*  And its program counter goes back to the first bytecode.  */
    check_oop("| c | c := Unwind contextTakingTwoArgs: 1 and: 2. c restart. "
              "^c pc = c method initialPC", ST_TRUE, "true");

    /*  numStack had the same double count: 12 slots less 3, not less 5.  */
    check_integer("(BlockClosure compiledMethodAt: #on:do:) frameSize", 12);
    check_integer("(BlockClosure compiledMethodAt: #on:do:) numStack", 9);

    test_dialect = ST_DIALECT_BLUE_BOOK;
}

/*
 *  Every method can find its own source.
 *
 *  Chapter 27 keeps a method's source location in its last three bytes, and
 *  CompiledMethod>>getSource reads position ZERO as "there is no source" --
 *  so whatever was written at offset 0 of the sources file was invisible.
 *  Exactly one method was: the first one compiled, which for this manifest
 *  is ArrayedCollection class>>new, and it had been sourceless since the
 *  bootstrap was written.  Nothing announces that; the Browser just shows
 *  an empty pane.
 *
 *  A filler byte at the front of each file fixes it, and the assertions
 *  below are spread across the load order because the failure was
 *  positional -- checking any method but the first would have passed all
 *  along.
 */
static void
test_every_method_can_find_its_source(void)
{
    /*  The first method compiled, which is the one that used to be lost.  */
    check_oop("(ArrayedCollection class compiledMethodAt: #new) getSource "
              "isNil", ST_FALSE, "false");
    check_integer("(ArrayedCollection class compiledMethodAt: #new) getSource "
                  "size", 64);
    check_integer("(ArrayedCollection class compiledMethodAt: #new) fileIndex",
                  1);

    /*  The middle and the end of the load order.  */
    check_oop("(Collection compiledMethodAt: #add:) getSource isNil",
              ST_FALSE, "false");
    check_oop("(SystemDictionary compiledMethodAt: #install) getSource isNil",
              ST_FALSE, "false");

    /*
     *  Two entries: 1 is .sources and 2 is .changes, which is the 1983
     *  convention.  A build needing more spills into 3 and 4 -- there is
     *  room for four in the two bits above the position -- and this library
     *  is nowhere near the limit.
     */
    check_integer("SourceFiles size", 2);
}

/*
 *  Collections, which is where the library really starts.  Every one of
 *  these runs hundreds of bytecodes through Xerox's own code.
 */
static void
test_collections(void)
{
    check_integer("#(1 2 3) size", 3);
    check_integer("'hello' size", 5);
    check_integer("(OrderedCollection new add: 1; add: 2; yourself) size", 2);
    check_integer("(Array with: 1 with: 2 with: 3) size", 3);
    check_integer("((1 to: 5) collect: [:i | i * i]) last", 25);
    check_integer("((1 to: 10) select: [:i | i even]) size", 5);
    check_integer("((1 to: 10) detect: [:i | i > 7]) ", 8);
    check_integer("(Set new add: 3; add: 3; yourself) size", 1);
    check_integer("('hello' indexOf: $l)", 3);
    check_integer("(#(3 1 2) asSortedCollection) first", 1);

    check_class("Dictionary new", "Dictionary");
    /*  printString on the structured collections, which needs Stream,
     *  Symbol and Character all working together.  */
    check_integer("(Set new add: 3; yourself) printString size", 9);
    check_integer("Object new printString size", 9);
    check_integer("(Dictionary new at: 1 put: 2; yourself) printString size",
                  18);
    check_class("(WriteStream on: String new)", "WriteStream");
    check_class("3/4", "Fraction");
}

/*
 *  printString is the deepest thing here: it runs Stream, WriteStream,
 *  String, Character and Symbol together, and it needed the class variables
 *  that BOOT_run_initializers now sets, plus primitives 63 and 64.
 */
static void
test_printing(void)
{
    check_integer("42 printString size", 2);
    check_integer("$A printString size", 2);
    check_integer("'hello' printString size", 7);       /*  with the quotes */
    check_integer("#(1 2 3) printString size", 8);      /*  "(1 2 3 )"      */
    check_integer("3 printString first asciiValue", 51);/*  $3              */
}

/*
 *  Symbols, which are what made this hard: interning consults a table that
 *  Symbol class>>initialize builds by interning, so a new image cannot
 *  bootstrap it from inside.  BOOT_run_initializers seeds it from C and then
 *  places every entry with the image's OWN intern:, so the hash is the
 *  library's and identity survives.
 */
static void
test_symbols(void)
{
    check_integer("#foo size", 3);
    check_integer("'hello' asSymbol size", 5);
    /*
     *  A Symbol equals a String spelling the same characters, and hashes
     *  the same as one.  1983 answers identity for both -- Symbol>>= is
     *  `^self == anObject' and Symbol>>hash is primitive 75, half the
     *  object pointer -- and lib/ overrides both, together.
     *
     *  Together is the point, and is what these four lines are here to
     *  hold.  Equality without the hash gives a collection an object it
     *  cannot find: a Set containing #foo answered false to
     *  (includes: 'foo') while #foo = 'foo' answered true, which is the
     *  collection working correctly on a contract its elements broke.
     */
    check_oop("#foo = 'foo'", ST_TRUE, "true");
    check_oop("'foo' = #foo", ST_TRUE, "true");
    check_oop("#foo hash = 'foo' hash", ST_TRUE, "true");
    check_oop("((Set new add: #foo; yourself) includes: 'foo')",
              ST_TRUE, "true");
    /*
     *  The same pairing one level up.  lib/ gives Set value equality, so
     *  Set must hash by its elements too -- defining = and leaving the
     *  inherited identity hash does not leave things as they were, it
     *  breaks them one indirection further away, where a Set of Sets is
     *  the thing that stops working.
     */
    check_oop("| a b | a := Set new. a add: 1; add: 7. "
              "b := Set new. b add: 7; add: 1. ^a = b", ST_TRUE, "true");
    check_oop("| a b | a := Set new. a add: 1; add: 7. "
              "b := Set new. b add: 7; add: 1. ^a hash = b hash",
              ST_TRUE, "true");
    check_oop("'foo' = #foo", ST_TRUE,  "true");    /*  String>>= is by value */

    /*
     *  Identity is the whole point of a Symbol, and it holds three ways: two
     *  lookups of the same text, a lookup against a symbol the COMPILER made
     *  while building the library, and a lookup against one made after the
     *  table existed.  The third is why BOOT_intern_symbol places into the
     *  table rather than only seeding it once.
     */
    check_oop("'hello' asSymbol == 'hello' asSymbol", ST_TRUE, "true");
    check_oop("#printString == 'printString' asSymbol", ST_TRUE, "true");
    check_oop("#at:put: == 'at:put:' asSymbol", ST_TRUE, "true");
    check_oop("#zzz == 'zzz' asSymbol", ST_TRUE, "true");

    /*
     *  A long selector is still one Symbol.  The intern table compared
     *  against a 64-byte C copy of the text, so anything past 63 characters
     *  never matched itself and every mention made a new Symbol -- the
     *  method installed under one and sent with another, and a dictionary
     *  keyed by identity could not find it.  Fourteen selectors in the
     *  library are that long.
     */
    check_integer("'subclass:instanceVariableNames:classVariableNames:"
                  "poolDictionaries:category:' asSymbol size", 76);
    check_oop("'setDestForm:sourceForm:halftoneForm:combinationRule:"
              "destOrigin:sourceOrigin:extent:clipRect:' asSymbol"
              " == #setDestForm:sourceForm:halftoneForm:combinationRule:"
              "destOrigin:sourceOrigin:extent:clipRect:", ST_TRUE, "true");
}

/*
 *  Floats are IEEE single precision in two words, most significant first --
 *  Chapter 30, and what the interpreter emits for every computed result.
 *  The bootstrap used to store the host's double in native word order, so a
 *  literal and a computed value of the same number were neither the same
 *  shape nor the same bits: 3.5 exponent answered -1060.
 */
static void
test_floats(void)
{
    check_integer("3.5 truncated", 3);
    check_integer("3.5 rounded", 4);
    check_integer("3.5 exponent", 1);
    check_integer("(3.5 + 1.5) truncated", 5);
    check_integer("(3.5 * 2) truncated", 7);
    check_integer("7 asFloat truncated", 7);
    check_oop("3.5 < 4.0", ST_TRUE, "true");
}

/*
 *  BOOT_string_hash duplicates String>>hash in C, to place symbols in the
 *  library's table without interpreting a send per symbol.  A duplicate that
 *  is merely believed is a bug with a long fuse, so it is checked here
 *  against the image's own answer.
 *
 *  There are two String>>hash, and the bootstrap has to follow whichever
 *  the image it built has: 1983's, three bytes of the string, which the
 *  bluebook profile keeps; and lib/Collections-Protocol's, primitive 223
 *  over every byte, which this profile loads.  The samples reach every
 *  branch of the 1983 formula -- empty, one character, two, and longer --
 *  and then strings the 1983 formula could not tell apart: same first
 *  character, same second-to-last, same length.  If the C ever followed
 *  the wrong method those would still agree on the first ten and disagree
 *  on the rest, which is the failure this is shaped to show.
 */
static void
test_string_hash_agrees(void)
{
    static const char *const samples[] = {
        "", "a", "z", "ab", "foo", "hello", "printString",
        "at:put:", "instanceVariableNames:", "x",
        "key1", "key2", "key9", "keyA", "keya", "kez1",
        "subclass:instanceVariableNames:classVariableNames:"
        "poolDictionaries:category:"
    };
    unsigned    i;

    for (i = 0; i < sizeof samples / sizeof samples[0]; ++i) {
        char    expression[160];
        st_oop  from_image;
        st_oop  interned = BOOT_intern_symbol(samples[i], NULL);
        uint32_t from_c  = BOOT_string_hash(interned);

        snprintf(expression, sizeof expression, "'%s' hash", samples[i]);
        from_image = evaluate(expression);
        /*  Recomputed after evaluate, which collects.  */
        from_c = BOOT_string_hash(interned);

        ++st_test_checks;
        if (!OM_is_int(from_image)
         || (uint32_t) OM_int_value(from_image) != from_c) {
            char    text[128];

            ++st_test_failures;
            ST_print_object(from_image, text, sizeof text);
            printf("  FAIL hash of \"%s\": C says %u, the image says %s\n",
                   samples[i], from_c, text);
        }
    }
}

/*
 *  The primitive, its Smalltalk fallback and the C are one function, and
 *  the function is a hash.
 *
 *  The first check runs the fallback by hand -- the loop from
 *  lib/Collections-Protocol/String.extension.st, spelled out here so that
 *  the literals are parsed a second time -- against what the primitive
 *  answered.  The second is why the method exists: two hundred names that
 *  1983 hashed to eleven values must hash to two hundred.  The third is
 *  the fold at the end of the function.  A 32-slot Set chooses its bucket
 *  by the low five bits of the hash, 'a' and 'A' differ in bit 5 alone,
 *  and a multiply alone carries a difference upward and never down, so
 *  without the fold the two would share a bucket in every small Set.
 */
static void
test_string_hash_spreads(void)
{
    check_oop("| h s | s _ 'instanceVariableNames:'. h _ 2166136261."
              " 1 to: s size do: [:i |"
              "  h _ ((h bitXor: (s at: i) asciiValue) * 16777619)"
              "       bitAnd: 16rFFFFFFFF]."
              " ^((h bitXor: (h bitShift: -16)) bitAnd: 16rFFFFFFF) = s hash",
              ST_TRUE, "true");
    check_integer("((1 to: 200) collect: [:i | ('key', i printString) hash])"
                  " asSet size", 200);
    check_oop("('a' hash \\\\ 32) = ('A' hash \\\\ 32)", ST_FALSE, "false");
}

static void
test_strings(void)
{
    /*  Unary binds tighter than binary, so the parentheses are required.  */
    check_integer("('ab' , 'cd') size", 4);
    check_integer("('hello' copyFrom: 2 to: 4) size", 3);
    check_integer("('hello' occurrencesOf: $l)", 2);
    check_integer("('hello' reverse) first asciiValue", 111);   /*  $o  */
    check_integer("((String new: 2) at: 1 put: $z) asInteger", 122);
}

static void
test_graphics_objects(void)
{
    /*  The MVC substrate: points and rectangles doing real geometry.  */
    check_class("3 @ 4", "Point");
    check_integer("(3 @ 4) x", 3);
    check_integer("((3 @ 4) + (1 @ 1)) x", 4);
    check_class("0 @ 0 corner: 10 @ 10", "Rectangle");
    check_integer("(0 @ 0 corner: 10 @ 20) width", 10);
    check_integer("(0 @ 0 corner: 10 @ 20) height", 20);
    check_integer("(0 @ 0 corner: 10 @ 20) area", 200);
    check_integer("((0 @ 0 corner: 10 @ 10) intersect: (5 @ 5 corner: 20 @ 20))"
                  " area", 25);
}

/*
 *  BitBlt, through the library's own Form and BitBlt classes.
 *
 *  Filling a form is the case that is exactly as wide as its raster, and
 *  Chapter 18's word count is off by one there unless the test is "<=".  With
 *  "<" a 16-pixel blit at x = 0 claims two words of a one-word row: it wrote
 *  a whole extra word per row, masked to all ones, and read past the end of
 *  the bitmap.  It survived the Xerox image because a form there is nearly
 *  always wider than the blit.
 */
static void
test_bitblt(void)
{
    /*  A fresh form is white.  */
    check_integer("(Form extent: 16 @ 16) bits inject: 0 into: [:a :b | a + b]",
                  0);

    /*  Filled black, every one of the sixteen words is all ones.  */
    check_integer("| f | f := Form extent: 16 @ 16."
                  " f fill: f boundingBox rule: 3 mask: Form black."
                  " ^f bits first", 65535);
    check_integer("| f | f := Form extent: 16 @ 16."
                  " f fill: f boundingBox rule: 3 mask: Form black."
                  " ^f bits inject: 0 into: [:a :b | a + b]", 16 * 65535);

    /*  Two words wide, eight rows: the same total by a different shape.  */
    check_integer("| f | f := Form extent: 32 @ 8."
                  " f fill: f boundingBox rule: 3 mask: Form black."
                  " ^f bits inject: 0 into: [:a :b | a + b]", 16 * 65535);

    /*  And the mask the library hands out really is black.  */
    check_integer("Form black bits inject: 0 into: [:a :b | a + b]",
                  16 * 65535);
}

/*  How many pixels of the Display are set.  */
static long
display_ink(void)
{
    gfx_form    form;
    int         x;
    int         y;
    long        ink = 0;

    if (!GFX_form_from_oop(GFX_display_form(), &form))
        return -1;
    for (y = 0; y < form.height; ++y) {
        for (x = 0; x < form.width; ++x) {
            uint16_t    word = form.bits[(size_t) y * form.raster + (x >> 4)];

            ink += (word >> (15 - (x & 15))) & 1;
        }
    }
    return ink;
}

static void
check_ink(const char *expression, long want)
{
    long    got;

    evaluate(expression);
    got = display_ink();
    ++st_test_checks;
    if (got != want) {
        ++st_test_failures;
        printf("  FAIL %s: %ld pixels of ink, want %ld\n", expression, got,
               want);
    }
}

/*
 *  The image drawing on a screen of its own.
 *
 *  A 1983 image inherits its Display from the image it was built from; this
 *  one is given a first by BOOT_install_display, and then draws on it with
 *  Xerox's own Form and BitBlt code through primitive 96.  The counts are
 *  exact because the areas are: a fill covers precisely the rectangle asked
 *  for, and gray covers half of it.
 */
static void
test_display(void)
{
    check_integer("Display width", 640);
    check_integer("Display height", 480);
    check_integer("Display bits size", 40 * 480);

    /*  Nothing drawn yet.  */
    CHECK_EQ_INT(display_ink(), 0);

    /*  160 x 80 = 12800.  */
    check_ink("Display fill: (40@40 corner: 200@120) rule: 3 mask: Form black."
              " ^Display width", 12800);

    /*  A white rectangle knocked out of it: 120 x 40 = 4800 fewer.  */
    check_ink("Display fill: (60@60 corner: 180@100) rule: 3 mask: Form white."
              " ^Display width", 12800 - 4800);

    /*  Gray is a halftone, so it covers half of its 160 x 80.  */
    check_ink("Display fill: (240@40 corner: 400@120) rule: 3 mask: Form gray."
              " ^Display width", 12800 - 4800 + 6400);

    /*  And back to white.  */
    check_ink("Display white. ^Display width", 0);
}

/*
 *  Text, which needed a font: the 1983 sources are code and carry none, so
 *  the image is given one written for this project.  Rendering a line runs
 *  the strike -- one Form holding every glyph, with an xTable saying where
 *  each begins -- through BitBlt, once per character, at whatever x the
 *  previous character left off.  Most of those are not word aligned, which
 *  is the case that needs each bitmap access bounds checked rather than the
 *  whole blit refused: an eight-pixel span starting four bits before a word
 *  boundary needs two destination words and so reads two source words from a
 *  form only one word wide.
 */
static void
test_text(void)
{
    check_integer("DefaultFont ascent", 18);
    check_integer("DefaultFont glyphs width", 1136);
    check_integer("DefaultFont descent", 8);
    check_integer("DefaultFont glyphs height", 26);
    /*
     *  And they DIFFER, which is the whole of the change: the face is
     *  proportional, so the image reads a real advance per character out of
     *  the xTable rather than the same number for every one.
     */
    check_integer("DefaultFont widthOf: $A", 12);
    check_integer("DefaultFont widthOf: $i", 4);
    check_integer("(DefaultFont characterForm: $A) width", 12);

    /*
     *  The glyph really is the letter A.  The rows are no longer worth
     *  writing out one by one -- they are a rasterisation, not a drawing --
     *  so this is their sum, and tools/make_font.py is where it comes from.
     */
    check_integer("(DefaultFont characterForm: $A) bits"
                  " inject: 0 into: [:a :b | a + b]", 158544);

    /*  An 8x8 black square lands whole wherever it is put, aligned or not. */
    check_ink("| f | Display white. f := Form extent: 8@8."
              " f fill: f boundingBox rule: 3 mask: Form black."
              " f displayOn: Display at: 20@10"
              " clippingBox: Display boundingBox rule: 3 mask: Form black."
              " ^1", 64);
    check_ink("| f | Display white. f := Form extent: 8@8."
              " f fill: f boundingBox rule: 3 mask: Form black."
              " f displayOn: Display at: 29@10"
              " clippingBox: Display boundingBox rule: 3 mask: Form black."
              " ^1", 64);

    /*  And a line of text draws every character of it.  */
    check_ink("Display white."
              " DefaultFont characters: (1 to: 12) in: 'Smalltalk-80'"
              " displayAt: 20@20 clippedBy: Display boundingBox rule: 3"
              " mask: Form black. ^1", 460);
    check_ink("Display white. ^1", 0);
}

/*
 *  Composing text: Paragraph, the CompositionScanner and the font together.
 *
 *  This is what a view displays, so it is the last thing under MVC rather
 *  than part of it.  The width of a line is the sum of the widths the font
 *  reports, which for this face is eight a character -- so the numbers here
 *  are the string lengths, and they are exact.
 */
static void
test_paragraph(void)
{
    check_integer("(Paragraph withText: 'hello' asText) numberOfLines", 1);
    check_integer("(Paragraph withText: 'hello' asText) width", 40);
    check_integer("('hello world' asParagraph) width", 93);
    /*  Empty text composes to no lines at all, not to one empty one.  */
    check_integer("('' asParagraph) numberOfLines", 0);

    /*  And it draws: 'Hi' is two glyphs of known ink.  */
    check_ink("Display white. 'Hi' asParagraph displayOn: Display at: 20@20."
              /*
               *  H at 13 wide and i at 4, inked and summed from the
               *  strike -- 46 and 24.  This number disagreeing with that
               *  sum is what found the default style clipping the face:
               *  it was 63 here, and the seven missing were the dot off
               *  the i and a row off the H.
               */
              " ^1", 70);
}

/*
 *  A view on the screen.
 *
 *  StandardSystemView is the window everything in MVC lives in: a labelled
 *  tab above a body, drawn by the library's own View code through the
 *  transformation, the border and the text it just learned to compose.  This
 *  is Phase 8's substrate -- what a Browser or an Inspector puts itself in.
 */
static void
test_view(void)
{
    check_integer("| v | v := StandardSystemView new. v label: 'Hello'."
                  " ^v label size", 5);
    check_integer("| v | v := StandardSystemView new."
                  " v window: (60@40 corner: 360@200). ^v window width", 300);

    /*
     *  Displayed, it covers its window: a gray body and a labelled tab.  The
     *  count is stable because the dither is, and it is far more than the
     *  border alone -- a view that failed to fill would be obvious here.
     */
    check_ink("| v | Display white. v := StandardSystemView new."
              " v label: 'Hello World'. v window: (60@40 corner: 360@200)."
              " v display. ^1", 12506);
}

/*
 *  The window scheduler.
 *
 *  ScheduledControllers is a ControlManager, made when an image is built and
 *  carried by every snapshot after; Sensor likewise.  Neither exists in an
 *  image built from sources, and without them a great deal of the interface
 *  asks nil for the active controller or the cursor and stops there.
 *
 *  Object class>>initialize cannot be run to get DependentsFields, because it
 *  asks the user to confirm resetting every dependency in the system and
 *  there is nobody to ask; its two halves are called directly instead.
 *  Without it addDependent: sends at:ifAbsent: to nil and no view can
 *  register interest in a model.
 */
static void
test_scheduler(void)
{
    ++st_test_checks;
    if (!OM_is_present(BOOT_global("Sensor"))) {
        ++st_test_failures;
        printf("  FAIL Sensor was not installed\n");
    }
    ++st_test_checks;
    if (!OM_is_present(BOOT_global("ScheduledControllers"))) {
        ++st_test_failures;
        printf("  FAIL ScheduledControllers was not installed\n");
    }

    /*  It starts holding the screen controller and nothing else.  */
    check_integer("ScheduledControllers scheduledControllers size", 1);

    /*
     *  Two windows scheduled and the screen restored: the manager redraws
     *  its gray background and both views over it.  Far more ink than the
     *  views alone, because the background is most of the screen.
     */
    check_ink("| a b | Display white."
              " a := StandardSystemView new. a label: 'Transcript'."
              " a window: (30@30 corner: 300@180)."
              " b := StandardSystemView new. b label: 'Workspace'."
              " b window: (200@120 corner: 560@340)."
              " ScheduledControllers schedulePassive: a controller."
              " ScheduledControllers schedulePassive: b controller."
              " ScheduledControllers restore. ^1", 124606);
    check_integer("ScheduledControllers scheduledControllers size", 3);
}

/*
 *  The scheduler, and the process a saved image resumes into.
 *
 *  ProcessorScheduler class>>new refuses on purpose -- "the integrity of the
 *  system depends on a unique scheduler" -- because in 1983 the one scheduler
 *  was made when the image was built and carried by every snapshot after.
 *  An image built from sources has to be given one, along with a process to
 *  wake up in, or there is nothing for -run to resume.
 */
static void
test_process_scheduler(void)
{
    CHECK(BOOT_install_scheduler("^Display width"));

    ++st_test_checks;
    if (!OM_is_present(BOOT_global("Processor"))) {
        ++st_test_failures;
        printf("  FAIL Processor was not installed\n");
    }
    check_oop("Processor activeProcess isNil", ST_FALSE, "false");
    check_integer("Processor activePriority", 4);

    /*  Eight priorities, each with a list of its own.  */
    check_integer("(Processor instVarAt: 1) size", 8);

    /*
     *  And the VM has been handed the semaphore to signal when input
     *  arrives.  InputSensor class>>install is what installs it, by way of
     *  primitive 93, and it can only run once there is a Processor with an
     *  active process to take a priority from -- the process it forks asks
     *  for Processor activePriority.  Run any earlier and the method stops
     *  before its last line with no complaint anyone would notice, and the
     *  image is left with no way to be told about a key or a mouse button:
     *  the events queue up and the semaphore they signal is nobody\'s.
     *
     *  So this is asserted here, immediately after the scheduler is built,
     *  because that ordering is the whole of what makes it work.
     */
    ++st_test_checks;
    if (!OM_is_present(SCHED_input_semaphore())) {
        ++st_test_failures;
        printf("  FAIL no input semaphore was installed\n");
    }

    /*
     *  The image is built with its system processes already running.
     *
     *  Two of the class initializers fork one.  Delay class>>initialize
     *  forks the timing process at Processor timingPriority, which is 8, and
     *  InputSensor class>>install forks the process that drains the event
     *  queue at lowIOPriority, which is 6.  Both sit on their run queues
     *  waiting on a semaphore, which is where a 1983 image keeps them.
     *
     *  Neither used to be there.  Both initializers ask Processor for a
     *  priority, and the scheduler was built after the initializers ran, so
     *  each stopped at that line -- Delay leaving an image in which every
     *  wait would have been forever.  Asserting the queues is the only way
     *  to see it, because a process that was never forked is not missing
     *  from anywhere you would think to look.
     *
     *  Asserted BEFORE the yields below, and that ordering is the point: a
     *  yield runs the highest-priority process that is ready, so the first
     *  two yields take these two off their queues and leave them waiting on
     *  their semaphores instead.  Both states are correct; only the first
     *  says anything about whether the processes were forked at all.
     */
    check_oop("^((Processor instVarAt: 1) at: 8) isEmpty", ST_FALSE, "false");

    check_oop("^((Processor instVarAt: 1) at: 6) isEmpty", ST_FALSE, "false");

    /*
     *  And that the timing process, once running, actually does its job.
     *
     *  Everything above says a Delay's machinery was BUILT.  None of it says
     *  a delay ever returns, and for the whole life of this system none did:
     *  primitive 100 was missing, then it compared against a different clock
     *  than the image had used, then the scheduler read the woken process's
     *  nomination as an empty run queue and called the image deadlocked.
     *  Four faults in the one path, and every test in the tree passed
     *  throughout, because no test ever waited.
     *
     *  After both queue assertions above, and that is not arbitrary: this
     *  check WAITS, and a wait runs whatever is ready -- which takes those
     *  two system processes off the queues those checks are about.  Putting
     *  it earlier failed the priority-6 assertion, exactly as the note above
     *  says it would.
     *
     *  Both halves are asserted, and the second is the one that matters:
     *  a delay that returns immediately looks exactly like a delay that
     *  works.  That is not hypothetical -- it is how the clock mismatch hid,
     *  and it passed a hand-written probe of mine before the elapsed time
     *  was checked.
     */
    check_oop("| t | t := Time millisecondClockValue. "
              "(Delay forMilliseconds: 60) wait. "
              "^(Time millisecondClockValue - t) >= 50", ST_TRUE, "true");

    /*
     *  Yielding, which is the smallest thing that needs two processes.
     *
     *  ProcessorScheduler>>yield forks a process to signal a semaphore and
     *  waits on it, so it exercises the whole handoff: the forked process is
     *  queued, control transfers to it, it signals, the waiting process is
     *  taken off the semaphore and resumed, and control comes back.  Each of
     *  those steps moves a process from a place that refers to it to one
     *  that does not yet, and every one of them was, at some point, the
     *  moment the process was reclaimed and the system reported that every
     *  process was blocked.
     */
    check_oop("Processor yield. ^true", ST_TRUE, "true");
    check_integer("Processor yield. ^3 + 4", 7);
}

/*
 *  SystemOrganization: the map from class categories to classes.
 *
 *  The Browser opens on it -- BrowserView openOn: SystemOrganization -- so
 *  without it there is nothing to browse.  Every class definition names its
 *  category, so the information has been going past all along; it is
 *  collected and handed to the library's own organizer.
 */
static void
test_system_organization(void)
{
    check_oop("SystemOrganization isNil", ST_FALSE, "false");
    /*  One per source directory.  */
    check_integer("SystemOrganization categories size",
                  BLUEBOOK_CATEGORIES + LIB_CATEGORIES);
}

/*
 *  Processes, and a controller that wants control.
 *
 *  These are what the interaction loop is made of.  A forked process really
 *  runs -- the array it writes proves the scheduler switched to it and back
 *  -- and a controller under the cursor says so, which is what
 *  searchForActiveController waits for.
 *
 *  What cannot be asserted here is the loop itself, and deliberately: when a
 *  controller does want control, searchForActiveController gives it and the
 *  controller runs ITS loop, which does not return.  That is correct MVC and
 *  it is also the reason a test cannot wait for it.
 */
static void
test_processes(void)
{
    check_oop("Semaphore new isNil", ST_FALSE, "false");
    check_oop("([1] newProcess) isNil", ST_FALSE, "false");
    check_integer("| p | p := [1] newProcess. p priority: 6. ^p priority", 6);
    check_integer("Processor lowIOPriority", 6);

    /*
     *  Forking and yielding is NOT asserted here, though it works: this
     *  harness stands a context up and interprets it directly rather than
     *  going through the scheduler, so a yield switches away from a process
     *  the harness still believes it is running.  What that would test is the
     *  harness.  The behaviour is shown instead by -eval, where the same
     *  expression answers 99, and by the booted image, whose input process is
     *  forked exactly this way.
     */

    /*  The Sensor answers, so the controller layer has something to ask.  */
    check_class("Sensor cursorPoint", "Point");
    check_oop("Sensor anyButtonPressed", ST_FALSE, "false");

    /*
     *  And what the Sensor makes of a shifted key.
     *
     *  The window sends the code a key carries unshifted and the image's
     *  keyboard map applies the shift, so the map has to be the map of the
     *  keyboard in front of the user.  1983's is the Alto's, and on three
     *  keys they disagree: shift-minus answered code 21, an Alto code this
     *  face has no glyph for; shift-6 answered ~, because the Alto put the
     *  tilde over the 6; and shift-backquote answered 255, unassigned,
     *  because no Alto key carried a backquote unshifted.  Reported as
     *  "~ doesn't show at all, ^ displays a ~" -- 255 is past the end of
     *  the face, so the scanner draws the zero-width `character not in
     *  font' and the key looks dead.
     *
     *  Read straight out of the map, at `256 * meta + code + 1', because
     *  that is the whole of what mapKeyboardEvent: does with it, and the
     *  instance variable is the only place the answer lives -- InputSensor
     *  has no accessor for it.
     */
    check_string("String with: ((Sensor instVarAt: 1) at: 256 + 45 + 1)"
                 " with: ((Sensor instVarAt: 1) at: 256 + 54 + 1)"
                 " with: ((Sensor instVarAt: 1) at: 256 + 96 + 1)", "_^~");

    /*  Unshifted, those same three keys are themselves, as they were.  */
    check_string("String with: ((Sensor instVarAt: 1) at: 45 + 1)"
                 " with: ((Sensor instVarAt: 1) at: 54 + 1)"
                 " with: ((Sensor instVarAt: 1) at: 96 + 1)", "-6`");

    /*  And a controller under the cursor wants control.  */
    check_oop("| v | v := StandardSystemView new."
              " v window: (0@0 corner: 640@480)."
              " ^v controller isControlWanted", ST_TRUE, "true");
    check_oop("| v | v := StandardSystemView new."
              " v window: (500@400 corner: 600@450)."
              " ^v controller isControlWanted", ST_FALSE, "false");
}

/*
 *  A System Browser.
 *
 *  Built the way BrowserView class>>openOn: builds one, but with the window
 *  set rather than swept out: "open" calls "view resize", which in 1983 asks
 *  the user to drag a rectangle, and there is nobody here to drag one.
 *  Everything else is the library's -- the Browser model on
 *  SystemOrganization, the five list views, the code view -- and what it
 *  draws is the categories this image was built from.
 */
static void
test_browser(void)
{
    check_ink("| b top | Display white."
              " b := Browser new on: SystemOrganization."
              " top := BrowserView model: b label: 'System Browser'"
              " minimumSize: 400@250."
              " top addCategoryView: (0@0 extent: 0.25@0.35) on: b"
              " readOnly: false."
              " top addClassView: (0.25@0 extent: 0.25@0.3) on: b"
              " readOnly: false."
              " top addMetaView: (0.25@0.3 extent: 0.25@0.05) on: b"
              " readOnly: false."
              " top addProtocolView: (0.5@0 extent: 0.25@0.35) on: b"
              " readOnly: false."
              " top addSelectorView: (0.75@0 extent: 0.25@0.35) on: b"
              " readOnly: false."
              " top addTextView: (0@0.35 extent: 1.0@0.65) on: b"
              " initialSelection: nil."
              " top window: (20@20 corner: 620@460). top display. ^1",
              /*
               *  It used to be 56618, and that number was the bug.
               *
               *  Displaying the text view reached CharacterBlock class>>
               *  stringIndex:character:boundingRectangle:, which sends a
               *  method the 1983 sources never define -- searching all of
               *  sources/ for BoundingRectangle: finds the send and nothing
               *  else.  The display died there, part drawn, and the count
               *  recorded whatever had been painted before it stopped.
               *
               *  kernel/Bootstrap.st supplies the method, so the browser now
               *  draws to completion: a title tab, five list panes with the
               *  category list filled, and an empty text pane because no
               *  method is selected yet.  Less ink, and all of it wanted.
               *
               *  11296 since lib/Clipboard: the category pane shows the
               *  first however-many categories in order, and `Clipboard'
               *  and `Clipboard-Tests' now stand ahead of the Collections
               *  ones, pushing two long names out of the pane and drawing
               *  two short ones in their place.  This number moves whenever
               *  a category is added near the front of the alphabet, and
               *  that is what it is for.
               */
              11296);
}

/*
 *  Browsing: category, class, protocol, selector, source.
 *
 *  Every pane of the Browser answers, and the last one answers real source
 *  text.  Smalltalk-80 does not keep source in the image -- a CompiledMethod
 *  carries a position into a sources file and the Browser reads the chunk
 *  there -- so the bootstrap writes every method's source into one String
 *  and hands it over as SourceFiles.  Nothing says that stream has to be a
 *  file: RemoteString asks it only to position: and nextChunk.
 */
/*
 *  The protocol forty years added, which 1983 does not have.
 *
 *  Every one of these is a send the 1983 image answers with a
 *  doesNotUnderstand:, and every one of them appears in ordinary modern
 *  Smalltalk.  They are the difference between "this system runs the Blue
 *  Book" and "you can write a program in this system".
 */
static void
test_modern_protocol(void)
{
    /*  nil and the ifNil: family.  Real sends here, not compiler magic.  */
    check_string("nil ifNil: ['was nil']", "was nil");
    check_integer("3 ifNil: [0] ifNotNil: [:x | x * 2]", 6);
    check_oop("nil ifNotNil: [:x | x]", ST_NIL, "nil");
    /*  cull: is what lets ifNotNil: take a block of either arity.  */
    check_integer("4 ifNotNil: [7]", 7);
    check_integer("4 ifNotNil: [:x | x]", 4);

    /*  displayString drops the syntax printString has to keep.  */
    check_string("'abc' displayString", "abc");
    check_string("'abc' printString", "'abc'");
    check_string("#foo displayString", "foo");
    check_string("42 displayString", "42");
    check_string("(1 -> 2) printString", "1->2");

    /*  Testing protocol: Object says no, the class in question says yes.  */
    check_boolean("3 isNumber", 1);
    check_boolean("'x' isString", 1);
    check_boolean("#x isSymbol", 1);
    check_boolean("$x isCharacter", 1);
    check_boolean("#(1) isArray", 1);
    check_boolean("3 isString", 0);
    check_boolean("Object isBehavior", 1);

    /*  assert: takes a boolean or a block, and signals a catchable Error.  */
    /*
     *  These run in the Blue Book dialect, like every expression here, so
     *  they are also the check that a 1983 block can catch: on:do: used to
     *  exist only on BlockClosure, which left the 4,500 methods of the
     *  1983 library able to signal an Error and unable to catch one.
     */
    check_string("[Object new assert: 1 = 2. 'no'] on: AssertionFailure"
                 " do: [:e | e messageText]", "assertion failed");
    check_string("[Object new assert: [1 = 2] description: 'nope'] on: Error"
                 " do: [:e | e messageText]", "nope");
    check_string("[Object new assert: 1 = 1. 'ran'] on: Error do: [:e | 'no']",
                 "ran");

    /*  Collections.  */
    check_string("#(3 1 2) sorted printString", "(1 2 3 )");
    /*
     *  A sequenceable collection sorts into its own species -- 'hello'
     *  sorted is 'ehllo', not five Characters in an Array.  Pharo's own
     *  doctest for the method is what said so; ours answered the Array
     *  everything else has to answer, and was wrong to.
     */
    check_string("'hello' sorted", "ehllo");
    check_string("'hello' sorted: [:a :b | a >= b]", "ollhe");
    /*
     *  The parallel primitives.  Single-threaded here, so the answers are
     *  the single-threaded ones -- worker zero of one -- and that is the
     *  point: they answer honestly rather than failing when there is no
     *  pool, so code written against them runs either way.
     */
    /*
     *  The two ready-list walks now live in the VM.  Nothing is waiting
     *  for the processor in a freshly built image, so these answer the
     *  empty answers -- and answering rather than failing is the point:
     *  ProcessorScheduler>>remove:ifAbsent: has to evaluate its block.
     */
    check_oop("Processor primFirstReadyProcessAt: 4", ST_NIL, "nil");
    check_boolean("Processor primRemoveReadyProcess: Processor activeProcess",
                  0);
    check_string("Processor remove: Processor activeProcess"
                 " ifAbsent: ['absent']", "absent");

    check_integer("Processor activeWorkerIndex", 0);
    check_integer("Processor workerCount", 1);
    check_string("Processor activeProcess class name", "Process");
    /*
     *  compareAndSwapSlot:from:to: answers whether the swap HAPPENED,
     *  rather than the old value -- that is what every caller tests, and
     *  it leaves no room to forget the comparison.
     */
    check_boolean("(Array with: 1 with: 2) compareAndSwapSlot: 1 from: 1 to: 9",
                  1);
    check_boolean("(Array with: 1 with: 2) compareAndSwapSlot: 1 from: 7 to: 9",
                  0);
    check_integer("| a | a := Array with: 1 with: 2."
                  " a compareAndSwapSlot: 2 from: 2 to: 42. ^a at: 2", 42);
    check_integer("| a | a := Array with: 1 with: 2."
                  " a compareAndSwapSlot: 2 from: 99 to: 42. ^a at: 2", 2);

    /*
     *  Primitives named by Pharo's Kernel, reachable because lib/ declares
     *  them.  ln and exp are the ones that matter: the 1983 Taylor series
     *  stops at MathApproximationEpsilon and was wrong in float32's last
     *  digit, which Pharo's own doctest for this expression caught.
     */
    check_boolean("(2 raisedTo: (1/12)) = 1.0594630943592953", 1);
    /*
     *  Sixteen digits, not six.  This memory's Float is a double, and
     *  lib/ prints the shortest decimal that reads back as the same one --
     *  0.693147 does not, so the old expectation was asserting that a
     *  Float could not survive its own printed form.  The Blue Book build
     *  still prints six, because it keeps single precision and never loads
     *  lib/.
     */
    check_string("2.0 ln printString", "0.6931471805599453");
    check_boolean("3 ~~ 4", 1);
    /*
     *  identityHash: the hash of the OBJECT, not of its value.  1983 has
     *  the primitive and never gave it this name, because where hash IS
     *  identity the distinction has nowhere to show.  Pharo's identity
     *  collections send it.
     */
    check_boolean("'ab' identityHash = 'ab' copy identityHash", 0);
    check_boolean("| s | s := 'ab'. ^s identityHash = s identityHash", 1);
    check_boolean("3 ~~ 3", 0);
    check_integer("1000 hashMultiply", 53912264);
    check_string("#(1 2) shallowCopy printString", "(1 2 )");
    check_boolean("#(1 2) shallowCopy == #(1 2)", 0);
    /*  A SmallInteger is its own copy; the primitive declines and says so. */
    check_integer("3 shallowCopy", 3);

    /*
     *  A minus written against a number inside #( ) is that number's sign.
     *  It is ambiguous in code -- "3-4" is a send -- and not ambiguous in
     *  a literal array, where there are no sends.  "#(1 5 10 -4)" was FIVE
     *  elements: 1, 5, 10, the symbol #-, and 4.  Nothing failed; the
     *  array was simply the wrong array, and its min answered 1.
     */
    check_integer("#(1 5 10 -4) size", 4);
    check_integer("#(1 5 10 -4) min", -4);
    check_string("#(-1 -2) printString", "(-1 -2 )");
    check_string("#(1.5 -2.5) printString", "(1.5 -2.5 )");
    /*  A minus with a space is still the symbol it looks like.  */
    check_integer("#(a - b) size", 3);
    check_boolean("(#(a - b) at: 2) == #-", 1);
    check_string("(#(3 1 2) sorted: [:a :b | a > b]) printString", "(3 2 1 )");
    check_string("((1 to: 3) flatCollect: [:i | Array with: i with: i])"
                 " printString", "(1 1 2 2 3 3 )");
    check_integer("#(1 2 3 4) count: [:e | e even]", 2);
    check_boolean("#(1 2 3) anySatisfy: [:e | e > 2]", 1);
    check_boolean("#(1 2 3) allSatisfy: [:e | e > 2]", 0);
    check_boolean("#(1 2 3) noneSatisfy: [:e | e > 5]", 1);
    check_string("#() ifEmpty: ['empty']", "empty");
    check_integer("#(9) ifNotEmpty: [:c | c first]", 9);
    check_integer("#(1 2 3) sum", 6);
    check_integer("#(4 9 2) max", 9);
    check_integer("#(4 9 2) min", 2);
    check_string("(#(1 2 3 2) copyWithout: 2) printString", "(1 3 )");
    check_string("#(1 2 3) reversed printString", "(3 2 1 )");
    check_string("#(1 2 3) allButFirst printString", "(2 3 )");
    check_string("(#(1 2 3) first: 2) printString", "(1 2 )");
    check_string("(#(1 2) with: #(10 20) collect: [:a :b | a + b]) printString",
                 "(11 22 )");
    check_string("(Array withAll: (1 to: 3)) printString", "(1 2 3 )");
    check_string("String streamContents: [:s | #(1 2 3)"
                 " do: [:e | s << e] separatedBy: [s << ', ']]", "1, 2, 3");
    /*  A Dictionary fills a missing key rather than answering nil twice.  */
    check_integer("| d | d := Dictionary new."
                  " ^(d at: #k ifAbsentPut: [1]) + (d at: #k ifAbsentPut: [99])",
                  2);

    /*  Streams: << is double dispatch, and writes the display form.  */
    check_string("String streamContents: [:s | s << 'n=' << 42 << ' ' << $x]",
                 "n=42 x");

    /*  Strings.  */
    check_string("', ' join: #('a' 'b' 'c')", "a, b, c");
    check_string("'  padded  ' trimBoth", "padded");
    /*  substrings: collapses runs; splitOn: keeps the empty field.  */
    check_string("('a,,b' substrings: ',') asArray printString", "('a' 'b' )");
    check_string("('a,,b' splitOn: $,) asArray printString", "('a' '' 'b' )");
    check_string("'{1} and {2}' format: #('this' 'that')", "this and that");
    check_boolean("'hello' beginsWith: 'he'", 1);
    check_boolean("'hello' endsWith: 'lo'", 1);
    check_boolean("'hello' includesSubstring: 'ell'", 1);
    check_boolean("'hello' beginsWith: 'xx'", 0);
    check_integer("'42' asInteger", 42);
    check_integer("'-7x' asInteger", -7);
    /*  nil, not 0: '0' and 'banana' are different answers.  */
    check_oop("'banana' asInteger", ST_NIL, "nil");

    /*
     *  fixTemps, which is why asSortedCollection: works at all under
     *  closures: SortedCollection>>sortBlock: sends it to the block, and a
     *  BlockClosure that could not answer it took every sort down with it.
     */
    check_string("((#(3 1 2) asSortedCollection: [:a :b | a > b]) asArray)"
                 " printString", "(3 2 1 )");

    /*
     *  Temporaries declared inside a block the compiler INLINES.  They are
     *  hoisted into the enclosing frame, and the thing to check is that
     *  the hoist stays invisible: the inner t must not be the outer t.
     */
    check_integer("| i | i := 0. [i < 3] whileTrue: [| t | t := i. i := t + 1]."
                  " ^i", 3);
    check_integer("| r | r := 0. true ifTrue: [| t | t := 10. r := r + t]."
                  " true ifTrue: [| t | t := 20. r := r + t]. ^r", 30);
    check_integer("^true ifTrue: [| t | t := 1."
                  " true ifTrue: [| t | t := 2]. t]", 1);
    /*  And a real closure may still capture and assign one.  */
    check_integer("| c | true ifTrue: [| t | t := 5."
                  " c := [t := t + 1. t]]. ^c value + c value", 13);
}

/*
 *  SUnit, running its own tests.
 *
 *  This is the phase's real deliverable: it turns "did the port work" from
 *  a judgement call into a number.  Everything above checks one expression
 *  at a time from C; from here on a ported package can bring its own suite
 *  and say so itself.
 *
 *  The suite is deliberately mixed.  SUnitTest is ordinary passing tests,
 *  and SUnitReportingTest runs tests that FAIL and BLOW UP on purpose and
 *  checks the result counted them in the right buckets -- because a runner
 *  that quietly reports every failure as a pass is worse than no runner,
 *  and nothing but a deliberate failure catches that.
 *
 *  The expressions run in the closure dialect, because the doIt has to
 *  build the blocks SUnit's assertions take.
 */
static void
test_sunit(void)
{
    int saved = test_dialect;

    test_dialect = ST_DIALECT_CLOSURES;

    /*  Every test of SUnit itself passes.  */
    check_string("| s | s := TestSuite new. s addTestCase: SUnitTest."
                 " s addTestCase: SUnitReportingTest. ^s run summary",
                 "12 run, 12 passed, 0 failed, 0 errors");
    check_boolean("| s | s := TestSuite new. s addTestCase: SUnitTest."
                  " s addTestCase: SUnitReportingTest. ^s run hasPassed", 1);

    /*
     *  The subclass graph, which the bootstrap never filled.  Behavior has
     *  four instance variables and only three of them were being written,
     *  so "Object subclasses" answered an empty Set for every class in the
     *  image -- and with it allSubclasses, withAllSubclasses, and anything
     *  that walks DOWN the hierarchy rather than up.  It answered EMPTY
     *  rather than failing, which is why nothing noticed: the same shape as
     *  the method dictionaries that were filled where the image does not
     *  look.  TestCase allSubclasses finding nothing, in an image with
     *  three TestCase subclasses in it, is what found it.
     */
    check_boolean("Object subclasses size > 50", 1);
    check_boolean("Collection subclasses includes: SequenceableCollection", 1);
    check_boolean("Object subclasses includes: Collection", 1);
    /*  The metaclass side has its own parallel chain and is wired too.  */
    check_boolean("Object class subclasses includes: Collection class", 1);
    check_string("(TestCase allSubclasses collect: [:c | c name])"
                 " asSortedCollection asArray printString",
                 "(AnthropicTest Base64Test ClassTestCase ClipboardTest CryptoTest DbQueryBuilderTest DbSchemaGraphTest "
                 "DbValueTest DelayTest HttpClientTest HttpRequestTest HttpResponseTest "
                 "HttpServerTest JSONArrayTest JSONObjectTest JSONParserTest "
                 "JSONWriterTest LLMConversationTest LLMTestCase MonitorTest OllamaTest OpenAITest OpenRouterTest "
                 "PasswordHashTest ProcessTest QdrantTest RestServerTest SocketStreamTest SocketTest "
                 "St80CollectionTest St80ExceptionTest St80FileTest St80NumberTest St80ReflectionTest "
                 "St80TextTest SUnitBrokenTest SUnitReportingTest SUnitTest "
                 "TonelReaderTest TonelSourceTest TonelWriterTest WebDemoTest )");
    /*
     *  allTests leaves out the fixture whose tests are meant to go wrong.
     *  A whole-image run that reported those would cry wolf every build.
     */
    /*
     *  61 -> 104 with lib/Database-Tests: the join graph, the SQL generator
     *  and the value conversions.  The database round trips are NOT among
     *  them and are not meant to be -- they need a driver this machine may
     *  not have, so they live in profiles/database-live.profile and are run
     *  deliberately.  See lib/Database-Live-Tests.
     */
    /*
     *  104 -> 203 with lib/Json-Tests, which is 99.  Something over a third
     *  of them are documents the parser must REFUSE, and those are the ones
     *  worth having: a parser that accepts too much passes every test of a
     *  valid document, so the leniency this one deliberately does not have
     *  can only be checked by writing down what must not be accepted.
     *
     *  205 with the two that walk a JSONObject and a JSONArray while
     *  changing them.  Those hold in place the one decision in lib/Json
     *  nothing else would notice being undone: no block of the caller's
     *  runs inside the lock, so `json do: [:each | json at: ...]' works
     *  rather than reporting a re-entered Mutex.
     *
     *  332 with lib/Crypto-Tests' twenty-nine: the published SHA-256, HMAC
     *  and PBKDF2 answers through primitive 209, Base64 both ways, and the
     *  hash Kiss's demo database stores, verified -- the proof that a users
     *  table Kiss made logs in here unchanged.
     *
     *  334 with the two for the startup hook: an Init class in the back
     *  end told init: and init2: in order, and one that raises leaving the
     *  server not listening.
     *
     *  343 with lib/Web-Demo-Tests' nine: the demo application without a
     *  database -- Kiss's front end served from the document root, the
     *  three addNumbers by the front end's three names, an upload, what
     *  the database screens say when there is none.
     *
     *  345 with the two for the Parser: a selector defined later in the
     *  file compiles, an undeclared variable is refused by name.
     *
     *  351 with the six for another origin: a preflight answered with no
     *  handler run, this host on another port echoed, another host given
     *  no permission and its preflight refused, no Origin served as
     *  before, an OPTIONS without one reaching the handler, and the host
     *  comparison on text.
     *
     *  370 with the nineteen for the HTTP client, Ollama and the demo's
     *  OllamaQuery: bodies by length, by chunks and by the close, a URL
     *  taken apart, a model server's three answers from a listener of the
     *  test's own, and Kiss's text substitutions; and one in SocketTest
     *  for a listener on 127.0.0.1 alone reached by the name localhost.
     *
     *  375 with lib/Clipboard-Tests' four: no window means nothing there,
     *  the line ends both ways, a wrong argument answering nil.
     *
     *  410 -> 420 with St80FileTest's ten: the tilde, the dot segments,
     *  where a designator is cut, and the pattern pane on the six things a
     *  person types into it -- the last of them a path that is not there.
     *
     *  420 -> 422 with two more: where a bare name typed into `new' is
     *  taken, and the places that have no directory to take it in.
     *
     *  422 -> 423 with the list pane keeping its selection when its rows are
     *  rebuilt, which is what leaves a new file ready to be typed into.
     *
     *  427 -> 504 with the Bugs3 fixes: MonitorTest, ProcessTest and three
     *  more in DelayTest for the process protocol; St80ExceptionTest and
     *  additions to St80CollectionTest, St80NumberTest and St80TextTest for
     *  the library; HttpRequestTest, HttpResponseTest, HttpServerTest,
     *  JSONParserTest, HttpClientTest and RestServerTest for what a hostile
     *  client sends; and TonelReaderTest, TonelWriterTest and
     *  TonelSourceTest for the files that come back as they went out.
     */
    check_integer("TestCase allTests tests size", 504);

    /*
     *  And the three buckets, from the outside as well as from within
     *  SUnitReportingTest: a pass, a failed assertion, and something
     *  nobody predicted, told apart.
     */
    check_string("SUnitBrokenTest suite run summary",
                 "3 run, 1 passed, 1 failed, 1 errors");

    /*
     *  ensure: runs when an EXCEPTION unwinds past it, not only when a
     *  block returns through it.  SUnit found this: tearDown did not run
     *  after a failed test, because Exception>>return: jumped to the
     *  handler's frame and threw away everything in between without
     *  looking at it.  A file left open and a lock left held, with nothing
     *  to say so.
     */
    check_integer("| f | f := 0."
                  " [[Error new signal: 'boom'] ensure: [f := 1]]"
                  " on: Error do: [:e | nil]. ^f", 1);
    /*  Nested unwinds run innermost first...  */
    check_string("| f | f := OrderedCollection new."
                 " [[[Error signal] ensure: [f add: #inner]]"
                 " ensure: [f add: #outer]] on: Error do: [:e | nil]."
                 " ^f asArray printString", "(inner outer )");
    /*  ...and exactly once, however many paths pass through the frame.  */
    check_integer("| f | f := 0. [[Error signal] ensure: [f := f + 1]]"
                  " on: Error do: [:e | nil]. ^f", 1);
    /*  ifCurtailed: fires on the way out and not on a normal return.  */
    check_integer("| f | f := 0. [[Error signal] ifCurtailed: [f := 1]]"
                  " on: Error do: [:e | nil]. ^f", 1);
    check_integer("| f | f := 0. [[7] ifCurtailed: [f := 1]] value. ^f", 0);
    /*  retry discards the frames in between too, so each go unwinds.  */
    check_string("| n f | n := 0. f := 0."
                 " [[n := n + 1. n < 3 ifTrue: [Error signal]. n]"
                 " ensure: [f := f + 1]] on: Error do: [:e | e retry]."
                 " ^(Array with: n with: f) printString", "(3 3 )");

    test_dialect = saved;
}

static void
test_browsing(void)
{
    check_integer("(Browser new on: SystemOrganization) categoryList size",
                  BLUEBOOK_CATEGORIES + LIB_CATEGORIES);

    /*  Kernel-Objects holds Boolean, False, Object, True, UndefinedObject.  */
    check_integer("| b | b := Browser new on: SystemOrganization."
                  " b category: #'Kernel-Objects'. ^b classList size", 5);

    /*
     *  Boolean's protocols, and the selectors in the first of them.
     *
     *  Five, not the four the 1983 image has: lib/Kernel-Protocol adds
     *  cull: as an extension, and an extension method's protocol is its
     *  own category with a leading star.  The Browser showing it is the
     *  Browser working -- that is what the star is for.
     *
     *  Six once lib/Json gave Boolean an asJsonValue, which is a second
     *  starred protocol on the same class.  This check moves whenever a
     *  package extends Boolean, and that is the point of it: an extension
     *  the Browser does NOT show is a method nobody can find.
     */
    check_integer("| b | b := Browser new on: SystemOrganization."
                  " b category: #'Kernel-Objects'. b className: #Boolean."
                  " ^b protocolList size", 6);
    /*
     *  Named rather than taken by position.  The pane is in alphabetical
     *  order since lib/Browser-Sorting, so `at: 1' now answers the starred
     *  extension protocol and not the one this check is about -- and an
     *  index whose meaning depends on the order is the wrong thing to pin
     *  either way.
     */
    check_integer("| b | b := Browser new on: SystemOrganization."
                  " b category: #'Kernel-Objects'. b className: #Boolean."
                  " b protocol: #'controlling'."
                  " ^b selectorList size", 6);

    /*
     *  And the two panes that lib/Browser-Sorting sorts really are sorted.
     *  Asked of the browser rather than of the organizer, because the
     *  organizers deliberately keep their own order -- that order is what a
     *  file out is written in -- and only the display was changed.
     */
    check_integer("| l | l := (Browser new on: SystemOrganization)"
                  " categoryList."
                  " ^(l asOrderedCollection = l asSortedCollection"
                  " asOrderedCollection) ifTrue: [1] ifFalse: [0]", 1);
    check_integer("| b l | b := Browser new on: SystemOrganization."
                  " b category: #'Kernel-Objects'. b className: #Boolean."
                  " l := b protocolList."
                  " ^(l asOrderedCollection = l asSortedCollection"
                  " asOrderedCollection) ifTrue: [1] ifFalse: [0]", 1);

    /*  And the source of a method, read back out of SourceFiles.  */
    check_integer("(Boolean sourceCodeAt: #not) size", 122);
    check_integer("((Boolean sourceCodeAt: #not) asText"
                  " makeSelectorBoldIn: Boolean) size", 122);
    /*
     *  The 1983 library's source, the closure package's on top of it, and
     *  one filler byte at the front so that nothing real starts at position
     *  zero -- which a CompiledMethod reads as "no source at all".  See
     *  test_every_method_can_find_its_source.
     */
    /*
     *  Moves whenever lib/ does: the source file is every method's text.
     *  lib/Concurrency's four classes went in and the 1983 SharedQueue
     *  came out.
     */
    /*
     *  Grows with lib/: every method's source is written to the file, so
     *  this moves whenever LIB_METHODS does.  It is checked at all because
     *  the source pointer is 22 bits and silently truncated once, and a
     *  size that stops growing is how that would show.
     */
    /*
     *  1948287 -> 2195190 is 46 new methods and, much the larger half, every
     *  class's COMMENT: they are filed into the sources beside the methods
     *  now instead of being read and dropped, so 367 of the 373 classes
     *  answer one where none did.
     */
    /*
     *  2238539 -> 2482762 with the Bugs3 fixes: 293 methods and the comments
     *  that say what each was for.
     */
    check_integer("(SourceFiles at: 1) contents size", 2482762);

    /*
     *  What TonelWriter writes, src/compiler/tonel.c reads.
     *
     *  The two readers -- the C one the bootstrap uses and the Smalltalk one
     *  a server uses -- and the one writer have to agree on the format, or
     *  a class written back from the Browser stops loading at the next
     *  bootstrap.  Checked with a real class, Mutex, whose every method has
     *  a comment and a few of which have brackets in them: the writer's
     *  text goes to a file, the C reader reads the file, and the number of
     *  methods it reports is the number the class has.
     */
    {
        /*
         *  The count first: evaluate collects before each expression and
         *  a doIt's answer is reachable from nothing once it returns, so
         *  the text has to be the LAST thing evaluated and be written out
         *  before anything else runs.
         */
        /*
         *  Less the `new' the loader synthesized, which TonelWriter leaves
         *  out of the file because the file never had it -- Bugs3 B61's
         *  round trip is byte for byte, and Mutex defines initialize and
         *  no new, so it has one.
         */
        st_oop      count = evaluate("Mutex selectors size + (Mutex class selectors "
                                     "select: [:s | ((Mutex class sourceCodeAt: s) "
                                     "includesSubstring: 'Synthesized by the loader') "
                                     "not]) size");
        st_oop      text = evaluate("TonelWriter sourceFor: Mutex");
        const char *path = "build/tonel-writer-check.class.st";

        ++st_test_checks;
        if (!OM_is_object(text) || OM_pointer_bit(text) || !OM_is_int(count)) {
            char    what[64] = "?";

            if (OM_is_object(text))
                OM_class_name_of(OM_fetch_class(text), what, sizeof what);
            ++st_test_failures;
            printf("  FAIL TonelWriter sourceFor: Mutex answered no text "
                   "(a%s; count is %s)\n", what,
                   OM_is_int(count) ? "an integer" : "not an integer");
        }  else  {
            FILE       *f = fopen(path, "wb");
            uint32_t    n = OM_fetch_byte_length(text);
            uint32_t    k;

            if (!f) {
                ++st_test_failures;
                printf("  FAIL cannot write %s\n", path);
            }  else  {
                for (k = 0; k < n; ++k)
                    fputc(OM_fetch_byte(k, text), f);
                fclose(f);
                {
                    tonel_count     seen = { 0, 0 };
                    char            error[256] = "";
                    st_source_sink  sink;

                    memset(&sink, 0, sizeof sink);
                    sink.class_def = count_class_def;
                    sink.method    = count_method;
                    if (!TONEL_read(path, &sink, &seen, error, sizeof error)) {
                        ++st_test_failures;
                        printf("  FAIL tonel.c refused what TonelWriter wrote: %s\n", error);
                    }  else if (seen.classes != 1
                            || seen.methods != (unsigned) OM_int_value(count)) {
                        ++st_test_failures;
                        printf("  FAIL tonel.c read %u classes and %u methods from "
                               "TonelWriter's Mutex, want 1 and %ld\n",
                               seen.classes, seen.methods,
                               (long) OM_int_value(count));
                    }
                }
                remove(path);
            }
        }
    }
}

/*
 *  Editing, compiling, inspecting and debugging -- inside the image.
 *
 *  This is the rest of Phase 8's exit criterion, and none of it is our code:
 *  the Compiler, the Inspector and the Debugger are the library's, running on
 *  an image bootstrapped from source.  A method compiled here is installed in
 *  a real method dictionary and answers when sent.
 */
static void
test_compile_inspect_debug(void)
{
    /*  The image compiles a method into a class, and it runs.  */
    check_integer("Object compile: 'answerFortyTwo ^42' classified: 'testing'"
                  " notifying: nil. ^3 answerFortyTwo", 42);
    check_integer("Object compile: 'twice: n ^n * 2' classified: 'testing'"
                  " notifying: nil. ^(3 twice: 21)", 42);

    /*  It is a real method: the Browser can find it and read it back.  */
    check_integer("Object compile: 'answerFortyTwo ^42' classified: 'testing'"
                  " notifying: nil."
                  " ^(Object sourceCodeAt: #answerFortyTwo) size", 18);

    /*  The Inspector opens on an object and lists its fields.  */
    check_integer("(Inspector new inspect: 3@4) fieldList size", 3);
    check_class("Inspector inspect: 3@4", "Inspector");

    /*  The Debugger opens on a context and lists the stack.  */
    check_integer("(Debugger context: thisContext) contextList size", 1);
    check_class("Debugger context: thisContext", "Debugger");
}

/*
 *  Self-hosting: the image's own compiler agrees with the C one.
 *
 *  This is the check the plan sets for the compiler, and it is worth being
 *  precise about why it is the right one.  That a method compiled inside the
 *  image RUNS proves the image can compile; it does not prove the two
 *  compilers agree, and they have to, because everything already in the
 *  image was built by the C compiler and everything compiled from now on is
 *  built by the image's.  A disagreement would be a system whose methods
 *  came in two dialects.
 *
 *  So the same source goes through both and the bytecodes are compared byte
 *  for byte.  The last three bytes are the source pointer, which is where
 *  the text was put rather than what was compiled, and the two put it in
 *  different places; everything before them must be identical.
 */
static void
check_same_bytecodes(const char *selector, const char *source)
{
    st_compile_context  ctx;
    st_compile_result   res;
    char                expression[1024];
    st_oop              theirs;
    st_oop              ours;
    uint32_t            n_ours;
    uint32_t            n_theirs;
    uint32_t            start_ours;
    uint32_t            start_theirs;
    uint32_t            i;

    /*
     *  Compile it inside the image, through Behavior>>compile:.
     *
     *  The source becomes a Smalltalk string literal, so every quote in it
     *  has to be doubled on the way in -- which is the lexer's escape and
     *  not this file's business, except that getting it wrong makes the
     *  image reject the text and look like a compiler that cannot handle
     *  string literals.
     */
    {
        char       *w = expression;
        const char *r;
        const char *prefix = "Object compile: '";
        const char *suffix = "' classified: 'self-hosting check'"
                             " notifying: nil. ^1";

        for (r = prefix; *r; ++r)
            *w++ = *r;
        for (r = source; *r; ++r) {
            *w++ = *r;
            if (*r == '\'')
                *w++ = '\'';
        }
        for (r = suffix; *r; ++r)
            *w++ = *r;
        *w = '\0';
    }
    evaluate(expression);

    snprintf(expression, sizeof expression,
             "^Object compiledMethodAt: #%s", selector);
    theirs = evaluate(expression);

    ++st_test_checks;
    if (!OM_is_present(theirs)) {
        ++st_test_failures;
        printf("  FAIL the image compiled no method for #%s\n", selector);
        return;
    }

    /*  And in C, from the same text.  */
    memset(&ctx, 0, sizeof ctx);
    ctx.dialect            = ST_DIALECT_CLOSURES;
    ctx.intern_symbol      = BOOT_intern_symbol;
    ctx.make_string        = BOOT_make_string;
    ctx.make_float         = BOOT_make_float;
    ctx.make_large_integer = BOOT_make_large_integer;
    ctx.make_large_integer_digits = BOOT_make_large_integer_digits;
    ctx.make_array         = BOOT_make_array;
    ctx.make_byte_array    = BOOT_make_byte_array;
    ctx.make_method_state  = BOOT_make_method_state;
    ctx.make_character     = BOOT_make_character;
    ctx.lookup_global      = BOOT_lookup_global;
    /*  A send to super needs the method's class in the literal frame.  */
    ctx.method_class_association = BOOT_lookup_global("Object", NULL);
    if (COMPILE_method(source, &ctx, &res) != 0) {
        ++st_test_checks;
        ++st_test_failures;
        printf("  FAIL C could not compile #%s: %s\n", selector, res.error);
        return;
    }
    ours = res.method;

    /*
     *  Same header means same argument, temporary and literal counts --
     *  comparing only the sixteen bits the Blue Book defines.
     *
     *  Ours carries more above them: the exact frame the method needs, so a
     *  context can be made to fit instead of overflowing a 32-slot one and
     *  writing into the next object's header.  The 1983 compiler has
     *  nothing to say there and leaves the bits zero, so masking is what
     *  keeps this check about the thing it is for.  A divergence in any
     *  Blue Book field still fails it.
     */
    CHECK_EQ_INT((int) (OM_fetch_pointer(0, ours) & 0xFFFF),
                 (int) (OM_fetch_pointer(0, theirs) & 0xFFFF));

    start_ours   = BOOT_method_initial_ip(ours);
    start_theirs = BOOT_method_initial_ip(theirs);
    n_ours   = OM_fetch_byte_length(ours);
    n_theirs = OM_fetch_byte_length(theirs);

    /*  Less the three-byte source pointer each carries.  */
    n_ours   = (n_ours   > start_ours   + 3) ? n_ours   - 3 : start_ours;
    n_theirs = (n_theirs > start_theirs + 3) ? n_theirs - 3 : start_theirs;

    ++st_test_checks;
    if (n_ours - start_ours != n_theirs - start_theirs) {
        ++st_test_failures;
        printf("  FAIL #%s: C emitted %u bytecodes, the image %u\n",
               selector, n_ours - start_ours, n_theirs - start_theirs);
        return;
    }

    for (i = 0; i < n_ours - start_ours; ++i) {
        uint8_t a = (uint8_t) OM_fetch_byte(start_ours + i, ours);
        uint8_t b = (uint8_t) OM_fetch_byte(start_theirs + i, theirs);

        ++st_test_checks;
        if (a != b) {
            ++st_test_failures;
            printf("  FAIL #%s bytecode %u: C emitted %u, the image %u\n",
                   selector, i, a, b);
            return;
        }
    }
}

static void
test_self_hosting(void)
{
    /*  A literal return, the smallest method there is.  */
    check_same_bytecodes("shAnswer", "shAnswer ^42");
    /*  Arguments, temporaries and assignment.  */
    check_same_bytecodes("shAdd:to:",
                         "shAdd: a to: b | t | t _ a + b. ^t");
    /*
     *  A conditional is left out for now, and the reason is recorded rather
     *  than hidden: the 1983 compiler has one-byte forms for a jump of eight
     *  bytes or less and this one always emits the two-byte form, so an
     *  inlined ifTrue:ifFalse: comes out two bytes longer.  The instructions
     *  are otherwise the same and in the same order.  Choosing the short
     *  form needs the distance before the body is compiled, which is what
     *  the 1983 compiler's separate sizing pass is for.
     */
    /*  A loop, which is a backward jump.  */
    check_same_bytecodes("shSum:",
                         "shSum: n | s | s _ 0. 1 to: n do: [:i | s _ s + i]."
                         " ^s");
    /*
     *  Literals of every kind the frame can hold are checked below rather
     *  than here, because the two compilers number the literal frame in
     *  different orders and so disagree on the index in every push that
     *  names one.  Ours assigns an index when it emits the push; the 1983
     *  one assigns a selector its index while parsing, before the sizing
     *  pass gets to the variables, so a selector mentioned later can hold a
     *  lower index than a variable pushed earlier.  Same literals, same
     *  instructions, different numbering.
     */
    /*  A cascade, and a send to super.  */

    check_same_bytecodes("shSuper", "shSuper ^super printString");

    /*
     *  And where the numbering differs, that the two agree on what the
     *  method DOES, which is the part that has to be true.
     */
    check_oop("Object compile: 'shLiterals ^Array with: ''text'' with: #sym"
              " with: $c with: 3.5' classified: 'self-hosting check'"
              " notifying: nil."
              " ^(3 shLiterals) = (Array with: 'text' with: #sym"
              " with: $c with: 3.5)", ST_TRUE, "true");
    check_oop("Object compile: 'shCascade | s | s _ WriteStream on:"
              " String new. s nextPut: $a; nextPut: $b. ^s contents'"
              " classified: 'self-hosting check' notifying: nil."
              " ^(3 shCascade) = 'ab'", ST_TRUE, "true");
    /*  A block with its own argument, closing over an outer temporary.  */
    check_same_bytecodes("shClosure",
                         "shClosure | t | t _ 0."
                         " #(1 2 3) do: [:each | t _ t + each]. ^t");
}

/*
 *  A method compiled inside the image can see a class variable, and sees the
 *  same one everything else does.
 *
 *  A class variable is reached two ways.  A method the C compiler built
 *  holds the Association in its literal frame and reads its value, so it
 *  works whether or not any dictionary exists.  A method compiled LATER has
 *  to find the binding by name, and the only place to look is the class's
 *  classPool.
 *
 *  Ours was nil on every class, so the image compiled a method naming a
 *  class variable and quietly bound it to nil.  It compiled, it ran, and it
 *  answered the wrong thing -- which is what the Browser does every time
 *  someone accepts a method.
 *
 *  MacroSelectors is the case that proves both halves: it is written by
 *  MessageNode class>>initialize, which the C compiler built, and read here
 *  by a method the image compiles now.  Eight entries means the two are
 *  looking at one binding and not at two spelled alike.
 */
static void
test_class_variables_from_the_image(void)
{
    check_integer("MessageNode class compile: 'shMacros ^MacroSelectors'"
                  " classified: 'class variable check' notifying: nil."
                  " ^MessageNode shMacros size", 8);

    /*  And that writing through one is seen through the other.  */
    check_integer("MessageNode class compile: 'shPut: x MacroSelectors _ x'"
                  " classified: 'class variable check' notifying: nil."
                  " MessageNode shPut: #(1 2 3)."
                  " ^MessageNode shMacros size", 3);
    check_integer("MessageNode shPut:"
                  " #(ifTrue: ifFalse: ifTrue:ifFalse: ifFalse:ifTrue:"
                  " and: or: whileFalse: whileTrue:)."
                  " ^MessageNode shMacros size", 8);
}

/*
 *  Every class the image defines is reachable by name from Smalltalk.
 *
 *  A binding is made the first time a name is needed, and for the first
 *  seventeen of them that is before the class named Association exists --
 *  Association being the seventeenth.  Those carried a nil class, and the
 *  one thing in the system that asks a binding what class it is happens to
 *  be Dictionary>>add:, which sends #key.  So the system dictionary refused
 *  exactly those seventeen, and "Smalltalk includesKey: #Array" answered
 *  false in a system where Array worked perfectly well -- a compiled method
 *  holds the binding and reads its value, and never asks its class.
 *
 *  The names below are chosen from the refused seventeen and from after
 *  them, because what makes this worth a test is that both must hold.
 */
static void
test_globals_are_reachable_by_name(void)
{
    /*  Refused when their bindings had no class.  */
    check_oop("^Smalltalk includesKey: #Array", ST_TRUE, "true");
    check_oop("^Smalltalk includesKey: #Association", ST_TRUE, "true");
    check_oop("^Smalltalk includesKey: #OrderedCollection", ST_TRUE, "true");
    check_oop("^Smalltalk includesKey: #Stream", ST_TRUE, "true");
    check_oop("^Smalltalk includesKey: #Interval", ST_TRUE, "true");

    /*  Defined after Association, so never affected.  */
    check_oop("^Smalltalk includesKey: #Point", ST_TRUE, "true");
    check_oop("^Smalltalk includesKey: #Object", ST_TRUE, "true");

    /*  And the lookup answers the class itself, not merely a binding.  */
    check_oop("^(Smalltalk at: #Array) == Array", ST_TRUE, "true");
    check_oop("^(Smalltalk at: #OrderedCollection) == OrderedCollection",
              ST_TRUE, "true");

    /*
     *  Nothing is missing.  Every class the bootstrap built is in the
     *  dictionary; a count that falls short means bindings were refused
     *  again, which is precisely how this went unnoticed before.
     */
    check_oop("^Smalltalk size >= 310", ST_TRUE, "true");
}

/*
 *  Changing the image, which is the half the audit did not cover.
 *
 *  Everything above asks whether what the bootstrap BUILT can be found.
 *  These ask whether what the image builds afterwards can be -- recompiling
 *  a method, removing one, adding enough of them that the image has to grow
 *  a method dictionary itself, interning a name nothing had used, and
 *  defining a whole class.  That is what a Browser does all day.
 *
 *  The growth case is the interesting one: twenty-four methods into a class
 *  with two makes HashedCollection>>grow build a dictionary the bootstrap
 *  never touched, and the interpreter then has to read it.
 */
static void
test_changing_the_image(void)
{
    /*  Recompiling replaces the method rather than adding a second.  */
    check_integer("Integer compile: 'shV ^99' classified: 'sh' notifying: nil."
                  " ^3 shV", 99);
    check_integer("Integer compile: 'shV ^100' classified: 'sh' notifying: nil."
                  " ^3 shV", 100);

    /*  Removing it, which goes through MethodDictionary>>become:.  */
    check_integer("Integer compile: 'shGone ^5' classified: 'sh' notifying: nil."
                  " ^3 shGone", 5);
    check_integer("Integer removeSelector: #shGone."
                  " ^(Integer includesSelector: #shGone)"
                  " ifTrue: [1] ifFalse: [0]", 0);

    /*  Enough methods to make the image grow the dictionary, then send them. */
    check_integer("| bad | bad _ 0. 1 to: 24 do: [:i |"
                  " Link compile: 'shM' , i printString , ' ^' , i printString"
                  " classified: 'sh grown' notifying: nil]."
                  " 1 to: 24 do: [:i |"
                  " ((Link new perform: ('shM' , i printString) asSymbol) = i)"
                  " ifFalse: [bad _ bad + 1]]. ^bad", 0);
    check_integer("| bad | bad _ 0. 1 to: 24 do: [:i |"
                  " (Link includesSelector: ('shM' , i printString) asSymbol)"
                  " ifFalse: [bad _ bad + 1]]. ^bad", 0);

    /*  A name nothing had used interns to one object.  */
    check_oop("^'shBrandNewNameNothingUsed' asSymbol"
              " == 'shBrandNewNameNothingUsed' asSymbol", ST_TRUE, "true");
    check_integer("| s | Link compile: 'shFreshName ^7' classified: 'sh'"
                  " notifying: nil. s _ 'shFreshName' asSymbol."
                  " ^Link new perform: s", 7);

    /*  Globals come and go.  */
    check_integer("Smalltalk at: #ShTestGlobal put: 42."
                  " ^Smalltalk at: #ShTestGlobal", 42);
    check_integer("Smalltalk removeKey: #ShTestGlobal."
                  " ^(Smalltalk includesKey: #ShTestGlobal)"
                  " ifTrue: [1] ifFalse: [0]", 0);

    /*
     *  And a class defined from inside the image, with an instance variable
     *  and a class variable, and a method that reads both.
     */
    check_integer("Object subclass: #ShTestClass"
                  " instanceVariableNames: 'aa bb'"
                  " classVariableNames: 'CC' poolDictionaries: ''"
                  " category: 'Sh-Test'."
                  " (Smalltalk at: #ShTestClass) compile:"
                  " 'shSet CC _ 5. aa _ 3. ^aa + CC'"
                  " classified: 'sh' notifying: nil."
                  " ^(Smalltalk at: #ShTestClass) new shSet", 8);
    check_integer("^(Smalltalk at: #ShTestClass) allInstVarNames size", 2);

    /*  The organization follows along, since that is what a Browser lists.  */
    check_oop("Link compile: 'shOrgTest ^1' classified: 'sh category'"
              " notifying: nil."
              " ^(Link organization listAtCategoryNamed: #'sh category')"
              " includes: #shOrgTest", ST_TRUE, "true");
    check_oop("Link removeSelector: #shOrgTest."
              " ^((Link organization listAtCategoryNamed: #'sh category')"
              " includes: #shOrgTest) not", ST_TRUE, "true");
}

/*
 *  The audit: everything the bootstrap builds in C, checked the way the
 *  image looks at it rather than the way the interpreter does.
 *
 *  Three bugs of one shape came out of the Browser, and the shape is worth
 *  naming: the VM is more forgiving than the image.  Method lookup SCANS a
 *  dictionary, so entries in the wrong slots are invisible to it; lookup
 *  steps over a nil dictionary, so a missing one is invisible; a compiled
 *  method holds its variable's Association, so an empty classPool is
 *  invisible.  Every one of those was fine to run and broken to browse.
 *
 *  So the invariant is: whatever a scan finds, a hashed lookup must find
 *  too, and whatever the image will search must be searchable the image's
 *  way.  These check that across every structure the bootstrap builds.
 */
static void
test_audit_what_the_image_searches(void)
{
    /*
     *  The symbol table, built in C with String>>hash duplicated there.
     *  Every selector in the system must come back as the same object when
     *  its characters are interned again.
     */
    check_integer("| bad cls | bad _ 0."
                  " SystemOrganization categories do: [:cat |"
                  " (SystemOrganization listAtCategoryNamed: cat) do: [:nm |"
                  " cls _ Smalltalk at: nm."
                  " (Array with: cls with: cls class) do: [:c |"
                  " c selectors do: [:sel |"
                  " (sel asString asSymbol == sel)"
                  " ifFalse: [bad _ bad + 1]]]]]. ^bad", 0);
    check_integer("| bad | bad _ 0."
                  " SystemOrganization categories do: [:cat |"
                  " (SystemOrganization listAtCategoryNamed: cat) do: [:nm |"
                  " (nm asString asSymbol == nm) ifFalse: [bad _ bad + 1]."
                  " ((Smalltalk at: nm) name asSymbol == nm)"
                  " ifFalse: [bad _ bad + 1]]]. ^bad", 0);

    /*  Smalltalk itself, and every Dictionary it holds.  */
    check_integer("| bad | bad _ 0."
                  " Smalltalk keys do: [:k |"
                  " (Smalltalk includesKey: k) ifFalse: [bad _ bad + 1]]."
                  " Smalltalk do: [:v | (v isKindOf: Dictionary) ifTrue: ["
                  " v keys do: [:k2 |"
                  " (v includesKey: k2) ifFalse: [bad _ bad + 1]]]]."
                  " ^bad", 0);

    /*  The class pools, which hold the class variables by name.  */
    check_integer("| bad cls p | bad _ 0."
                  " SystemOrganization categories do: [:cat |"
                  " (SystemOrganization listAtCategoryNamed: cat) do: [:nm |"
                  " cls _ Smalltalk at: nm. p _ cls classPool."
                  " p keys do: [:k |"
                  " (p includesKey: k) ifFalse: [bad _ bad + 1]]]]. ^bad", 0);

    /*  Every class organization answers for every category it lists.  */
    check_integer("| bad cls o | bad _ 0."
                  " SystemOrganization categories do: [:cat |"
                  " (SystemOrganization listAtCategoryNamed: cat) do: [:nm |"
                  " cls _ Smalltalk at: nm."
                  " (Array with: cls with: cls class) do: [:c |"
                  " o _ c organization. o isNil ifFalse: ["
                  " o categories do: [:mc |"
                  " (o listAtCategoryNamed: mc) isNil"
                  " ifTrue: [bad _ bad + 1]]]]]]. ^bad", 0);

    /*  The Character table, which is indexed rather than hashed.  */
    check_integer("| bad | bad _ 0. 0 to: 255 do: [:i |"
                  " ((Character value: i) asInteger = i)"
                  " ifFalse: [bad _ bad + 1]."
                  " ((Character value: i) == (Character value: i))"
                  " ifFalse: [bad _ bad + 1]]. ^bad", 0);

    /*
     *  And the instance variable names, which only the image ever reads.
     *
     *  Each class is given an Array of them, and the array was made before
     *  anything was called Array for the four classes that come before it in
     *  file order -- so those four had one with no class, which answers no
     *  messages at all.  Behavior>>allInstVarNames adds each class's names
     *  to its superclass's, so asking any collection what its fields are
     *  called failed, which is the first thing an Inspector does.
     */
    check_oop("| n cls | n _ 0."
              " SystemOrganization categories do: [:cat |"
              " (SystemOrganization listAtCategoryNamed: cat) do: [:nm |"
              " cls _ Smalltalk at: nm."
              " n _ n + cls allInstVarNames size]]."
              " ^n > 1500", ST_TRUE, "true");
    check_integer("^(Inspector new inspect: 3@4) fieldList size", 3);
    check_oop("^((Inspector new inspect:"
              " (OrderedCollection with: 1 with: 2)) fieldList size > 2)",
              ST_TRUE, "true");
}

/*
 *  What the Browser needs, which is more than what a send needs.
 *
 *  Two things were wrong here and both were invisible to the interpreter.
 *
 *  A method dictionary is an IdentityDictionary, and findKeyOrNil: begins
 *  probing at "key asOop \\ length + 1".  The bootstrap filled it from slot
 *  zero instead.  Lookup scans the whole dictionary, so every send in the
 *  system worked; includesSelector:, compiledMethodAt: and sourceCodeAt: all
 *  go through the hash, so three selectors in five answered "key not found"
 *  -- the ones whose slot did not happen to lie on the probe path from their
 *  own hash.  The other two in five worked, which made it look like
 *  particular methods were broken rather than all of them.
 *
 *  And a dictionary was made only when a class received its first method, so
 *  a class with no methods on a side -- which is most classes, on the class
 *  side -- had nil there.  Lookup steps over that happily.  Behavior>>
 *  selectors is "^methodDict keys", which does not.
 */
static void
test_browsing_finds_every_method(void)
{
    /*
     *  Every selector the organization lists is findable by the image's own
     *  hashing, on both sides of every class.  4521 of them, and the count
     *  is the same one the bootstrap reports compiling.
     */
    check_integer("| bad cls | bad _ 0."
                  " SystemOrganization categories do: [:cat |"
                  " (SystemOrganization listAtCategoryNamed: cat) do: [:nm |"
                  " cls _ Smalltalk at: nm."
                  " (Array with: cls with: cls class) do: [:c |"
                  " c organization isNil ifFalse: ["
                  " c organization categories do: [:mc |"
                  " (c organization listAtCategoryNamed: mc) do: [:sel |"
                  " (c includesSelector: sel) ifFalse: [bad _ bad + 1]]]]]]]."
                  " ^bad", 0);

    /*
     *  And every class and metaclass answers selectors at all -- at least
     *  the 4521 the bootstrap compiled, plus whatever the checks above have
     *  compiled into the image since.
     */
    check_integer("| n cls | n _ 0."
                  " SystemOrganization categories do: [:cat |"
                  " (SystemOrganization listAtCategoryNamed: cat) do: [:nm |"
                  " cls _ Smalltalk at: nm."
                  " (Array with: cls with: cls class) do: [:c |"
                  " n _ n + c selectors size]]]."
                  " ^n >= 4521 ifTrue: [1] ifFalse: [0]", 1);

    /*
     *  And the path a person takes: a category, a class, a protocol, a
     *  message, and the source of it.  A 1983 category rather than the
     *  first in the list: the first is whichever of ours sorts earliest,
     *  and lib/Clipboard's class has no instance side to browse, which is
     *  the browser answering nothing rather than the browser being broken.
     */
    check_oop("| b | b _ Browser new on: SystemOrganization."
              " b category: 'Collections-Abstract'."
              " b className: b classList first."
              " b protocol: b protocolList first."
              " b selector: b selectorList first."
              " ^b text size > 0", ST_TRUE, "true");
    check_integer("| n | n _ 0."
                  " (Array with: Point with: Point class with: Rectangle)"
                  " do: [:c | c selectors do: [:sel |"
                  " n _ n + (c sourceCodeAt: sel) size]]."
                  " ^n > 5000 ifTrue: [1] ifFalse: [0]", 1);
}

/*
 *  Class-side instance variables, which a class definition declares in its
 *  second half:
 *
 *      Form class
 *        instanceVariableNames: 'whiteMask darkGrayMask grayMask ...'
 *
 *  and which hold the stock Forms every piece of drawing asks for.  The
 *  bootstrap parses that header itself, and it treated a newline as
 *  whitespace and a carriage return as part of a name -- so when the chunk
 *  reader started answering carriage returns, every one of these became an
 *  undeclared global bound to nil.
 *
 *  Nothing failed.  The suite stayed green through it, because nothing here
 *  had ever asked Form for a Form.  That is the gap this closes.
 */
static void
test_class_side_instance_variables(void)
{
    check_oop("^Form gray isNil", ST_FALSE, "false");
    check_oop("^Form black isNil", ST_FALSE, "false");
    check_oop("^Form white isNil", ST_FALSE, "false");
    check_oop("^Form lightGray isNil", ST_FALSE, "false");
    /*  And they are real Forms, not something that merely is not nil.  */
    check_integer("^Form gray width", 16);
    check_integer("^Form gray height", 16);
}

/*
 *  Quitting.  SystemDictionary>>quitPrimitive is primitive 113, and with it
 *  unimplemented the method fell through to "self primitiveFailed" -- so
 *  choosing "Quit, without saving" from the system menu raised an error and
 *  printed a backtrace instead of quitting, which is the one menu item whose
 *  whole job is to leave.
 */
static void
test_quit(void)
{
    CHECK_EQ_INT(ST_quit_requested, 0);
    evaluate("Smalltalk quit. ^1");
    CHECK_EQ_INT(ST_quit_requested, 1);
    /*  Cleared, so the checks after this one still have an interpreter.  */
    ST_quit_requested = 0;
}

/*
 *  A multi-line string is multi-line, and the system menu is the proof.
 *
 *  Smalltalk-80 separates lines with Character cr, which is 13.  Not the
 *  linefeed C uses -- Paragraph, CharacterScanner and String>>lines all
 *  break on 13 and on nothing else, and the 1983 sources file is written
 *  with it.  The chunk reader used to normalize every ending to a linefeed,
 *  which is the sensible thing to do for a C program reading a text file and
 *  produces an image in which no string has any line breaks in it.
 *
 *  Nothing reports that.  A Paragraph with no line breaks is a perfectly
 *  good Paragraph; it is just one line long.  The system menu is ten items
 *  in one string separated by nine of them, so it composed to 872 pixels
 *  wide and 8 high -- ten labels side by side, running off the screen.  From
 *  the outside, pressing the yellow button on the desktop did nothing at all.
 */
static void
test_menus_compose_as_lines(void)
{
    /*  The separators survived into the image as carriage returns.  */
    check_integer("^(ScreenController class classPool at: #ScreenYellowButtonMenu)"
                  " isNil ifTrue: [0] ifFalse: [1]", 1);
    check_integer("PopUpMenu compile: 'shLabels ^labelString'"
                  " classified: 'line ending check' notifying: nil."
                  " ^((ScreenController class classPool"
                  " at: #ScreenYellowButtonMenu) shLabels)"
                  " occurrencesOf: (Character value: 13)", 9);
    check_integer("^((ScreenController class classPool"
                  " at: #ScreenYellowButtonMenu) shLabels)"
                  " occurrencesOf: (Character value: 10)", 0);

    /*
     *  And the menu composed to ten lines rather than one.  Twenty-six
     *  pixels a line in this face, so 260 tall and narrow enough to fit --
     *  not one line of all ten items laid end to end.
     */
    check_integer("PopUpMenu compile: 'shForm ^form'"
                  " classified: 'line ending check' notifying: nil."
                  " ^((ScreenController class classPool"
                  " at: #ScreenYellowButtonMenu) shForm) height", 260);
    check_oop("^(((ScreenController class classPool"
              " at: #ScreenYellowButtonMenu) shForm) width < 300)",
              ST_TRUE, "true");
}

/*
 *  A file out travels between hosts.
 *
 *  Writing the host's own line ending is only half a promise.  The other
 *  half is that a file written on one machine reads back on another: a class
 *  filed out on Linux, mailed to somebody on Windows and filed in there has
 *  to arrive as the same source, and the Alto's carriage returns have to keep
 *  working too, because every .st file written before this change and the
 *  whole of sources/ is in them.
 *
 *  So the reader takes all three and the writer picks one.  That asymmetry
 *  is deliberate and it is what makes the exchange work in both directions
 *  at once -- there is no negotiation and no marker in the file, and a file
 *  half converted by something else on the way still reads, because each
 *  ending is decided on its own.
 *
 *  PositionableStream>>nextChunk does the reading, src/compiler/chunk.c does
 *  the same for the bootstrap, and Character>>isSeparator already counted a
 *  line feed in 1983 -- which is why the bang framing between chunks never
 *  needed anything doing to it.
 */
static void
test_file_out_travels_between_hosts(void)
{
    /*
     *  The same text in each of the three conventions reads back as one
     *  String, and it is the carriage-return one.
     */
    check_boolean(
        "| cr lf crlf read want |"
        " cr := String with: (Character value: 13)."
        " lf := String with: (Character value: 10)."
        " crlf := cr , lf."
        " read := [:e | (ReadStream on: 'x' , e , 'y' , e , 'z!') nextChunk]."
        " want := 'x' , cr , 'y' , cr , 'z'."
        " ^((read value: cr) = want)"
        " and: [((read value: lf) = want)"
        " and: [(read value: crlf) = want]]", 1);

    /*  A carriage return and a line feed are ONE ending, not two.  */
    check_integer(
        "| crlf |"
        " crlf := (String with: (Character value: 13)) , (String with: (Character value: 10))."
        " ^((ReadStream on: 'x' , crlf , 'y!') nextChunk) size", 3);

    /*  Nothing foreign survives into the image.  */
    check_integer(
        "| lf |"
        " lf := String with: (Character value: 10)."
        " ^((ReadStream on: 'x' , lf , 'y!') nextChunk)"
        " occurrencesOf: (Character value: 10)", 0);

    /*  The writer answers one of the two endings anybody now runs.  */
    check_boolean(
        "| e lf |"
        " lf := String with: (Character value: 10)."
        " e := FileStream nativeLineEnd."
        " ^(e = lf) or: [e = ((String with: (Character value: 13)) , lf)]", 1);

    /*
     *  And a real file out is written in it: on a line-feed host no carriage
     *  return reaches the disk at all, and on a carriage-return-and-line-feed
     *  one every carriage return is paired with a line feed.  Stated that way
     *  the check is the same check on both.
     */
    check_boolean(
        "| name raw cr lf native |"
        " cr := Character value: 13."
        " lf := Character value: 10."
        " native := FileStream nativeLineEnd."
        " name := 'zz-line-ending-test.st'."
        " Object fileOutMessage: #printString fileName: name."
        " raw := (FileStream oldFileNamed: name) contentsOfEntireFile."
        " Disk removeKey: name."
        " ^native = (String with: lf)"
        "     ifTrue: [((raw occurrencesOf: cr) = 0)"
        "                and: [(raw occurrencesOf: lf) > 0]]"
        "     ifFalse: [((raw occurrencesOf: cr) = (raw occurrencesOf: lf))"
        "                and: [(raw occurrencesOf: cr) > 0]]", 1);
}

/*
 *  A file out ends where its text ends.
 *
 *  A file is written a page at a time and a page is 512 bytes, so the last
 *  one is almost never full.  FileStream>>shorten exists to say where the
 *  data really stopped -- it sets `page dataEnd: position' and asks the file
 *  to end there -- and the truncate underneath it ignored the answer and cut
 *  at `pageNumber * 512' instead, the end of the PAGE rather than the end of
 *  the data.
 *
 *  So every file this system wrote through a stream was rounded up to a
 *  multiple of 512 and the difference arrived as trailing zero bytes: up to
 *  511 of them stuck on the end of a filed-out class, which an editor shows
 *  as ^@^@^@, which git calls a binary file, and which anything reading the
 *  file back has to skip past.  It was invisible from inside the image,
 *  because nextChunk stops at the last bang and never reads far enough to
 *  meet them.
 */
static void
test_file_out_has_no_page_padding(void)
{
    /*  Nothing at all after the text -- not one zero byte.  */
    check_integer(
        "| name raw |"
        " name := 'zz-file-out-padding-test.st'."
        " Object fileOutMessage: #printString fileName: name."
        " raw := (FileStream oldFileNamed: name) contentsOfEntireFile."
        " Disk removeKey: name."
        " ^raw occurrencesOf: (Character value: 0)", 0);

    /*
     *  And the last byte is the chunk terminator, which is the positive form
     *  of the same statement: the file ends where the writer stopped.
     */
    check_boolean(
        "| name raw |"
        " name := 'zz-file-out-padding-test.st'."
        " Object fileOutMessage: #printString fileName: name."
        " raw := (FileStream oldFileNamed: name) contentsOfEntireFile."
        " Disk removeKey: name."
        " ^raw last = $!", 1);

    /*
     *  A file whose data does not fill its last page is the whole of the
     *  case, so write one that certainly does not: a short file out is a few
     *  hundred bytes and 512 is the page.  Its size on disk is its text.
     */
    check_boolean(
        "| name raw size |"
        " name := 'zz-file-out-padding-test.st'."
        " Object fileOutMessage: #printString fileName: name."
        " raw := (FileStream oldFileNamed: name) contentsOfEntireFile."
        " size := (FileStream oldFileNamed: name) size."
        " Disk removeKey: name."
        " ^size = raw size", 1);
}

/*
 *  Input, through the path a window's events take.
 *
 *  GFX_inject_* does exactly what the SDL handlers do -- move the pointer,
 *  set the button state, queue the event words, signal the input semaphore.
 *  Driving a private queue instead would prove nothing about the one the
 *  image reads; this is the same one.
 *
 *  With it the interactive half can be tested without a person in front of
 *  it: the Sensor answers where the pointer is, and a controller says whether
 *  it wants control -- which is the question searchForActiveController spends
 *  its whole life asking.
 */
static void
test_input(void)
{
    /*  The Sensor reports where the pointer was put.  */
    GFX_inject_mouse(100, 80);
    check_integer("Sensor cursorPoint x", 100);
    check_integer("Sensor cursorPoint y", 80);

    GFX_inject_mouse(300, 240);
    check_integer("Sensor cursorPoint x", 300);
    check_integer("Sensor cursorPoint y", 240);

    /*
     *  Buttons take the longer way round, and it is worth saying which.
     *
     *  The pointer's position is polled straight from the VM by primitive
     *  90, so it answers whatever was last injected.  The button state is
     *  not polled: it is kept by InputState and updated by the input PROCESS
     *  as it drains the event queue, which it does when the semaphore
     *  primitive 93 installed is signalled.  So nothing here is true until a
     *  process other than this one has run, and the yield is what lets it.
     *
     *  Codes 128, 129 and 130 are the blue, yellow and red buttons, and
     *  InputState keeps them as bits 1, 2 and 4 in that order.
     */
    GFX_inject_button(130, 1);
    check_integer("Processor yield. ^Sensor buttons", 4);
    check_oop("^Sensor anyButtonPressed", ST_TRUE, "true");
    check_oop("^Sensor redButtonPressed", ST_TRUE, "true");

    GFX_inject_button(130, 0);
    check_integer("Processor yield. ^Sensor buttons", 0);
    check_oop("^Sensor noButtonPressed", ST_TRUE, "true");

    /*  And the keyboard, which arrives on the same queue.  */
    GFX_inject_key('A', 1);
    GFX_inject_key('A', 0);
    check_oop("Processor yield. ^Sensor keyboardPressed", ST_TRUE, "true");
    check_integer("Processor yield. ^Sensor keyboard asInteger", 'A');

    /*
     *  Moving the pointer and nothing else, which is its own kind of load.
     *
     *  Every motion posts an X event and a Y event, so it signals the input
     *  semaphore twice, and both are drained in one pass before any bytecode
     *  runs.  A transfer only NOMINATES a process -- the switch happens when
     *  the interpreter next reaches the top of its loop -- so the second
     *  signal was scheduling against an activeProcess that had already been
     *  displaced, and put it on a run queue a second time.
     *
     *  A process chained onto a list twice has a nextLink pointing at
     *  itself, and is both running and queued.  Suspending it then hands
     *  control straight back to itself, so a terminating process returns
     *  from the terminate it was never meant to return from, off the bottom
     *  of its stack, and the whole image stops.  Resting a hand on the mouse
     *  did it in well under a second.
     */
    {
        int i;

        for (i = 0; i < 200; ++i) {
            GFX_inject_mouse(100 + (i % 400), 80 + (i % 300));
            evaluate("Processor yield. ^1");
        }
    }
    check_oop("Processor yield. ^true", ST_TRUE, "true");
    /*  Still only ever on one list, so the queues are still walkable.  */
    check_oop("| n | n _ 0. 1 to: 8 do: [:i |"
              " ((Processor instVarAt: 1) at: i) do: [:p | n _ n + 1]]."
              " ^n < 100", ST_TRUE, "true");

    /*
     *  A controller wants control when the pointer is over its view, and
     *  does not when it is not.  Moving the pointer changes the answer,
     *  which is the whole of how MVC decides who is in charge.
     */
    GFX_inject_mouse(50, 50);
    check_oop("| v | v := StandardSystemView new."
              " v window: (0@0 corner: 200@200)."
              " ^v controller isControlWanted", ST_TRUE, "true");
    GFX_inject_mouse(500, 400);
    check_oop("| v | v := StandardSystemView new."
              " v window: (0@0 corner: 200@200)."
              " ^v controller isControlWanted", ST_FALSE, "false");

    /*
     *  A drag at full rate must not cost a button release.
     *
     *  The image rebuilds its idea of which buttons are down from this
     *  stream and from nothing else, so a dropped BISTATE_OFF leaves it
     *  believing a button is held for ever: Sensor noButtonPressed never
     *  answers true again, no controller takes control, no menu opens, and
     *  the window stays on the screen looking perfectly fine.  That is a
     *  frozen system with no diagnostic, and it is what framing a window
     *  used to do -- the one gesture that drags at full rate with a button
     *  held while the image is busy drawing a rubber band.  Measured before
     *  the fix: 379 events dropped in a single drag of 700 moves, into a
     *  ring of 1024 words that each move fills two of.
     *
     *  Positions now coalesce and transitions never do, so what is checked
     *  here is both halves of that: nothing was dropped, and the two edges
     *  came out of the far end.
     */
    {
        unsigned    before = GFX_events_dropped();
        unsigned    ons = 0, offs = 0, locations = 0;
        uint16_t    word;
        int         i;

        while (GFX_next_event_word(&word))
            ;                           /*  start from a drained ring  */
        GFX_inject_button(130, 1);
        for (i = 0; i < 2000; ++i)
            GFX_inject_mouse(200 + (i % 400), 300 + (i % 200));
        GFX_inject_button(130, 0);

        CHECK_EQ_INT((int) (GFX_events_dropped() - before), 0);
        while (GFX_next_event_word(&word)) {
            unsigned    type  = (unsigned) (word >> ST_EVENT_TYPE_SHIFT);
            unsigned    value = (unsigned) (word & ST_EVENT_VALUE_MASK);

            if (type == ST_EVENT_BISTATE_ON && value == 130)
                ++ons;
            else if (type == ST_EVENT_BISTATE_OFF && value == 130)
                ++offs;
            else if (type == ST_EVENT_XLOCATION || type == ST_EVENT_YLOCATION)
                ++locations;
        }
        CHECK_EQ_INT((int) ons, 1);
        CHECK_EQ_INT((int) offs, 1);
        /*
         *  And the two thousand moves cost a bounded number of words rather
         *  than four thousand: one pair held at a time, plus the pair the
         *  closing transition flushed in front of itself.
         */
        CHECK(locations <= 8);
    }
}

/*
 *  Printing, which is the deepest path in the library: printOn: runs Stream,
 *  WriteStream, String, Symbol, Character and -- for a Float -- LargeInteger
 *  division, all at once.  Everything that used to be listed here as not
 *  working now is here as an assertion instead.
 */
static void
test_printing_deep(void)
{
    check_integer("3.5 printString size", 3);
    /*  18, for the reason given at "2.0 ln printString" above.  */
    check_integer("2.0 sqrt printString size", 18);
    /*
     *  A Float survives its own printed form -- the whole point of printing
     *  enough digits, and it took three fixes to hold: the printer (too few
     *  digits), the reader (rounded twice), and Fraction>>asFloat (converted
     *  numerator and denominator before dividing, so anything above 2^53 was
     *  already wrong).  Any one of them alone leaves this false.
     *
     *  1/11 rather than something tidier because its shortest decimal needs
     *  seventeen digits and a numerator past 2^53, which is exactly where
     *  each of the three used to fail.
     */
    check_oop("| x | x := 1.0 / 11.0. ^x printString asNumber = x",
              ST_TRUE, "true");
    check_oop("(9090909090909091/100000000000000000) asFloat = (1.0/11.0)",
              ST_TRUE, "true");
    check_integer("OrderedCollection new printString size", 22);
    check_integer("(1 to: 5) asOrderedCollection printString size", 32);
    check_integer("(OrderedCollection new add: 1; yourself) printString size",
                  24);
    check_integer("OrderedCollection name size", 17);
}

/*
 *  Mixed-mode arithmetic, which the library does by coercing to the higher
 *  generality and retrying.  Both directions matter and only one of them used
 *  to work: a Float receiver with an Integer argument fell back through
 *  "super >= aNumber", and a binary message to super was not compiled as a
 *  super send at all.
 */
static void
test_mixed_arithmetic(void)
{
    check_oop("3.5 >= 0", ST_TRUE, "true");
    check_oop("3.5 <= 4", ST_TRUE, "true");
    check_oop("3 < 3.5",  ST_TRUE, "true");
    check_integer("(3 + 1.5) truncated", 4);
    check_integer("(3.5 + 1) truncated", 4);
    check_integer("3.5 floor", 3);
    check_integer("3.5 ceiling", 4);
}

/*
 *  ----------  Where the ink lands  ----------
 *
 *  check_ink counts pixels and never asks WHERE they are, and that blind
 *  spot has a shape: it cannot see anything drawn in the wrong PLACE, only
 *  something drawn in the wrong AMOUNT.  The caret spent this system's whole
 *  life fifteen pixels right of the insertion point -- `off by 1.5
 *  characters', as it was reported -- with an ink count identical to the
 *  correct one, so every suite here was green throughout.
 *
 *  What was missing is not more pixels checked but a different KIND of
 *  check: drawn output held against the model that produced it.  The image
 *  computes where the next character goes -- characterBlockForIndex: -- and
 *  then something draws a caret; the test is that those two agree.  Nothing
 *  here hard-codes a coordinate, so a new face or a new size moves both
 *  sides together and the assertion still means what it says.
 */
static void
ink_bounds(int top, int bottom, int *left, int *right)
{
    gfx_form    form;
    int         x, y;

    *left  = -1;
    *right = -1;
    if (!GFX_form_from_oop(GFX_display_form(), &form))
        return;
    if (bottom > form.height)
        bottom = form.height;
    for (y = top; y < bottom; ++y) {
        for (x = 0; x < form.width; ++x) {
            uint16_t    word = form.bits[(size_t) y * form.raster + (x >> 4)];

            if ((word >> (15 - (x & 15))) & 1) {
                if (*left < 0 || x < *left)  *left = x;
                if (x > *right)              *right = x;
            }
        }
    }
}

/*
 *  Draw a paragraph at a known place, show the caret for one index, and
 *  answer where the image SAYS that index is.  The caret is the only ink
 *  below the text, so its extent can be read off without disturbing
 *  anything.
 */
static void
check_caret_agrees(const char *text, int index, int origin_y)
{
    char    source[512];
    st_oop  answer;
    int     model_x, left, right, centre;

    snprintf(source, sizeof source,
             "| p b | Display white. p := '%s' asParagraph."
             " p displayOn: Display at: 100@%d."
             " b := p characterBlockForIndex: %d."
             " p displayCaretForBlock: b. ^b topLeft x",
             text, origin_y, index);
    answer = evaluate(source);
    ++st_test_checks;
    if (!OM_is_int(answer)) {
        ++st_test_failures;
        printf("  FAIL caret '%s' index %d: no answer\n", text, index);
        return;
    }
    model_x = (int) OM_int_value(answer);

    /*
     *  Below the baseline is the caret and nothing else: the glyphs stop at
     *  the baseline and the caret form's ink is in its top rows, drawn from
     *  there down.
     */
    ink_bounds(origin_y + 19, origin_y + 26, &left, &right);
    ++st_test_checks;
    if (left < 0) {
        ++st_test_failures;
        printf("  FAIL caret '%s' index %d: no caret drawn\n", text, index);
        return;
    }
    /*
     *  The caret is a small symmetric arrow, so its ink straddles the point
     *  it marks.  One pixel of slack, because an even-width tip cannot sit
     *  exactly on a coordinate.
     */
    centre = (left + right + 1) / 2;
    ++st_test_checks;
    if (centre < model_x - 1 || centre > model_x + 1) {
        ++st_test_failures;
        printf("  FAIL caret '%s' index %d: ink %d..%d (centre %d), but the "
               "image puts that character at x=%d\n",
               text, index, left, right, centre, model_x);
    }
}

static void
test_where_the_ink_lands(void)
{
    printf("---- drawn output against the model that produced it ----\n");

    /*
     *  The caret, at the three places it can be: before everything, between
     *  two characters, and after the last one -- which is where it lives
     *  while you type and where the fault was reported.
     */
    check_caret_agrees("HELLO", 1, 100);
    check_caret_agrees("HELLO", 3, 140);
    check_caret_agrees("HELLO", 6, 180);
    check_caret_agrees("WIlL", 5, 220);      /*  widths 21, 5, 5, 11  */

    /*
     *  And the other direction: a point inside a character has to name that
     *  character.  Drawing and hit-testing walk the same widths, so this is
     *  what catches them walking them differently -- a click landing on the
     *  wrong letter, which is the same fault as the caret wearing different
     *  clothes.
     */
    check_integer("| p | p := 'HELLO' asParagraph."
                  " ^(p characterBlockAtPoint:"
                  "    ((p characterBlockForIndex: 3) topLeft + (2@2))) stringIndex", 3);
    check_integer("| p | p := 'HELLO' asParagraph."
                  " ^(p characterBlockAtPoint:"
                  "    ((p characterBlockForIndex: 5) topLeft + (2@2))) stringIndex", 5);

    /*
     *  Form gray's phase, which the VM has to match.
     *
     *  Form class>>initializeMasks fills the odd rows of the 16x16 mask with
     *  21845 and the even ones with 43690, so row 0 is 0x5555 -- and BitBlt
     *  indexes a halftone by the destination row, so Display row 0 takes
     *  halftone row 0.  display.c paints the desktop background itself when
     *  it grows the screen to fill the window, and had the phase inverted:
     *  both are 50% grey with the same ink count, so the rectangle a window
     *  had occupied came back in the opposite phase and sat there as a patch
     *  of different texture.
     *
     *  This pins the image's half.  The VM's half cannot be reached from
     *  here -- that fill only runs when a real window grows the display --
     *  so display.c carries the reason beside the constant.
     */
    {
        gfx_form    form;

        evaluate("Display white."
                 " Display fill: (0@0 corner: 64@64) mask: Form gray. ^1");
        ++st_test_checks;
        if (!GFX_form_from_oop(GFX_display_form(), &form)) {
            ++st_test_failures;
            printf("  FAIL no display form\n");
        }  else if (form.bits[0] != 0x5555 || form.bits[form.raster] != 0xAAAA) {
            ++st_test_failures;
            printf("  FAIL Form gray row 0 is %04x and row 1 is %04x; "
                   "display.c paints 0x5555 then 0xAAAA\n",
                   form.bits[0], form.bits[form.raster]);
        }
    }

    /*
     *  A window is drawn where it was framed.  Three rounds of this session
     *  went looking for a framing fault that was not there, and nothing in
     *  the suite could have said so: check_ink counts a border without
     *  caring where it is.  This does.
     */
    {
        int left, right;

        evaluate("| v | Display white. v := StandardSystemView new."
                 " v label: 'W'. v window: (200@300 corner: 320@360)."
                 " v display. ^1");
        ink_bounds(300, 361, &left, &right);
        ++st_test_checks;
        /*
         *  The left edge is exact and the right is within the border's own
         *  width, which is the view's business and not this test's.  What
         *  is being asserted is that the window is drawn WHERE it was put.
         */
        if (left != 200 || right < 317 || right > 320) {
            ++st_test_failures;
            printf("  FAIL a view windowed to 200..320 drew from %d to %d\n",
                   left, right);
        }
    }

    /*
     *  A paragraph drawn at a point starts there.  The first glyph's ink may
     *  sit a pixel or two in -- that is its left side bearing -- but not a
     *  character and a half, which is what a bad offset looks like.
     */
    {
        int left, right;

        evaluate("Display white."
                 " 'HELLO' asParagraph displayOn: Display at: 100@100. ^1");
        ink_bounds(100, 120, &left, &right);
        ++st_test_checks;
        if (left < 100 || left > 104) {
            ++st_test_failures;
            printf("  FAIL text drawn at 100 starts its ink at %d\n", left);
        }
    }
}

/*
 *  Bugs2.md: a second outside-in audit, and what each of its findings looks
 *  like from in here.
 *
 *  The first audit's fixes are held by the checks above, scattered among
 *  the subjects they belong to.  These are kept together instead, because
 *  what they have in common is not a subject: every one of them is a case
 *  where the system answered something plausible rather than failing, and
 *  the reason to write them down in one place is that the NEXT such audit
 *  should be able to read what the last one found.
 */
static void
test_bugs2(void)
{
    int     saved = test_dialect;
    char    expression[1600];
    size_t  i;
    size_t  k;

    test_dialect = ST_DIALECT_CLOSURES;

    /*
     *  B1: a string literal is as long as it is written.
     *
     *  The lexer's token buffer was 256 bytes and the scanner consumed the
     *  rest of the literal without storing it, so every string literal past
     *  255 characters was silently truncated -- and four methods in the
     *  image as built were already carrying one.  Benchmark>>longishString
     *  is a 422-character quotation that stopped mid-word at `ssible
     *  example o'.
     */
    expression[0] = '\'';
    for (i = 1; i <= 300; ++i)
        expression[i] = 'a';
    expression[301] = '\'';
    snprintf(expression + 302, sizeof expression - 302, " size");
    check_integer(expression, 300);
    check_integer("Benchmark new longishString size", 422);

    /*  B3: and so is a symbol literal, which had a second limit at 127.  */
    expression[0] = '#';
    for (i = 1; i <= 300; ++i)
        expression[i] = 'b';
    snprintf(expression + 301, sizeof expression - 301, " size");
    check_integer(expression, 300);

    /*
     *  B2: a literal array holds every element written in it.
     *
     *  It stopped at 256, and the visible half of that was storeString:
     *  Array>>storeOn: writes the literal form when every element is a
     *  literal, so an Array of a thousand numbers stored itself into
     *  something that read back a quarter the size.
     */
    k = 0;
    expression[k++] = '#';
    expression[k++] = '(';
    for (i = 0; i < 300; ++i) {
        expression[k++] = '1';
        expression[k++] = ' ';
    }
    expression[k++] = ')';
    snprintf(expression + k, sizeof expression - k, " size");
    check_integer(expression, 300);
    check_integer("(Compiler evaluate: (1 to: 300) asArray storeString) size",
                  300);

    /*
     *  B4: a name too long to hold is refused, and the refusal says so.
     *
     *  It used to truncate the declaration and the use to 63 characters and
     *  then look the ASSIGNMENT up against the full text -- so the compiler
     *  reported `cannot assign to' for a name that was declared right there,
     *  which is the one thing that was not wrong with the code.
     */
    k = 0;
    expression[k++] = '|';
    expression[k++] = ' ';
    for (i = 0; i < 200; ++i)
        expression[k++] = 'c';
    snprintf(expression + k, sizeof expression - k, " | ^1");
    check_refused(expression, "characters long");

    /*
     *  B8: nil, true and false inside #( ) are the objects.
     *
     *  They were three Symbols, and `#nil printString' is 'nil', so nothing
     *  printed could tell you.  In the closure dialect only; the Blue Book
     *  keeps 1983's reading, which its own library depends on.
     */
    check_boolean("#(nil) first isNil", 1);
    check_boolean("#(true) first", 1);
    check_boolean("#(false) first not", 1);
    check_integer("#(1 nil 2) indexOf: nil", 2);
    check_boolean("#(nil) = (Array with: nil)", 1);
    check_integer("(#(1 nil 2) copyWithout: nil) size", 2);

    /*
     *  B5 and B6: = and hash agree across the sequenceable collections.
     *
     *  Array>>hash read the first and last element only, so a hundred
     *  arrays of the shape (0, i, 0) shared one hash and a Dictionary keyed
     *  by Arrays was a linear scan.  Everything else sequenceable had
     *  structural = and Object's identity hash, which a Set cannot hold:
     *  600 distinct one-element OrderedCollections deduplicated to 599, and
     *  which 599 depended on where the collector had put them.
     */
    check_boolean("(OrderedCollection with: 1) hash "
                  "= (OrderedCollection with: 1) hash", 1);
    check_boolean("#(1 2 3) hash = #(1 99 3) hash", 0);
    check_boolean("#(1 2 3) hash = (1 to: 3) hash", 1);
    check_integer("((1 to: 100) collect: [:i | (Array with: 0 with: i "
                  "with: 0) hash]) asSet size", 100);
    check_integer("| s | s := Set new. 1 to: 300 do: [:i | "
                  "s add: (OrderedCollection with: i)]. 1 to: 300 do: [:i | "
                  "s add: (OrderedCollection with: i)]. ^s size", 300);
    check_integer("| s | s := Set new. 1 to: 300 do: [:i | "
                  "s add: i printString asByteArray]. 1 to: 300 do: [:i | "
                  "s add: i printString asByteArray]. ^s size", 300);
    check_string("| d | d := Dictionary new. "
                 "d at: (OrderedCollection with: 1) put: 'v'. "
                 "^d at: (OrderedCollection with: 1) ifAbsent: ['MISS']",
                 "v");
    /*
     *  And LinkedList back OUT of it, in the same breath.  Semaphore is a
     *  LinkedList, its `elements' are the processes waiting on it at this
     *  instant, and `Semaphore new = Semaphore new' has answered true since
     *  1983 -- harmlessly, while the hash was Object's, because a Set could
     *  still hold both.  A structural hash would have made the two agree in
     *  the wrong direction and given a Semaphore a hash that moved every
     *  time a process waited on it.
     */
    check_boolean("Semaphore new = Semaphore new", 0);
    /*
     *  And identityHash, which is the same argument at the other end: a
     *  hash that collapses.  Primitive 75 refuses a SmallInteger -- it has
     *  no object pointer and needs none -- and the fallback answered 0, so
     *  every integer shared one identity hash.  1983's collections bucket
     *  by `hash' and never saw it; Pharo's IdentitySet and IdentityDictionary
     *  bucket by `identityHash \\ size', so in profiles/pharo-collections
     *  every integer key landed in bucket one.
     */
    check_integer("((1 to: 50) collect: [:i | i identityHash]) asSet size", 50);
    check_integer("3 identityHash", 3);
    check_boolean("| a b | a := 'abc' copy. b := 'abc' copy. "
                  "^(a hash = b hash) and: [a identityHash ~= b identityHash]",
                  1);
    check_integer("| a b | a := Semaphore new. b := Semaphore new. "
                  "^(Set with: a with: b) size", 2);

    /*  B7: and across Bag, which had neither = nor hash.  */
    check_boolean("(Bag withAll: #(1 1 2)) = (Bag withAll: #(1 1 2))", 1);
    check_boolean("(Bag withAll: #(1 1 2)) = (Bag withAll: #(1 2 2))", 0);
    check_boolean("(Bag withAll: #(1 1 2)) hash "
                  "= (Bag withAll: #(2 1 1)) hash", 1);
    check_string("[Bag new add: 3 withOccurrences: -2] "
                 "on: Error do: [:e | e messageText]",
                 "a Bag cannot hold an element -2 times");

    /*
     *  B9: a Set can hold nil.
     *
     *  `aSet add: nil' did nothing and answered its argument, so a caller
     *  checking the return value saw a successful add.  It reached
     *  Dictionary, which stores a nil key perfectly well and then could not
     *  enumerate it, because keys builds a Set.
     */
    check_integer("| s | s := Set new. s add: nil. ^s size", 1);
    check_integer("(Array with: 1 with: nil with: 2) asSet size", 3);
    check_integer("| d | d := Dictionary new. d at: nil put: 7. "
                  "^d keys size", 1);
    check_boolean("| s | s := Set new. s add: nil. ^s includes: nil", 1);
    check_integer("| s | s := Set new. s add: nil. s remove: nil. ^s size", 0);
    check_integer("| s | s := IdentitySet new. s add: nil; add: nil; add: 3. "
                  "^s size", 2);

    /*
     *  B10: dividing by zero is a ZeroDivide whatever the receiver is.
     *
     *  It was raised for a SmallInteger and nowhere else, so the two cases
     *  that fire on computed data rather than on literals -- every Float,
     *  and every Integer past 2^62 -- defeated the handler a careful caller
     *  had written.
     */
    check_string("[1/0] on: ZeroDivide do: [:e | 'caught']", "caught");
    check_string("[1.0/0] on: ZeroDivide do: [:e | 'caught']", "caught");
    check_string("[1.0/0.0] on: ZeroDivide do: [:e | 'caught']", "caught");
    check_string("[(1/2)/0] on: ZeroDivide do: [:e | 'caught']", "caught");
    check_string("[(2 raisedTo: 70)//0] on: ZeroDivide do: [:e | 'caught']",
                 "caught");
    check_string("[(2 raisedTo: 70)\\\\0] on: ZeroDivide do: [:e | 'caught']",
                 "caught");
    check_string("[0 reciprocal] on: ZeroDivide do: [:e | 'caught']", "caught");
    check_string("[0.0 reciprocal] on: ZeroDivide do: [:e | 'caught']",
                 "caught");

    /*
     *  B11: the five methods that opened a window signal instead when there
     *  is nothing to open one on.
     *
     *  A headless image has no screen and nobody to click, so each of them
     *  parked its process on the input semaphore for ever -- and `[self
     *  halt] on: Error do:' did not catch it, because nothing was
     *  signalled.  halt is the single most likely thing to be left in code
     *  by accident.
     */
    check_integer("[nil halt] on: Warning do: [:e | 42]", 42);
    check_integer("[nil halt: 'x'] on: Warning do: [:e | 43]", 43);
    check_boolean("[nil notify: 'x'] on: Warning do: [:e | e resume: nil]", 1);
    check_boolean("nil confirm: 'ok?'", 0);
    check_boolean("[nil confirm: 'ok?'] on: Warning do: [:e | e resume: true]",
                  1);
    check_integer("3 inspect. ^7", 7);

    /*
     *  B12: a method that does not stop calling itself is an Error, not the
     *  end of the process.
     *
     *  It reached about five million frames, 12 GB and `out of memory
     *  activating a method' -- every worker, every open connection and every
     *  request in flight.  Nothing was signalled on the way.
     */
    check_integer("[[:b | b value: b] value: [:b | b value: b]] "
                  "on: RecursionDepthExceeded do: [:e | 99]", 99);
    /*
     *  And a stack that is merely DEEP still works, which is the other half
     *  of the same claim: an exception 150,000 frames down is delivered.
     *  It was not -- context_is_live gave up walking after 100,000 hops and
     *  answered `that context has already returned', so the handler's
     *  return signalled a fresh error from the same depth, which did the
     *  same thing.  A loop with no output at all.
     */
    check_string("[[:blk :n | n = 0 ifTrue: [Error new signal: 'deep'] "
                 "ifFalse: [blk value: blk value: n - 1]] "
                 "value: [:blk :n | n = 0 ifTrue: [Error new signal: 'deep'] "
                 "ifFalse: [blk value: blk value: n - 1]] value: 150000] "
                 "on: Error do: [:e | 'caught']", "caught");

    /*
     *  B14: a class answers its comment.
     *
     *  Zero of 373 did.  Both readers handed the text to the bootstrap and
     *  the bootstrap dropped it; it is filed into the sources beside the
     *  methods now, which is where a shipped 1983 image keeps it.
     */
    check_string("Set comment",
                 "I am an unordered collection of elements that are not "
                 "duplicated in me.");
    check_boolean("Object comment isEmpty", 0);
    check_boolean("CollectionElement comment isEmpty", 0);

    /*
     *  B15: a Time is a time of day at both ends.
     *
     *  fromSeconds: did not reduce and printOn: subtracted twelve rather
     *  than taking the remainder, so hour 24 printed as noon and hour 25 as
     *  the impossible `13 pm'.
     */
    check_string("(Time fromSeconds: 86400) printString", "12:00:00 am");
    check_string("(Time fromSeconds: 90000) printString", "1:00:00 am");
    check_string("(Time fromSeconds: -1) printString", "11:59:59 pm");
    check_string("(Time fromSeconds: 0) printString", "12:00:00 am");
    check_string("(Time fromSeconds: 43200) printString", "12:00:00 pm");
    /*  200 consecutive seconds used to produce 60 distinct hashes.  */
    check_integer("((1 to: 200) collect: [:i | (Time fromSeconds: i) hash]) "
                  "asSet size", 200);

    /*  B16: the Blue Book protocol the audit found missing.  */
    check_integer("(OrderedCollection withAll: #(1 2 3 4 5)) removeFirst: 2; "
                  "yourself; size", 3);
    check_string("((OrderedCollection withAll: #(1 2 3 4 5)) removeLast: 2) "
                 "printString", "an OrderedCollection(4 5 )");
    check_string("| c | c := OrderedCollection withAll: #(1 2 3). "
                 "c add: 99 afterIndex: 2. ^c printString",
                 "an OrderedCollection(1 2 99 3 )");
    check_string("'  hi  ' trimSeparators", "hi");
    check_string("'hello' copy replaceAll: $l with: $L", "heLLo");
    check_string("((ReadStream on: #(1 2 3)) collect: [:x | x * 2]) upToEnd "
                 "printString", "(2 4 6 )");

    /*
     *  B17: the smaller things.  A NaN prints as one spelling on every
     *  machine; a String that is not a number answers nil rather than 0,
     *  which cannot be told from a parse of '0'; and skip: past the end
     *  clamps rather than signalling.
     */
    check_string("Float nan printString", "nan");
    /*
     *  And raisedTo: with a non-integer exponent, which 1983 computes as
     *  `(exponent * self ln) exp' -- two transcendental functions where the
     *  machine has one.  An INTEGER exponent still keeps the receiver's
     *  kind, which is the part that must not change.
     */
    check_string("(10 raisedTo: 2.0) printString", "100.0");
    check_boolean("(2 raisedTo: 0.5) = 2 sqrt", 1);
    check_integer("2 raisedTo: 10", 1024);
    check_string("((1/2) raisedTo: 2) printString", "(1/4)");
    check_boolean("'abc' asNumber isNil", 1);
    check_integer("'12abc' asNumber", 12);
    check_integer("'0' asNumber", 0);
    check_boolean("(ReadStream on: #(1 2 3)) skip: 10; atEnd", 1);
    check_boolean("| s | s := ReadStream on: #(1 2 3). s skip: -10. "
                  "^s position = 0", 1);

    test_dialect = saved;
}
/*
 *  Bugs3.md: a third audit -- the system at its limits, under load, and
 *  from outside.  One function per area, in the order the report keeps
 *  them, so that the next audit can read what this one found.
 */

/*
 *  Bugs3.md: the interpreter and its primitives.
 */
/*
 *  The interpreter's frame counter after an expression, compared with the
 *  counter after an expression known to leak nothing.  The counter is read
 *  at the moment the doIt returns off the bottom, before anything resets
 *  it, so what it holds is the doIt's own frame plus whatever the
 *  expression left behind -- which is the B15 leak, one frame per
 *  non-local return, made visible without the 200,000 iterations it took
 *  to reach the ceiling.
 */
static void
check_depth_unchanged(const char *expression, int baseline)
{
    st_oop  value = evaluate(expression);

    ++st_test_checks;
    if (value == ST_OOP_INVALID) {
        ++st_test_failures;
        printf("  FAIL %s: no answer\n", expression);
        return;
    }
    if (st_vm.call_depth != baseline) {
        ++st_test_failures;
        printf("  FAIL %s: left the depth counter at %d, the baseline is "
               "%d\n", expression, st_vm.call_depth, baseline);
    }
}

static void
test_bugs3_interp(void)
{
    int     saved = test_dialect;
    int     baseline;
    char    expression[1600];
    char    ro_path[64];
    int     ro_fd;

    test_dialect = ST_DIALECT_CLOSURES;

    /*
     *  B1: an Array too long for the calling frame fails the primitive
     *  and the fallback says so.
     *
     *  perform:withArguments:, valueWithArguments: and
     *  withArgs:executeMethod: spread the Array onto the SENDER's stack,
     *  sized by the compiler for the sender's own pushes; twelve elements
     *  overflowed the frame and stopped the run, eighteen segfaulted.  The
     *  sends here are made from inside a block so that the frame is a
     *  small one -- the doIt's own context is 64 slots and takes eighteen
     *  in its stride, which is also checked.
     */
    check_string("Object subclass: #Bugs3CM instanceVariableNames: ''"
                 " classVariableNames: '' poolDictionaries: ''"
                 " category: 'Bugs3'."
                 " (Smalltalk at: #Bugs3CM) compile: 'a1: a a2: b a3: c a4: d"
                 " a5: e a6: f a7: g a8: h a9: i a10: j a11: k a12: l a13: m"
                 " a14: n a15: o a16: p a17: q a18: r a19: s a20: t a21: u"
                 " a22: v a23: w a24: x a25: y a26: z a27: aa a28: bb a29: cc"
                 " a30: dd ^dd'."
                 " (Smalltalk at: #Bugs3CM) compile: 'wide ^[:a :b :c :d :e :f"
                 " :g :h :i :j :k :l :m :n :o | a]'."
                 " (Smalltalk at: #Bugs3CM) compile: 'bar ^self wide"
                 " valueWithArguments: (Array new: 15)'."
                 " ^((Smalltalk at: #Bugs3CM) compiledMethodAt: "
                 "#a1:a2:a3:a4:a5:a6:a7:a8:a9:a10:a11:a12:a13:a14:a15:a16:a17:a18:a19:a20:a21:a22:a23:a24:a25:a26:a27:a28:a29:a30:) numArgs printString", "30");
    check_string("[nil perform: #a1:a2:a3:a4:a5:a6:a7:a8:a9:a10:a11:a12:a13:a14:a15:a16:a17:a18:a19:a20:a21:a22:a23:a24:a25:a26:a27:a28:a29:a30:"
                 " withArguments: (Array new: 30)]"
                 " on: Error do: [:e | e messageText]",
                 "perform:withArguments: cannot spread 30 arguments: the "
                 "calling method's frame has no room for them");
    /*
     *  A block's arguments count toward the frame of the method that
     *  DEFINES it -- a closure's context is sized from its home method's
     *  header -- so the fifteen-argument block is made in one method and
     *  sent valueWithArguments: from another, whose frame is the small
     *  one.  Sent from this doIt, whose frame is 64 slots, fifteen fit.
     */
    check_string("[(Smalltalk at: #Bugs3CM) new bar]"
                 " on: Error do: [:e | e messageText]",
                 "valueWithArguments: cannot spread 15 arguments: the "
                 "calling method's frame has no room for them");
    check_integer("[:a :b :c :d :e :f :g :h :i :j :k :l :m :n :o | o]"
                  " valueWithArguments: (1 to: 15) asArray", 15);
    check_string("[(Smalltalk at: #Bugs3CM) new withArgs: (Array new: 30)"
                 " executeMethod: ((Smalltalk at: #Bugs3CM) compiledMethodAt:"
                 " #a1:a2:a3:a4:a5:a6:a7:a8:a9:a10:a11:a12:a13:a14:a15:a16:a17:a18:a19:a20:a21:a22:a23:a24:a25:a26:a27:a28:a29:a30:)]"
                 " on: Error do: [:e | e messageText]",
                 "withArgs:executeMethod: cannot spread 30 arguments: the "
                 "calling method's frame has no room for them");
    check_string("[nil perform: #a1:a2:a3: withArguments: (Array new: 3)]"
                 " on: MessageNotUnderstood do: [:e | e message selector]",
                 "a1:a2:a3:");
    /*  And with room -- this doIt's frame has 64 slots -- thirty fit.  */
    check_integer("(Smalltalk at: #Bugs3CM) new perform: #a1:a2:a3:a4:a5:a6:a7:a8:a9:a10:a11:a12:a13:a14:a15:a16:a17:a18:a19:a20:a21:a22:a23:a24:a25:a26:a27:a28:a29:a30:"
                  " withArguments: (1 to: 30) asArray", 30);
    check_integer("(Smalltalk at: #Bugs3CM) new withArgs: (1 to: 30) asArray"
                  " executeMethod: ((Smalltalk at: #Bugs3CM) compiledMethodAt:"
                  " #a1:a2:a3:a4:a5:a6:a7:a8:a9:a10:a11:a12:a13:a14:a15:a16:a17:a18:a19:a20:a21:a22:a23:a24:a25:a26:a27:a28:a29:a30:)", 30);

    /*
     *  B5: perform: with the wrong number of arguments is an error in the
     *  image, not the end of it.  Primitives 83 and 84 fail on the
     *  mismatch, as the Blue Book's primitivePerform does, and the fallback
     *  names both counts.  A selector nobody implements is not a mismatch:
     *  doesNotUnderstand: takes the arguments however many there are.
     */
    check_string("[3 perform: #+] on: Error do: [:e | e messageText]",
                 "perform: + with 0 arguments; it takes 1");
    check_string("[3 perform: #at:put: with: 1] on: Error do:"
                 " [:e | e messageText]",
                 "perform: at:put: with 1 arguments; it takes 2");
    check_string("[3 perform: #== with: 3 with: 4] on: Error do:"
                 " [:e | e messageText]",
                 "perform: == with 2 arguments; it takes 1");
    check_integer("3 perform: #+ with: 4", 7);
    check_boolean("3 perform: #between:and: withArguments: #(1 5)", 1);
    check_string("[3 perform: #zork with: 1 with: 2]"
                 " on: MessageNotUnderstood do:"
                 " [:e | e message arguments size printString]", "2");

    /*
     *  B6: a cycle in the superclass chain is refused where it is made,
     *  and one made behind the library's back -- through instVarAt:put:
     *  -- is a doesNotUnderstand rather than a worker spinning in C for
     *  ever.  The chain is put back afterwards: two classes going round
     *  would be found by any later walk of the class tree.
     */
    check_string("Object subclass: #Bugs3CA instanceVariableNames: ''"
                 " classVariableNames: '' poolDictionaries: ''"
                 " category: 'Bugs3'."
                 " Object subclass: #Bugs3CB instanceVariableNames: ''"
                 " classVariableNames: '' poolDictionaries: ''"
                 " category: 'Bugs3'."
                 " (Smalltalk at: #Bugs3CA) superclass: (Smalltalk at: #Bugs3CB)."
                 " ^[(Smalltalk at: #Bugs3CB) superclass: (Smalltalk at: #Bugs3CA)]"
                 " on: Error do: [:e | e messageText]",
                 "Bugs3CA inherits from Bugs3CB, so making it the superclass"
                 " would put a cycle in the class chain");
    check_string("[(Smalltalk at: #Bugs3CA) superclass: (Smalltalk at: #Bugs3CA)]"
                 " on: Error do: [:e | e messageText]",
                 "Bugs3CA inherits from Bugs3CA, so making it the superclass"
                 " would put a cycle in the class chain");
    check_string("(Smalltalk at: #Bugs3CA) superclass name", "Bugs3CB");
    check_string("[:ca :cb | | answer |"
                 " ca instVarAt: 1 put: cb. cb instVarAt: 1 put: ca."
                 " answer := [ca new zork] on: MessageNotUnderstood do:"
                 " [:e | e message selector]."
                 " ca instVarAt: 1 put: Object. cb instVarAt: 1 put: Object."
                 " answer] value: (Smalltalk at: #Bugs3CA)"
                 " value: (Smalltalk at: #Bugs3CB)", "zork");
    check_string("(Smalltalk at: #Bugs3CA) superclass name", "Object");

    /*
     *  B7: a receiver whose class chain has no doesNotUnderstand: gets a
     *  MessageNotUnderstood like everything else, through Object's
     *  handler sent to it directly, and cannotInterpret: is there for a
     *  chain that has lost doesNotUnderstand: but still reaches Object.
     *  Before, `Behavior new new printString' cleared the interpreter's
     *  running flag, and under -serve that is the pool.
     */
    check_string("[Behavior new new printString]"
                 " on: MessageNotUnderstood do: [:e | e message selector]",
                 "printString");
    check_string("[Behavior new new foo: 1 bar: 2]"
                 " on: MessageNotUnderstood do:"
                 " [:e | e message arguments size printString]", "2");
    check_string("[3 cannotInterpret:"
                 " (Message selector: #zork arguments: #())]"
                 " on: MessageNotUnderstood do: [:e | e message selector]",
                 "zork");
    check_boolean("(Smalltalk at: #Object) includesSelector:"
                  " #cannotInterpret:", 1);

    /*
     *  B11: a method whose bytes have been rewritten, and a context whose
     *  registers have been, are abandoned with a CorruptMethod raised in
     *  the image -- where each was a segfault.  255 is `send literal 15
     *  with two arguments' and foo has one literal; 0 is `push the first
     *  instance variable' to the end of the method with no return, which
     *  runs off the end -- and, before it does, names an instance
     *  variable the receiver has not got, which ASAN caught reading the
     *  word after the object; the index is bounded now like the literal.
     *  The context cases write the ip and then the sp of a frame that is
     *  about to be returned to.
     */
    check_string("(Smalltalk at: #Bugs3CM) compile: 'foo ^3'."
                 " [:m | m initialPC to: m size do: [:i | m at: i put: 255]]"
                 " value: ((Smalltalk at: #Bugs3CM) compiledMethodAt: #foo)."
                 " ^[(Smalltalk at: #Bugs3CM) new foo]"
                 " on: CorruptMethod do: [:e | e class name]",
                 "CorruptMethod");
    check_string("[:m | m initialPC to: m size do: [:i | m at: i put: 0]]"
                 " value: ((Smalltalk at: #Bugs3CM) compiledMethodAt: #foo)."
                 " ^[(Smalltalk at: #Bugs3CM) new foo]"
                 " on: Error do: [:e | e class name]",
                 "CorruptMethod");
    check_integer("[[thisContext sender instVarAt: 2 put: -1. 3] value]"
                  " on: CorruptMethod do: [:e | 7]", 7);
    check_integer("[[thisContext sender instVarAt: 2 put: 1. 3] value]"
                  " on: CorruptMethod do: [:e | 7]", 7);
    check_integer("[[thisContext sender instVarAt: 3 put: -1. 3] value]"
                  " on: CorruptMethod do: [:e | 7]", 7);
    check_integer("[[thisContext sender instVarAt: 3 put: 100000. 3] value]"
                  " on: CorruptMethod do: [:e | 7]", 7);
    check_boolean("CorruptMethod new isResumable", 0);

    /*
     *  B15: a non-local return takes every frame it discards off the
     *  depth counter, not one.  The counter is read straight after each
     *  expression and held to what an expression with no non-local return
     *  leaves; then the loop that used to raise RecursionDepthExceeded at
     *  200,000 iterations -- the Blue Book block arm through includes:,
     *  the closure arm through detect: -- is run past that number.
     */
    evaluate("3 + 4");
    baseline = st_vm.call_depth;
    check_depth_unchanged("3 + 4", baseline);
    check_depth_unchanged("1 to: 1000 do: [:i | #(1 2 3) includes: 2]. ^3 + 4",
                          baseline);
    check_depth_unchanged("1 to: 1000 do: [:i | #(1 2 3) detect:"
                          " [:x | x = 2]]. ^3 + 4", baseline);
    check_depth_unchanged("1 to: 1000 do: [:i | 1 isKindOf: Integer]. ^3 + 4",
                          baseline);
    check_depth_unchanged("1 to: 300 do: [:i | 1 + 1.0]. ^3 + 4", baseline);
    check_depth_unchanged("1 to: 300 do: [:i | [:k | #(1 2 3) do:"
                          " [:x | x = k ifTrue: [^x]]. nil] value: 2]. 3 + 4",
                          baseline);
    check_integer("1 to: 205000 do: [:i | #(1 2 3) includes: 2]. ^7", 7);
    check_integer("1 to: 205000 do: [:i | #(1 2 3) detect: [:x | x = 2]]. ^7",
                  7);

    /*
     *  B19: the whole line, in one write.  Interleaving needs eight
     *  workers and is checked in test_serve_faults; here only that the
     *  primitive still answers the receiver and still prints.
     */
    check_string("'bugs3-b19 one line' displayNl", "bugs3-b19 one line");

    /*
     *  B21: become: refuses a Symbol on either side.  `#zzE become: #zzF'
     *  left two Symbols spelled zzE, and the literal was not the interned
     *  one.
     */
    check_string("[#zzE become: #zzF] on: Error do: [:e | e messageText]",
                 "become: refused: a Symbol names itself for the whole "
                 "image's life; the symbol table and every method dictionary"
                 " are keyed by its identity");
    check_string("[String new become: #zzF] on: Error do:"
                 " [:e | e messageText]",
                 "become: refused: a Symbol names itself for the whole "
                 "image's life; the symbol table and every method dictionary"
                 " are keyed by its identity");
    check_boolean("'zzE' asSymbol == #zzE", 1);
    check_boolean("'zzF' asSymbol == #zzF", 1);
    check_integer("(Symbol allInstances select: [:s | s = 'zzE']) size", 1);

    /*
     *  B58: Smalltalk garbageCollect answers the count its comment has
     *  always promised, not the receiver.
     */
    check_boolean("Smalltalk garbageCollect isInteger", 1);
    check_boolean("Smalltalk garbageCollect >= 0", 1);

    /*
     *  B58: a file this process may only read opens read-only and the
     *  stream KNOWS: close does not flush, a write is refused where it is
     *  made, and reading works.  A file that cannot be opened at all says
     *  why in the operating system's words, and asking for an old file
     *  that is not there does not create it.  Root may write anything, so
     *  the read-only half is skipped there.
     */
    snprintf(ro_path, sizeof ro_path, "/tmp/bugs3-ro-XXXXXX");
    ro_fd = mkstemp(ro_path);
    if (ro_fd >= 0) {
        if (write(ro_fd, "ro\n", 3) != 3)
            printf("  (could not write %s)\n", ro_path);
        close(ro_fd);
        chmod(ro_path, 0444);
        if (geteuid() != 0) {
            snprintf(expression, sizeof expression,
                     "[(FileStream oldFileNamed: '%s') close. 'closed']"
                     " on: Error do: [:e | e messageText]", ro_path);
            check_string(expression, "closed");
            snprintf(expression, sizeof expression,
                     "[(FileStream oldFileNamed: '%s') nextPutAll: 'x'; close."
                     " 'wrote'] on: Error do: [:e | e messageText]", ro_path);
            check_string(expression, "no writing allowed!");
            snprintf(expression, sizeof expression,
                     "(FileStream oldFileNamed: '%s') contentsOfEntireFile"
                     " size", ro_path);
            check_integer(expression, 3);
            snprintf(expression, sizeof expression,
                     "[:f | | answer | f open. answer := f isReadOnly."
                     " f close. answer] value: (Disk findKey: '%s')", ro_path);
            check_boolean(expression, 1);
        }
        unlink(ro_path);
    }
    check_string("[FileStream oldFileNamed: '/nonexistent/bugs3-zz']"
                 " on: Error do: [:e | e messageText]",
                 "/nonexistent/bugs3-zz file not found, No such file or "
                 "directory");
    check_string("[FileStream oldFileNamed: '/tmp/bugs3-not-here-either']"
                 " on: Error do: [:e | e messageText]",
                 "/tmp/bugs3-not-here-either file not found, No such file "
                 "or directory");
    ++st_test_checks;
    if (access("/tmp/bugs3-not-here-either", F_OK) == 0) {
        ++st_test_failures;
        printf("  FAIL oldFileNamed: created the file it was asked for\n");
        unlink("/tmp/bugs3-not-here-either");
    }
    check_boolean("[:f | | answer | f open. answer := f isReadOnly. f close."
                  " answer] value: (Disk findKey: 'profiles/st2026.profile')",
                  0);

    test_dialect = saved;
}

/*
 *  Bugs3.md: the scheduler and the process protocol.
 *
 *  One worker here, so these hold the single-process half of each fix;
 *  the cross-worker half -- a process running on another core being
 *  terminated, and a snapshot taken while one is -- is in
 *  tests/unit/test_parallel_shared.c and tests/run_snapshot.sh.
 */
static void
test_bugs3_sched(void)
{
    int saved = test_dialect;

    test_dialect = ST_DIALECT_CLOSURES;

    /*
     *  B3: resuming a terminated process raises; it used to stop the
     *  image.  Primitive 87 queued any Process, and a terminated one has
     *  a nil suspendedContext, so the next worker to take it asked to run
     *  nil as a context and the whole run ended.  The primitive now
     *  refuses a process whose context is not a context, or that is on a
     *  list, and Process>>resume says which.  The process here never ran
     *  -- forked at the same priority, it waits on the ready list until
     *  the doIt waits, and the doIt never does -- which is the ready-list
     *  branch of terminate; the running branch is in the parallel gate.
     */
    check_string("| p | p := [Semaphore new wait] fork. p terminate. "
                 "^[p resume. #resumed] on: Error do: [:e | e messageText]",
                 "cannot resume a process that has terminated");
    check_boolean("| p | p := [Semaphore new wait] fork. p terminate. "
                  "^p suspendedContext isNil", 1);
    /*  And a process already waiting is refused too, not queued twice. */
    check_string("| p | p := [Semaphore new wait] fork. "
                 "^[p resume. #resumed] on: Error do: [:e | e messageText]",
                 "cannot resume a process that is already waiting");

    /*
     *  B17: `Processor activeProcess resume' put the running process on
     *  its ready list while this worker was still executing it, and an
     *  idle worker ran it from its stale context.  Refused now.
     */
    check_string("^[Processor activeProcess resume. #resumed] "
                 "on: Error do: [:e | e messageText]",
                 "cannot resume a process that is running");
    check_integer("[Processor activeProcess resume] on: Error do: [:e | nil]. "
                  "^3 + 4", 7);

    /*
     *  B13: terminate runs the ensure: blocks the process is inside.
     *  1983's ran none, so a process terminated inside `aMutex critical:'
     *  left the Mutex held for ever.  Processor yield runs the forked
     *  process up to its wait and comes back; terminate then unwinds its
     *  parked stack on THIS process, which is where the ensure: block's
     *  side effect is checked.
     */
    check_string("| log sem p | log := OrderedCollection new. sem := Semaphore new. "
                 "p := [[sem wait. log add: #after] ensure: [log add: #ensure]] fork. "
                 "Processor yield. p terminate. ^log asArray printString",
                 "(ensure )");
    check_boolean("| m sem p | m := Mutex new. sem := Semaphore new. "
                  "p := [m critical: [sem wait]] fork. Processor yield. "
                  "p terminate. ^m isHeld", 0);
    check_string("| m sem p | m := Mutex new. sem := Semaphore new. "
                 "p := [m critical: [sem wait]] fork. Processor yield. "
                 "p terminate. ^m critical: [#acquired]", "acquired");
    /*  Nested ensure: blocks run innermost first, each once.  */
    check_string("| log sem p | log := OrderedCollection new. sem := Semaphore new. "
                 "p := [[[sem wait] ensure: [log add: #inner]] ensure: [log add: #outer]] fork. "
                 "Processor yield. p terminate. p terminate. ^log asArray printString",
                 "(inner outer )");
    /*  A process that ends on its own is terminated the same way.  */
    check_boolean("| p | p := [3 + 4] fork. Processor yield. "
                  "^p suspendedContext isNil", 1);
    /*
     *  suspend of a process on a Semaphore is still refused, as 1983
     *  refused it, but of a ready process it works and resume restarts
     *  it where it stood.
     */
    check_string("| sem p | sem := Semaphore new. p := [sem wait] fork. Processor yield. "
                 "^[p suspend. #suspended] on: Error do: [:e | e messageText]",
                 "This process is waiting on a Semaphore and cannot be suspended");
    check_string("| log p | log := OrderedCollection new. "
                 "p := [log add: #ran] fork. p suspend. Processor yield. "
                 "log add: #between. p resume. Processor yield. ^log asArray printString",
                 "(between ran )");
    /*  signalException: reaches a process parked on a Semaphore.  */
    check_string("| sem p log | sem := Semaphore new. log := OrderedCollection new. "
                 "p := [[sem wait] on: Error do: [:e | log add: e messageText]] fork. "
                 "Processor yield. p signalException: (Error new messageText: 'cut short'). "
                 "Processor yield. ^log asArray printString",
                 "('cut short' )");

    /*
     *  B12: a Delay the timer could not arm poisoned every later Delay.
     *  `Delay forMilliseconds: 0.5' raised digitLength inside wait with
     *  ActiveDelay already pointing at it, and the next Delay queued
     *  behind a delay that would never fire.  Durations are checked when
     *  the Delay is made: rounded to the millisecond, floored at zero,
     *  refused beyond what the timer counts -- and the second wait here
     *  is the check that the first did no harm.
     */
    check_integer("(Delay forMilliseconds: 0.5) instVarAt: 1", 1);
    check_integer("(Delay forMilliseconds: 0.4) instVarAt: 1", 0);
    check_integer("(Delay forMilliseconds: -7) instVarAt: 1", 0);
    check_integer("(Delay forSeconds: 1.5) instVarAt: 1", 1500);
    check_boolean("(Delay forMilliseconds: 0.5) wait. (Delay forMilliseconds: 5) wait. ^true", 1);
    check_string("^[Delay forSeconds: 1e12] on: Error do: [:e | e messageText]",
                 "a Delay of 1000000000000000 milliseconds is longer than "
                 "the timer can count (536870911)");
    check_string("^[Delay forMilliseconds: 'soon'] on: Error do: [:e | e messageText]",
                 "a Delay is made from a number of milliseconds, not 'soon'");
    check_string("^[Delay untilMilliseconds: -1] on: Error do: [:e | e messageText]",
                 "-1 is not a value of the millisecond clock");
    check_integer("Delay maximumMilliseconds", 536870911);

    /*
     *  B60: Monitor is reentrant, as doc/CONCURRENCY.md and the manual
     *  say, and was not -- `critical:' was a Mutex's, which refuses a
     *  second acquire by its holder.  An owner and a depth now; and
     *  waitForChange gives up the whole depth, or a waiter two levels in
     *  would sleep holding the monitor and nothing could ever change.
     */
    check_string("| m | m := Monitor new. ^m critical: [m critical: [#reentered]]",
                 "reentered");
    check_boolean("| m | m := Monitor new. m critical: [m critical: [nil]]. "
                  "^m isHeldByActiveProcess", 0);
    check_boolean("| m | m := Monitor new. "
                  "^m critical: [m critical: [m isHeldByActiveProcess]]", 1);
    check_string("| m log p | m := Monitor new. log := OrderedCollection new. "
                 "p := [m critical: [m critical: [m waitUntil: [log includes: #go]. "
                 "log add: #woken]]] fork. Processor yield. "
                 "m critical: [log add: #go. m signalAll]. Processor yield. "
                 "^log asArray printString", "(go woken )");
    /*  The outermost exit releases, however it happens.  */
    check_string("| m | m := Monitor new. "
                 "[m critical: [m critical: [Error new signal: 'out']]] on: Error do: [:e | nil]. "
                 "^m critical: [#free]", "free");

    test_dialect = saved;
}

/*
 *  A whole method source this compiler must REFUSE, pattern and all, and
 *  the words it must refuse it in.  check_refused writes "doIt " in front
 *  of its expression, which cannot spell a method with arguments -- and
 *  half of what Bugs3 found wrong with the compiler is about arguments.
 */
static void
check_refused_method(const char *source, const char *wanted)
{
    st_compile_context  ctx;
    st_compile_result   res;

    fill_compile_context(&ctx);
    memset(&res, 0, sizeof res);
    ++st_test_checks;
    if (COMPILE_method(source, &ctx, &res) == 0) {
        ++st_test_failures;
        printf("  FAIL '%.60s' compiled; it should have been refused with "
               "'%s'\n", source, wanted);
        return;
    }
    if (!strstr(res.error, wanted)) {
        ++st_test_failures;
        printf("  FAIL refused with '%s', want something holding '%s'\n",
               res.error, wanted);
    }
}

/*
 *  And one that must compile, when the point is that it does.  A method
 *  of more than four arguments carries a header extension and so needs
 *  its class as its last literal, which fill_compile_context does not
 *  supply; Object's binding serves.
 */
static void
check_compiles_method(const char *source)
{
    st_compile_context  ctx;
    st_compile_result   res;

    fill_compile_context(&ctx);
    ctx.method_class_association = BOOT_lookup_global("Object", NULL);
    memset(&res, 0, sizeof res);
    ++st_test_checks;
    if (COMPILE_method(source, &ctx, &res) != 0) {
        ++st_test_failures;
        printf("  FAIL '%.60s' was refused: %s\n", source, res.error);
    }
}

/*
 *  Bugs3.md: the C compiler and lexer.
 *
 *  The thread through these is the one the audit named: the bytecode
 *  format has fields of four, five, six and seven bits, and the compiler
 *  masked every count down to fit rather than refusing what did not --
 *  so a method with 32 temporaries, a class with 65 instance variables and
 *  a block with 16 arguments all installed and all answered wrongly.  The
 *  rest are the lexer reading a number, a character or a NUL as something
 *  other than what was written, and the parser accepting what 1983's
 *  refused.  Every check here is a refusal that used to be an answer, or
 *  an answer that used to be a different one.
 */
static void
test_bugs3_compiler(void)
{
    int     saved = test_dialect;
    char    expression[2000];
    size_t  i;
    size_t  k;

    test_dialect = ST_DIALECT_CLOSURES;

    /*
     *  B2: nesting is bounded by the compiler, not by the C stack.
     *
     *  Three thousand parentheses, or ten thousand nested literal arrays,
     *  recursed the parser off the end of the thread's stack and the
     *  server died of SIGSEGV.  Three hundred is past MAX_NESTING and is
     *  refused by name; two hundred compiles and answers.
     */
    k = 0;
    for (i = 0; i < 300; ++i)
        expression[k++] = '(';
    expression[k++] = '1';
    for (i = 0; i < 300; ++i)
        expression[k++] = ')';
    expression[k] = '\0';
    check_refused(expression, "nested too deeply");
    k = 0;
    for (i = 0; i < 200; ++i)
        expression[k++] = '(';
    expression[k++] = '1';
    for (i = 0; i < 200; ++i)
        expression[k++] = ')';
    expression[k] = '\0';
    check_integer(expression, 1);
    k = 0;
    expression[k++] = '#';
    for (i = 0; i < 300; ++i)
        expression[k++] = '(';
    for (i = 0; i < 300; ++i)
        expression[k++] = ')';
    expression[k] = '\0';
    check_refused(expression, "nested too deeply");
    k = 0;
    expression[k++] = '#';
    for (i = 0; i < 200; ++i)
        expression[k++] = '(';
    for (i = 0; i < 200; ++i)
        expression[k++] = ')';
    snprintf(expression + k, sizeof expression - k, " size");
    check_integer(expression, 1);

    /*
     *  B22: 32 temporaries are refused, not wrapped to 0.
     *
     *  The header's temporary count is five bits and build_header masked
     *  it, so a 32-temporary method claimed none and its stack began on
     *  top of its first temporary; `^Array with: t1 with: t2' answered the
     *  class the expression had just pushed.  Thirty-one still compile,
     *  as they do in 1983.
     */
    k = (size_t) snprintf(expression, sizeof expression, "| ");
    for (i = 1; i <= 32; ++i)
        k += (size_t) snprintf(expression + k, sizeof expression - k,
                               "t%u ", (unsigned) i);
    snprintf(expression + k, sizeof expression - k, "| ^ 1");
    check_refused(expression, "at most 31");
    k = (size_t) snprintf(expression, sizeof expression, "| ");
    for (i = 1; i <= 31; ++i)
        k += (size_t) snprintf(expression + k, sizeof expression - k,
                               "t%u ", (unsigned) i);
    snprintf(expression + k, sizeof expression - k, "| t31 := 31. ^ t31");
    check_integer(expression, 31);
    /*
     *  And a frame slot past 63 is refused where it would be emitted: a
     *  closure block's own temporaries are numbered by the compiler, and
     *  the extended bytecodes hold six bits of index.
     */
    k = (size_t) snprintf(expression, sizeof expression, "[ | ");
    for (i = 1; i <= 70; ++i)
        k += (size_t) snprintf(expression + k, sizeof expression - k,
                               "b%u ", (unsigned) i);
    snprintf(expression + k, sizeof expression - k, "| b70 := 1. b70 ] value");
    check_refused(expression, "addresses 64");

    /*
     *  B23: the 65th instance variable is refused, not read as the first.
     *
     *  `index & 63' in the receiver-variable emitters made `v65 ^v65'
     *  push receiver variable 0.  The compiler refuses the reference by
     *  name, and the class-definition message refuses the class.
     */
    {
        st_compile_context  ctx;
        st_compile_result   res;
        char                names[70][8];
        const char         *pointers[70];

        for (i = 0; i < 70; ++i) {
            snprintf(names[i], sizeof names[i], "v%u", (unsigned) i + 1);
            pointers[i] = names[i];
        }
        fill_compile_context(&ctx);
        ctx.instance_variables      = pointers;
        ctx.instance_variable_count = 70;
        ++st_test_checks;
        if (COMPILE_method("foo ^v65", &ctx, &res) == 0
         || !strstr(res.error, "first 64")) {
            ++st_test_failures;
            printf("  FAIL 'foo ^v65' with 70 instance variables: %s\n",
                   res.error[0] ? res.error : "compiled");
        }
        ++st_test_checks;
        if (COMPILE_method("foo: x v65 := x", &ctx, &res) == 0
         || !strstr(res.error, "first 64")) {
            ++st_test_failures;
            printf("  FAIL 'foo: x v65 := x' with 70 instance variables: "
                   "%s\n", res.error[0] ? res.error : "compiled");
        }
        ++st_test_checks;
        if (COMPILE_method("foo ^v64", &ctx, &res) != 0) {
            ++st_test_failures;
            printf("  FAIL 'foo ^v64' was refused: %s\n", res.error);
        }
    }
    k = (size_t) snprintf(expression, sizeof expression,
                          "[Object subclass: #Iv70 instanceVariableNames: '");
    for (i = 1; i <= 70; ++i)
        k += (size_t) snprintf(expression + k, sizeof expression - k,
                               "v%u ", (unsigned) i);
    snprintf(expression + k, sizeof expression - k,
             "' classVariableNames: '' poolDictionaries: '' category: 'x']"
             " on: Error do: [:e | e messageText]");
    check_string(expression, "Iv70 would have 70 instance variables, its "
                             "superclasses' included; the bytecode set "
                             "addresses 64");
    check_boolean("Smalltalk includesKey: #Iv70", 0);

    /*
     *  B24: 16 block arguments and 32 method arguments are refused.
     *
     *  Bytecode 143 holds numArgs in four bits and the header extension
     *  holds a method's in five; both were masked, so a sixteen-argument
     *  block answered 0 to numArgs and a 32-keyword method installed with
     *  none.  Fifteen and thirty-one are the ceilings and still compile.
     */
    k = (size_t) snprintf(expression, sizeof expression, "[");
    for (i = 1; i <= 16; ++i)
        k += (size_t) snprintf(expression + k, sizeof expression - k,
                               ":a%u ", (unsigned) i);
    snprintf(expression + k, sizeof expression - k, "| a16] numArgs");
    check_refused(expression, "at most 15");
    k = (size_t) snprintf(expression, sizeof expression, "[");
    for (i = 1; i <= 15; ++i)
        k += (size_t) snprintf(expression + k, sizeof expression - k,
                               ":a%u ", (unsigned) i);
    snprintf(expression + k, sizeof expression - k, "| a15] numArgs");
    check_integer(expression, 15);
    k = 0;
    for (i = 1; i <= 32; ++i)
        k += (size_t) snprintf(expression + k, sizeof expression - k,
                               "k%u: a%u ", (unsigned) i, (unsigned) i);
    snprintf(expression + k, sizeof expression - k, "^a1");
    check_refused_method(expression, "at most 31 arguments");
    k = 0;
    for (i = 1; i <= 31; ++i)
        k += (size_t) snprintf(expression + k, sizeof expression - k,
                               "k%u: a%u ", (unsigned) i, (unsigned) i);
    snprintf(expression + k, sizeof expression - k, "^a1");
    check_compiles_method(expression);

    /*
     *  B25: `d' is not an exponent marker.
     *
     *  The lexer took `1d2' as one token and strtod read it as far as the
     *  d, so 1d2 was 1.0.  It is `1 d2' now -- a unary send, as in 1983
     *  -- which inside a literal array is the integer and a symbol.
     */
    check_integer("#(1d2) size", 2);
    check_boolean("(#(1d2) at: 2) == #d2", 1);
    check_boolean("1e2 = 100.0", 1);

    /*
     *  B26: a radix number's sign goes after the r, and digits must
     *  follow.
     *
     *  `16r-FF' is what Integer>>storeStringRadix: writes for -255 and
     *  what Number class>>readFrom: reads; the lexer left the minus after
     *  a digitless `16r', which was 0.  `2r-101' was therefore 0 - 101.
     */
    check_integer("16r-FF", -255);
    check_integer("2r-101", -5);
    check_integer("-2r101", -5);
    check_boolean("16r-1.8 = -1.5", 1);
    check_refused("16r", "digits expected after 16r");
    check_refused("16rff", "digits expected after 16r");
    check_refused("-16r-FF", "one minus sign");
    check_integer("Compiler evaluate: (-255 storeStringRadix: 16)", -255);
    check_boolean("(Number readFrom: (ReadStream on: '16r-FF')) = 16r-FF", 1);

    /*
     *  B27: `$' followed by a Latin-1 byte is that character.
     *
     *  A lead byte that promises continuation bytes it does not have is
     *  not UTF-8; the lexer kept the partial decode, so `$é' in a Latin-1
     *  file was $<tab> and the storeString of any Character from 192 up
     *  read back as a different one.  The UTF-8 spelling still reads.
     */
    check_integer("$\xE9 asInteger", 233);
    check_integer("$\xC3\xA9 asInteger", 233);
    check_integer("$\xFF asInteger", 255);
    check_integer("(Compiler evaluate: (Character value: 233) storeString) "
                  "asInteger", 233);
    check_integer("(Compiler evaluate: (Character value: 255) storeString) "
                  "asInteger", 255);

    /*
     *  B28: a NUL in the source is read, not silently taken as its end.
     *
     *  The seam from the image handed the compiler a C string, so a String
     *  holding a NUL was compiled up to it and no further: `3 + 4 <NUL>
     *  + 100' answered 7.  Inside a string literal it is data and the
     *  literal round-trips through storeString; outside one it is refused
     *  by name.  These go through Compiler evaluate: because a C string
     *  literal in this file cannot carry a NUL either.
     */
    check_boolean("[:x | (Compiler evaluate: x storeString) = x] value: "
                  "(String with: (Character value: 0) with: $a)", 1);
    check_integer("(Compiler evaluate: (String with: $' with: "
                  "(Character value: 0) with: $a with: $')) size", 2);
    check_string("[Compiler evaluate: ('3 + 4 ', (String with: "
                 "(Character value: 0)), ' + 100')] on: Error do: "
                 "[:e | e messageText]",
                 "line 1: a NUL byte outside a string literal");

    /*
     *  B29: a pop-and-store counts as a pop in the frame estimate.
     *
     *  Bytecodes 96..111 were tested after the `b <= 119' branch that
     *  counts pushes, so every short assignment in statement position
     *  added two slots to the frame; a method of 125 assignments was
     *  refused as needing 503 slots.  Twenty temporaries and four stores
     *  need twenty-one: the stack never holds more than the one value
     *  being stored.
     */
    {
        st_compile_context  ctx;
        st_compiled_code    code;

        k = (size_t) snprintf(expression, sizeof expression, "foo | ");
        for (i = 1; i <= 20; ++i)
            k += (size_t) snprintf(expression + k, sizeof expression - k,
                                   "t%u ", (unsigned) i);
        snprintf(expression + k, sizeof expression - k,
                 "| t1 := 3. t1 := 4. t1 := 5. t1 := 6");
        fill_compile_context(&ctx);
        ++st_test_checks;
        if (COMPILE_to_bytecodes(expression, &ctx, &code) != 0) {
            ++st_test_failures;
            printf("  FAIL the twenty-temporary method was refused: %s\n",
                   code.error);
        }  else if (code.frame_slots != 21) {
            ++st_test_failures;
            printf("  FAIL twenty temporaries and four stores were given "
                   "%u frame slots, want 21\n", code.frame_slots);
        }
    }
    /*
     *  And a hundred and fifty of them run, in a frame the estimate now
     *  sizes at two slots rather than three hundred.  (Not the audit's
     *  250: eight characters each and the expression buffer is 2000.)
     */
    k = (size_t) snprintf(expression, sizeof expression, "| a | ");
    for (i = 0; i < 150; ++i)
        k += (size_t) snprintf(expression + k, sizeof expression - k,
                               "a := 1. ");
    snprintf(expression + k, sizeof expression - k, "^a");
    check_integer(expression, 1);

    /*
     *  B30: a brace array is built by bytecode 138, one pass, no literals.
     *
     *  It was `Array new: n' and an at:put: per element, which spent a
     *  literal on every index from 3 up and compiled the elements twice
     *  -- 2^depth times for nested braces, 68 seconds for 22 deep.  Now
     *  eight strings are eight literals and no SmallIntegers, 62 strings
     *  compile, 127 elements is the operand's ceiling, and the deep brace
     *  takes no time worth measuring.
     */
    check_integer("{1. 2. 3} size", 3);
    check_integer("{} size", 0);
    check_string("{1. {2. 3}. 4} printString", "(1 (2 3 ) 4 )");
    check_string("| a b | a := 0. b := {a := 5. a + 1. "
                 "[a > 3] whileTrue: [a := a - 1]. {a. {}}}. "
                 "^(Array with: b with: a) printString",
                 "((5 6 nil (3 () ) ) 3 )");
    check_integer("Object compile: 'zzB30 ^ {''s1''. ''s2''. ''s3''. "
                  "''s4''. ''s5''. ''s6''. ''s7''. ''s8''}'. "
                  "^((Object compiledMethodAt: #zzB30) literals "
                  "select: [:l | l isKindOf: SmallInteger]) size "
                  "+ (nil zzB30 size * 100)", 800);
    /*
     *  Through Compiler evaluate:, because the elements sit on the stack
     *  until the 138 takes them and the context evaluate() hands a doIt
     *  here has 64 slots; the image sizes its own contexts from the
     *  method's frame.
     */
    k = (size_t) snprintf(expression, sizeof expression,
                          "Compiler evaluate: '{");
    for (i = 1; i <= 62; ++i)
        k += (size_t) snprintf(expression + k, sizeof expression - k,
                               "''s%u''. ", (unsigned) i);
    snprintf(expression + k, sizeof expression - k, "''last''} size'");
    check_integer(expression, 63);
    k = (size_t) snprintf(expression, sizeof expression,
                          "Compiler evaluate: '{");
    for (i = 1; i <= 127; ++i)
        k += (size_t) snprintf(expression + k, sizeof expression - k,
                               "1. ");
    snprintf(expression + k, sizeof expression - k, "} size'");
    check_integer(expression, 127);
    k = (size_t) snprintf(expression, sizeof expression, "{");
    for (i = 1; i <= 128; ++i)
        k += (size_t) snprintf(expression + k, sizeof expression - k,
                               "1. ");
    snprintf(expression + k, sizeof expression - k, "} size");
    check_refused(expression, "at most 127 elements");
    {
        clock_t     started = clock();
        double      seconds;

        k = 0;
        for (i = 0; i < 22; ++i)
            expression[k++] = '{';
        expression[k++] = '1';
        for (i = 0; i < 22; ++i)
            expression[k++] = '}';
        snprintf(expression + k, sizeof expression - k, " size");
        check_integer(expression, 1);
        seconds = (double) (clock() - started) / CLOCKS_PER_SEC;
        ++st_test_checks;
        if (seconds > 2.0) {
            ++st_test_failures;
            printf("  FAIL a 22-deep brace took %.1f seconds to compile\n",
                   seconds);
        }
    }

    /*
     *  B31: what 1983 refuses, this compiler refuses.
     *
     *  Assignment to a method argument; a duplicate temporary, against a
     *  temporary or an argument, in a method or in a block; a statement
     *  after a return; a comment with no closing quote, which used to
     *  swallow the rest of the method; a literal array with no closing
     *  parenthesis, which used to swallow the rest as symbols.  And
     *  `self := 5' no longer names `self' as undeclared, which put it in
     *  Undeclared for good.  What 1983 ALLOWS stays allowed: a block
     *  storing into its own argument, and `[:a | a | 2]', whose second
     *  `a' is an operand and not a redeclaration.
     */
    check_refused_method("zzArg: a a := 5. ^a", "cannot assign to argument 'a'");
    check_refused("| a a | ^a", "'a' is already declared");
    check_refused_method("zzDup2: a | a | ^a", "'a' is already declared");
    check_refused("[:a :a | a] value: 1 value: 2", "'a' is already declared");
    check_refused("[:a | | a | a] value: 1", "'a' is already declared");
    check_integer("[:a | a | 2] value: 1", 3);
    check_integer("[:x | x := x + 1. x] value: 1", 2);
    check_refused("^ 1. 3", "after a return");
    check_refused("[^ 1. 3] value", "after a return");
    check_refused("\"abc", "unterminated comment");
    check_integer("\"a \"\"quoted\"\" comment\" 3", 3);
    check_refused("#(1 2", "closing a literal array");
    {
        st_compile_context  ctx;
        st_compile_result   res;

        fill_compile_context(&ctx);
        /*
         *  Only the unassignable half can be checked at this level: the
         *  bootstrap's lookup_global declares a name it has never seen,
         *  so nothing is undeclared to it.  The image-level check below
         *  covers what the field is FOR.
         */
        ++st_test_checks;
        if (COMPILE_method("doIt self := 5", &ctx, &res) == 0
         || res.undeclared[0] != '\0') {
            ++st_test_failures;
            printf("  FAIL 'self := 5': %s, undeclared '%s'\n",
                   res.error[0] ? res.error : "compiled", res.undeclared);
        }
    }
    check_boolean("[Compiler evaluate: 'self := 5'] on: Error do: "
                  "[:e | Undeclared includesKey: #self]", 0);

    /*
     *  B32: an infinity or a NaN stores as something that reads back.
     *
     *  They printed as `inf' and `nan', and Number>>storeOn: prints, so
     *  their storeString read back as an undeclared variable -- nil, plus
     *  `inf' in Undeclared.  They store as the expressions that make them
     *  now, an Array holding one falls back from the literal form, and
     *  `2r1e3000' is refused as `1e400' already was.
     */
    check_refused("2r1e3000", "too large for a Float");
    check_string("(1.0e308 * 10) storeString", "Float infinity");
    check_string("(1.0e308 * 10) negated storeString",
                 "Float infinity negated");
    check_string("Float nan storeString", "Float nan");
    check_string("1.5 storeString", "1.5");
    check_boolean("(Compiler evaluate: (1.0e308 * 10) storeString) "
                  "= (1.0e308 * 10)", 1);
    check_boolean("(Compiler evaluate: Float nan storeString) isNaN", 1);
    check_boolean("Float infinity isLiteral", 0);
    check_boolean("1.5 isLiteral", 1);
    check_boolean("(Compiler evaluate: (Array with: Float infinity with: 1.5)"
                  " storeString) first = Float infinity", 1);
    check_boolean("[Compiler evaluate: '2r1e3000'] on: Error do: "
                  "[:e | Undeclared includesKey: #inf]", 0);

    test_dialect = saved;
}

/*
 *  Bugs3.md: the object memory and the image file.
 */
static void
test_bugs3_om(void)
{
    int saved = test_dialect;

    test_dialect = ST_DIALECT_CLOSURES;

    /*
     *  B8: dropping a chain of objects does not recurse on the C stack.
     *
     *  OM_deallocate released an object's fields from inside itself, one C
     *  frame pair per link, so a linked list of 400,000 Arrays let go in a
     *  method was a segfault in every no-worker mode -- which is this
     *  harness, -bootstrap, -eval and -run, every image build and every
     *  doctest.  The free is a worklist now, and this is the audit's own
     *  program at five hundred thousand links; test_om_mt.c drops three
     *  million by hand.  The trailing allocations are the audit's too:
     *  they make sure the memory is still usable after the drop.
     */
    check_string("| a b | a := Array new: 1. "
                 "1 to: 500000 do: [:i | b := Array new: 1. b at: 1 put: a. "
                 "a := b]. "
                 "b := nil. a := nil. "
                 "1 to: 30000 do: [:i | Array new: 10]. "
                 "^'dropped in method'",
                 "dropped in method");

    /*
     *  B33, the C half: identityHash is thirty bits wide.
     *
     *  OM_identity_hash masked the allocation number to fourteen bits, the
     *  width of a 1983 oop, so a hundred thousand fresh objects had 16,384
     *  hashes between them and every identity-hashed collection past a few
     *  thousand elements was a linear scan -- and Object>>hash IS
     *  identityHash, so that was a Set of anything without a hash of its
     *  own.  Forty thousand rather than the audit's hundred thousand only
     *  because a Set that size, through the mixed probe Set>>findElementOrNil:
     *  now runs, is more bytecodes than one doIt gets here; forty thousand
     *  is still two and a half times what fourteen bits could hold.  The
     *  hash is still a SmallInteger, still the same for the life of the
     *  object, and stays one across a become: (test_om_mt.c holds it across
     *  a save and a reload).
     */
    check_integer("((1 to: 40000) collect: [:i | Object new identityHash]) "
                  "asSet size", 40000);
    check_boolean("| o | o := Object new. ^o identityHash = o identityHash",
                  1);
    check_boolean("Object new identityHash class == SmallInteger", 1);
    check_boolean("Object new identityHash < (1 bitShift: 30)", 1);
    check_boolean("| a b h | a := Array new: 1. b := Array new: 2. "
                  "h := a identityHash. a become: b. "
                  "^a identityHash = h and: [a size = 2]", 1);

    /*
     *  B10 and B58 are faults of the image file -- a snapshot written over
     *  its target in place, and a corrupt image followed rather than
     *  refused -- and need a file and a fresh memory to show.  Their
     *  checks are in test_om_mt.c: test_snapshot_never_destroys_the_old_image
     *  and test_corrupt_image_is_refused.
     */

    test_dialect = saved;
}

/*
 *  Bugs3.md: hashing, equality and the numeric conversions.
 *
 *  A note on the timing gates.  evaluate() above runs each expression for
 *  at most twenty million bytecodes and reports "did not finish" past that,
 *  so a Set fill written here IS a timing test: the quadratic fills Bugs3
 *  B33 measured -- five thousand round numbers in one bucket is twelve
 *  million probes -- cannot complete inside the budget, and the linear ones
 *  use a fraction of it.  No clock is read, so the checks do not flake
 *  under load.
 */
static void
test_bugs3_hashing(void)
{
    int saved = test_dialect;

    test_dialect = ST_DIALECT_CLOSURES;

    /*
     *  B33: Float>>hash reads all sixty-four bits.
     *
     *  1983's read the first sixteen-bit word -- sign, exponent and four
     *  bits of mantissa -- so every Float between 0.5 and 1.0 hashed to one
     *  of sixteen values and a Set of twenty thousand Floats in (0, 1) took
     *  sixty-four seconds.  The integral rule survives: a Float with no
     *  fractional part hashes as the Integer it equals, and a Fraction as
     *  its Float.
     */
    check_integer("((1 to: 2000) collect: [:i | (i / 2001.0) hash]) asSet size",
                  2000);
    check_integer("((1 to: 2000) collect: [:i | (i * 0.001) hash]) asSet size",
                  2000);
    check_integer("| s | s := Set new. 1 to: 3000 do: [:i | s add: i / 3001.0]. "
                  "^s size", 3000);
    check_integer("| d | d := Dictionary new. "
                  "1 to: 3000 do: [:i | d at: i / 3001.0 put: i]. "
                  "^d at: 1500 / 3001.0", 1500);
    check_boolean("1 hash = 1.0 hash", 1);
    check_boolean("(2 raisedTo: 70) hash = (2 raisedTo: 70) asFloat hash", 1);
    check_boolean("0.5 hash = (1/2) hash", 1);
    check_boolean("-0.0 hash = 0 hash", 1);
    check_boolean("1.5 hash = -1.5 hash", 0);

    /*
     *  B33: Integer>>hash for a LargeInteger reads every byte.
     *
     *  1983's read the top byte and the bottom byte: 256 values for a run
     *  of consecutive numbers, and eighty-eight seconds for a Set of five
     *  thousand of them.  The number is built once outside the loop
     *  because `2 raisedTo: 70' is LargeInteger multiplication in
     *  Smalltalk and costs more than the hash being measured.
     */
    check_integer("| big | big := 2 raisedTo: 70. "
                  "^((1 to: 2000) collect: [:i | (big + i) hash]) asSet size",
                  2000);
    check_integer("| s big | big := 2 raisedTo: 70. s := Set new. "
                  "1 to: 3000 do: [:i | s add: big + i]. ^s size", 3000);
    check_boolean("(2 raisedTo: 70) hash = (2 raisedTo: 70) negated hash", 0);

    /*
     *  B33: Date, Rectangle and Point.
     *
     *  Date's hash shifted the year by three and xor-ed the day over it,
     *  510 values for 5,000 consecutive days.  Rectangle's xor-ed an origin
     *  and a corner that move together, 58 values for 5,000 rectangles of
     *  one shape.  Point's shifted x by two, so `i @ 0' used a quarter of a
     *  power-of-two table.  A Point with Float coordinates is equal to the
     *  Point with the Integer ones, and hashes with it.
     */
    check_integer("((1 to: 2000) collect: [:i | (Date fromDays: i) hash]) "
                  "asSet size", 2000);
    check_integer("| s | s := Set new. "
                  "1 to: 3000 do: [:i | s add: (Date fromDays: i)]. ^s size",
                  3000);
    check_integer("((1 to: 2000) collect: [:i | "
                  "(i @ i corner: (i + 5) @ (i + 7)) hash]) asSet size", 2000);
    check_integer("| s | s := Set new. 1 to: 3000 do: [:i | "
                  "s add: (i @ i corner: (i + 5) @ (i + 7))]. ^s size", 3000);
    check_integer("((1 to: 2000) collect: [:i | (i @ 0) hash]) asSet size",
                  2000);
    check_boolean("(1 @ 2) hash = (1.0 @ 2.0) hash", 1);

    /*
     *  B33: the probe loops mix the hash before choosing a bucket.
     *
     *  A Set's capacity is always a power of two and SmallInteger>>hash is
     *  the number, so every multiple of 65536 landed in one slot until the
     *  table outgrew 65536: twenty thousand of them took forty-seven
     *  seconds.  Set, Dictionary and IdentitySet each have their own copy
     *  of the loop and are each checked; a Dictionary is also read back,
     *  because a probe that stores by one arithmetic and finds by another
     *  would fill happily and answer `key not found'.
     */
    check_integer("| s | s := Set new. 1 to: 5000 do: [:i | s add: i * 65536]. "
                  "^s size", 5000);
    check_integer("| s | s := Set new. "
                  "1 to: 5000 do: [:i | s add: (i bitShift: 40)]. ^s size", 5000);
    check_integer("| d | d := Dictionary new. "
                  "1 to: 5000 do: [:i | d at: i * 65536 put: i]. "
                  "^(d at: 4000 * 65536) + (d at: 65536)", 4001);
    check_integer("| s | s := IdentitySet new. "
                  "1 to: 5000 do: [:i | s add: i * 65536]. ^s size", 5000);
    check_boolean("| s | s := Set new. 1 to: 5000 do: [:i | s add: i * 65536]. "
                  "1 to: 5000 do: [:i | (s includes: i * 65536) ifFalse: [^false]]. "
                  "^(s includes: 65536 * 5001) not", 1);

    /*
     *  B33: Interval>>hash is constant in the Interval's length, and still
     *  agrees with the Array of its elements.
     *
     *  `#(1 2 3) = (1 to: 3)' is true, so the two must hash alike, and the
     *  shared hash walked every element -- a million for `(1 to: 1000000)'
     *  and two minutes without finishing for a Set of twenty thousand
     *  Intervals.  It walks thirty-two now and samples sixteen beyond that,
     *  and Interval computes the same sample arithmetically; the checks
     *  straddle the threshold, go backwards, step by three, cross zero and
     *  use Floats, which is the arm Interval hands back to the shared body.
     */
    check_boolean("#(1 2 3) hash = (1 to: 3) hash", 1);
    check_boolean("(1 to: 32) asArray hash = (1 to: 32) hash", 1);
    check_boolean("(1 to: 33) asArray hash = (1 to: 33) hash", 1);
    check_boolean("(1 to: 100 by: 3) asArray hash = (1 to: 100 by: 3) hash", 1);
    check_boolean("(100 to: 1 by: -1) asArray hash = (100 to: 1 by: -1) hash", 1);
    check_boolean("(-50 to: 50) asArray hash = (-50 to: 50) hash", 1);
    check_boolean("(0.5 to: 50.5) asArray hash = (0.5 to: 50.5) hash", 1);
    check_boolean("(1 to: 100000) hash = (1 to: 100000) asArray hash", 1);
    check_boolean("(Set with: (1 to: 100)) includes: (1 to: 100) asArray", 1);
    check_boolean("(Set with: (1 to: 100) asArray) includes: (1 to: 100)", 1);
    check_boolean("#(1 2 3) hash = #(1 99 3) hash", 0);
    check_integer("| s | s := Set new. 1 to: 3000 do: [:i | s add: (1 to: i)]. "
                  "^s size", 3000);

    /*
     *  B34: a collection that holds itself can be hashed, so a class can
     *  name TextConstants as a pool at run time.
     *
     *  Set>>hash xor-ed every element's hash and a Dictionary's elements
     *  are its values; TextConstants holds itself under #TextConstants and
     *  Smalltalk holds Smalltalk, so hashing either recursed until
     *  RecursionDepthExceeded, and 1983's Class>>sharing: -- which adds
     *  each pool to a Set -- failed for any class naming TextConstants.
     *  Dictionary hashes its keys now, Set and Array skip themselves, and
     *  Dictionary has the = its key-only hash is lawful for: two
     *  Dictionaries with the same values under different keys were equal.
     *  sharing: is sent to a class that already shares the pool, which
     *  walks the whole path and defines nothing new.
     */
    check_boolean("TextConstants hash class == SmallInteger", 1);
    check_boolean("Smalltalk hash class == SmallInteger", 1);
    check_integer("(Set new add: TextConstants; yourself) size", 1);
    check_boolean("[:a | a at: 1 put: a. a hash class == SmallInteger] "
                  "value: (Array new: 2)", 1);
    check_boolean("[:s | s add: s. s hash class == SmallInteger] value: Set new",
                  1);
    check_boolean("TextStyle sharing: 'TextConstants'", 0);
    check_boolean("(Dictionary new at: 1 put: 2; yourself) "
                  "= (Dictionary new at: 3 put: 2; yourself)", 0);
    check_boolean("(Dictionary new at: 1 put: 2; yourself) "
                  "= (Dictionary new at: 1 put: 2; yourself)", 1);
    check_boolean("(Dictionary new at: 1 put: 2; yourself) hash "
                  "= (Dictionary new at: 1 put: 2; yourself) hash", 1);
    check_boolean("(Dictionary new at: 1 put: 2; yourself) "
                  "= (Dictionary new at: 1 put: 3; yourself)", 0);
    /*
     *  And the bootstrap records a class's pools in sharedPools, so that
     *  definition and TonelWriter can write the clause back.  TextStyle is
     *  a 1983 chunk-file class with `poolDictionaries: 'TextConstants'';
     *  Object shares nothing.
     */
    check_integer("TextStyle sharedPools size", 1);
    check_boolean("TextStyle sharedPools includes: TextConstants", 1);
    check_boolean("TextStyle definition includesSubstring: "
                  "'poolDictionaries: ''TextConstants '''", 1);
    check_integer("Object sharedPools size", 0);

    /*
     *  B35: the two classes whose = was structural and whose hash was not.
     */
    check_integer("(Set new add: (MethodDescription whichClass: Object "
                  "selector: #yourself); add: (MethodDescription whichClass: "
                  "Object selector: #yourself); yourself) size", 1);
    check_boolean("(CharacterBlock stringIndex: 3 character: $a topLeft: 0@0 "
                  "extent: 5@7) hash = (CharacterBlock stringIndex: 3 "
                  "character: $a topLeft: 10@3 extent: 5@7) hash", 1);

    /*
     *  B36: a comparison between a Float and an exact number is exact.
     *
     *  retry:coercing: rounded the Integer or Fraction to a double first,
     *  so 2^53 + 1 was equal to 2^53 asFloat, SmallInteger maxVal to its
     *  own asFloat, 1/3 to (1/3) asFloat, and every Integer past 1.8e308
     *  to infinity -- and not below it.  The Set of three has two elements
     *  because (2^53 + 1) asFloat IS 2^53, correctly rounded, and 2^53 is
     *  already there; before, it had two for the wrong reason.
     */
    check_boolean("9007199254740993 = 9007199254740992.0", 0);
    check_boolean("9007199254740993 > 9007199254740992.0", 1);
    check_boolean("SmallInteger maxVal = SmallInteger maxVal asFloat", 0);
    check_boolean("SmallInteger maxVal < SmallInteger maxVal asFloat", 1);
    check_boolean("(1/3) = (1/3) asFloat", 0);
    check_boolean("(1/3) > (1/3) asFloat", 1);
    check_boolean("(10 raisedTo: 400) = Float infinity", 0);
    check_boolean("(10 raisedTo: 400) < Float infinity", 1);
    check_boolean("Float infinity > (10 raisedTo: 400)", 1);
    check_boolean("Float infinity negated < (10 raisedTo: 400) negated", 1);
    check_boolean("Float nan = 1", 0);
    check_boolean("Float nan ~= 1", 1);
    check_boolean("1 < Float nan", 0);
    check_boolean("(1/2) = 0.5", 1);
    check_boolean("0.5 = (1/2)", 1);
    check_boolean("1 = 1.0", 1);
    check_boolean("3 < 3.5", 1);
    check_boolean("1.0e22 = (10 raisedTo: 22)", 1);
    check_boolean("1.0e23 = (10 raisedTo: 23)", 0);
    check_boolean("((2 raisedTo: 53) + 1) = ((2 raisedTo: 53) + 1) asFloat", 0);
    check_integer("(Set with: (2 raisedTo: 53) with: (2 raisedTo: 53) + 1 "
                  "with: ((2 raisedTo: 53) + 1) asFloat) size", 2);
    check_string("0.1 asExactFraction printString",
                 "(3602879701896397/36028797018963968)");
    check_string("-2.5 asExactFraction printString", "(-5/2)");
    check_integer("3.0 asExactFraction", 3);

    /*
     *  B37: LargePositiveInteger>>asFloat rounds once, to nearest, ties to
     *  even.  The four values are the finding's, against Python's float();
     *  the three after them sit exactly on, just above and just below a
     *  tie in the fifty-fourth bit, where ties-to-even is the whole
     *  question.
     */
    check_string("35249751169645885018 asFloat printString",
                 "3.5249751169645883e19");
    check_string("5527461779710717517408 asFloat printString",
                 "5.527461779710718e21");
    check_string("-235876359782728832226947132462 asFloat truncated printString",
                 "-235876359782728846121470263296");
    check_string("888821955897402360796333836200876596023 asFloat truncated "
                 "printString",
                 "888821955897402289882734643824384016384");
    check_boolean("(((2 raisedTo: 52) + 1) * (2 raisedTo: 20) + (2 raisedTo: 19)) "
                  "asFloat truncated = (((2 raisedTo: 52) + 2) * (2 raisedTo: 20))",
                  1);
    check_boolean("(((2 raisedTo: 52) + 1) * (2 raisedTo: 20) + (2 raisedTo: 19) + 1) "
                  "asFloat truncated = (((2 raisedTo: 52) + 2) * (2 raisedTo: 20))",
                  1);
    check_boolean("(((2 raisedTo: 52) + 1) * (2 raisedTo: 20) + (2 raisedTo: 19) - 1) "
                  "asFloat truncated = (((2 raisedTo: 52) + 1) * (2 raisedTo: 20))",
                  1);
    check_string("(2 raisedTo: 62) negated asFloat printString",
                 "-4.611686018427388e18");
    check_string("(1/3) asFloat printString", "0.3333333333333333");

    /*
     *  B38: Float>>rounded does not add a half first.
     */
    check_string("0.49999999999999994 rounded printString", "0");
    check_string("-0.49999999999999994 rounded printString", "0");
    check_string("4503599627370497.0 rounded printString", "4503599627370497");
    check_string("2.5 rounded printString", "3");
    check_string("-2.5 rounded printString", "-3");
    check_string("1.5e30 rounded printString", "1499999999999999889089448902656");
    check_string("(1/2) rounded printString", "1");
    check_string("(-1/2) rounded printString", "-1");

    /*
     *  B39: ln, log:, sqrt and log of a LargeInteger past 1e308, and a
     *  base-ten logarithm that counts digits exactly.
     */
    check_string("((2 raisedTo: 1100) log: 2) printString", "1100.0");
    check_string("(2 raisedTo: 1100) ln printString", "762.4618986159398");
    check_string("(2 raisedTo: 1100) sqrt printString", "3.6855101804897865e165");
    check_string("(2 raisedTo: 1100) log printString", "331.13299523037927");
    check_string("1000 log printString", "3.0");
    check_string("1000.0 log printString", "3.0");
    check_string("0.001 log printString", "-3.0");
    check_string("(1000 log: 10) printString", "3.0");
    check_string("(1024 log: 2) printString", "10.0");
    check_string("(1000 log: 3) printString", "6.287709822868153");
    check_string("[0 log] on: Error do: [:e | e messageText]",
                 "ln is defined only for positive numbers");

    /*
     *  B40: gcd: and lcm: with a zero.
     */
    check_integer("12 gcd: 0", 12);
    check_integer("0 gcd: 12", 12);
    check_integer("0 gcd: 0", 0);
    check_integer("12 gcd: 18", 6);
    check_integer("4 lcm: 0", 0);
    check_integer("0 lcm: 0", 0);
    check_integer("4 lcm: 6", 12);
    check_boolean("((2 raisedTo: 70) gcd: 0) = (2 raisedTo: 70)", 1);

    /*
     *  B41: a base outside 2..36 is refused by name, on both the
     *  SmallInteger and the LargeInteger path; and -0.0 abs is 0.0.
     */
    check_string("[40 printString: 37] on: Error do: [:e | e messageText]",
                 "a number can be printed in a base from 2 to 36, not 37");
    check_string("[12 printString: 1] on: Error do: [:e | e messageText]",
                 "a number can be printed in a base from 2 to 36, not 1");
    check_string("[(2 raisedTo: 70) printString: 0] on: Error do: [:e | e messageText]",
                 "a number can be printed in a base from 2 to 36, not 0");
    check_string("255 printString: 16", "FF");
    check_string("-255 printString: 36", "-73");
    check_string("(2 raisedTo: 70) printString: 16", "400000000000000000");
    check_string("-0.0 abs printString", "0.0");
    check_string("-3.5 abs printString", "3.5");
    check_boolean("Float nan abs isNaN", 1);

    test_dialect = saved;
}

/*  access, unlink and rmdir, for the restart check below.  */
#include <unistd.h>

/*
 *  Bugs3.md B20: a restarted image appends to its changes file rather than
 *  writing over what the session before it appended.
 *
 *  This one cannot be asked of the image this test builds in-process,
 *  because the fault is in what a SAVED image remembers: the -o path wrote
 *  the image with its changes stream open, so the page buffer and the
 *  File's cached last page number went into the image as they stood, and
 *  every session that resumed it took setToEnd as the end the buffer
 *  remembered.  So this drives the real binary the way a person does --
 *  build an image with one method compiled into it, run it once compiling
 *  a second, run it AGAIN from the same image compiling a third -- and
 *  reads the changes file back: all three records must be there.  Before
 *  the fix the second session wrote over the first's record.
 *
 *  The sessions run under -serve with a startup that evaluates the
 *  argument, which exits when the expression has been evaluated; -run
 *  keeps the display loop going and never comes back.  Skipped, saying so,
 *  when ./st80 is not beside the test -- the Makefile copies it there
 *  after every build, so that is a build that was not made rather than a
 *  fault to hide.
 */
static void
check_changes_file_survives_a_restart(void)
{
    static const char *const startup =
        "Compiler evaluate: (Smalltalk arguments isEmpty"
        " ifTrue: ['nil'] ifFalse: [Smalltalk arguments first])";
    char        dir[] = "/tmp/st80-bugs3-b20-XXXXXX";
    char        image[256];
    char        changes[256];
    char        command[2048];
    char       *text;
    long        length;
    FILE       *f;
    int         ok;

    ++st_test_checks;
    if (access("./st80", X_OK) != 0) {
        printf("  (skipping the changes-file restart check: no ./st80)\n");
        return;
    }
    if (!mkdtemp(dir)) {
        ++st_test_failures;
        printf("  FAIL cannot make a scratch directory for the restart check\n");
        return;
    }
    snprintf(image, sizeof image, "%s/b20.im", dir);
    snprintf(changes, sizeof changes, "%s/b20.im.changes", dir);
    snprintf(command, sizeof command,
             "./st80 -bootstrap -profile " PROFILE " -startup \"%s\""
             " -eval \"Object compile: 'zzBootA ^ 42' classified: 'b20'\""
             " -o %s >/dev/null 2>&1", startup, image);
    ok = system(command) == 0;
    if (ok) {
        snprintf(command, sizeof command,
                 "./st80 -serve %s -workers 1"
                 " \"Object compile: 'zzBootB ^ 43' classified: 'b20'\""
                 " >/dev/null 2>&1", image);
        (void) system(command);
        snprintf(command, sizeof command,
                 "./st80 -serve %s -workers 1"
                 " \"Object compile: 'zzBootC ^ 44' classified: 'b20'\""
                 " >/dev/null 2>&1", image);
        (void) system(command);
    }
    text = NULL;
    length = 0;
    f = ok ? fopen(changes, "rb") : NULL;
    if (f) {
        if (fseek(f, 0, SEEK_END) == 0 && (length = ftell(f)) > 0
         && fseek(f, 0, SEEK_SET) == 0
         && (text = malloc((size_t) length + 1)) != NULL) {
            length = (long) fread(text, 1, (size_t) length, f);
            text[length] = '\0';
        }
        fclose(f);
    }
    if (!text || !strstr(text, "zzBootA ^ 42") || !strstr(text, "zzBootB ^ 43")
     || !strstr(text, "zzBootC ^ 44")) {
        ++st_test_failures;
        printf("  FAIL the changes file of a restarted image should hold all"
               " three methods; it holds:\n%s\n", text ? text : "(nothing)");
    }
    free(text);
    unlink(image);
    unlink(changes);
    rmdir(dir);
}

/*
 *  Bugs3.md: collections, streams, dates and files -- and the two exception
 *  faults that hung or double-ran, which are library code in the same
 *  packages.
 */
static void
test_bugs3_collections(void)
{
    int saved = test_dialect;

    test_dialect = ST_DIALECT_CLOSURES;

    /*
     *  B4: `on: 3 do:' -- any guard that is not an exception class -- hung
     *  the process for ever.  Asking 3 whether it handles the exception
     *  was a doesNotUnderstand, itself a signal, whose search came back
     *  through the same still-active frame and asked 3 again.  The frame
     *  is out of service while its guard is asked, so the fault goes
     *  outward; Object>>handles: gives it a sentence; and the next
     *  expression runs.  evaluate() has a bytecode budget, so a hang here
     *  would fail as "did not finish" rather than hold the test.
     */
    check_string("[[Error new signal: 'x'] on: 3 do: [:e | 'inner']] "
                 "on: Error do: [:e | e messageText]",
                 "3 cannot be the guard of an on:do: -- it is not an exception class");
    check_string("[[Error new signal: 'x'] on: #foo do: [:e | 'inner']] "
                 "on: Error do: [:e | 'reached']", "reached");
    check_integer("([[Error new signal: 'x'] on: 'str' do: [:e | 0]] "
                  "on: Error do: [:e | 41]) + 1", 42);
    check_string("[Error new signal: 'x'] on: Error do: [:e | 'caught']", "caught");
    check_string("[[Error new signal: 'x'] on: nil do: [:e | 'inner']] "
                 "on: Error do: [:e | 'outer']", "outer");

    /*
     *  B18: an ensure: block that raised, or returned non-locally, ran
     *  twice, and the second raise escaped the handler that caught the
     *  first.  The normal path ran the block with the frame still armed;
     *  the unwind through that frame found it and ran it again.  The log
     *  says what ran and how often.
     */
    check_string("| log r | log := OrderedCollection new. "
                 "r := [[log add: #body. 1] ensure: [log add: #ensure. "
                 "Error new signal: 'in ensure']] "
                 "on: Error do: [:e | log add: e messageText. e return: 9]. "
                 "^(log asArray -> r) printString",
                 "(body ensure 'in ensure' )->9");
    /*
     *  The non-local return is judged by its side effect, in a global,
     *  and not by what the expression answers.  What it answers is a
     *  finding of its own, outside this list: a ^ through an ensure: from
     *  a doIt that ST_interp_run drives directly -- this evaluate(), or
     *  -eval -- answers a MethodContext, or falls off the end when the
     *  doIt has no sender (primitive 246 refuses to return from it); under
     *  a scheduled process it answers what it should.  The log is what
     *  B18 is about: before the fix it read (1 2 2).
     */
    evaluate("Smalltalk at: #ZzBugs3Log put: OrderedCollection new. "
             "[(Smalltalk at: #ZzBugs3Log) add: 1. 1] "
             "ensure: [(Smalltalk at: #ZzBugs3Log) add: 2. ^#nlr]. ^#fell");
    check_string("(Smalltalk at: #ZzBugs3Log) asArray printString", "(1 2 )");
    check_integer("| n | n := 0. [1] ensure: [n := n + 1]. ^n", 1);
    check_string("| log | log := OrderedCollection new. "
                 "[[[Error new signal: 'x'] ensure: [log add: 2]] ensure: [log add: 3]] "
                 "on: Error do: [:e | e return: nil]. ^log asArray printString",
                 "(2 3 )");
    /*  ifCurtailed: was right, and must stay right.  */
    check_string("| log | log := OrderedCollection new. "
                 "[[log add: #body. Error new signal: 'boom'] "
                 "ifCurtailed: [log add: #curtailed]] on: Error do: [:e | e return: nil]. "
                 "^log asArray printString", "(body curtailed )");

    /*
     *  B14: `(1 to: 10 by: 0) do:' never terminated.  Refused where the
     *  Interval is made, which every spelling goes through.
     */
    check_string("[(1 to: 10 by: 0) do: [:i | i]. 'ran'] on: Error do: [:e | e messageText]",
                 "an Interval cannot have a step of zero");
    check_string("[1 to: 10 by: 0] on: Error do: [:e | 'refused']", "refused");
    check_integer("(1 to: 10 by: 3) size", 4);
    check_string("(10 to: 1 by: -3) asArray printString", "(10 7 4 1 )");

    /*
     *  B42: Dictionary copy shared its Associations with the original, so
     *  a store into the copy was a store into the original; select: and
     *  reject: added the receiver's own Associations; the library's `,'
     *  began with copy and so modified its receiver.
     */
    check_integer("| d d2 | d := Dictionary new. d at: #k put: 1. "
                  "d2 := d copy. d2 at: #k put: 2. ^d at: #k", 1);
    check_integer("| d d2 | d := Dictionary new. d at: #k put: 1. "
                  "d2 := d select: [:x | true]. d2 at: #k put: 99. ^d at: #k", 1);
    check_integer("| d d2 | d := Dictionary new. d at: #k put: 1. "
                  "d2 := d reject: [:x | false]. d2 at: #k put: 99. ^d at: #k", 1);
    check_integer("| a b c | a := Dictionary new. a at: 1 put: 1. "
                  "b := Dictionary new. b at: 1 put: 2. c := a , b. ^(a at: 1) * 10 + (c at: 1)",
                  12);
    check_integer("| d d2 | d := IdentityDictionary new. d at: #k put: 1. "
                  "d2 := d copy. d2 at: #k put: 2. ^d at: #k", 1);
    check_integer("| d d2 | d := IdentityDictionary new. d at: #k put: 1. "
                  "d2 := d shallowCopy postCopy. d2 at: #k put: 2. ^d at: #k", 1);

    /*  B43: Bag copy shared the Bag's Dictionary.  */
    check_integer("| b b2 | b := Bag with: 1. b2 := b copy. b2 add: 2. ^b size", 1);
    check_integer("| b b2 | b := Bag with: 1. b2 := b copy. b2 remove: 1. ^b size", 1);

    /*
     *  B44: IdentityDictionary at: nil put: made a ghost -- counted by
     *  size, found by nothing, and a second store made size 0.  nil is
     *  refused as a key with a sentence; a Dictionary still holds it.
     */
    check_string("[IdentityDictionary new at: nil put: 1] on: Error do: [:e | e messageText]",
                 "an IdentityDictionary cannot hold nil as a key; a Dictionary can");
    check_integer("| d | d := IdentityDictionary new. "
                  "[d at: nil put: 1] on: Error do: [:e | nil]. ^d size", 0);
    check_boolean("IdentityDictionary new includesKey: nil", 0);
    check_string("IdentityDictionary new at: nil ifAbsent: ['absent']", "absent");
    check_integer("(IdentityDictionary new at: #a put: 1; yourself) size", 1);
    check_integer("(Dictionary new at: nil put: 7; yourself) at: nil", 7);

    /*
     *  B45: a SortedCollection took addFirst: and its relatives and broke
     *  its order; and every copy made through `species new:' -- copyFrom:to:
     *  and so shallowCopy, first:, allButFirst; reverse; `,'; copyWithout:
     *  -- had the default block instead of the receiver's.  Each copy is
     *  judged by where its next add: lands.
     */
    check_string("| s | s := SortedCollection withAll: #(3 1 2). "
                 "^(Array with: ([s addFirst: 9. 0] on: Error do: [:e | 1])"
                 " with: ([s addLast: 0. 0] on: Error do: [:e | 1])"
                 " with: ([s addAllFirst: #(9 8). 0] on: Error do: [:e | 1])"
                 " with: s asArray) printString",
                 "(1 1 1 (1 2 3 ) )");
    check_string("| s | s := SortedCollection withAll: #(3 1 2). "
                 "^(Array with: ([s addAllLast: #(0). 0] on: Error do: [:e | 1])"
                 " with: ([s add: 9 before: 1. 0] on: Error do: [:e | 1])"
                 " with: ([s add: 9 after: 1. 0] on: Error do: [:e | 1])"
                 " with: s asArray) printString",
                 "(1 1 1 (1 2 3 ) )");
    check_string("| s | s := SortedCollection sortBlock: [:a :b | a > b]. s addAll: #(3 1 2). "
                 "^((s copyFrom: 1 to: 2) add: 5; yourself) asArray printString", "(5 3 2 )");
    check_string("| s | s := SortedCollection sortBlock: [:a :b | a > b]. s addAll: #(3 1 2). "
                 "^(s shallowCopy add: 0; yourself) asArray printString", "(3 2 1 0 )");
    check_string("| s | s := SortedCollection sortBlock: [:a :b | a > b]. s addAll: #(3 1 2). "
                 "^((s first: 2) add: 5; yourself) asArray printString", "(5 3 2 )");
    check_string("| s | s := SortedCollection sortBlock: [:a :b | a > b]. s addAll: #(3 1 2). "
                 "^(s allButFirst add: 5; yourself) asArray printString", "(5 2 1 )");
    check_string("| s | s := SortedCollection sortBlock: [:a :b | a > b]. s addAll: #(3 1 2). "
                 "^((s , #(7)) add: 5; yourself) asArray printString", "(7 5 3 2 1 )");
    check_string("| s | s := SortedCollection sortBlock: [:a :b | a > b]. s addAll: #(3 1 2). "
                 "^((s copyWithout: 2) add: 5; yourself) asArray printString", "(5 3 1 )");
    check_string("| s | s := SortedCollection sortBlock: [:a :b | a > b]. s addAll: #(3 1 2). "
                 "^(s reverse add: 5; yourself) asArray printString", "(1 2 3 5 )");
    check_string("| s | s := SortedCollection sortBlock: [:a :b | a > b]. s addAll: #(3 1 2). "
                 "^s reverse reverse asArray printString", "(3 2 1 )");
    /*  copy, select: and copyWith: were right, and must stay right.  */
    check_string("| s | s := SortedCollection sortBlock: [:a :b | a > b]. s addAll: #(3 1 2). "
                 "^(s copy add: 5; yourself) asArray printString", "(5 3 2 1 )");
    check_string("| s | s := SortedCollection sortBlock: [:a :b | a > b]. s addAll: #(3 1 2). "
                 "^((s select: [:x | x > 1]) add: 5; yourself) asArray printString", "(5 3 2 )");

    /*
     *  B46: a ReadStream over an OrderedCollection failed on every method
     *  that builds a result, and a WriteStream on one could not nextPut:,
     *  because `OrderedCollection new: n' is empty and refuses at:put:
     *  past its end.  A WriteStream grows an OrderedCollection by addLast:
     *  now, and a ReadStream copies its run of elements.
     */
    check_string("(ReadStream on: (OrderedCollection withAll: #(1 2 3))) upToEnd printString",
                 "an OrderedCollection(1 2 3 )");
    check_string("((ReadStream on: (OrderedCollection withAll: #(1 2 3))) next: 2) printString",
                 "an OrderedCollection(1 2 )");
    check_string("| r | r := ReadStream on: (OrderedCollection withAll: #(1 2 3)). "
                 "r next: 2. ^(r next: 5) printString, ' ', r atEnd printString",
                 "an OrderedCollection(3 ) true");
    check_string("((ReadStream on: (OrderedCollection withAll: #(1 2 3))) upTo: 2) printString",
                 "an OrderedCollection(1 )");
    check_string("(ReadStream on: (OrderedCollection with: $a with: Character lf with: $b)) "
                 "nextLine printString", "an OrderedCollection($a )");
    check_string("((WriteStream on: OrderedCollection new) nextPut: 1; nextPut: 2; "
                 "nextPutAll: #(3 4); contents) printString", "an OrderedCollection(1 2 3 4 )");
    check_string("((WriteStream with: (OrderedCollection with: 0)) nextPut: 1; contents) "
                 "printString", "an OrderedCollection(0 1 )");
    check_string("| w | w := ReadWriteStream on: OrderedCollection new. "
                 "w nextPut: 7; nextPut: 8; reset. ^w upToEnd printString",
                 "an OrderedCollection(7 8 )");
    check_string("[(WriteStream on: SortedCollection new) nextPut: 1. 'took it'] "
                 "on: Error do: [:e | 'refused']", "refused");
    /*  And the streams that worked keep working, index-aware or not.  */
    check_string("(ReadStream on: (1 to: 5)) upToEnd printString", "(1 2 3 4 5 )");
    check_string("(ReadStream on: 'hello world') next: 30", "hello world");
    check_string("(ReadStream on: 'hello world') next: 3", "hel");
    check_string("(ReadStream on: 'hello world') upToEnd", "hello world");
    check_string("| r | r := ReadStream on: 'hello world'. r next: 6. ^r upToEnd", "world");
    check_string("(ReadStream on: #sym) upToEnd", "sym");
    check_string("(ReadStream on: 'abc' from: 2 to: 3) upToEnd", "bc");
    check_integer("((ReadStream on: 'abc') next: -1) size", 0);

    /*
     *  B47: deepCopy of anything holding a block took 47 seconds and
     *  788 MB and then failed, because the closure's outer context reaches
     *  the whole image.  Blocks, contexts, classes, methods, processes and
     *  semaphores answer themselves; a nested collection is still copied
     *  all the way down.  evaluate()'s budget turns the old behaviour into
     *  "did not finish".
     */
    check_integer("SortedCollection new deepCopy size", 0);
    check_string("| s | s := SortedCollection sortBlock: [:a :b | a > b]. s addAll: #(3 1 2). "
                 "^(s deepCopy add: 5; yourself) asArray printString", "(5 3 2 1 )");
    check_boolean("[:x | x deepCopy == x] value: [:a :b | a > b]", 1);
    check_boolean("Object deepCopy == Object", 1);
    check_boolean("Object class deepCopy == Object class", 1);
    check_boolean("(Object >> #copy) deepCopy == (Object >> #copy)", 1);
    check_boolean("Processor activeProcess deepCopy == Processor activeProcess", 1);
    check_boolean("[:s | s deepCopy == s] value: Semaphore new", 1);
    check_boolean("| a b | a := Array with: (OrderedCollection with: 'x'). b := a deepCopy. "
                  "^(b first == a first) | (b first first == a first first)", 0);
    check_string("(Array with: (OrderedCollection with: 1 with: (Array with: 2))) "
                 "deepCopy printString", "(an OrderedCollection(1 (2 ) ) )");

    /*
     *  B49: `removeAll: self' removed half, `addAll: self' added four copies
     *  of three -- the enumeration walked over its own changes.  The
     *  argument is copied first when it is the receiver, in Collection and
     *  in the two OrderedCollection and SortedCollection overrides.
     */
    check_string("| oc | oc := OrderedCollection withAll: #(1 2 3 4). oc removeAll: oc. "
                 "^oc asArray printString", "()");
    check_string("| oc | oc := OrderedCollection withAll: #(1 2 3). oc addAll: oc. "
                 "^oc asArray printString", "(1 2 3 1 2 3 )");
    check_integer("| oc | oc := OrderedCollection withAll: #(1 2 3). oc addAllFirst: oc. ^oc size",
                  6);
    check_string("| s | s := SortedCollection withAll: #(1 2 3). s addAll: s. "
                 "^s asArray printString", "(1 1 2 2 3 3 )");
    check_integer("| s | s := Set withAll: #(1 2 3). s removeAll: s. ^s size", 0);
    check_integer("| b | b := Bag withAll: #(1 2 3). b addAll: b. ^b size", 6);
    check_integer("| b | b := Bag withAll: #(1 2 3). b removeAll: b. ^b size", 0);
    check_integer("| oc | oc := OrderedCollection withAll: #(1 2). ^(oc addAll: #(3 4)) size",
                  2);

    /*
     *  B20, the part that can be asked in-process: a CLOSED FileStream
     *  reopens to the file as it is now.  The stream below is closed, the
     *  file is grown past a page by another stream, and setToEnd on the
     *  first must land at the real end -- a fresh page and a fresh last
     *  page number, which is the mechanism the restart fix relies on.
     */
    {
        char    dir[] = "/tmp/st80-bugs3-b20i-XXXXXX";

        if (mkdtemp(dir)) {
            char    expression[512];

            snprintf(expression, sizeof expression,
                     "| a b | a := FileStream fileNamed: '%s/f.txt'. "
                     "a nextPutAll: 'abc'. a close. "
                     "b := FileStream oldFileNamed: '%s/f.txt'. b setToEnd. "
                     "1 to: 600 do: [:i | b nextPut: $x]. b close. "
                     "a setToEnd. ^a position", dir, dir);
            check_integer(expression, 603);
            snprintf(expression, sizeof expression,
                     "(FileStream oldFileNamed: '%s/f.txt') size", dir);
            check_integer(expression, 603);
            snprintf(expression, sizeof expression, "%s/f.txt", dir);
            unlink(expression);
            rmdir(dir);
        }
    }
    check_changes_file_survives_a_restart();

    test_dialect = saved;
}

/*
 *  Bugs3.md: streams, dates, files and the smaller collection faults.
 */
static void
test_bugs3_streams(void)
{
    int         saved = test_dialect;
    char        expression[1024];
    const char *scratch = "bugs3-streams-scratch.txt";
    FILE       *f;

    test_dialect = ST_DIALECT_CLOSURES;

    /*
     *  B48: noon reads as noon and midnight as midnight.
     *
     *  Time class>>readFrom: added twelve for pm and left am alone, so on
     *  the twelve-hour clock it writes, 12 pm was hour 24 -- which
     *  fromSeconds: folds to midnight -- and 12 am stayed noon.  The
     *  storeString of noon evaluated to midnight, and 119 of the day's
     *  minutes did not survive printString and readFromString:.  What is
     *  not a time -- hour 25, minute 60, `garbage' -- read as some time or
     *  other; it is refused now, in words that quote the text.
     */
    check_integer("(Time readFromString: '12:00:00 pm') asSeconds", 43200);
    check_integer("(Time readFromString: '12:00:00 am') asSeconds", 0);
    check_integer("(Compiler evaluate: (Time fromSeconds: 43200) storeString) "
                  "asSeconds", 43200);
    check_integer("((0 to: 86399 by: 61) select: [:s | (Time readFromString: "
                  "(Time fromSeconds: s) printString) asSeconds ~= s]) size", 0);
    check_integer("(Time readFromString: '8AM') asSeconds", 28800);
    check_integer("(Time readFromString: '15:30') asSeconds", 55800);
    check_integer("(Time readFromString: '1:59:30 pm') asSeconds", 50370);
    check_boolean("[Time readFromString: '25:00'. false] on: Error do: [:e | "
                  "e messageText = 'not a time: ''25:00''']", 1);
    check_boolean("[Time readFromString: 'garbage'. false] on: Error do: "
                  "[:e | true]", 1);
    check_boolean("[Time readFromString: '8:60'. false] on: Error do: "
                  "[:e | true]", 1);
    check_boolean("[Time readFromString: '13:00 pm'. false] on: Error do: "
                  "[:e | true]", 1);

    /*
     *  B50: Random is 48 bits of state seeded so that no two are alike.
     *
     *  1983's held sixteen bits seeded from the low sixteen bits of the
     *  millisecond clock, so a thousand generators made in a loop were
     *  twenty distinct streams and every answer was a multiple of 1/65536.
     *  The generator is java.util.Random's, which is why a seed of 42 has
     *  a published first answer; the split multiply that keeps it in
     *  SmallInteger arithmetic is checked by that answer being exact.
     */
    check_integer("((1 to: 1000) collect: [:i | Random new next]) asSet size",
                  1000);
    check_boolean("| a b | a := Random new. b := Random new. ^a next = b next",
                  0);
    check_string("(Random seed: 42) next printString", "0.7275636800328681");
    check_string("| r | r := Random seed: 42. r next. ^r next printString",
                 "0.6832234717598454");
    check_integer("(Random seed: 42) nextBits: 32", 3124862261);
    check_boolean("(Random seed: 1) next = (Random seed: 1) next", 1);
    check_boolean("| r | r := Random seed: 7. ^((1 to: 1000) collect: [:i | "
                  "r between: 3 and: 5]) asSet size = 3", 1);
    check_boolean("| r | r := Random new. ^((1 to: 1000) select: [:i | | x | "
                  "x := r next. x < 0 or: [x >= 1]]) isEmpty", 1);
    check_integer("(Random new next: 3) size", 3);

    /*
     *  B51: a FileStream meets its end the way a ReadStream does.
     *
     *  next: past the end raised `Strings only store Characters' where a
     *  ReadStream answers what is there; skip: backwards raised `cannot
     *  skip -5' where a ReadStream clamps; position: past the end was
     *  refused as `cannot read page 1'.  A writer may still position past
     *  its end, because that is how a file is extended (B58 below).
     */
    f = fopen(scratch, "wb");
    if (f == NULL) {
        printf("  skipped: cannot write %s\n", scratch);
    } else {
        fputs("abc", f);
        fclose(f);
        /*
         *  readOnly on every one of these, and not only for the position:
         *  check that needs it: a stream that was never told a mode is a
         *  writer by 1983's default, and close on a writer SHORTENS the
         *  file to wherever its position is.  Without it the `skip: -5'
         *  check closed at position 0 and left an empty file for the
         *  next check to read.
         */
        snprintf(expression, sizeof expression,
                 "| s r | s := FileStream oldFileNamed: '%s'. s readOnly. "
                 "r := s next: 10. s close. ^r", scratch);
        check_string(expression, "abc");
        snprintf(expression, sizeof expression,
                 "| s r | s := FileStream oldFileNamed: '%s'. s readOnly; "
                 "skip: -5. r := s position. s close. ^r", scratch);
        check_integer(expression, 0);
        snprintf(expression, sizeof expression,
                 "| s r | s := FileStream oldFileNamed: '%s'. s readOnly; "
                 "skip: 10. r := s position. s close. ^r", scratch);
        check_integer(expression, 3);
        snprintf(expression, sizeof expression,
                 "| s | s := FileStream oldFileNamed: '%s'. s readOnly. "
                 "^[s position: 99. false] on: Error do: [:e | s close. true]",
                 scratch);
        check_boolean(expression, 1);
        snprintf(expression, sizeof expression,
                 "| s | s := FileStream oldFileNamed: '%s'. s readOnly. "
                 "^[s position: -1. false] on: Error do: [:e | s close. true]",
                 scratch);
        check_boolean(expression, 1);
        /*  And the file is still the three bytes it was.  */
        snprintf(expression, sizeof expression,
                 "(FileStream oldFileNamed: '%s') contentsOfEntireFile", scratch);
        check_string(expression, "abc");
        remove(scratch);
    }

    /*
     *  B51: an inverted range copies nothing, a negative count is refused
     *  by name, and copyFrom: with one argument is the tail.
     *
     *  copyFrom:to: asked the species for `stop - start + 1' elements,
     *  which for `#() allButFirst' is -1 and fails a primitive; `#(1 2 3)
     *  copyFrom: 2' found Object>>copyFrom:, which copies instance
     *  variables, and answered the whole array.
     */
    check_integer("#() allButFirst size", 0);
    check_integer("(#(1 2 3) allButFirst: 5) size", 0);
    check_boolean("(#(1 2 3) allButFirst: 5) class == Array", 1);
    check_integer("('abc' allButLast: 7) size", 0);
    check_boolean("[#(1 2 3) first: -1. false] on: Error do: [:e | "
                  "e messageText = 'a count of elements cannot be negative: -1']",
                  1);
    check_boolean("[#(1 2 3) last: -1. false] on: Error do: [:e | true]", 1);
    check_string("(#(1 2 3) copyFrom: 2) printString", "(2 3 )");
    check_string("'hello' copyFrom: 3", "llo");

    /*
     *  B51: a LinkedList refuses what broke it.
     *
     *  Adding the same Link twice pointed it at itself and size never
     *  returned; a non-Link went into the chain and the next walk was a
     *  doesNotUnderstand; at: indexed the list's own slots; copy shared one
     *  chain between two lists.
     */
    check_boolean("| l a | l := LinkedList new. a := Link new. l add: a. "
                  "^[l add: a. false] on: Error do: [:e | l size = 1]", 1);
    check_boolean("[LinkedList new add: 3. false] on: Error do: [:e | true]", 1);
    check_boolean("| l a b | l := LinkedList new. a := Link new. b := Link new. "
                  "l add: a; add: b. ^(l at: 1) == a and: [(l at: 2) == b]", 1);
    check_boolean("| l | l := LinkedList new. l add: Link new. "
                  "^[l at: 2. false] on: Error do: [:e | true]", 1);
    check_boolean("| l c | l := LinkedList new. l add: Link new; add: Link new. "
                  "c := l copy. c add: Link new. "
                  "^l size = 2 and: [c size = 3 and: [c first ~~ l first]]", 1);
    check_boolean("| l a | l := LinkedList new. a := Link new. l add: a. "
                  "l remove: a. l add: a. ^l size = 1", 1);

    /*
     *  B51: two Associations are equal when both halves are.
     *
     *  LookupKey>>= compared keys alone, so a Set of Associations lost
     *  every pair whose key it had seen.  The hash stays the key's, and the
     *  Dictionary check is why: removeKey: re-probes displaced entries by
     *  the Association's hash and looks them up by the key's, and the two
     *  must land in the same slot.
     */
    check_boolean("(1->2) = (1->3)", 0);
    check_boolean("(1->2) = (1->2)", 1);
    check_boolean("(Set with: 1->2) includes: 1->3", 0);
    check_integer("(Set with: 1->2 with: 1->3) size", 2);
    check_boolean("(1->2) hash = 1 hash", 1);
    check_integer("| d | d := Dictionary new. 1 to: 50 do: [:i | d at: i put: "
                  "i * i]. 1 to: 50 by: 2 do: [:i | d removeKey: i]. "
                  "^(2 to: 50 by: 2) inject: 0 into: [:sum :i | sum + (d at: i)]",
                  22100);
    check_boolean("| d | d := Dictionary new. d add: 1->2. ^(d includesAssociation: "
                  "1->3) not and: [d includesAssociation: 1->2]", 1);

    /*
     *  B51: `,' refuses what is not a collection.
     *
     *  Object>>size answers 0 for a SmallInteger and for nil, so `'abc' , 3'
     *  and `'abc' , nil' were 'abc'.
     */
    check_boolean("['abc' , 3. false] on: Error do: [:e | true]", 1);
    check_boolean("['abc' , nil. false] on: Error do: [:e | true]", 1);
    check_string("'abc' , 'def'", "abcdef");
    check_string("(#(1 2) , (OrderedCollection with: 3)) printString", "(1 2 3 )");
    check_string("((1 to: 3) , #(4)) printString", "(1 2 3 4 )");

    /*
     *  B51: a Date reads in 1983's three forms and the year-first one, and
     *  what is not a date is one error.
     *
     *  1983's reader checked nothing: month 13 and day 2024 went to a table
     *  lookup and died as SubscriptOutOfBounds, `garbage' as a
     *  doesNotUnderstand of isLetter.
     */
    check_string("(Date readFromString: '2024-01-02') printString", "2 January 2024");
    check_string("(Date readFromString: '5 April 1982') printString", "5 April 1982");
    check_string("(Date readFromString: 'April 5, 1982') printString", "5 April 1982");
    check_string("(Date readFromString: '4/5/82') printString", "5 April 1982");
    check_string("(Date readFromString: '5APR82') printString", "5 April 1982");
    check_string("(Date readFromString: 'February 29, 2024') printString",
                 "29 February 2024");
    check_boolean("[Date readFromString: '13/13/2024'. false] on: Error do: [:e | "
                  "e messageText = 'not a date: ''13/13/2024''']", 1);
    check_boolean("[Date readFromString: '31/2/2024'. false] on: Error do: "
                  "[:e | true]", 1);
    check_boolean("[Date readFromString: 'garbage'. false] on: Error do: "
                  "[:e | true]", 1);
    check_boolean("[Date readFromString: 'February 29, 2023'. false] on: Error "
                  "do: [:e | true]", 1);
    check_boolean("[Date readFromString: '2024-02-30'. false] on: Error do: "
                  "[:e | true]", 1);

    /*
     *  B51: radix digits in either case, an r with no digits is a letter,
     *  and a reader takes a String.
     *
     *  `'16rff' asNumber' was 0 because digitValue knew capitals only;
     *  `'16r' asNumber' was 0 where `'16r' asInteger' was 16; `Number
     *  readFrom: '42'' sent atEnd to a String.  `'abc' asNumber' is still
     *  nil, which Bugs2 established.
     */
    check_integer("'16rff' asNumber", 255);
    check_integer("'16rFF' asNumber", 255);
    check_integer("'16r' asNumber", 16);
    check_integer("'16r' asInteger", 16);
    check_integer("'16r-FF' asNumber", -255);
    check_integer("Number readFrom: '42'", 42);
    check_integer("Integer readFrom: '42'", 42);
    check_boolean("'abc' asNumber isNil", 1);
    check_integer("'12abc' asNumber", 12);
    check_integer("$f digitValue", 15);
    check_integer("$F digitValue", 15);
    check_integer("$z digitValue", 35);
    check_integer("$? digitValue", -1);
    check_string("(Number readFrom: '3r-22.2') printString", "-8.666666666666666");

    /*
     *  B51: the rest of the missing protocol.  A nil sortBlock is the
     *  default; newFrom: takes a Dictionary and copies its bindings rather
     *  than sharing its Associations; space: and tab: write that many;
     *  subStrings: is substrings: under its other spelling.
     */
    check_string("((SortedCollection sortBlock: nil) add: 3; add: 1; add: 2; "
                 "yourself) asArray printString", "(1 2 3 )");
    check_integer("(Dictionary newFrom: (Dictionary new at: 1 put: 2; yourself)) "
                  "at: 1", 2);
    check_boolean("| a b | a := Dictionary new at: 1 put: 2; yourself. "
                  "b := Dictionary newFrom: a. b at: 1 put: 99. ^(a at: 1) = 2", 1);
    check_integer("(Dictionary newFrom: {1->2. 3->4}) size", 2);
    check_string("(WriteStream on: String new) space: 3; tab: 2; contents",
                 "   \t\t");
    check_string("('a,b;c' subStrings: ',;') asArray printString",
                 "('a' 'b' 'c' )");
    check_integer("'a b  c' subStrings size", 3);

    /*
     *  B51: sort: is stable, and so is everything that sorts through
     *  SortedCollection>>reSort -- sorted:, asSortedCollection: and a
     *  sortBlock: on a full collection.
     *
     *  All of them went through 1983's quicksort, so pairs sorted by their
     *  first element came back with the second elements shuffled -- and
     *  shuffled differently at every size.
     */
    check_string("(#(#(1 a) #(0 b) #(1 c) #(0 d) #(1 e) #(0 f) #(1 g) #(0 h)) "
                 "copy sort: [:a :b | a first <= b first]) printString",
                 "((0 b ) (0 d ) (0 f ) (0 h ) (1 a ) (1 c ) (1 e ) (1 g ) )");
    check_string("(#(#(1 a) #(0 b) #(1 c) #(0 d) #(1 e) #(0 f) #(1 g) #(0 h)) "
                 "asSortedCollection: [:a :b | a first <= b first]) asArray "
                 "printString",
                 "((0 b ) (0 d ) (0 f ) (0 h ) (1 a ) (1 c ) (1 e ) (1 g ) )");
    check_string("(#(#(1 a) #(0 b) #(1 c) #(0 d) #(1 e) #(0 f) #(1 g) #(0 h)) "
                 "sorted: [:a :b | a first <= b first]) printString",
                 "((0 b ) (0 d ) (0 f ) (0 h ) (1 a ) (1 c ) (1 e ) (1 g ) )");
    check_boolean("(((1 to: 500) asArray collect: [:i | i \\\\ 7 -> i]) sort: "
                  "[:a :b | a key <= b key]) isSortedBy: [:a :b | a key < b key "
                  "or: [a key = b key and: [a value < b value]]]", 1);
    check_boolean("(((1 to: 500) asArray collect: [:i | i \\\\ 7 -> i]) "
                  "asSortedCollection: [:a :b | a key <= b key]) asArray "
                  "isSortedBy: [:a :b | a key < b key "
                  "or: [a key = b key and: [a value < b value]]]", 1);
    check_string("#(3 1 2) copy sort printString", "(1 2 3 )");
    check_string("'hello' copy sort", "ehllo");
    check_string("((OrderedCollection withAll: #(3 1 2)) sort: [:a :b | a > b]) "
                 "printString", "an OrderedCollection(3 2 1 )");

    /*
     *  B58: a file whose stat size is 0 is read until it ends.
     *
     *  PosixFile>>findLastPageNumber divided the size on disk by the page
     *  size, and for anything under /proc the size on disk is 0, so
     *  contentsOfEntireFile of /proc/cpuinfo was its first 512 bytes.  The
     *  process's own environment is the same bytes here and in the image
     *  -- the image runs in this process -- so its size is compared
     *  exactly.  /proc/cpuinfo is the larger one, a hundred pages, and the
     *  kernel regenerates it on every read with the clock speeds of the
     *  moment, so its size is compared to within a page and its first and
     *  last bytes to what this process read: the last two are what say the
     *  read went to the end rather than stopping at the first page.  (Not
     *  a line count: splitting 55 KB into lines in the image is more than
     *  the 20,000,000 bytecodes a doIt here is allowed.)
     */
    f = fopen("/proc/self/environ", "rb");
    if (f == NULL) {
        printf("  skipped: no /proc on this machine\n");
    } else {
        long    bytes = 0;
        size_t  got;
        char    block[4096];
        char    tail[2] = { 0, 0 };   /*  tail[1]: environ's last byte  */

        /*  fread, not fgets: the environment is NUL-separated.  */
        while ((got = fread(block, 1, sizeof block, f)) > 0) {
            bytes += (long) got;
            tail[1] = block[got - 1];
        }
        fclose(f);
        check_integer("(FileStream oldFileNamed: '/proc/self/environ') "
                      "contentsOfEntireFile size", (st_int) bytes);
        /*
         *  And its last byte, which is what says the read went to the END
         *  rather than stopping at a page boundary: the environment does
         *  not change while it is read, so its last byte is a fact.
         */
        if (bytes > 0) {
            snprintf(expression, sizeof expression,
                     "(FileStream oldFileNamed: '/proc/self/environ') "
                     "contentsOfEntireFile last asInteger = %d",
                     (int) (unsigned char) tail[1]);
            check_boolean(expression, 1);
        }
        f = fopen("/proc/cpuinfo", "rb");
        if (f != NULL) {
            bytes = 0;
            while ((got = fread(block, 1, sizeof block, f)) > 0)
                bytes += (long) got;
            fclose(f);
            /*
             *  Three separate checks rather than one conjunction, so that
             *  a failure names its part.  Not its last two bytes: the
             *  kernel regenerates cpuinfo on every read, and under TSAN --
             *  fifty times slower, on a machine whose clock speeds were
             *  changing width -- the file was longer by the time the last
             *  page was read than when its size was taken, so the read
             *  ended mid-line.  A file that changes while it is read has
             *  no end to check; environ's, above, is the check that the
             *  read goes to the end.
             */
            check_boolean("(FileStream oldFileNamed: '/proc/cpuinfo') "
                          "contentsOfEntireFile size > 512", 1);
            snprintf(expression, sizeof expression,
                     "((FileStream oldFileNamed: '/proc/cpuinfo') "
                     "contentsOfEntireFile size - %ld) abs < 512", bytes);
            check_boolean(expression, 1);
            check_string("((FileStream oldFileNamed: '/proc/cpuinfo') "
                         "contentsOfEntireFile copyFrom: 1 to: 9)",
                         "processor");
        }
    }

    /*
     *  B58: a writer positioned more than one page past its end.
     *
     *  File>>readOrAdd: handed a zero-argument block to Interval>>do: for
     *  the intermediate pages, so `position: 600' worked and `position:
     *  2000' raised `The block needs more or fewer arguments defined' and
     *  lost the write.  The bytes before the write read as zeros.
     */
    remove(scratch);
    snprintf(expression, sizeof expression,
             "| f s | f := FileStream newFileNamed: '%s'. f position: 2000. "
             "f nextPut: $X. f close. "
             "s := (FileStream oldFileNamed: '%s') contentsOfEntireFile. "
             "^s size = 2001 and: [(s at: 2001) = $X and: [(s copyFrom: 1 to: 2000) "
             "asSet size = 1 and: [(s at: 1) asInteger = 0]]]",
             scratch, scratch);
    check_boolean(expression, 1);
    remove(scratch);

    test_dialect = saved;
}

/*
 *  Bugs3.md: the HTTP, JSON and ODBC layers.
 *
 *  The wire itself is tested where the wire is: lib/JSON-RPC-Server-Tests
 *  and lib/Rest-Server-Tests start a server on a port the system picks and
 *  drive it with a raw socket, and lib/HTTP-Client-Tests answers a client
 *  from a listener of its own.  What is here is the half of each fix that
 *  is a pure function -- a codec, a validator, a limit -- because those are
 *  the ones a check can hold in one expression, and because a fault in one
 *  of them is a fault in every request rather than in a shape of request.
 */
static void
test_bugs3_web(void)
{
    int saved = test_dialect;

    test_dialect = ST_DIALECT_CLOSURES;

    /*
     *  B52: a percent escape is decoded the same in either case.
     *
     *  1983's Character>>digitValue answers -1 for a lowercase letter, and
     *  HttpCodec checked the uppercase of the character while taking the
     *  value from the character as written.  So `%2f' answered character 31
     *  rather than a slash -- an ordinary lowercase URL 404'd -- and `%0a',
     *  whose second digit is a lowercase letter, computed 0 * 16 + -1 and
     *  raised SubscriptOutOfBounds inside the request parser.  That is not
     *  an HttpError, so it escaped the parser and the connection was
     *  dropped with no reply at all.
     */
    check_string("HttpCodec percentDecode: '%2F'", "/");
    check_string("HttpCodec percentDecode: '%2f'", "/");
    check_integer("(HttpCodec percentDecode: '%0a') first asInteger", 10);
    check_integer("(HttpCodec percentDecode: '%0A') first asInteger", 10);
    check_integer("(HttpCodec percentDecodePath: '/caf%c3%a9') size", 6);
    check_string("HttpCodec percentDecode: 'a%20b+c%2Fd'", "a b c/d");
    check_string("HttpCodec percentDecodePath: 'a+b%41'", "a+bA");
    check_integer("$f asUppercase digitValue", 15);

    /*
     *  B52, the other half: a malformed escape is a 400 and not the text it
     *  is written with.  `%zz' means nothing, and a proxy in front that
     *  decides differently from this parser is how a path gets past a
     *  filter here and arrives elsewhere as another path.
     */
    check_integer("[HttpCodec percentDecode: '%zz'. 0] on: HttpError do: [:e | e status]",
                  400);
    check_integer("[HttpCodec percentDecode: '100%'. 0] on: HttpError do: [:e | e status]",
                  400);
    check_integer("[HttpCodec percentDecode: '%2'. 0] on: HttpError do: [:e | e status]",
                  400);
    check_integer("[HttpCodec percentDecodePath: '/a%g0'. 0] on: HttpError do: [:e | e status]",
                  400);

    /*
     *  B53: a number is bounded by its digits, and reading one is no longer
     *  quadratic.
     *
     *  Four thousand sevens took thirteen and a half seconds to read, and a
     *  REST request carrying one spent every one of them before the session
     *  token in the same document had been looked at -- the token is INSIDE
     *  the document, so the parser is the only place that can refuse the
     *  work.  The digits are counted before any arithmetic is done, so the
     *  refusal costs nothing; and the arithmetic that remains reads
     *  SmallInteger chunks rather than one digit at a time and reduces the
     *  fraction without a gcd.
     */
    check_boolean("[JSONParser parse: (String new: 300 withAll: $7). false] "
                  "on: JSONError do: [:e | true]", 1);
    check_boolean("[JSONParser parse: '0.', (String new: 300 withAll: $7). false] "
                  "on: JSONError do: [:e | true]", 1);
    check_boolean("[JSONParser parse: '1e', (String new: 300 withAll: $0). false] "
                  "on: JSONError do: [:e | true]", 1);
    check_integer("(JSONParser parse: (String new: 256 withAll: $7)) printString size", 256);
    check_integer("JSONParser new maxDigits", 256);
    /*  And the answers themselves, which the chunking must not have moved.  */
    check_string("(JSONParser parse: '123456789012345678901234567890') printString",
                 "123456789012345678901234567890");
    check_string("(JSONParser parse: '0.1') printString", "(1/10)");
    check_string("(JSONParser parse: '12.34') printString", "(617/50)");
    check_string("(JSONParser parse: '3.0') printString", "3");
    check_string("(JSONParser parse: '2.5e-1') printString", "(1/4)");
    check_string("(JSONParser parse: '1.5e3') printString", "1500");
    check_string("(JSONParser parse: '-0.125') printString", "(-1/8)");
    check_boolean("(JSONParser parse: (String new: 200 withAll: $9)) = ((10 raisedTo: 200) - 1)", 1);

    /*
     *  B54: a header value with a line ending in it is an Error, not a
     *  header.
     *
     *  Nothing filtered CR or LF, so a handler reflecting a request
     *  parameter into a Location -- the ordinary shape of a redirect -- let
     *  the client write headers of its own, and everything after the
     *  injected one was the client's too.  Refused rather than stripped: a
     *  stripped Location is still a redirect, to a URL the handler did not
     *  write, and nothing anywhere says so.
     */
    check_boolean("[(HttpResponse status: 302) headerAt: 'Location' "
                  "put: 'http://x/', (String with: (Character value: 13) "
                  "with: (Character value: 10)), 'Set-Cookie: s=1'. false] "
                  "on: Error do: [:e | true]", 1);
    check_boolean("[(HttpResponse ok: 'x') headerAt: 'X-Thing' "
                  "put: 'a', (String with: (Character value: 0)), 'b'. false] "
                  "on: Error do: [:e | true]", 1);
    check_boolean("[(HttpResponse ok: 'x') headerAt: 'X Thing' put: 'v'. false] "
                  "on: Error do: [:e | true]", 1);
    check_boolean("[(HttpResponse ok: 'x') headerAt: 'X:Thing' put: 'v'. false] "
                  "on: Error do: [:e | true]", 1);
    check_string("(HttpResponse ok: 'x') headerAt: 'X-Thing' put: 'plain'; "
                 "headerAt: 'x-thing'", "plain");

    /*
     *  B55: a name is looked up without being interned.
     *
     *  A Symbol is never collected, and the REST dispatcher interned
     *  `_class' and `_method' before anything asked whether such a class or
     *  such a method existed: eight thousand bogus lookups left eight
     *  thousand permanent Symbols that two full collections did not touch.
     *  Symbol class>>lookup: is what the two now ask.
     */
    check_boolean("(Symbol lookup: 'printString') == #printString", 1);
    check_boolean("(Symbol lookup: 'zzNoSuchSymbolHasEverBeenMadeZz') isNil", 1);
    check_boolean("| before | "
                  "before := Symbol allInstances size. "
                  "1 to: 100 do: [:i | Symbol lookup: 'zzNothingNamedThis', i printString]. "
                  "^Symbol allInstances size = before", 1);

    /*
     *  B56: framing is strict, because the documented way to run this
     *  server is behind a reverse proxy and two parsers that divide the
     *  same bytes differently is the whole of request smuggling.  A bare
     *  line feed, a field name with whitespace before its colon, a second
     *  Content-Length, and a length that is not all digits were each
     *  accepted; each is 400 now.
     */
    check_integer("[HttpRequest fromString: 'GET / HTTP/1.1', "
                  "(String with: (Character value: 10)), "
                  "(String with: (Character value: 10)). 0] "
                  "on: HttpError do: [:e | e status]", 400);
    check_integer("[:nl | [HttpRequest fromString: 'POST / HTTP/1.1', nl, "
                  "'Content-Length : 25', nl, nl, 'GET /x HTTP/1.1'. 0] "
                  "on: HttpError do: [:e | e status]] "
                  "value: (String with: (Character value: 13) with: (Character value: 10))",
                  400);
    check_integer("[:nl | [HttpRequest fromString: 'POST / HTTP/1.1', nl, "
                  "'Content-Length: 5', nl, 'Content-Length: 5', nl, nl, 'hello'. 0] "
                  "on: HttpError do: [:e | e status]] "
                  "value: (String with: (Character value: 13) with: (Character value: 10))",
                  400);
    check_integer("[:nl | [HttpRequest fromString: 'POST / HTTP/1.1', nl, "
                  "'Content-Length: 5, 5', nl, nl, 'hello'. 0] "
                  "on: HttpError do: [:e | e status]] "
                  "value: (String with: (Character value: 13) with: (Character value: 10))",
                  400);
    check_integer("[:nl | [HttpRequest fromString: 'POST / HTTP/1.1', nl, "
                  "'Transfer-Encoding: chunked', nl, 'Content-Length: 5', nl, nl, 'hello'. 0] "
                  "on: HttpError do: [:e | e status]] "
                  "value: (String with: (Character value: 13) with: (Character value: 10))",
                  400);
    /*  Transfer-Encoding on its own is still the 411 it was.  */
    check_integer("[:nl | [HttpRequest fromString: 'POST / HTTP/1.1', nl, "
                  "'Transfer-Encoding: chunked', nl, nl. 0] "
                  "on: HttpError do: [:e | e status]] "
                  "value: (String with: (Character value: 13) with: (Character value: 10))",
                  411);
    /*  And a well-formed request is read as it always was.  */
    check_string("[:nl | (HttpRequest fromString: 'POST /rest HTTP/1.1', nl, "
                 "'Content-Length: 5', nl, nl, 'hello') body] "
                 "value: (String with: (Character value: 13) with: (Character value: 10))",
                 "hello");

    /*
     *  B57: an Integer wider than a parameter is a DbError and not a row
     *  that quietly is not there.
     *
     *  statement:bind:to: answered whatever the primitive answered, and the
     *  primitive answers FALSE for a refusal rather than nil -- so `self
     *  check:', which raises on nil, would not have helped either.  Binding
     *  2^63 reported success and inserted nothing.  The range is checked
     *  here, before the driver is asked, so that the message can name the
     *  value; the primitive's only vocabulary is false.
     */
    check_boolean("Odbc maxParameterInteger = ((2 raisedTo: 63) - 1)", 1);
    check_boolean("Odbc minParameterInteger = (0 - (2 raisedTo: 63))", 1);
    check_boolean("[Odbc statement: 1 bind: 1 to: (2 raisedTo: 63). false] "
                  "on: DbError do: [:e | e messageText includesSubstring: '64 bits']", 1);
    check_boolean("[Odbc statement: 1 bind: 1 to: (0 - (2 raisedTo: 63) - 1). false] "
                  "on: DbError do: [:e | e messageText includesSubstring: '64 bits']", 1);

    /*
     *  B57, lower: a HEAD answers the length a GET would have.  It answered
     *  Content-Length: 0, which is a lie to the one client that asks a HEAD
     *  to find out how big something is.  The response is built as the
     *  GET's and written without its body.
     */
    check_boolean("| out | out := WriteStream on: (String new: 200). "
                  "((HttpResponse text: 'twelve bytes') omitBody; yourself) "
                  "writeOn: out keepAlive: false. "
                  "^(out contents includesSubstring: 'Content-Length: 12') "
                  "and: [(out contents includesSubstring: 'twelve bytes') not]", 1);

    /*
     *  B57, lower: the body of a reply is bounded, and a chunk size that
     *  cannot be one is a NetError rather than `a primitive has failed'.
     */
    check_boolean("HttpBodyStream defaultMaxBodyBytes = (64 * 1024 * 1024)", 1);
    check_boolean("HttpClient new maxBodyBytes = (64 * 1024 * 1024)", 1);

    test_dialect = saved;
}

/*
 *  Bugs3.md: packages, the Debugger seam and the manual.
 */
static void
test_bugs3_tonel(void)
{
    int saved = test_dialect;

    test_dialect = ST_DIALECT_CLOSURES;

    /*
     *  B62: a doIt in a context -- what the Debugger's code pane sends --
     *  runs, and can read the context's temporaries.
     *
     *  lib/Compiler-Fixes's evaluate:in:to:notifying:ifFail: routes a doIt
     *  to the C compiler, which has no notion of a context, and for the
     *  context case sent `super' meaning "the 1983 one" -- which it had
     *  overwritten, being in the same method dictionary.  super was
     *  Object, and every `do it' in a Debugger was a doesNotUnderstand
     *  on the Compiler.  The body it wanted is kept under its own selector
     *  now.
     *
     *  The context is a real method's, because 1983's Encoder asks the
     *  context for its tempNames, and a context whose method has no source
     *  -- this test's doIts -- gets them by DECOMPILING the method, which
     *  1983's Decompiler cannot do past a closure bytecode.  So the one
     *  doIt evaluated in thisContext is written with no block in it, and
     *  the rest use a context of Unwind's.  A syntax error with no
     *  requestor is a SyntaxErrorNotification on this path as on the C
     *  one, and the ifFail: block is not reached on either; that case is
     *  not checked here because 1983's parser signalling inside this
     *  harness's hand-made context stops the interpreter without an
     *  answer, where the same expression under -eval and -serve answers
     *  the handler's value.
     */
    check_integer("Compiler new evaluate: '3 + 4' in: nil to: nil "
                  "notifying: nil ifFail: [#failed]", 7);
    check_integer("Compiler new evaluate: '3 + 4' in: thisContext to: nil "
                  "notifying: nil ifFail: nil", 7);
    check_integer("Compiler new evaluate: '3 + 4' "
                  "in: (Unwind contextTakingTwoArgs: 40 and: 2) to: nil "
                  "notifying: nil ifFail: [#failed]", 7);
    check_integer("Compiler new evaluate: 'a + b' "
                  "in: (Unwind contextTakingTwoArgs: 40 and: 2) to: nil "
                  "notifying: nil ifFail: [#failed]", 42);

    /*
     *  B64: the protocol the manual documents.  display: on a stream and
     *  on the Transcript, which is a StringHolder and not a Stream;
     *  valueWithExit, answering what the exit was given or the block's
     *  value when it was never taken; valuesDo: on a Dictionary.
     */
    check_string("(WriteStream on: (String new: 0)) display: 42; "
                 "display: 'x'; display: #y; contents", "42xy");
    check_boolean("(TextCollector canUnderstand: #display:)", 1);
    check_integer("[:exit | exit value: 3. 4] valueWithExit", 3);
    check_integer("[:exit | 4] valueWithExit", 4);
    check_integer("[:exit | #(1 2 3) do: [:each | "
                  "each > 1 ifTrue: [exit value: each]]. 0] valueWithExit",
                  2);
    check_integer("| n | n := 0. (Dictionary new at: #a put: 1; at: #b put: 2; "
                  "yourself) valuesDo: [:v | n := n + v]. ^n", 3);
    check_integer("| n | n := 0. (IdentityDictionary new at: #a put: 5; "
                  "yourself) valuesDo: [:v | n := n + v]. ^n", 5);

    /*
     *  B61: the format's weak bit is answerable, from the format word the
     *  way isVariable reads its own bit; a weak class can be made at run
     *  time; and the writer says what the loader read -- `weak', not
     *  `variable' -- and names a class's trait rather than owning the
     *  trait's methods.  The composition comes from TraitCompositions,
     *  which the bootstrap fills.
     */
    check_boolean("WeakArray isWeak", 1);
    check_boolean("WeakArray isVariable", 1);
    check_boolean("Array isWeak", 0);
    check_boolean("Object isWeak", 0);
    check_boolean("WeakArray isEphemeron", 0);
    /*
     *  And the sign bit is not the ephemeron bit.  1983's definition
     *  messages write the pointers flag as -16384, so a class made at run
     *  time has a negative format, and both isEphemeron and the VM's
     *  shape_of_class read bit 15 -- the sign, extended -- as "ephemeron"
     *  for every one of them.  A run-time class is ordinary; one asked
     *  for with ephemeronSubclass: is not, and its format is positive.
     */
    check_boolean("(Object subclass: #Bugs3Plain instanceVariableNames: 'k v' "
                  "classVariableNames: '' poolDictionaries: '' category: 'x') "
                  "format < 0", 1);
    check_boolean("(Smalltalk at: #Bugs3Plain) isEphemeron", 0);
    check_boolean("(Smalltalk at: #Bugs3Plain) isPointers", 1);
    check_boolean("(Object ephemeronSubclass: #Bugs3Eph instanceVariableNames: 'k v' "
                  "classVariableNames: '' poolDictionaries: '' category: 'x') "
                  "isEphemeron", 1);
    check_boolean("(Smalltalk at: #Bugs3Eph) format > 0", 1);
    check_boolean("(Smalltalk at: #Bugs3Eph) isPointers", 1);
    check_boolean("(Smalltalk at: #Bugs3Eph) isVariable", 0);
    check_integer("(Smalltalk at: #Bugs3Eph) instSize", 2);
    check_boolean("(Smalltalk at: #Bugs3Plain) removeFromSystem. "
                  "(Smalltalk at: #Bugs3Eph) removeFromSystem. "
                  "^Smalltalk includesKey: #Bugs3Eph", 0);
    check_boolean("(Smalltalk at: #TraitCompositions) class == Dictionary", 1);
    check_string("Greeter traitComposition", "TGreeting");
    check_boolean("Object traitComposition isNil", 1);
    check_boolean("(TonelWriter sourceFor: WeakArray) "
                  "includesSubstring: '#type : ''weak'''", 1);
    check_boolean("(TonelWriter sourceFor: WeakArray) "
                  "includesSubstring: '#type : ''variable'''", 0);
    check_boolean("(TonelWriter sourceFor: Greeter) "
                  "includesSubstring: '#traits : ''TGreeting'''", 1);
    check_boolean("(TonelWriter sourceFor: Greeter) "
                  "includesSubstring: 'Greeter >> greeting'", 0);
    check_boolean("(TonelWriter sourceFor: Greeter) "
                  "includesSubstring: 'Synthesized'", 0);
    check_string("TonelWriter classTraitsFor: 'TA + (TB - {#x})'",
                 "TA classTrait + TB classTrait");

    /*
     *  B63: a file with a mistake in it loads nothing, and a byte-order
     *  mark is not a type word.  Through TonelReader on text; the C
     *  reader's mark is tests/unit/test_tonel.c's.
     */
    /*
     *  Asked after the handler has returned, not inside it: the class is
     *  taken out by an ifCurtailed: block, and unwinding runs those when
     *  the handler completes -- a handler that looked would still see it.
     */
    check_boolean("[TonelReader loadString: 'Class { #name : ''Bugs3Bad'', "
                  "#superclass : ''Object'' }', (String with: Character cr), "
                  "'Bugs3Bad >> one [ ^1 ]', (String with: Character cr), "
                  "'Bugs3Bad >> two [ ^( ]' named: 'bad'] "
                  "on: TonelError do: [:e | nil]. "
                  "^Smalltalk includesKey: #Bugs3Bad", 0);
    check_integer("| bom c | bom := String with: (Character value: 239) "
                  "with: (Character value: 187) with: (Character value: 191). "
                  "c := TonelReader loadString: bom, 'Class { #name : "
                  "''Bugs3Bom'', #superclass : ''Object'' }', "
                  "(String with: Character cr), 'Bugs3Bom >> one [ ^1 ]' "
                  "named: 'bom'. ^[c new one] ensure: [c removeFromSystem]",
                  1);

    test_dialect = saved;
}

int
main(void)
{
    ST_TEST_BEGIN("1983 image");

    if (!load_sources())
        return ST_TEST_END();
    if (!build_once())
        return ST_TEST_END();

    /*
     *  The screen first: Text class>>initialize asks Display how wide it is
     *  when it works out the default tab stops, so an image with no Display
     *  gets no text constants and then no text style.
     */
    CHECK(BOOT_install_display(640, 480));

    /*
     *  The step a fileIn does not do and an image build does.  Without it the
     *  library's class variables are all nil, and printString, Symbol
     *  interning and Character creation all walk into nil.
     */
    {
        st_boot_init_report init;

        BOOT_run_initializers(&init);
        printf("  %u class initializers, %u ran, %u skipped, %u unfinished",
               init.defined, init.ran, init.skipped, init.unfinished);
        if (init.unfinished)
            printf(" (first: %s)", init.first_unfinished);
        printf("\n");
        CHECK(init.defined >= 45);
        /*
         *  Every one either ran or was deliberately skipped.  Three are:
         *  Object asks the user a question, Symbol builds the table that
         *  interning reads, and FormMenuView reads Xerox files we do not
         *  ship.  never_initialize in the bootstrap says why for each.
         */
        CHECK_EQ_INT(init.ran + init.skipped, init.defined);
        CHECK_EQ_INT(init.skipped, 3);
    }

    test_classes_present();
    test_arithmetic();
    test_collections();
    test_printing();
    test_symbols();
    test_string_hash_agrees();
    test_string_hash_spreads();
    test_floats();
    test_strings();
    test_graphics_objects();
    test_bitblt();
    test_display();
    test_text();
    test_paragraph();
    test_view();
    test_scheduler();
    test_process_scheduler();
    test_processes();
    test_system_organization();
    test_modern_protocol();
    test_sunit();
    test_browsing();
    test_browser();
    test_compile_inspect_debug();
    test_globals_are_reachable_by_name();
    test_self_hosting();
    test_class_variables_from_the_image();
    test_browsing_finds_every_method();
    test_audit_what_the_image_searches();
    test_changing_the_image();
    test_class_side_instance_variables();
    test_menus_compose_as_lines();
    test_file_out_travels_between_hosts();
    test_file_out_has_no_page_padding();
    test_quit();
    test_input();
    test_where_the_ink_lands();
    test_printing_deep();
    test_mixed_arithmetic();
    test_integers_larger_than_a_smallinteger();
    test_blocks_activate_separately();
    test_every_method_can_find_its_source();
    test_closures();
    test_exceptions();
    test_a_restarted_frame_counts_its_arguments_once();
    test_weak_references();
    test_pragmas_are_objects();
    test_bugs2();
    test_bugs3_interp();
    test_bugs3_sched();
    test_bugs3_compiler();
    test_bugs3_om();
    test_bugs3_hashing();
    test_bugs3_collections();
    test_bugs3_streams();
    test_bugs3_web();
    test_bugs3_tonel();

    OM_shutdown();
    return ST_TEST_END();
}

#else   /*  not ST_OM_MT  */

int
main(void)
{
    printf("skipped: the bootstrap targets the 64-bit object memory\n");
    return 0;
}

#endif
