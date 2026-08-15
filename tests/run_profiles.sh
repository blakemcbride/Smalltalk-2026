#!/bin/sh
#
#  Run every profile's own SUnit suites and hold them to tests/profiles.expected.
#
#  The unit tests in tests/unit check this system against itself: the C
#  compiler against the image's compiler, the object memory against its own
#  invariants, the interpreter against Xerox's traces.  None of them can say
#  whether a package we IMPORTED still works, because the only authority on
#  that is the package's own tests, and those live in the image.
#
#  So this runs them the only way they can be run -- build the image, ask it
#  -- and compares the score against a checked-in file.  That comparison is
#  the whole point.  Running the suites and printing the result is something
#  a person has to remember to look at; the Chronology package was declared
#  done on per-class runs while the whole-image run reported nothing at all,
#  and stayed that way for weeks because the number nobody printed was the
#  number nobody read.
#
#  Both directions fail:
#
#    fewer passing   a regression, which is the obvious one
#    fewer RUN       tests that stopped being found, which looks like success
#                    and is worse
#    more of either  an unrecorded improvement, which nothing is protecting
#
#  And one thing that is not a score at all: a profile that substitutes a
#  class may drop protocol something still sends.  The loader reports those
#  and separates the ones nothing else answers -- the holes -- from the far
#  larger number of names that merely collide with another class's.  A hole
#  fails here, with no expected count, because the only correct number is
#  zero.
#
#  Usage: run_profiles.sh <path-to-st80> [expectations-file]
#
set -u

ST80=${1:?usage: run_profiles.sh <st80> [expected]}
EXPECTED=${2:-tests/profiles.expected}

if [ ! -x "$ST80" ]; then
    echo "run_profiles: $ST80 is not executable" >&2
    exit 1
fi
if [ ! -f "$EXPECTED" ]; then
    echo "run_profiles: cannot read $EXPECTED" >&2
    exit 1
fi

status=0
#  Two counters, because they answer different questions: `attempted' says the
#  expectations file had lines in it, and `checked' says a score came back.  A
#  single counter reported "no profiles were checked" whenever every profile
#  failed, which reads as an empty file and is the opposite of what happened.
attempted=0
checked=0

while read -r name want_run want_passed rest; do
    case "$name" in ''|'#'*) continue ;; esac
    attempted=$((attempted + 1))
    profile="profiles/$name.profile"
    if [ ! -f "$profile" ]; then
        echo "  FAIL $name: $profile does not exist"
        status=1
        continue
    fi

    #  -tests exits non-zero when anything failed, which is not the question
    #  here -- the question is whether the SCORE moved -- so the exit code is
    #  deliberately ignored and the summary line is parsed instead.
    out=$("$ST80" -bootstrap -profile "$profile" -tests 2>&1)
    line=$(printf '%s\n' "$out" | grep -E '^[0-9]+ run, ' | tail -1)

    #  A HOLE is protocol a supersession dropped that something still sends
    #  and nothing at all answers -- a doesNotUnderstand waiting for the
    #  first caller to take that path.  The loader finds them; this is what
    #  makes finding them matter.  There is no expected count and no
    #  ratchet line: the number is zero, because any other number is a
    #  method that is going to fail.
    #
    #  Names a supersession dropped that ANOTHER class answers, and names
    #  nothing sends at all, are reported by the loader and not checked
    #  here.  Both are ordinary and neither is a fault.
    if printf '%s\n' "$out" | grep -q 'these are holes'; then
        echo "  FAIL $name: superseded protocol that something sends and nothing answers"
        printf '%s\n' "$out" | sed -n '/these are holes/,/^st80: superseded protocol whose\|^st80: superseded protocol that nothing\|^st80: [0-9]* selector/p' \
            | grep -v '^st80:' | sed 's/^/      /'
        status=1
    fi

    if [ -z "$line" ]; then
        #  No summary at all: the run died before reporting.  This is the
        #  case that used to be invisible.
        echo "  FAIL $name: the suites did not report at all"
        printf '%s\n' "$out" | tail -3 | sed 's/^/        /'
        status=1
        continue
    fi

    got_run=$(printf '%s\n' "$line" | sed 's/ run,.*//')
    got_passed=$(printf '%s\n' "$line" | sed 's/.* run, //; s/ passed.*//')
    checked=$((checked + 1))

    if [ "$got_run" -lt "$want_run" ] || [ "$got_passed" -lt "$want_passed" ]; then
        echo "  FAIL $name: $line"
        echo "        expected at least $want_run run and $want_passed passed -- this is a REGRESSION"
        status=1
    elif [ "$got_run" -gt "$want_run" ] || [ "$got_passed" -gt "$want_passed" ]; then
        echo "  FAIL $name: $line"
        echo "        better than the recorded $want_run run / $want_passed passed."
        echo "        Update $EXPECTED in the commit that earned it."
        status=1
    else
        echo "  ok   $name: $line"
    fi
done < "$EXPECTED"

if [ "$attempted" -eq 0 ]; then
    echo "  FAIL no profiles listed -- is $EXPECTED empty?"
    status=1
fi

if [ "$status" -eq 0 ]; then
    echo "ok: $checked profiles at their recorded scores"
fi
exit $status
