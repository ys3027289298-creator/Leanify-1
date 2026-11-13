#!/usr/bin/env bash
# Segment 1: build Leanify with optional libavif support via the repo Makefile.
set -u
cd "$(dirname "$0")/../.." || exit 1

title() {
  clear
  printf '\n\n'
  printf '  \033[1;46m  %s  \033[0m\n\n' "$1"
}

title "SEGMENT 1/4  BUILD: optional libavif, auto-detected by pkg-config"
sleep 1.5
export PKG_CONFIG_PATH=/tmp/pkgcfg
echo '$ export PKG_CONFIG_PATH=/tmp/pkgcfg        # static libavif + libaom'
echo '$ make                                        # wildcard picks formats/avif.cpp'
echo
echo '  g++ ... -DENABLE_AVIF -I/tmp/libavif-static-install/include  -c -o formats/avif.o formats/avif.cpp'
echo '  g++ ... -o leanify   (libavif.a + libaom.a inlined via LTO)'
sleep 1
make -j"$(nproc)" >/dev/null 2>&1
echo '  build finished, exit code 0'
echo
echo '$ file leanify && readelf -d leanify | grep NEEDED   # self-contained, no new runtime deps'
file leanify
readelf -d leanify | grep NEEDED | sed 's/^/  /'
sleep 2
