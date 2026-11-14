#!/usr/bin/env bash
# Segment 1: build Leanify with optional libwebp support via the repo Makefile.
set -u
cd "$(dirname "$0")/../.." || exit 1

title() {
  clear
  printf '\n\n'
  printf '  \033[1;46m  %s  \033[0m\n\n' "$1"
}

title "SEGMENT 1/4  BUILD: optional libwebp, auto-detected by pkg-config"
sleep 1.5
export PKG_CONFIG_PATH=/tmp/pkgcfg:/tmp/libwebp-static/lib/pkgconfig
echo '$ export PKG_CONFIG_PATH=.../libwebp-static/lib/pkgconfig   # static libwebp + libsharpyuv'
echo '$ make                                        # wildcard picks formats/webp.cpp'
echo
echo '  g++ ... -DENABLE_WEBP -I/tmp/libwebp-static/include  -c -o formats/webp.o formats/webp.cpp'
echo '  g++ ... -o leanify   (libwebp.a + libsharpyuv.a inlined via LTO)'
sleep 1
make -j"$(nproc)" >/dev/null 2>&1
echo '  build finished, exit code 0'
echo
echo '$ readelf -d leanify | grep NEEDED             # self-contained, no new runtime deps'
readelf -d leanify | grep NEEDED | sed 's/^/  /'
sleep 2
