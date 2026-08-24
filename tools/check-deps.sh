#!/bin/sh
#
#  What st80 needs from the machine, and whether it is there.
#
#  Two callers, two jobs:
#
#    sh tools/check-deps.sh              report on every requirement
#    sh tools/check-deps.sh --install X  print the command that installs X
#
#  The Makefile stops at the first missing requirement, because a build that
#  cannot finish should say so once and get out of the way.  This reports on
#  all of them and does not stop, because the question a person asks before
#  building -- "what do I have to install" -- is not answered by learning
#  about one package at a time across four failed builds.
#
#  The --install table lives here and only here.  The Makefile's messages
#  quote it rather than keeping a second copy: a distribution that renames
#  its SDL3 package should not be able to make one of them right and the
#  other one wrong.
#
#  Exit status: 0 if a full graphical build is possible, 1 otherwise.  A
#  missing SDL3 counts, since `make' refuses it unless HEADLESS=1 is asked
#  for -- and the report says so where it says it.

set -u

CC=${CC:-gcc}
PKG_CONFIG=${PKG_CONFIG:-pkg-config}
HEADLESS=${HEADLESS:-}
NODB=${NODB:-}

#  ---------------------------------------------------------------------
#  The install table.
#
#  Keyed by what is missing, then by which package manager is on PATH.  The
#  Darwin case is decided by uname and not by PATH, because a Mac with
#  Homebrew still has no dnf and would otherwise fall through to the
#  generic line.
#  ---------------------------------------------------------------------

install_hint() {
    fedora= debian= arch= suse= alpine= mac= generic=

    case ${1:-} in
    cc)
        fedora='sudo dnf install gcc make'
        debian='sudo apt install build-essential'
        arch='sudo pacman -S base-devel'
        suse='sudo zypper install gcc make'
        alpine='sudo apk add build-base'
        mac='xcode-select --install'
        generic='install a C11 compiler (gcc 4.9 or later, clang 3.6 or later) and GNU make'
        ;;
    libc)
        fedora='sudo dnf install glibc-devel'
        debian='sudo apt install libc6-dev'
        arch='sudo pacman -S glibc'
        suse='sudo zypper install glibc-devel'
        alpine='sudo apk add musl-dev'
        mac='xcode-select --install'
        generic="install your C library's development package -- it is what carries libm and pthreads"
        ;;
    pkg-config)
        fedora='sudo dnf install pkgconf-pkg-config'
        debian='sudo apt install pkg-config'
        arch='sudo pacman -S pkgconf'
        suse='sudo zypper install pkg-config'
        alpine='sudo apk add pkgconf'
        mac='brew install pkg-config'
        generic='install pkg-config (or pkgconf)'
        ;;
    sdl3)
        fedora='sudo dnf install SDL3-devel'
        debian='sudo apt install libsdl3-dev'
        arch='sudo pacman -S sdl3'
        suse='sudo zypper install SDL3-devel'
        alpine='sudo apk add sdl3-dev'
        mac='brew install sdl3'
        generic='install SDL3 with its headers -- https://libsdl.org'
        ;;
    odbc)
        fedora='sudo dnf install unixODBC-devel'
        debian='sudo apt install unixodbc-dev'
        arch='sudo pacman -S unixodbc'
        suse='sudo zypper install unixODBC-devel'
        alpine='sudo apk add unixodbc-dev'
        mac='brew install unixodbc'
        generic='install unixODBC with its headers -- https://www.unixodbc.org'
        ;;
    *)
        echo "check-deps: no install hint for '${1:-}'" >&2
        return 1
        ;;
    esac

    if [ "$(uname -s)" = Darwin ]; then
        echo "$mac"
    elif command -v dnf    >/dev/null 2>&1; then echo "$fedora"
    elif command -v apt-get >/dev/null 2>&1; then echo "$debian"
    elif command -v pacman >/dev/null 2>&1; then echo "$arch"
    elif command -v zypper >/dev/null 2>&1; then echo "$suse"
    elif command -v apk    >/dev/null 2>&1; then echo "$alpine"
    else echo "$generic"
    fi
}

case ${1:-} in
--install)
    install_hint "${2:-}"
    exit $?
    ;;
--help|-h)
    sed -n '2,26p' "$0" | sed 's/^#\{1,2\} \{0,1\}//'
    exit 0
    ;;
'') ;;
*)
    echo "check-deps: unknown argument '$1'" >&2
    exit 2
    ;;
esac

#  ---------------------------------------------------------------------
#  The report.
#  ---------------------------------------------------------------------

tmp=$(mktemp -d 2>/dev/null || mktemp -d -t st80deps) || exit 1
trap 'rm -rf "$tmp"' EXIT INT TERM HUP

status=0
missing=''

#  Apple folds pthreads into libSystem and wants neither -pthread nor
#  -lpthread; everyone else wants both.  Same rule as the Makefile's.
if [ "$(uname -s)" = Darwin ]; then
    THREAD_FLAGS=''
else
    THREAD_FLAGS='-pthread'
fi

note() {        # note <what> <how it is asked for> <verdict>
    printf '  %-12s %-26s %s\n' "$1" "$2" "$3"
}

want() {        # want <install-key> -- remember that something is missing
    status=1
    missing="$missing $1"
}

#  A compiler is the instrument every other check here is made with, so a
#  missing one is not one line of the report -- it is the end of it.
if cc_path=$(command -v "$CC" 2>/dev/null); then
    cc_version=$("$CC" --version 2>/dev/null | sed -n 1p)
    [ -n "$cc_version" ] || cc_version=$cc_path
else
    echo "C compiler: CC is '$CC' and PATH has no such program."
    echo
    echo "    $(install_hint cc)"
    echo
    echo "Nothing else can be checked without one."
    exit 1
fi

echo "st80 external requirements, as this machine answers for them:"
echo

note 'C compiler' "$CC" "$cc_version"

#  pkg-config is how SDL3 is normally located, and is not itself required:
#  the SDL3 check below falls back to asking the compiler.
if command -v "$PKG_CONFIG" >/dev/null 2>&1; then
    note 'pkg-config' "$PKG_CONFIG" "$("$PKG_CONFIG" --version 2>/dev/null)"
    have_pkg_config=yes
else
    note 'pkg-config' "$PKG_CONFIG" 'not found -- optional, see SDL3 below'
    have_pkg_config=''
fi

cat > "$tmp/pthread.c" <<'EOF'
#include <pthread.h>
static void *body(void *p) { return p; }
int main(void)
{
    pthread_t t;
    if (pthread_create(&t, NULL, body, NULL) == 0)
        pthread_join(t, NULL);
    return 0;
}
EOF

# shellcheck disable=SC2086
if "$CC" "$tmp/pthread.c" -o "$tmp/a.out" $THREAD_FLAGS >/dev/null 2>&1; then
    note 'pthreads' "${THREAD_FLAGS:-libSystem}" 'links'
else
    note 'pthreads' "${THREAD_FLAGS:-libSystem}" 'WILL NOT LINK'
    want libc
fi

#  The volatile is load-bearing: sqrt(4.0) is folded at compile time and the
#  probe would link on a machine with no libm at all.
cat > "$tmp/libm.c" <<'EOF'
#include <math.h>
int main(void) { volatile double x = 4.0; return (int) sqrt(x) - 2; }
EOF

if "$CC" "$tmp/libm.c" -o "$tmp/a.out" -lm >/dev/null 2>&1; then
    note 'libm' '-lm' 'links'
else
    note 'libm' '-lm' 'WILL NOT LINK'
    want libc
fi

cat > "$tmp/sdl3.c" <<'EOF'
#include <SDL3/SDL.h>
int main(void) { (void) SDL_GetVersion(); return 0; }
EOF

sdl3_cflags='' sdl3_libs='' sdl3_how=''
if [ -n "$have_pkg_config" ] && "$PKG_CONFIG" --exists sdl3 2>/dev/null; then
    sdl3_cflags=$("$PKG_CONFIG" --cflags sdl3 2>/dev/null)
    sdl3_libs=$("$PKG_CONFIG" --libs sdl3 2>/dev/null)
    sdl3_how="pkg-config, $("$PKG_CONFIG" --modversion sdl3 2>/dev/null)"
else
    sdl3_libs='-lSDL3'
    sdl3_how='no pkg-config entry; tried -lSDL3'
fi

# shellcheck disable=SC2086
if [ -n "${HEADLESS:-}" ]; then
    note 'SDL3' '(not used)' 'HEADLESS=1 -- the display is a stub'
elif "$CC" $sdl3_cflags "$tmp/sdl3.c" -o "$tmp/a.out" $sdl3_libs >/dev/null 2>&1; then
    note 'SDL3' "$sdl3_libs" "links -- $sdl3_how"
else
    note 'SDL3' "$sdl3_libs" "WILL NOT LINK -- $sdl3_how"
    want sdl3
fi

#
#  ODBC is OPTIONAL and its absence is not counted against the build.
#
#  `want' is deliberately not called here.  Everything above it is something
#  st80 cannot run without, so a missing one has to be a failure; a database
#  is something st80 can run perfectly well without, and reporting its
#  absence as a fault would teach a reader to ignore this report -- which is
#  the only thing that could make the required entries stop working.
#
cat > "$tmp/odbc.c" <<'EOF'
#include <sql.h>
#include <sqlext.h>
int main(void) { SQLHENV e; return SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &e); }
EOF

odbc_cflags='' odbc_libs='' odbc_how=''
if [ -n "$have_pkg_config" ] && "$PKG_CONFIG" --exists odbc 2>/dev/null; then
    odbc_cflags=$("$PKG_CONFIG" --cflags odbc 2>/dev/null)
    odbc_libs=$("$PKG_CONFIG" --libs odbc 2>/dev/null)
    odbc_how="pkg-config, $("$PKG_CONFIG" --modversion odbc 2>/dev/null)"
elif [ -n "$have_pkg_config" ] && "$PKG_CONFIG" --exists libiodbc 2>/dev/null; then
    odbc_cflags=$("$PKG_CONFIG" --cflags libiodbc 2>/dev/null)
    odbc_libs=$("$PKG_CONFIG" --libs libiodbc 2>/dev/null)
    odbc_how="pkg-config, iODBC $("$PKG_CONFIG" --modversion libiodbc 2>/dev/null)"
else
    odbc_libs='-lodbc'
    odbc_how='no pkg-config entry; tried -lodbc'
fi

# shellcheck disable=SC2086
if [ -n "$NODB" ]; then
    note 'ODBC' '(not used)' 'NODB=1 -- the Database package will refuse to connect'
elif "$CC" $odbc_cflags "$tmp/odbc.c" -o "$tmp/a.out" $odbc_libs >/dev/null 2>&1; then
    note 'ODBC' "$odbc_libs" "links -- $odbc_how"
else
    note 'ODBC' '(absent)' "optional -- no database.  $(install_hint odbc)"
fi

echo

if [ "$status" -eq 0 ]; then
    if [ -n "$HEADLESS" ]; then
        echo "Everything a headless build needs is here.  Drop HEADLESS=1 for a window."
    else
        echo 'Everything st80 needs is here.'
    fi
    exit 0
fi

echo 'Missing.  To install:'
echo
for m in $(echo "$missing" | tr ' ' '\n' | sort -u); do
    echo "    $(install_hint "$m")"
done
echo

case " $missing " in
*' sdl3 '*)
    cat <<'EOF'
SDL3 is the display and `make' will not build without it.  If this machine
is meant to have no display, ask for that and the graphics layer becomes a
stub -- -bootstrap, -eval, -doctests and `make test' all still work:

    make HEADLESS=1
EOF
    ;;
esac

exit 1
