#!/usr/bin/env bash
# Smoke test for the ScreamAPI Linux .so
#
# Verifies that:
#   1. The file is a valid ELF shared object
#   2. It can be loaded by the dynamic linker (no missing dependencies)
#   3. It exports the expected EOS SDK entry-point symbols
#
# Usage: ./scripts/smoke-test-screamapi.sh path/to/libScreamAPI.so
#
# Exit codes:
#   0 — all checks passed
#   1 — at least one check failed
#   2 — usage error
#
# This script is invoked by .github/workflows/build-linux.yml in the
# screamapi job. It will only run once ScreamAPI/CMakeLists.txt exists
# (Phase 3 of Docs/LINUX_PORT_ANALYSIS.md).

set -euo pipefail

# ── Configuration ──────────────────────────────────────────────────────
# Symbols we expect the proxy .so to export. These are the EOS SDK entry
# points the game's dynamic linker will resolve through the proxy.
#
# Update this list as new hooks are added in eos_hooks.cpp. A symbol listed
# here but missing from the .so means a hook was forgotten; a symbol in the
# .so but not listed here means the list is stale (warn but don't fail).
EXPECTED_SYMBOLS=(
    "EOS_Initialize"
    "EOS_Shutdown"
    "EOS_Platform_Create"
    "EOS_Platform_Release"
    "EOS_Achievements_QueryPlayerAchievements"
    "EOS_Ecom_QueryOwnership"
    "EOS_Ecom_QueryEntitlements"
    "EOS_Auth_Login"
    "EOS_Connect_Login"
)

# ── Helpers ────────────────────────────────────────────────────────────
red()    { printf '\033[31m%s\033[0m\n' "$*" >&2; }
green()  { printf '\033[32m%s\033[0m\n' "$*"; }
yellow() { printf '\033[33m%s\033[0m\n' "$*" >&2; }
bold()   { printf '\033[1m%s\033[0m\n' "$*"; }

# ── Arg parsing ────────────────────────────────────────────────────────
if [[ $# -ne 1 ]]; then
    cat >&2 <<EOF
Usage: $0 <path-to-libScreamAPI.so>

Verifies the ScreamAPI Linux .so is loadable and exports the expected symbols.
EOF
    exit 2
fi

SO_PATH="$1"

if [[ ! -f "$SO_PATH" ]]; then
    red "ERROR: file not found: $SO_PATH"
    exit 1
fi

echo "── ScreamAPI Linux smoke test ──"
echo "Target: $SO_PATH ($(stat -c%s "$SO_PATH") bytes)"
echo ""

# ── Check 1: ELF shared object ─────────────────────────────────────────
echo -n "1. ELF type check... "
FILE_OUT=$(file "$SO_PATH")
if echo "$FILE_OUT" | grep -q "ELF 64-bit LSB shared object"; then
    green "OK"
elif echo "$FILE_OUT" | grep -q "ELF.*shared object"; then
    yellow "WARN (not 64-bit LSB — may not load on target arch)"
    echo "  $FILE_OUT"
else
    red "FAIL (not an ELF shared object)"
    echo "  $FILE_OUT"
    exit 1
fi

# ── Check 2: loadable by dynamic linker ────────────────────────────────
echo -n "2. Dynamic load test... "
# Tiny C program that dlopen's the .so. ld.so will report any missing
# dependencies. We don't call any functions — just verify the load succeeds.
TMPDIR_TEST=$(mktemp -d)
trap 'rm -rf "$TMPDIR_TEST"' EXIT

cat > "$TMPDIR_TEST/test_load.c" <<'EOF'
#include <dlfcn.h>
#include <stdio.h>
int main(int argc, char** argv) {
    if (argc != 2) { fprintf(stderr, "usage: %s <so>\n", argv[0]); return 2; }
    void* h = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!h) { fprintf(stderr, "dlopen failed: %s\n", dlerror()); return 1; }
    printf("loaded OK\n");
    dlclose(h);
    return 0;
}
EOF

if ! cc -o "$TMPDIR_TEST/test_load" "$TMPDIR_TEST/test_load.c" -ldl 2>"$TMPDIR_TEST/cc.err"; then
    red "FAIL (could not compile test harness)"
    cat "$TMPDIR_TEST/cc.err" >&2
    exit 1
fi

LD_LIBRARY_PATH="$(dirname "$SO_PATH"):${LD_LIBRARY_PATH:-}" \
    "$TMPDIR_TEST/test_load" "$SO_PATH" > "$TMPDIR_TEST/out" 2> "$TMPDIR_TEST/err" && \
    LOAD_RESULT=0 || LOAD_RESULT=$?

if [[ $LOAD_RESULT -eq 0 ]]; then
    green "OK"
else
    red "FAIL"
    cat "$TMPDIR_TEST/err" >&2
    exit 1
fi

# ── Check 3: expected symbols exported ─────────────────────────────────
echo -n "3. Symbol exports... "
# Get the set of exported (defined, dynamic) symbols
EXPORTED=$(nm -D --defined-only "$SO_PATH" 2>/dev/null | awk '{print $NF}' | sort -u)

if [[ -z "$EXPORTED" ]]; then
    red "FAIL (no exported symbols found)"
    exit 1
fi

MISSING=()
for sym in "${EXPECTED_SYMBOLS[@]}"; do
    if ! echo "$EXPORTED" | grep -qx "$sym"; then
        MISSING+=("$sym")
    fi
done

if [[ ${#MISSING[@]} -gt 0 ]]; then
    red "FAIL"
    echo ""
    echo "Missing expected symbols (update EXPECTED_SYMBOLS in this script if outdated,"
    echo "or add the missing hooks to ScreamAPI/src/eos_hooks.cpp):"
    for s in "${MISSING[@]}"; do
        echo "  - $s"
    done
    exit 1
fi
green "OK (${#EXPECTED_SYMBOLS[@]} expected symbols found)"

# ── Check 4: no unexpected undefined symbols ──────────────────────────
echo -n "4. No unexpected undefined symbols... "
UNDEFINED=$(nm -D --undefined-only "$SO_PATH" 2>/dev/null | awk '{print $NF}' | sort -u)
# We expect undefined references to the real EOS SDK (we forward via dlsym
# at runtime). Anything else is suspicious — probably a missing -l flag.
UNEXPECTED_UNDEFINED=()
while IFS= read -r sym; do
    [[ -z "$sym" ]] && continue
    case "$sym" in
        EOS_*|libScreamAPI_*) ;;  # expected
        _init|_fini|__bss_start|_edata|_end|__cxa_*) ;;  # CRT/ABI
        *) UNEXPECTED_UNDEFINED+=("$sym") ;;
    esac
done <<< "$UNDEFINED"

if [[ ${#UNEXPECTED_UNDEFINED[@]} -gt 0 ]]; then
    yellow "WARN"
    echo "  Undefined symbols that are not EOS_* and may indicate a missing dep:"
    for s in "${UNEXPECTED_UNDEFINED[@]}"; do
        echo "  - $s"
    done
else
    green "OK"
fi

echo ""
bold "All smoke tests passed."
