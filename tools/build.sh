#!/bin/bash
# Build các binary macOS (Intel + Apple Silicon) và đóng gói zip.
# Cần: zig (https://ziglang.org, hoặc `pip install ziglang` rồi đặt ZIG="python -m ziglang"), python 3.
set -e
cd "$(dirname "$0")/.."
ZIG=${ZIG:-zig}
PY=${PY:-python3}

mkdir -p build/bin
for t in x86_64 aarch64; do
  $ZIG cc -target $t-macos.11.0 -O2 -Wall -o build/bin/rhebar-btbridge-$t src/btbridge.c
  $ZIG cc -target $t-macos.11.0 -O2 -Wall -o build/bin/rastertotspl-$t src/rastertotspl.c
done
# bản Linux tĩnh để test trong Docker (tests/)
$ZIG cc -target x86_64-linux-musl -static -O2 -o build/btbridge-linux src/btbridge.c
$ZIG cc -target x86_64-linux-musl -static -O2 -o build/rastertotspl-linux src/rastertotspl.c

$PY tools/gen_ppd.py build/PPDs
$PY tools/pack_bt.py
$PY tools/pack.py
