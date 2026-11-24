#!/usr/bin/env bash
# mayhem/build.sh — build SimpleNES's fuzz harnesses, their standalone reproducers and the selftest.
#
# Upstream builds one GUI executable (CMakeLists.txt: main.cpp + src/*.cpp + src/APU/*.cpp +
# vendor/miniaudio/miniaudio.c, linked against SFML 2). The harnesses need the same sources as a
# library, so this compiles them the way the CMake target does (C++11 dialect bumped to C++17 for the
# harness, -Iinclude -Ivendor/miniaudio, per-file __FILENAME__) into a static archive, once with
# $SANITIZER_FLAGS $DEBUG_FLAGS (fuzz build) and once with normal flags (selftest build).
# SFML comes from the image's apt libsfml-dev. Re-runnable and offline: no fetches.
set -euo pipefail
[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH

: "${SANITIZER_FLAGS=-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer}"
: "${DEBUG_FLAGS:=-g -gdwarf-3}"
: "${CC:=clang}" ; : "${CXX:=clang++}" ; : "${LIB_FUZZING_ENGINE:=-fsanitize=fuzzer}"
: "${MAYHEM_JOBS:=$(nproc)}"
: "${COVERAGE_FLAGS=}"
: "${SRC:=$(cd "$(dirname "$0")/.." && pwd)}"
cd "$SRC"

SFML_LIBS="-lsfml-graphics -lsfml-window -lsfml-system"
INCS="-I$SRC/include -I$SRC/vendor/miniaudio -I$SRC/mayhem/harnesses"
SOURCES=$(ls src/*.cpp src/APU/*.cpp | LC_ALL=C sort)

# build_lib <outdir> <c++ flags> <c flags>: every project TU -> <outdir>/libsimplenes.a
build_lib() {
  local out="$1" cxxflags="$2" cflags="$3"
  mkdir -p "$out/obj"
  local objs=() pids=() n=0
  for s in $SOURCES; do
    local o="$out/obj/$(echo "$s" | tr '/' '_').o"
    objs+=("$o")
    $CXX -std=c++17 $cxxflags $INCS "-D__FILENAME__=\"$(basename "$s")\"" -c "$s" -o "$o" &
    pids+=($!); n=$((n+1))
    if [ "$n" -ge "$MAYHEM_JOBS" ]; then for p in "${pids[@]}"; do wait "$p"; done; pids=(); n=0; fi
  done
  for p in "${pids[@]}"; do wait "$p"; done
  # Vendored third-party miniaudio: plain C, like upstream's VENDOR_SOURCES.
  $CC $cflags -I"$SRC/vendor/miniaudio" -c vendor/miniaudio/miniaudio.c -o "$out/obj/miniaudio.o"
  objs+=("$out/obj/miniaudio.o")
  rm -f "$out/libsimplenes.a"
  ar rcs "$out/libsimplenes.a" "${objs[@]}"
}

FUZZ_OUT="$SRC/build/mayhem-fuzz"
TEST_OUT="$SRC/build/mayhem-test"

# 1) Sanitized project (the fuzzed code). miniaudio is vendored and only reached through
#    AudioPlayer::start(), which the harness never calls: built uninstrumented (-O1, DWARF 3).
# -fsanitize=fuzzer-no-link: SanCov on the project code, so libFuzzer/Mayhem see its edges.
build_lib "$FUZZ_OUT" "$SANITIZER_FLAGS $DEBUG_FLAGS -O1 -fsanitize=fuzzer-no-link" "-O1 $DEBUG_FLAGS"

# 2) Harnesses: libFuzzer binary + standalone reproducer each.
$CXX $SANITIZER_FLAGS $DEBUG_FLAGS -c "$SRC/mayhem/lsan_off.cc" -o "$FUZZ_OUT/lsan_off.o"
$CC  $SANITIZER_FLAGS $DEBUG_FLAGS -c "$STANDALONE_FUZZ_MAIN" -o "$FUZZ_OUT/standalone_main.o"
$CXX -std=c++17 $SANITIZER_FLAGS $DEBUG_FLAGS -fsanitize=fuzzer-no-link $INCS -c mayhem/harnesses/keyboard_stub.cpp -o "$FUZZ_OUT/keyboard_stub.o"
for h in fuzz_rom fuzz_keybindings; do
  $CXX -std=c++17 $SANITIZER_FLAGS $DEBUG_FLAGS -O1 -fsanitize=fuzzer-no-link $INCS -c "mayhem/harnesses/$h.cpp" -o "$FUZZ_OUT/$h.o"
  $CXX $SANITIZER_FLAGS $DEBUG_FLAGS $LIB_FUZZING_ENGINE "$FUZZ_OUT/$h.o" "$FUZZ_OUT/keyboard_stub.o" \
       "$FUZZ_OUT/lsan_off.o" "$FUZZ_OUT/libsimplenes.a" $SFML_LIBS -lpthread -lm -ldl -o "$SRC/$h"
  $CXX $SANITIZER_FLAGS $DEBUG_FLAGS "$FUZZ_OUT/standalone_main.o" "$FUZZ_OUT/$h.o" "$FUZZ_OUT/keyboard_stub.o" \
       "$FUZZ_OUT/lsan_off.o" "$FUZZ_OUT/libsimplenes.a" $SFML_LIBS -lpthread -lm -ldl -o "$SRC/$h-standalone"
done

# 3) Selftest (functional oracle): normal flags, no sanitizers.
build_lib "$TEST_OUT" "-O2 $COVERAGE_FLAGS" "-O2"
$CXX -std=c++17 -O2 $COVERAGE_FLAGS $INCS mayhem/harnesses/selftest.cpp mayhem/harnesses/keyboard_stub.cpp \
     "$TEST_OUT/libsimplenes.a" $SFML_LIBS -lpthread -lm -ldl $COVERAGE_FLAGS -o "$TEST_OUT/simplenes_selftest"
echo "built: $SRC/fuzz_rom $SRC/fuzz_keybindings (+ -standalone), $TEST_OUT/simplenes_selftest"
