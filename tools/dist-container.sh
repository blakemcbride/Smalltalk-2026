#!/bin/sh
#
#  The Linux distribution, built on Ubuntu 24.04: tools/dist-container.sh
#
#  `make dist' on the development machine makes a distribution whose glibc
#  floor is that machine's -- 2.43 on Fedora 44 -- so it runs nowhere older.
#  This makes the same distribution in a container of
#  tools/dist/linux/Dockerfile instead, and the floor is Ubuntu 24.04's.
#
#  The tree goes into the container read-only and is copied there without
#  build/, distributions/ or anything built, so neither side's objects meet
#  the other's.  The result replaces distributions/linux-<arch>/ and its
#  .tar.gz here, owned by the caller.  The container image is cached by
#  docker; the first run takes some minutes for SDL3.
#
set -eu

ROOT=$(cd "$(dirname "$0")/.." && pwd)
DOCKER=${DOCKER:-docker}
TAG=st2026-dist:ubuntu24.04
NAME=linux-$(uname -m)

command -v "$DOCKER" > /dev/null || {
    echo "dist-container: $DOCKER is not installed -- install docker or podman, or set DOCKER=" >&2
    exit 1
}

"$DOCKER" build -t "$TAG" "$ROOT/tools/dist/linux"

mkdir -p "$ROOT/distributions"
rm -rf "$ROOT/distributions/$NAME" "$ROOT/distributions/$NAME.tar.gz"

"$DOCKER" run --rm --user "$(id -u):$(id -g)" -e HOME=/tmp \
    -v "$ROOT:/src:ro" -v "$ROOT/distributions:/out" "$TAG" sh -euc "
        mkdir /tmp/work
        cd /src
        tar -cf - --exclude=./build --exclude=./distributions --exclude=./oracle \
            --exclude='./*.image' --exclude='./*.im' --exclude='./*.changes' \
            --exclude=./st2026 --exclude=./.git . | tar -xf - -C /tmp/work
        cd /tmp/work
        make dist
        cp -a distributions/$NAME distributions/$NAME.tar.gz /out/
    "

echo "  distribution in distributions/$NAME, built on Ubuntu 24.04"
