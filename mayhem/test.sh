#!/usr/bin/env bash
# mayhem/test.sh — run the known-answer selftest built by mayhem/build.sh (upstream has no suite):
# a hand-assembled 6502 ROM's computed RAM/OAM/controller values and keybindings.conf parsing.
set -uo pipefail
[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH
: "${SRC:=$(cd "$(dirname "$0")/.." && pwd)}"
cd "$SRC"

emit_ctrf() {
  local tool="$1" passed="$2" failed="$3" skipped="${4:-0}" pending="${5:-0}" other="${6:-0}"
  local tests=$(( passed + failed + skipped + pending + other ))
  cat > "${CTRF_REPORT:-$SRC/ctrf-report.json}" <<JSON
{
  "results": {
    "tool": { "name": "$tool" },
    "summary": {
      "tests": $tests,
      "passed": $passed,
      "failed": $failed,
      "pending": $pending,
      "skipped": $skipped,
      "other": $other
    }
  }
}
JSON
  printf 'CTRF {"results":{"tool":{"name":"%s"},"summary":{"tests":%d,"passed":%d,"failed":%d,"pending":%d,"skipped":%d,"other":%d}}}\n' \
    "$tool" "$tests" "$passed" "$failed" "$pending" "$skipped" "$other"
  [ "$failed" -eq 0 ]
}

EXPECTED=9   # checks in mayhem/harnesses/selftest.cpp
bin="$SRC/build/mayhem-test/simplenes_selftest"
if [ ! -x "$bin" ]; then
  echo "missing $bin — run mayhem/build.sh" >&2
  emit_ctrf simplenes-selftest 0 "$EXPECTED"; exit 1
fi
out="$("$bin" "$SRC/keybindings.conf" 2>&1)"; rc=$?
echo "$out"
passed=$(grep -c '^PASS ' <<<"$out" || true)
failed=$(grep -c '^FAIL ' <<<"$out" || true)
# Checks that never reported (crash, early exit) count as failures.
missing=$(( EXPECTED - passed - failed )); [ "$missing" -gt 0 ] && failed=$(( failed + missing ))
[ "$rc" -ne 0 ] && [ "$failed" -eq 0 ] && failed=1
emit_ctrf simplenes-selftest "$passed" "$failed"
