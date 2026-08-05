# Smalltalk-2026 -- a parallel Smalltalk-80 in C
#
# Targets:
#   all        (default) build the st80 binary
#   test       build and run the unit tests
#   clean      remove build artifacts
#   help       list targets and variables
#
# Variables:
#   OM=bb      faithful 16-bit Blue Book object memory (default; the
#              validation harness that must reproduce the Xerox traces)
#   OM=mt      64-bit threaded object memory (the real system)
#   TSAN=1     build with the thread sanitizer
#   ASAN=1     build with the address and undefined-behaviour sanitizers

CC        ?= gcc
CSTD      := -std=c11
WARN      := -Wall -Wextra -Wpedantic -Werror=implicit-function-declaration
OPT       ?= -O2 -g

OM        ?= bb

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

# SDL3 is optional: without it the graphics layer builds as a headless stub
# so the test suite and CI need no display.
SDL3_CFLAGS := $(shell pkg-config --cflags sdl3 2>/dev/null)
SDL3_LIBS   := $(shell pkg-config --libs   sdl3 2>/dev/null)
ifneq ($(SDL3_LIBS),)
    SDL3_CFLAGS += -DST_HAVE_SDL3
endif

INCLUDES  := -Isrc -Isrc/port -Isrc/om -Isrc/interp -Isrc/gfx -Isrc/sched \
             -Isrc/compiler -Isrc/boot -Itests
CPPFLAGS  := $(INCLUDES) -D_GNU_SOURCE $(SDL3_CFLAGS)

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
LIBS      := $(THREAD_LIBS) $(SAN_LIBS) $(SDL3_LIBS) -lm

# The sanitizer variant is part of the build directory name.  Without this,
# switching TSAN on would silently link freshly instrumented test code
# against stale uninstrumented library objects, and the sanitizer would
# report nothing because it could not see most of the program.
BUILD_VARIANT := $(OM)$(if $(TSAN),-tsan)$(if $(ASAN),-asan)
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

.PHONY: all clean test unit-test help
.NOTPARALLEL:
.DEFAULT_GOAL := all

all: $(BIN)

$(OBJ_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

$(LIB_AR): $(LIB_OBJ)
	@mkdir -p $(dir $@)
	ar rcs $@ $^

$(VARIANT_BIN): $(MAIN_OBJ) $(LIB_AR)
	$(CC) $(CFLAGS) $(MAIN_OBJ) $(LIB_AR) -o $@ $(LDFLAGS) $(LIBS)

$(BIN): $(VARIANT_BIN)
	cp -f $< $@

# Unit tests ----------------------------------------------------------------

UNIT_SRC  := $(wildcard tests/unit/test_*.c)
UNIT_BIN  := $(patsubst tests/unit/%.c,$(TEST_DIR)/%,$(UNIT_SRC))

$(TEST_DIR)/%: tests/unit/%.c $(LIB_AR)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(LIB_AR) -o $@ $(LDFLAGS) $(LIBS)

test: unit-test

unit-test: $(UNIT_BIN)
	@status=0; \
	for t in $(UNIT_BIN); do \
	    echo "==> $$t"; \
	    "$$t" || status=1; \
	done; \
	exit $$status

clean:
	rm -rf build $(BIN)

help:
	@echo "Targets:"
	@echo "  all          (default) build the st80 binary"
	@echo "  test         build and run the unit tests"
	@echo "  clean        remove build artifacts"
	@echo
	@echo "Variables:"
	@echo "  OM=bb        16-bit Blue Book object memory (default)"
	@echo "  OM=mt        64-bit threaded object memory"
	@echo "  TSAN=1       build with the thread sanitizer"
	@echo "  ASAN=1       build with address/UB sanitizers"
	@echo "  OPT=-O0      override optimization flags"
	@echo
	@echo "SDL3: $(if $(SDL3_LIBS),found -- graphics enabled,not found -- headless build)"

-include $(shell find build -name '*.d' 2>/dev/null)
