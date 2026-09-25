#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
motion_test_dir="$(mktemp -d /tmp/pteronaut-motion.XXXXXX)"
motion_cxx="${CXX:-clang++}"
common=(-std=c++17 -O1 -DPROGMEM= -Isketches/yoshimitsu/src)
tables=src/lib/Ornithopter/OrnithopterWaveformTables.cpp
"$motion_cxx" "${common[@]}" tools/mushin_test/test_prepared.cpp "$tables" -o "$motion_test_dir/parity"
"$motion_test_dir/parity"
"$motion_cxx" "${common[@]}" -DMUSHIN_ENABLED -Itools/mushin_test -Isrc/lib/Ornithopter \
  tools/mushin_test/test_preparer.cpp src/lib/Ornithopter/Ornithopter.cpp "$tables" -o "$motion_test_dir/preparer"
"$motion_test_dir/preparer"
"$motion_cxx" "${common[@]}" -DARDUINO_ARCH_RP2040 -DYOSHI_RTC=0 \
  -Itools/motion_core_test -Itools/mushin_test -Itools/stub \
  tools/motion_core_test/main.cpp "$tables" -o "$motion_test_dir/core"
"$motion_test_dir/core"
"$motion_cxx" "${common[@]}" -DARDUINO_ARCH_RP2040 -Itools/mushin_test -Itools/stub \
  tools/mushin_test/test_wave.cpp "$tables" -o "$motion_test_dir/muscle"
"$motion_test_dir/muscle"
"$motion_cxx" "${common[@]}" -Itools/spirit_test -Isrc/lib/Mushin \
  tools/spirit_test/main.cpp -o "$motion_test_dir/spirit"
"$motion_test_dir/spirit"
printf 'Test executables: %s\n' "$motion_test_dir"
