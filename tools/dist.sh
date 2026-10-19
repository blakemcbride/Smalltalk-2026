#!/bin/sh
#
#  Fill a binary distribution: tools/dist.sh <dir>
#
#  Called by `make dist' once it has linked <dir>/st2026 with a run path
#  that looks in <dir>/lib first.  This copies in the libraries a user's
#  machine is least likely to have, bootstraps the image with the binary
#  being shipped, writes the README for the platform, and packs the
#  directory into <dir>.tar.gz.
#
#  What is bundled and what is not:
#
#    Linux   SDL3 (no package on Ubuntu 24.04 or Debian 12), unixODBC and
#            its libltdl -- the binary links libodbc whether or not anyone
#            uses a database, so a machine without unixODBC would refuse to
#            start it.  glibc, OpenSSL 3 and zlib stay the system's: every
#            desktop distribution has them, and a private OpenSSL would not
#            find the system's certificates.
#    macOS   every library from outside /usr/lib and /System -- Homebrew's
#            SDL3, and its OpenSSL when the build found one.
#
set -eu

DIR=$1
ROOT=$(cd "$(dirname "$0")/.." && pwd)
BIN=$DIR/st2026
OS=$(uname -s)
ARCH=$(uname -m)

[ -x "$BIN" ] || { echo "dist: $BIN is missing" >&2; exit 1; }
mkdir -p "$DIR/lib"

case $OS in
Linux)
    for name in libSDL3.so.0 libodbc.so.2 libltdl.so.7; do
        path=$(ldd "$BIN" | awk -v n="$name" '$1 == n { print $3 }')
        [ -n "$path" ] || continue
        cp -fL "$path" "$DIR/lib/$name"
        chmod 644 "$DIR/lib/$name"
    done
    #  The run path is DT_RPATH, so the libltdl libodbc wants is found here
    #  too.  Anything still resolving outside lib/ but on the list is a
    #  bundle that did not take.
    if ldd "$BIN" | grep -E 'libSDL3|libodbc|libltdl' | grep -v "$DIR/lib/" | grep -q .; then
        echo "dist: a bundled library still resolves outside $DIR/lib:" >&2
        ldd "$BIN" >&2
        exit 1
    fi
    GLIBC=$(objdump -T "$BIN" "$DIR"/lib/*.so* 2>/dev/null \
            | grep -o 'GLIBC_[0-9.]*' | sed 's/GLIBC_//' | sort -uV | tail -1)
    TEMPLATE=README-linux.md
    ;;
Darwin)
    #  Breadth first over the dependency graph: each non-system library is
    #  copied once, given an @rpath name, and every reference to it in the
    #  binary and in the other copies is rewritten to that name.
    external() {
        otool -L "$1" | tail -n +2 | awk '{ print $1 }' \
            | grep -vE '^(/usr/lib/|/System/|@)' || true
    }
    queue=$(external "$BIN")
    done_list=
    while [ -n "$queue" ]; do
        next=
        for path in $queue; do
            base=$(basename "$path")
            case " $done_list " in *" $base "*) continue ;; esac
            done_list="$done_list $base"
            cp -fL "$path" "$DIR/lib/$base"
            chmod 644 "$DIR/lib/$base"
            install_name_tool -id "@rpath/$base" "$DIR/lib/$base"
            next="$next $(external "$DIR/lib/$base")"
        done
        queue=$next
    done
    for f in "$BIN" "$DIR"/lib/*.dylib; do
        [ -f "$f" ] || continue
        for path in $(external "$f"); do
            install_name_tool -change "$path" "@rpath/$(basename "$path")" "$f"
        done
    done
    #  install_name_tool breaks the signature, and Apple Silicon refuses to
    #  run an unsigned binary: an ad-hoc signature is what the linker gave
    #  them in the first place.
    for f in "$DIR"/lib/*.dylib "$BIN"; do
        [ -f "$f" ] && codesign --force --sign - "$f" 2>/dev/null
    done
    MACOS=$(sw_vers -productVersion)
    TEMPLATE=README-macos.md
    ;;
*)
    echo "dist: no distribution recipe for $OS" >&2
    exit 1
    ;;
esac

rmdir "$DIR/lib" 2>/dev/null || true

echo "  bootstrapping $DIR/st2026.image"
(cd "$DIR" && ./st2026 -bootstrap -profile "$ROOT/profiles/st2026.profile" \
                       -o st2026.image > bootstrap.log 2>&1) || {
    echo "dist: the bootstrap failed -- see $DIR/bootstrap.log" >&2
    exit 1
}
rm -f "$DIR/bootstrap.log"

VERSION=$("$BIN" -version | head -1 | sed 's/^Smalltalk-2026 //')
CONFIG=$("$BIN" -version | tail -n +2 | grep -v "CPUs")

cp -f "$ROOT/LICENSE" "$DIR/LICENSE"
cp -f "$ROOT/doc/LICENSING.md" "$DIR/LICENSING.md"
[ -f "$ROOT/manual/smalltalk-2026-manual.pdf" ] \
    && cp -f "$ROOT/manual/smalltalk-2026-manual.pdf" "$DIR/"

#  The template's @NAME@ fields; CONFIG is several lines, so it goes in by
#  awk rather than sed.
awk -v version="$VERSION" -v arch="$ARCH" -v dir="$(basename "$DIR")" \
    -v glibc="${GLIBC:-}" -v macos="${MACOS:-}" -v config="$CONFIG" '
    {
        gsub(/@VERSION@/, version); gsub(/@ARCH@/, arch); gsub(/@DIR@/, dir)
        gsub(/@GLIBC@/, glibc);     gsub(/@MACOS@/, macos)
        if ($0 == "@CONFIG@") print config; else print
    }' "$ROOT/tools/dist/$TEMPLATE" > "$DIR/README.md"

#  And the same directory as one file to hand someone.
(cd "$(dirname "$DIR")" && tar -czf "$(basename "$DIR").tar.gz" "$(basename "$DIR")")

echo "  distribution in $DIR, and as $DIR.tar.gz"
