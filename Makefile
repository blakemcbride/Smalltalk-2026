# Smalltalk-2026 -- a parallel Smalltalk-80 in C
#
# Targets:
#   all        (default) build the st80 binary
#   test       build and run the unit tests
#   deps       report on SDL3 and the other external requirements
#   clean      remove build artifacts
#   help       list targets and variables
#
# Variables:
#   OM=mt      64-bit threaded object memory (default; the real system)
#   OM=bb      faithful 16-bit Blue Book object memory (the validation
#              harness that must reproduce the Xerox traces)
#   HEADLESS=1 build without SDL3; the display becomes a stub
#   TSAN=1     build with the thread sanitizer
#   ASAN=1     build with the address and undefined-behaviour sanitizers
#
# A missing SDL3, compiler or C library stops the build here with the
# command that installs it -- see the requirements section below.

CC        ?= gcc
CSTD      := -std=c11
WARN      := -Wall -Wextra -Wpedantic -Werror=implicit-function-declaration
OPT       ?= -O2 -g

#
#  The 64-bit threaded memory is the system; the Blue Book one is the harness
#  that proves the interpreter right against Xerox's traces.  So `make' with
#  no arguments gives you the system.  It used to give you the harness, and
#  the harness cannot bootstrap an image -- which meant the first thing a new
#  reader did after `make clean; make' was hit a failure two layers below the
#  mistake.  Say `make OM=bb' for the oracle; `make OM=bb test' still runs it.
#
OM        ?= mt

UNAME_S   := $(shell uname -s)

# Apple folds pthreads into libSystem and wants neither -pthread nor
# -lpthread; everyone else wants both.
ifeq ($(UNAME_S),Darwin)
    THREAD_CFLAGS :=
    THREAD_LIBS   :=
else
    THREAD_CFLAGS := -pthread
    THREAD_LIBS   := -pthread
endif

SAN_CFLAGS :=
SAN_LIBS   :=
ifdef TSAN
    SAN_CFLAGS := -fsanitize=thread -fno-omit-frame-pointer
    SAN_LIBS   := -fsanitize=thread
endif
ifdef ASAN
    SAN_CFLAGS := -fsanitize=address,undefined -fno-omit-frame-pointer
    SAN_LIBS   := -fsanitize=address,undefined
endif

# ---------------------------------------------------------------------------
#  What this build needs from the machine, and what it says when one of
#  those things is not there.
#
#  They used to be found silently or not at all.  pkg-config answered
#  nothing for SDL3, ST_HAVE_SDL3 went undefined, src/gfx/display.c compiled
#  its headless stub, and `make' printed not one word about any of it -- so
#  a package nobody installed on Monday surfaced on Tuesday, two layers
#  down, as `./st80 -run' opening no window and answering "built without
#  SDL3".  That names the symptom, in another tool, on another day.  A
#  missing compiler or C library was worse still: the first gcc line of the
#  build failed on its own terms and left the reader to work backwards from
#  a linker error to a package name.
#
#  So every external requirement is checked here, before one object is
#  compiled, and a missing one stops the build carrying the line to type.
#  Two rules keep the check from becoming the obstacle:
#
#    - `make clean' and `make help' never need a toolchain and never probe
#      for one.  Refusing to clean a tree because a library is missing is
#      the one thing a diagnostic must not do.
#    - the fast path is a single compiler invocation -- about 40ms -- that
#      links tools/probe.c against the whole external surface at once.
#      Only when that fails does anything ask which piece was at fault, and
#      by then the build is stopping anyway.
#
#  `make deps' runs the same checks and reports all of them instead of
#  stopping at the first.  It is tools/check-deps.sh, which also owns the
#  table of per-platform install commands the messages below quote -- a
#  distribution that renames its SDL3 package must not be able to make one
#  of them right and the other wrong.
# ---------------------------------------------------------------------------

PKG_CONFIG  ?= pkg-config
DEPS_SCRIPT := tools/check-deps.sh
PROBE_SRC   := tools/probe.c

GOALS := $(if $(MAKECMDGOALS),$(MAKECMDGOALS),all)

#  Goals that need no toolchain at all, and goals that must not be stopped
#  by a parse-time error.  `deps' is in the second list and not the first
#  because running the probes is its whole job -- it still exits non-zero
#  when something is missing, but only after it has said what.  `help' is in
#  it because a reader whose build just refused to run is exactly the reader
#  who types `make help' next.
NO_PROBE_GOALS := clean font
NO_STOP_GOALS  := clean font help deps

PROBE := $(if $(filter-out $(NO_PROBE_GOALS),$(GOALS)),yes)
STOP  := $(if $(filter-out $(NO_STOP_GOALS),$(GOALS)),yes)

#  `command -v' rather than `which': it is in POSIX, it is a shell builtin
#  so it costs no process, and it does not print a line of its own on
#  failure the way some `which' do.
have = $(shell command -v $(1) >/dev/null 2>&1 && echo yes)

HAVE_CC         := $(call have,$(firstword $(CC)))
HAVE_PKG_CONFIG := $(call have,$(PKG_CONFIG))

# SDL3 ----------------------------------------------------------------------

ifdef HAVE_PKG_CONFIG
SDL3_CFLAGS := $(shell $(PKG_CONFIG) --cflags sdl3 2>/dev/null)
SDL3_LIBS   := $(shell $(PKG_CONFIG) --libs   sdl3 2>/dev/null)
endif

#  pkg-config is how SDL3 is usually found.  It is not SDL3.  A machine can
#  carry the library and not the tool -- a hand-built SDL, a distribution
#  that splits pkgconf out -- and telling that reader to install SDL3 sends
#  them to install what they already have.  So when pkg-config has no
#  answer, ask the compiler, which is the only thing that actually knows.
#  -include puts the header in without a #include line, which make would
#  otherwise eat as a comment before the shell ever saw it.
ifeq ($(strip $(SDL3_LIBS)),)
ifdef HAVE_CC
ifdef PROBE
SDL3_LIBS := $(shell printf 'int main(void) { return SDL_Init(0) ? 0 : 1; }\n' | \
               $(CC) -x c - -include SDL3/SDL.h -o /dev/null -lSDL3 2>/dev/null \
               && echo -lSDL3)
endif
endif
endif

ifneq ($(strip $(SDL3_LIBS)),)
    HAVE_SDL3     := yes
    SDL3_CFLAGS   += -DST_HAVE_SDL3
    SDL3_VERSION  := $(if $(HAVE_PKG_CONFIG),$(shell $(PKG_CONFIG) --modversion sdl3 2>/dev/null))
endif

#  HEADLESS=1 means it, on a machine with SDL3 as much as on one without.
#  A flag that asks for the stub and silently builds the window when the
#  library happens to be present is a flag that cannot be used to reproduce
#  anything.
ifdef HEADLESS
    HAVE_SDL3   :=
    SDL3_CFLAGS :=
    SDL3_LIBS   :=
endif

# ODBC ----------------------------------------------------------------------
#
#  Optional, and absent is a first-class outcome.  A system that cannot reach
#  a database is still a Smalltalk; one that refuses to build because a
#  database library is missing is not.  So this follows SDL3's shape -- ask
#  pkg-config, then ask the compiler, and let NODB=1 mean it -- but where a
#  missing SDL3 stops the build, a missing ODBC only clears ST_HAVE_ODBC and
#  the Database package answers "this build has no ODBC" when asked to
#  connect.  See the stub half of src/db/st_odbc.c.
#
#  unixODBC calls its pkg-config file `odbc'; iODBC, which is what macOS
#  ships, calls it `libiodbc'.  Both answer the same headers and the same
#  entry points, which is the whole reason this port targets ODBC and not a
#  driver.

ifdef HAVE_PKG_CONFIG
ODBC_CFLAGS := $(shell $(PKG_CONFIG) --cflags odbc 2>/dev/null)
ODBC_LIBS   := $(shell $(PKG_CONFIG) --libs   odbc 2>/dev/null)
ifeq ($(strip $(ODBC_LIBS)),)
ODBC_CFLAGS := $(shell $(PKG_CONFIG) --cflags libiodbc 2>/dev/null)
ODBC_LIBS   := $(shell $(PKG_CONFIG) --libs   libiodbc 2>/dev/null)
endif
endif

#  No pkg-config answer is not the same as no ODBC -- see the SDL3 note above,
#  which this has the same reason for.  Windows carries the driver manager in
#  the operating system and has never had a .pc file for it.
ifeq ($(strip $(ODBC_LIBS)),)
ifdef HAVE_CC
ifdef PROBE
ODBC_LIBS := $(shell printf 'int main(void) { SQLHENV e; return SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &e); }\n' | \
               $(CC) -x c - -include sql.h -include sqlext.h -o /dev/null -lodbc 2>/dev/null \
               && echo -lodbc)
ifeq ($(strip $(ODBC_LIBS)),)
ODBC_LIBS := $(shell printf 'int main(void) { SQLHENV e; return SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &e); }\n' | \
               $(CC) -x c - -include sql.h -include sqlext.h -o /dev/null -lodbc32 2>/dev/null \
               && echo -lodbc32)
endif
endif
endif
endif

ifneq ($(strip $(ODBC_LIBS)),)
    HAVE_ODBC     := yes
    ODBC_CFLAGS   += -DST_HAVE_ODBC
    ODBC_VERSION  := $(if $(HAVE_PKG_CONFIG),$(shell $(PKG_CONFIG) --modversion odbc 2>/dev/null))
endif

#  NODB=1 means it, on a machine with ODBC as much as on one without -- the
#  same promise HEADLESS=1 makes, and for the same reason: a flag that asks
#  for the stub and silently builds the real thing cannot reproduce anything.
ifdef NODB
    HAVE_ODBC   :=
    ODBC_CFLAGS :=
    ODBC_LIBS   :=
endif

HINT_ODBC = $(or $(shell sh $(DEPS_SCRIPT) --install odbc 2>/dev/null),install unixODBC with its headers -- the package is usually called unixODBC-devel or unixodbc-dev)

# TLS -----------------------------------------------------------------------
#
#  OpenSSL, for the client side of https: what reaching a language model's
#  API needs, since every one of them is https only.  Optional in ODBC's
#  exact shape -- ask pkg-config, then the compiler, let NOTLS=1 mean it --
#  and absent is a first-class outcome: the build clears ST_HAVE_TLS, and an
#  https URL answers "this build has no TLS" by name instead of connecting
#  to something it cannot check.  1.1.1 is the floor, for SSL_read_ex and
#  SSL_set1_host; LibreSSL answers the same names.
#
#  The server side is not built at all, with or without the library: a
#  server here sits behind a reverse proxy that terminates TLS, which is
#  the arrangement Kiss has with Tomcat and nginx.

ifdef HAVE_PKG_CONFIG
TLS_CFLAGS := $(shell $(PKG_CONFIG) --cflags openssl 2>/dev/null)
TLS_LIBS   := $(shell $(PKG_CONFIG) --libs   openssl 2>/dev/null)
endif

ifeq ($(strip $(TLS_LIBS)),)
ifdef HAVE_CC
ifdef PROBE
TLS_LIBS := $(shell printf 'int main(void) { return SSL_CTX_new(TLS_client_method()) == 0; }\n' | \
              $(CC) -x c - -include openssl/ssl.h -o /dev/null -lssl -lcrypto 2>/dev/null \
              && echo -lssl -lcrypto)
endif
endif
endif

ifneq ($(strip $(TLS_LIBS)),)
    HAVE_TLS     := yes
    TLS_CFLAGS   += -DST_HAVE_TLS
    TLS_VERSION  := $(if $(HAVE_PKG_CONFIG),$(shell $(PKG_CONFIG) --modversion openssl 2>/dev/null))
endif

#  NOTLS=1 means it, on a machine with OpenSSL as much as on one without,
#  for the reason NODB=1 does.
ifdef NOTLS
    HAVE_TLS   :=
    TLS_CFLAGS :=
    TLS_LIBS   :=
endif

HINT_TLS = $(or $(shell sh $(DEPS_SCRIPT) --install openssl 2>/dev/null),install OpenSSL with its headers -- the package is usually called openssl-devel or libssl-dev)

# The link the build is really going to make ---------------------------------

PROBE_LINK = $(CC) $(SDL3_CFLAGS) $(THREAD_CFLAGS) $(PROBE_SRC) -o /dev/null \
             $(THREAD_LIBS) $(SDL3_LIBS) $(ODBC_LIBS) $(TLS_LIBS) -lm

ifdef PROBE
ifdef HAVE_CC
ifneq ($(wildcard $(PROBE_SRC)),)
LINK_OK := $(shell $(PROBE_LINK) 2>/dev/null && echo yes)
else
LINK_OK := unchecked
endif
endif
endif

# What it says ---------------------------------------------------------------
#
#  Recursively assigned, every one of them: the hints shell out to
#  check-deps.sh and the narrow probes run a compiler, and none of that
#  should happen on a build that is going to succeed.  They expand inside
#  $(error), which is to say only on the way out.

HINT_CC   = $(or $(shell sh $(DEPS_SCRIPT) --install cc   2>/dev/null),install a C11 compiler)
HINT_LIBC = $(or $(shell sh $(DEPS_SCRIPT) --install libc 2>/dev/null),install your C library's development package)
HINT_SDL3 = $(or $(shell sh $(DEPS_SCRIPT) --install sdl3 2>/dev/null),install SDL3 with its headers -- https://libsdl.org)

#  Two ways to have no SDL3, and they send the reader to different places.
#  Nothing on the machine mentions it -- install the package.  Or
#  pkg-config names a version and the link still fails, which is the
#  runtime installed without the headers, or a -devel package left behind by
#  an upgrade: telling that reader to install SDL3 is telling them to do
#  what they did.  SDL3_WHY is set to whichever one is true at the point the
#  build gives up.
ifdef HAVE_PKG_CONFIG
SDL3_ABSENT_WHY := pkg-config knows no `sdl3' package, and $(firstword $(CC)) cannot link -lSDL3 either.
else
SDL3_ABSENT_WHY := pkg-config is not installed, and $(firstword $(CC)) cannot link -lSDL3 either.
endif

SDL3_BROKEN_WHY = This machine reports an SDL3$(if $(SDL3_VERSION), -- pkg-config names version $(SDL3_VERSION)) and $(firstword $(CC)) still cannot link $(SDL3_LIBS) against it. The runtime is installed and the development package is not, or an upgrade left the two disagreeing.

SDL3_WHY      := $(SDL3_ABSENT_WHY)
SDL3_HEADLINE := SDL3 not found.

define ERR_NO_CC

No C compiler.

CC is `$(CC)' and PATH has no such program, so nothing here can be built.

    $(HINT_CC)

Or point the build at a compiler this machine does have:

    make CC=clang

endef

define ERR_NO_SDL3

$(SDL3_HEADLINE)

st80 draws its display through SDL3.
$(SDL3_WHY)

Install it with its headers -- the runtime package alone is not enough:

    $(HINT_SDL3)

Or ask for a build with no display.  The graphics layer becomes a stub:
-bootstrap, -eval, -doctests and `make test' all work, and `./st80 -run'
refuses to open a window and says why.

    make HEADLESS=1

`make deps' reports on every requirement at once.

endef

define ERR_NO_LINK

Required libraries are missing.

$(firstword $(CC)) runs, but tools/probe.c will not link against what st80
needs.  The verdict comes first because the flags after it are whatever
this machine's pkg-config handed over, and nothing can be lined up behind
that:

    $(if $(HAVE_PTHREAD),ok     ,MISSING) pthreads  $(if $(strip $(THREAD_LIBS)),$(THREAD_LIBS),through libSystem)
    $(if $(HAVE_LIBM),ok     ,MISSING) libm      -lm
    $(if $(HAVE_SDL3),$(if $(HAVE_SDL3_LINK),ok     ,MISSING) SDL3      $(SDL3_LIBS),--     SDL3      not in this build -- HEADLESS=1)
    $(if $(HAVE_ODBC),ok     ODBC      $(ODBC_LIBS),--     ODBC      not in this build -- $(if $(NODB),NODB=1,no driver manager found; $(HINT_ODBC)))
    $(if $(HAVE_TLS),ok     OpenSSL   $(TLS_LIBS),--     OpenSSL   not in this build -- $(if $(NOTLS),NOTLS=1,not found; $(HINT_TLS)))

    $(HINT_LIBC)

The link that failed, to run by hand and read the whole error:

    $(PROBE_LINK)

`make deps' reports on every requirement at once.

endef

# Enforcement ----------------------------------------------------------------
#
#  In order, and the order is the point.  The compiler is the instrument
#  every other check here is made with, so a missing one would otherwise
#  report every library as missing for the same wrong reason -- and SDL3 is
#  asked about before the link, because "no SDL3 anywhere" and "an SDL3 that
#  will not link" are different sentences and only one of them is true.

ifdef STOP

ifndef HAVE_CC
$(error $(ERR_NO_CC))
endif

#  HEADLESS=1 clears HAVE_SDL3 above, which is the same emptiness a machine
#  with no SDL3 produces -- so this asks whether the stub was requested
#  before it decides the library is missing.  Without that, `make
#  HEADLESS=1' refused to build for want of the library it had just been
#  told not to use.
ifndef HEADLESS
ifndef HAVE_SDL3
$(error $(ERR_NO_SDL3))
endif
endif

ifndef LINK_OK
#  Only now, with the build stopping whatever the answer is, is it worth
#  three more compiler invocations to say which piece was at fault.
HAVE_PTHREAD := $(shell printf 'int main(void) { pthread_t t; return pthread_create(&t, 0, 0, 0); }\n' | \
                  $(CC) -x c - -include pthread.h $(THREAD_CFLAGS) -o /dev/null $(THREAD_LIBS) 2>/dev/null && echo yes)
HAVE_LIBM    := $(shell printf 'int main(void) { volatile double x = 4.0; return (int) sqrt(x) - 2; }\n' | \
                  $(CC) -x c - -include math.h -o /dev/null -lm 2>/dev/null && echo yes)
HAVE_SDL3_LINK := $(if $(HAVE_SDL3),$(shell printf 'int main(void) { return 0; }\n' | \
                  $(CC) -x c - $(SDL3_CFLAGS) -include SDL3/SDL.h -o /dev/null $(SDL3_LIBS) 2>/dev/null && echo yes))

#  A C library that cannot produce a thread or a square root is the deeper
#  fault and gets the message, even when SDL3 is broken too -- fixing SDL3
#  on such a machine would only reach the next failure.  When libc is fine
#  and SDL3 alone will not link, the reader wants the SDL3 message and not a
#  table of two libraries that work.
ifeq ($(and $(HAVE_PTHREAD),$(HAVE_LIBM)),)
$(error $(ERR_NO_LINK))
else
SDL3_WHY      := $(SDL3_BROKEN_WHY)
SDL3_HEADLINE := SDL3 will not link.
$(error $(ERR_NO_SDL3))
endif
endif

endif

INCLUDES  := -Isrc -Isrc/port -Isrc/om -Isrc/interp -Isrc/gfx -Isrc/sched \
             -Isrc/compiler -Isrc/boot -Isrc/db -Isrc/net -Isrc/crypto -Itests
CPPFLAGS  := $(INCLUDES) -D_GNU_SOURCE $(SDL3_CFLAGS) $(ODBC_CFLAGS) $(TLS_CFLAGS)

ifeq ($(OM),bb)
    CPPFLAGS += -DST_OM_BB
else ifeq ($(OM),mt)
    CPPFLAGS += -DST_OM_MT
else
    $(error OM must be 'bb' or 'mt', got '$(OM)')
endif

CFLAGS    ?= $(CSTD) $(WARN) $(OPT)
CFLAGS    += $(THREAD_CFLAGS) $(SAN_CFLAGS)
LDFLAGS   ?=
LIBS      := $(THREAD_LIBS) $(SAN_LIBS) $(SDL3_LIBS) $(ODBC_LIBS) $(TLS_LIBS) -lm

# The sanitizer variant is part of the build directory name.  Without this,
# switching TSAN on would silently link freshly instrumented test code
# against stale uninstrumented library objects, and the sanitizer would
# report nothing because it could not see most of the program.
#  HEADLESS is in the name for the same reason the sanitizers are: a stub
#  display and a real one are different programs built from the same tree,
#  and sharing build/mt between them means `make HEADLESS=1' followed by
#  `make' relinks against whichever objects happened to survive.
#
#  NODB is in it for exactly that reason, found exactly that way: `make
#  NODB=1' and then `make' produced a binary that still had the stub ODBC
#  compiled in, and the live database suite failed 22 tests saying this
#  build has no ODBC -- on a machine where it does, from a tree that had
#  just been told to build it.  A flag that changes which code is compiled
#  has to change where the objects go, or the second build is a lie.
BUILD_VARIANT := $(OM)$(if $(HEADLESS),-headless)$(if $(NODB),-nodb)$(if $(NOTLS),-notls)$(if $(TSAN),-tsan)$(if $(ASAN),-asan)
BUILD_DIR := build/$(BUILD_VARIANT)
OBJ_DIR   := $(BUILD_DIR)/obj
TEST_DIR  := $(BUILD_DIR)/tests

# Sources -------------------------------------------------------------------
#
# Everything except src/om is unconditional.  The object memory is chosen by
# OM= so that exactly one implementation is compiled in and the om.h macros
# inline fully -- see doc/OBJECT-MEMORY.md.

CORE_SRC  := $(wildcard src/port/*.c) \
             $(wildcard src/interp/*.c) \
             $(wildcard src/sched/*.c) \
             $(wildcard src/gfx/*.c) \
             $(wildcard src/compiler/*.c) \
             $(wildcard src/db/*.c) \
             $(wildcard src/net/*.c) \
             $(wildcard src/crypto/*.c) \
             $(wildcard src/boot/*.c)

# Object-memory sources: the selected implementation plus every file in
# src/om that is not tied to a particular one (census, shared helpers).
OM_VARIANT_SRC := $(wildcard src/om/om_*.c) $(wildcard src/om/image_*.c)
OM_SRC    := $(wildcard src/om/om_$(OM).c) \
             $(wildcard src/om/image_$(OM).c) \
             $(filter-out $(OM_VARIANT_SRC),$(wildcard src/om/*.c))

LIB_SRC   := $(CORE_SRC) $(OM_SRC)
LIB_OBJ   := $(patsubst %.c,$(OBJ_DIR)/%.o,$(LIB_SRC))
LIB_AR    := $(BUILD_DIR)/libst.a

MAIN_SRC  := src/main.c
MAIN_OBJ  := $(patsubst %.c,$(OBJ_DIR)/%.o,$(MAIN_SRC))

# The binary lives in the variant's build directory and is copied to the top
# level.  With a single shared path, switching OM= left a newer st80 sitting
# there and make reported nothing to do, silently running the other memory.
VARIANT_BIN := $(BUILD_DIR)/st80
BIN         := st80

.PHONY: all clean test unit-test help deps
.NOTPARALLEL:
.DEFAULT_GOAL := all

all: $(BIN)

$(OBJ_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

#  Built from scratch, never updated in place.
#
#  `ar r' ADDS and REPLACES and never removes, so an archive outlives the
#  sources it was made from: delete a .c and its .o stays a member for ever.
#  That is not a stale build in the usual harmless sense.  When the deleted
#  file defined a symbol the new one also defines -- font8x8.c and
#  font8x12.c both define ST_FONT_GLYPHS -- the link resolves to whichever
#  member it reaches first, and picking the old one meant reading a table of
#  eight rows per glyph as though it had twelve.  Every character came out
#  of the following characters' rows.  It looked exactly like a font bug and
#  was not one.
$(LIB_AR): $(LIB_OBJ)
	@mkdir -p $(dir $@)
	@rm -f $@
	ar rcs $@ $^

$(VARIANT_BIN): $(MAIN_OBJ) $(LIB_AR)
	$(CC) $(CFLAGS) $(MAIN_OBJ) $(LIB_AR) -o $@ $(LDFLAGS) $(LIBS)

#
#  The convenience copy at the top of the tree.
#
#  Forced, not timestamp-driven.  ./st80 is whichever variant was built last,
#  and after "make OM=bb" it is NEWER than build/mt/st80 -- so a following
#  "make OM=mt" answers "nothing to be done" and leaves the Blue Book binary
#  sitting there under a name that now means something else.  Every command
#  then fails in a way that has nothing to do with the change being made.
#  A copy costs nothing; guessing wrong costs an afternoon.
#
#
#  A SANITIZER build is not copied, and that is not tidiness.  A TSAN binary
#  interprets 50 times slower than a plain one -- 5 million bytecodes in 33
#  seconds against 0.56 -- and nothing about the file at the top level says
#  which it is, so `make OM=mt TSAN=1' followed by `./st80 -run' looks
#  exactly like a system that has become desperately slow.  The test targets
#  run $(VARIANT_BIN) out of the build directory and never wanted the copy;
#  only a person typing ./st80 does.
#
.PHONY: $(BIN)
$(BIN): $(VARIANT_BIN)
ifeq ($(strip $(TSAN)$(ASAN)),)
	@cp -f $< $@
else
	@echo "  $(BUILD_VARIANT) build left in $(VARIANT_BIN); ./st80 untouched"
endif

# Unit tests ----------------------------------------------------------------

UNIT_SRC  := $(wildcard tests/unit/test_*.c)
UNIT_BIN  := $(patsubst tests/unit/%.c,$(TEST_DIR)/%,$(UNIT_SRC))

$(TEST_DIR)/%: tests/unit/%.c $(LIB_AR)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(LIB_AR) -o $@ $(LDFLAGS) $(LIBS)

# Benchmarks ----------------------------------------------------------------
#
# Built but not run by `make test`: a scaling measurement takes minutes and
# wants a quiet machine, which is the opposite of what a test suite wants.
# `make bench` runs it.

BENCH_SRC := $(wildcard tests/bench/bench_*.c)
BENCH_BIN := $(patsubst tests/bench/%.c,$(TEST_DIR)/%,$(BENCH_SRC))

$(TEST_DIR)/bench_%: tests/bench/bench_%.c $(LIB_AR)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(LIB_AR) -o $@ $(LDFLAGS) $(LIBS)

#
#  Regenerate the built-in face.  NOT part of any build: src/gfx/font_face.c
#  and .h are checked in, so building needs no font and no Python.  This is
#  here only to change the face, its size, or its leading -- and it needs
#  Pillow and the font itself installed.
#
#      make font                                   # Inter 18, lead 3
#      make font FONT=/path/to/X.ttf SIZE=15 LEAD=2
#
#  Rebuild afterwards, and rebuild any image too: the face is compiled into
#  an image at bootstrap and an image cannot be shown a different one.
#
FONT ?= /usr/share/fonts/rsms-inter-fonts/Inter-Regular.ttf
SIZE ?= 18
LEAD ?= 3

.PHONY: font
font:
	python3 tools/make_font.py "$(FONT)" $(SIZE) $(LEAD) src/gfx
	@echo "regenerated src/gfx/font_face.[ch] -- now: make && rebuild your image"

.PHONY: bench
bench: $(BENCH_BIN)
	@for b in $(BENCH_BIN); do \
	    echo "==> $$b"; \
	    "$$b" || exit 1; \
	done

# The demo application's server image: demo/README.md and doc/WebDemo.md.
# The startup is baked in at bootstrap because the bootstrap's compiler
# resolves globals through tables a loaded image does not have.
.PHONY: demo-image
demo-image: $(BIN)
	./st80 -bootstrap -profile profiles/st2026.profile -startup 'RestServer serve' -o demo.im
	@echo "built demo.im -- now: ./st80 -serve demo.im demo/server.json"

test: unit-test suite-test

# The imported packages' own suites -------------------------------------------
#
# The unit tests check this system against itself.  Only a package's own tests
# can say whether the package still works, and those live in the image -- so
# this builds each profile and asks it, holding the score to
# tests/profiles.expected.  See tests/run_profiles.sh for why the comparison
# matters more than the run.
#
# Only under OM=mt: the bootstrap writes the 64-bit format, and under the Blue
# Book memory it refuses before it reaches a single test.  Skipped rather than
# failed there, the same way test_trace skips under mt.

.PHONY: suite-test
suite-test: $(VARIANT_BIN)
ifeq ($(OM),mt)
	@echo "==> imported package suites"
	@sh tests/run_profiles.sh $(VARIANT_BIN) tests/profiles.expected
else
	@echo "==> imported package suites"
	@echo "skipped: the bootstrap targets the 64-bit object memory"
endif

unit-test: $(UNIT_BIN)
	@status=0; \
	for t in $(UNIT_BIN); do \
	    echo "==> $$t"; \
	    "$$t" || status=1; \
	done; \
	exit $$status

#
#  Everything a build, a test run or a bootstrap leaves behind.
#
#  .gitignore is the list, because it already had to decide what is generated
#  and what is source -- "The bootstrapped image is a build product" is its
#  words.  clean used to remove two of the things on it and leave the rest,
#  so `make clean' left an image and a pile of screenshots behind and the
#  tree was not clean.
#
#  Two things on that list are deliberately NOT removed.  oracle/ is ignored
#  because it may never be committed, not because it is generated: it is the
#  Xerox tape and nothing here can make another.  And *.pdf is a rendering of
#  a document whose source is tracked -- no rule here produces one, so no
#  rule here should delete one.
#
clean:
	rm -rf build $(BIN)
	rm -f *.image *.changes
	rm -f screen*.pbm screen*.cov

#
#  The same probes make runs, reporting on all of them instead of stopping
#  at the first.  This is the target to name when someone says the build
#  will not go: it answers "what does this machine have" in one screen,
#  and it runs on a machine too bare to build anything.
#
deps:
	@CC='$(CC)' PKG_CONFIG='$(PKG_CONFIG)' HEADLESS='$(HEADLESS)' NODB='$(NODB)' NOTLS='$(NOTLS)' sh $(DEPS_SCRIPT)

help:
	@echo "Targets:"
	@echo "  all          (default) build the st80 binary"
	@echo "  test         build and run the unit tests and the package suites"
	@echo "  unit-test    just the unit tests"
	@echo "  suite-test   just the imported packages' own SUnit suites"
	@echo "  bench        the parallel scaling benchmark"
	@echo "  deps         report on SDL3 and the other external requirements"
	@echo "  clean        remove build artifacts"
	@echo "  font         regenerate the built-in face (needs Pillow and a font)"
	@echo "  demo-image   bootstrap the demo application's server image (demo/README.md)"
	@echo
	@echo "Variables:"
	@echo "  OM=mt        64-bit threaded object memory (default) -- the system"
	@echo "  OM=bb        16-bit Blue Book memory -- the Xerox trace harness"
	@echo "  HEADLESS=1   build without SDL3 -- the display becomes a stub"
	@echo "  NODB=1       build without ODBC -- the Database package refuses to connect"
	@echo "  NOTLS=1      build without OpenSSL -- an https URL is refused by name"
	@echo "  TSAN=1       build with the thread sanitizer"
	@echo "  ASAN=1       build with address/UB sanitizers"
	@echo "  OPT=-O0      override optimization flags"
	@echo "  FONT=, SIZE=, LEAD=   inputs to the font target"
	@echo
	@echo "SDL3: $(if $(HAVE_SDL3),found -- graphics enabled,$(if $(HEADLESS),not used -- HEADLESS=1 was asked for,NOT FOUND -- make will stop; run 'make deps'))"

-include $(shell find build -name '*.d' 2>/dev/null)
