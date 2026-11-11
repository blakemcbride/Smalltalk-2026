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
#            SDL3, and its OpenSSL and unixODBC when the build found them.
#            The README's database section says which driver manager that
#            is and where it looks for drivers (Bugs07 DOCS-3).
#
#  And the licence of everything in it that is other people's, in licenses/
#  (Bugs07 DOCS-2): the OFL of the face compiled into the binary, the MIT
#  notice of the 1983 sources compiled into the image, and the licence text
#  of every bundled library, found where the package that installed it put
#  it, with licenses/BUNDLED.md saying which is which and where each
#  library's source is.  LICENSING.md is tools/dist/LICENSING.md, which
#  describes this directory -- not doc/LICENSING.md, which describes the
#  source tree and names files a distribution does not have.  A bundled
#  library whose licence cannot be found stops the build: the libraries'
#  licences are conditions of shipping them, and the LGPL of unixODBC and
#  libltdl and the Apache licence of OpenSSL both require the text to go
#  with the copy.
#
set -eu

DIR=$1
ROOT=$(cd "$(dirname "$0")/.." && pwd)
BIN=$DIR/st2026
OS=$(uname -s)
ARCH=$(uname -m)

[ -x "$BIN" ] || { echo "dist: $BIN is missing" >&2; exit 1; }
mkdir -p "$DIR/lib"

LIC=$DIR/licenses
rm -rf "$LIC"
mkdir -p "$LIC"
cp -f "$ROOT/src/gfx/LICENSE.font" "$LIC/LICENSE.font"
cp -f "$ROOT/sources/LICENSE" "$LIC/LICENSE.smalltalk-80"
{
    echo "# The libraries bundled with this distribution"
    echo
    echo "Each is a copy of the library as the package named here installed it on"
    echo "the machine that built this distribution, unmodified. Its licence text is"
    echo "in the directory of \`licenses/\` named in the fifth column."
    echo
    echo "| File | Package | Version | Licence | Text | Source |"
    echo "|---|---|---|---|---|---|"
} > "$LIC/BUNDLED.md"

#  A path with every symbolic link in it resolved: readlink -f where there
#  is one (Linux, macOS 12.3 and later), and the directory resolved by cd
#  where there is not, which is enough to find a Homebrew keg.
resolve() {
    readlink -f "$1" 2>/dev/null \
        || echo "$(cd "$(dirname "$1")" && pwd -P)/$(basename "$1")"
}

#  note_library SHIPPED FROM: SHIPPED (lib/libodbc.so.2) was copied from FROM.
#  Copy its licence files into licenses/<package>/ and give it a row in
#  BUNDLED.md.  Asked of the package manager that owns the file -- rpm, then
#  dpkg -- then of a Homebrew keg, then, for a library built by hand (the
#  container's SDL3), of <prefix>/share/licenses and <prefix>/share/doc.
note_library() {
    shipped=$1
    real=$(resolve "$2")
    pkg='' version='' licence='' source='' files=''
    #  rpm says `not owned by any package' on its standard output, so its
    #  exit status, not its output, is the answer.
    if command -v rpm > /dev/null 2>&1 \
       && pkg=$(rpm -qf --qf '%{NAME}\n' "$real" 2> /dev/null) \
       && pkg=$(echo "$pkg" | head -1) && [ -n "$pkg" ]; then
        version=$(rpm -q --qf '%{VERSION}-%{RELEASE}' "$pkg")
        licence=$(rpm -q --qf '%{LICENSE}' "$pkg")
        source="source package $(rpm -q --qf '%{SOURCERPM}' "$pkg") of the distribution that built it"
        files=$(rpm -qL "$pkg" 2> /dev/null || true)
    else
        pkg=
    fi
    if [ -z "$pkg" ] && command -v dpkg-query > /dev/null 2>&1; then
        #  usrmerge: the database may name /lib what the link resolves to
        #  /usr/lib, or the other way round.
        for candidate in "$real" "$2" "${real#/usr}" "/usr$2"; do
            pkg=$(dpkg-query -S "$candidate" 2> /dev/null | head -1 | cut -d: -f1)
            [ -n "$pkg" ] && break
        done
        if [ -n "$pkg" ]; then
            version=$(dpkg-query -W -f='${Version}' "$pkg")
            src=$(dpkg-query -W -f='${source:Package}' "$pkg")
            srcver=$(dpkg-query -W -f='${source:Version}' "$pkg")
            licence="see its copyright file"
            source="source package $src $srcver (apt-get source $src=$srcver)"
            files=/usr/share/doc/$pkg/copyright
        fi
    fi
    if [ -z "$pkg" ]; then
        case $real in
        */Cellar/*/*/*)
            keg=$(echo "$real" | sed 's|^\(.*/Cellar/[^/]*/[^/]*\)/.*|\1|')
            pkg=$(basename "$(dirname "$keg")")
            version=$(basename "$keg")
            licence="see its licence text"
            source="Homebrew formula $pkg $version (brew info $pkg names its source)"
            files=$(find "$keg" -maxdepth 1 -type f \( -iname 'licen[cs]e*' \
                    -o -iname 'copying*' -o -iname 'notice*' -o -iname 'copyright*' \))
            ;;
        esac
    fi
    if [ -z "$pkg" ]; then
        #  Built by hand: lib/<file> under a prefix, its licence under
        #  <prefix>/share/licenses/<name> or <prefix>/share/doc/<name>.
        stem=$(basename "$real" | sed -e 's/^lib//' -e 's/\..*//')
        prefix=$(dirname "$(dirname "$real")")
        for dir in "$prefix"/share/licenses/* "$prefix"/share/doc/*; do
            [ -d "$dir" ] || continue
            name=$(basename "$dir")
            [ "$(echo "$name" | tr '[:upper:]' '[:lower:]')" = "$(echo "$stem" | tr '[:upper:]' '[:lower:]')" ] || continue
            files=$(find "$dir" -maxdepth 1 -type f \( -iname 'licen[cs]e*' \
                    -o -iname 'copying*' -o -iname 'copyright*' \))
            [ -n "$files" ] || continue
            pkg=$name
            version=$(basename "$real")
            licence="see its licence text"
            source="not from a package: built from $name's own release by the recipe that made this distribution"
            break
        done
    fi
    [ -n "$pkg" ] && [ -n "$files" ] || {
        echo "dist: no licence text found for $shipped (from $real); a distribution" >&2
        echo "dist: must carry the licence of every library it bundles" >&2
        exit 1
    }
    mkdir -p "$LIC/$pkg"
    for f in $files; do
        [ -f "$f" ] && cp -f "$f" "$LIC/$pkg/"
    done
    [ -n "$(ls "$LIC/$pkg")" ] || {
        echo "dist: the licence of $shipped is listed as $files and is not there" >&2
        exit 1
    }
    echo "| \`$shipped\` | $pkg | $version | $licence | \`licenses/$pkg/\` | $source |" \
        >> "$LIC/BUNDLED.md"
}

case $OS in
Linux)
    for name in libSDL3.so.0 libodbc.so.2 libltdl.so.7; do
        path=$(ldd "$BIN" | awk -v n="$name" '$1 == n { print $3 }')
        [ -n "$path" ] || continue
        cp -fL "$path" "$DIR/lib/$name"
        chmod 644 "$DIR/lib/$name"
        note_library "lib/$name" "$path"
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
            note_library "lib/$base" "$path"
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
    #  And no run path but lib/: pkg-config's -rpath /opt/homebrew/lib would
    #  otherwise send the binary to whatever Homebrew the user has.
    for rp in $(otool -l "$BIN" | awk '/LC_RPATH/ { getline; getline; print $2 }'); do
        [ "$rp" = "@executable_path/lib" ] || install_name_tool -delete_rpath "$rp" "$BIN"
    done
    #  install_name_tool breaks the signature, and Apple Silicon refuses to
    #  run an unsigned binary: an ad-hoc signature is what the linker gave
    #  them in the first place.
    for f in "$DIR"/lib/*.dylib "$BIN"; do
        [ -f "$f" ] && codesign --force --sign - "$f" 2>/dev/null
    done
    MACOS=$(sw_vers -productVersion)
    TEMPLATE=README-macos.md
    #  Which ODBC driver manager the user's copy will load, for the README's
    #  database section (Bugs07 DOCS-3).  It said "ODBC (iODBC) is part of
    #  macOS" whatever was bundled, and the release recipe bundles
    #  Homebrew's unixODBC, which does not read the files a macOS driver
    #  installer registers with (/Library/ODBC) but the directory Homebrew
    #  compiled into it -- on the user's machine usually not there at all --
    #  so a driver installed as the README said was never found.  Asked of
    #  what was shipped: a libodbc in lib/ is unixODBC, a libiodbc the
    #  binary still names is the system's iODBC, and neither is a build
    #  without the database.  The template holds all three texts; the awk
    #  below keeps the one named here.
    if ls "$DIR"/lib/libodbc.*dylib > /dev/null 2>&1; then
        ODBC=unixodbc
        ODBC_INI=$(odbcinst -j 2> /dev/null | sed -n 's/^DRIVERS\.*: *//p')
        [ -n "$ODBC_INI" ] \
            || ODBC_INI="$(brew --prefix 2> /dev/null || echo /opt/homebrew)/etc/odbcinst.ini"
    elif otool -L "$BIN" | grep -q libiodbc; then
        ODBC=iodbc
    else
        ODBC=none
    fi
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
cp -f "$ROOT/tools/dist/LICENSING.md" "$DIR/LICENSING.md"
[ -f "$ROOT/manual/smalltalk-2026-manual.pdf" ] \
    && cp -f "$ROOT/manual/smalltalk-2026-manual.pdf" "$DIR/"

#  The template's @NAME@ fields; CONFIG is several lines, so it goes in by
#  awk rather than sed, and through the environment rather than -v, which
#  macOS's awk refuses a newline in.  A line `@ODBC <name>@' opens a block
#  kept only when <name> is the driver manager found above, and `@ODBC end@'
#  closes it.
D_VERSION=$VERSION D_ARCH=$ARCH D_DIR=$(basename "$DIR") \
D_GLIBC=${GLIBC:-} D_MACOS=${MACOS:-} D_CONFIG=$CONFIG \
D_ODBC=${ODBC:-} D_ODBCINI=${ODBC_INI:-} awk '
    {
        if ($0 ~ /^@ODBC [a-z]+@$/) {
            block = substr($0, 7, length($0) - 7)
            if (block == "end") block = ""
            next
        }
        if (block != "" && block != ENVIRON["D_ODBC"]) next
        gsub(/@VERSION@/, ENVIRON["D_VERSION"]); gsub(/@ARCH@/, ENVIRON["D_ARCH"])
        gsub(/@DIR@/, ENVIRON["D_DIR"]);         gsub(/@GLIBC@/, ENVIRON["D_GLIBC"])
        gsub(/@MACOS@/, ENVIRON["D_MACOS"]);     gsub(/@ODBCINI@/, ENVIRON["D_ODBCINI"])
        if ($0 == "@CONFIG@") print ENVIRON["D_CONFIG"]; else print
    }' "$ROOT/tools/dist/$TEMPLATE" > "$DIR/README.md"

#  And the same directory as one file to hand someone.
(cd "$(dirname "$DIR")" && tar -czf "$(basename "$DIR").tar.gz" "$(basename "$DIR")")

echo "  distribution in $DIR, and as $DIR.tar.gz"
