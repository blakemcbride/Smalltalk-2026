#!/bin/sh
#
#  timeout(1), where there is one, and the same thing where there is not.
#
#      sh tools/timeout.sh [-k GRACE] SECONDS COMMAND [ARGUMENT...]
#
#  Runs COMMAND; if it is still running after SECONDS, sends it SIGTERM,
#  and with -k, SIGKILL GRACE seconds after that.  Exits with COMMAND's
#  status, or 124 when the time ran out -- the GNU convention, so a caller
#  can tell a hang from a failure the same way on every system.
#
#  Stock macOS has no timeout(1); Homebrew's coreutils spells it gtimeout.
#  The snapshot check and every serve check in test_serve_faults called
#  `timeout' by name, so on a Mac each one exited 127 and failed, while
#  macOS.md said the whole suite runs there (Bugs5 DOCS-8).  So the gates
#  call this instead: GNU's when it is installed under either name, and a
#  watchdog in plain sh when it is not.
#
#  ST_TIMEOUT_WATCHDOG=1 skips the first two and uses the watchdog, which
#  is how test_serve_faults checks the path a Mac takes from a machine that
#  has timeout(1).
#
set -u

grace=
if [ "${1:-}" = -k ]; then
    grace=${2:?usage: timeout.sh [-k grace] seconds command...}
    shift 2
fi
seconds=${1:?usage: timeout.sh [-k grace] seconds command...}
shift
[ $# -gt 0 ] || { echo "usage: timeout.sh [-k grace] seconds command..." >&2; exit 125; }

if [ -z "${ST_TIMEOUT_WATCHDOG:-}" ]; then
    for t in timeout gtimeout; do
        if command -v "$t" >/dev/null 2>&1; then
            if [ -n "$grace" ]; then
                exec "$t" -k "$grace" "$seconds" "$@"
            fi
            exec "$t" "$seconds" "$@"
        fi
    done
fi

#
#  The watchdog.
#
#  The command goes in the background with this script's standard input
#  handed to it by name: a background command in a non-interactive shell
#  otherwise reads /dev/null.  The watchdog's own output goes to /dev/null,
#  and that is not tidiness: it would otherwise hold the caller's pipe open
#  for as long as its sleep ran, and a caller reading to end of file -- the
#  C tests read through popen -- would wait out the whole limit after the
#  command had long finished.  For the same reason the watchdog kills its
#  sleep when it is itself told to stop.
#
#  Whether it fired is a file and not the watchdog's being alive: with -k
#  it stays alive through the grace period, after the command it stopped
#  has already gone.
#
fired=$(mktemp "${TMPDIR:-/tmp}/st2026-timeout.XXXXXX") || exit 125

exec 3<&0
"$@" 0<&3 3<&- &
child=$!
exec 3<&-

(
    exec >/dev/null 2>&1
    sleep "$seconds" &
    nap=$!
    trap 'kill $nap 2>/dev/null; exit 0' TERM
    wait $nap
    echo fired > "$fired"
    kill -TERM $child 2>/dev/null || exit 0
    if [ -n "$grace" ]; then
        sleep "$grace" &
        nap=$!
        wait $nap
        kill -KILL $child 2>/dev/null
    fi
    exit 0
) &
watchdog=$!

wait $child
status=$?

kill -TERM $watchdog 2>/dev/null
wait $watchdog 2>/dev/null
if [ -s "$fired" ]; then
    status=124
fi
rm -f "$fired"
exit $status
