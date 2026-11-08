#!/usr/bin/env bash
# ============================================================================
# run_tests.sh - builds and runs every automated check for the system
#
#   1. SystemTests.cpp     : unit + integration tests for Tasks 1-5
#   2. The real program    : scripted run of Main.cpp on a COPY of the data
#   3. End of input        : every menu must return when input runs out
#
# Usage (from anywhere):   bash SMART_CAMPUS_PARCEL_COLLECTION/Tests/run_tests.sh
# Needs g++ (or set CXX=clang++). Built with AddressSanitizer and
# UndefinedBehaviorSanitizer, so memory errors are reported as failures.
# The real Data folder is never modified.
# ============================================================================
set -u

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
CODE="$ROOT/Code"
DATA="$(cd "$ROOT/../Data" && pwd)"
BUILD="$HERE/build"
CXX="${CXX:-g++}"
FLAGS="-std=c++20 -Wall -Wextra -g -fsanitize=address,undefined -fno-omit-frame-pointer"
MODULES="$CODE/Task1/ParcelIntake.cpp $CODE/Task2/ServiceRequest.cpp $CODE/Task3/ResourceQueue.cpp \
$CODE/Task4/ActivityLog.cpp $CODE/Task5/ParcelCategoryTree.cpp"

export ASAN_OPTIONS="detect_leaks=1:halt_on_error=1"
export UBSAN_OPTIONS="print_stacktrace=1:halt_on_error=1"

mkdir -p "$BUILD"
FAILED=0
fail() { echo "  FAIL  $1"; FAILED=$((FAILED + 1)); }
pass() { echo "  ok    $1"; }

echo "== Building (sanitizers on) =="
# shellcheck disable=SC2086
$CXX $FLAGS "$HERE/SystemTests.cpp" $MODULES -o "$BUILD/SystemTests" || { echo "Test build failed"; exit 2; }
# shellcheck disable=SC2086
$CXX $FLAGS "$CODE/Main.cpp" $MODULES -o "$BUILD/SmartCampus" || { echo "App build failed"; exit 2; }

echo
echo "== 1. Unit and integration tests =="
"$BUILD/SystemTests" "$DATA" || FAILED=$((FAILED + 1))

# ---------------------------------------------------------------------------
# Fresh copy of the data for each scripted run. On Linux, Main.cpp's
# Windows-style "Data\\" paths are not found, so it opens the CSV files in
# the current folder - the copy.
# ---------------------------------------------------------------------------
fresh_copy() {
    local dir="$BUILD/run_$1"
    rm -rf "$dir" && mkdir -p "$dir" && cp "$DATA"/*.csv "$dir"/
    echo "$dir"
}
csv_field() {   # file id column(1-based)
    awk -F, -v id="$2" -v c="$3" '$1 == id { gsub(/\r/, "", $c); print $c }' "$1"
}

echo
echo "== 2. Scripted run of the real program =="
RUN="$(fresh_copy journey)"
ACT_BEFORE=$(wc -l < "$RUN/activities.csv")
# 5 = sort the next parcel (P021, Fragile) with the suggested route and open
#     its request; 4 = assign the next request to a resource; 0 = exit
( cd "$RUN" && printf '5\n\n4\n0\n' | timeout 30 "$BUILD/SmartCampus" > out.txt 2> err.txt )
CODE_RC=$?
if [ $CODE_RC -eq 0 ] && [ ! -s "$RUN/err.txt" ]; then pass "program exits normally, no sanitizer reports"
else fail "program exit code $CODE_RC (see $RUN/err.txt)"; fi

[ "$(csv_field "$RUN/parcels.csv" P021 8)" = "SpecialHandling" ] \
    && pass "P021 sorted to SpecialHandling and saved" || fail "P021 not SpecialHandling in parcels.csv"
[ "$(awk -F, '$2 == "P021" && $3 == "SpecialHandling"' "$RUN/requests.csv" | wc -l)" -eq 1 ] \
    && pass "a SpecialHandling request for P021 was saved" || fail "no SpecialHandling request for P021"
[ "$(grep -c ',Assigned' "$RUN/requests.csv")" -gt "$(grep -c ',Assigned' "$DATA/requests.csv")" ] \
    && pass "option 4 assigned the next request" || fail "option 4 did not assign a request"
[ "$(grep -c ',Occupied,' "$RUN/resources.csv")" -gt "$(grep -c ',Occupied,' "$DATA/resources.csv")" ] \
    && pass "the resource used is saved as Occupied" || fail "no extra Occupied resource in resources.csv"
[ "$(wc -l < "$RUN/activities.csv")" -ge $((ACT_BEFORE + 3)) ] \
    && pass "Task 4 logged Sorted, RequestCreated and Assigned" || fail "activities.csv did not grow by 3"

echo
echo "== 3. Every menu returns when the input ends (no endless loop) =="
for entry in "1:Task 1 menu" "2:Task 2 menu" "3:Task 3 menu" "7:Task 5 menu" "8:Task 4 menu"; do
    key="${entry%%:*}"; name="${entry#*:}"
    RUN="$(fresh_copy eof$key)"
    ( cd "$RUN" && printf '%s\n' "$key" | timeout 10 "$BUILD/SmartCampus" > /dev/null 2> err.txt )
    rc=$?
    if [ $rc -eq 124 ]; then fail "$name loops forever when input ends"
    elif [ $rc -ne 0 ]; then fail "$name exit code $rc (see $RUN/err.txt)"
    else pass "$name"; fi
done

echo
if [ $FAILED -eq 0 ]; then echo "ALL CHECKS PASSED"; exit 0; fi
echo "$FAILED check group(s) failed - see FAIL lines above"
exit 1
