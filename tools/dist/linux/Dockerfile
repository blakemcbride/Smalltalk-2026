#
#  The Linux distribution's build machine: Ubuntu 24.04, whose glibc (2.39)
#  is the floor the distribution then asks for.  tools/dist-container.sh
#  builds this and runs `make dist' in it; see Distributions.md.
#
#  Ubuntu 24.04 has no SDL3 package, so SDL3 is built here from its release
#  tag.  The development packages below are what SDL's own docs/README-linux
#  lists; SDL loads every one of them with dlopen at run time, so the
#  libSDL3.so.0 this makes links nothing but libc and libm, and runs on a
#  machine that has only some of them.
#
FROM ubuntu:24.04

ARG SDL3_VERSION=3.4.18

RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
        build-essential pkg-config cmake ninja-build git ca-certificates \
        libssl-dev unixodbc-dev \
        libasound2-dev libpulse-dev libpipewire-0.3-dev libjack-dev libsndio-dev \
        libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxfixes-dev \
        libxi-dev libxss-dev libxtst-dev libxkbcommon-dev \
        libwayland-dev libdecor-0-dev wayland-protocols \
        libdrm-dev libgbm-dev libgl1-mesa-dev libgles2-mesa-dev libegl1-mesa-dev \
        libdbus-1-dev libibus-1.0-dev libudev-dev liburing-dev \
    && rm -rf /var/lib/apt/lists/*

RUN git clone --depth 1 --branch release-${SDL3_VERSION} https://github.com/libsdl-org/SDL.git /tmp/SDL \
    && cmake -S /tmp/SDL -B /tmp/SDL/build -G Ninja \
        -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr/local \
        -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF \
    && cmake --build /tmp/SDL/build \
    && cmake --install /tmp/SDL/build \
    && ldconfig \
    && rm -rf /tmp/SDL
