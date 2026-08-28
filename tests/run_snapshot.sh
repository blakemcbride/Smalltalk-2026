#!/bin/sh
#
#  A snapshot taken on a worker pool, and the image it writes, resumed.
#
#  Bugs3 B9.  A snapshot from `st80 -serve' on two workers wrote an image
#  that could not be resumed: primitive 97 parked the registers into THIS
#  worker's process, and a reload resumed from the scheduler's activeProcess
#  field, which with several workers names whichever process any worker
#  switched to last -- the InputState process worker 1 had picked up.  And
#  a process running on the other worker at that instant was written with
#  the context it had been parked with last time, on no list, and was gone
#  from the image that came back.
#
#  So this runs the case exactly as the finding did: an image on two
#  workers, a process spinning on the other one, a snapshot taken with no
#  process switch before it, and then the snapshot resumed on two workers.
#  The first run must say it wrote the file and that the spinner is still
#  counting afterwards; the second must say it resumed, that the spinner
#  came back with it, and must not report that every process is blocked.
#
#  A shell check rather than a C one because what is being tested is a
#  process boundary: the file has to be written by one st80 and read by
#  another.  Usage: run_snapshot.sh <path-to-st80>
#
set -u

ST80=${1:?usage: run_snapshot.sh <st80>}
DIR=build/snapshot-test

if [ ! -x "$ST80" ]; then
    echo "run_snapshot: $ST80 is not executable" >&2
    exit 1
fi
mkdir -p "$DIR"
rm -f "$DIR"/base.im "$DIR"/snap.im "$DIR"/prog.st "$DIR"/first.log "$DIR"/second.log

#  The image evaluates each line of its first argument, as Bugs3's probe
#  image does; results go to standard error.
STARTUP='[:strm | [strm atEnd] whileFalse: [[:line | line isEmpty ifTrue: [] ifFalse: [
    (line , '"'"' ==> '"'"', ([(Compiler evaluate: line) printString]
        on: Error do: [:e | '"'"'ERR '"'"', e class name, '"'"': '"'"', (e messageText ifNil: ['"'"'<nil>'"'"'])])) displayNl]]
  value: (strm upTo: Character lf)]]
    value: (ReadStream on: (Smalltalk arguments isEmpty ifTrue: ['"'"''"'"'] ifFalse: [Smalltalk arguments first]))'

if ! "$ST80" -bootstrap -profile profiles/st2026.profile -startup "$STARTUP" \
        -o "$DIR/base.im" > "$DIR/bootstrap.log" 2>&1; then
    echo "run_snapshot: the bootstrap failed; see $DIR/bootstrap.log" >&2
    exit 1
fi

cat > "$DIR/prog.st" <<PROG
| spin c1 |
spin := Array with: 0.
Smalltalk at: #SnapshotTestSpin put: spin.
[[true] whileTrue: [spin at: 1 put: (spin at: 1) + 1]] forkAt: 3.
(Delay forMilliseconds: 50) wait.
(FileStream fileNamed: '$DIR/snap.im') beSnapshotFile; readWrite; close.
Smalltalk snapshotPrimitive isNil
    ifTrue: ['snapshot written' displayNl]
    ifFalse: ['snapshot resumed' displayNl].
(Delay forMilliseconds: 50) wait.
c1 := spin at: 1.
(Delay forMilliseconds: 100) wait.
(spin at: 1) > c1
    ifTrue: ['spinner counting' displayNl]
    ifFalse: ['spinner dead' displayNl].
^#done
PROG

status=0
timeout -k 2 60 "$ST80" -serve "$DIR/base.im" -workers 2 \
    "Compiler evaluate: (FileStream oldFileNamed: '$DIR/prog.st') contentsOfEntireFile" \
    > "$DIR/first.log" 2>&1
if ! grep -q 'snapshot written' "$DIR/first.log" \
   || ! grep -q 'spinner counting' "$DIR/first.log" \
   || [ ! -s "$DIR/snap.im" ]; then
    echo "run_snapshot: FAIL the snapshot was not written, or the image did not go on; see $DIR/first.log" >&2
    status=1
fi

timeout -k 2 60 "$ST80" -serve "$DIR/snap.im" -workers 2 > "$DIR/second.log" 2>&1
if ! grep -q 'snapshot resumed' "$DIR/second.log" \
   || ! grep -q 'spinner counting' "$DIR/second.log" \
   || grep -q 'every process is blocked' "$DIR/second.log"; then
    echo "run_snapshot: FAIL the snapshot did not resume with its processes; see $DIR/second.log" >&2
    status=1
fi

if [ $status -eq 0 ]; then
    echo "  a snapshot from two workers, with a process running on the other, resumed on two"
fi
exit $status
