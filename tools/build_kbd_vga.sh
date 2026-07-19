#!/usr/bin/env bash
# ============================================================================
# build_kbd_vga.sh -- build EmulatR V5 with the keyboard(8042)+VGA interfaces
# and confirm the new VGA code linked.  JRN-VMB-006.  ASCII(128) only.
# ----------------------------------------------------------------------------
# Thin wrapper over tools/build_emulatr.sh (the canonical builder).  Builds the
# given config (default relwithdebinfo -- diagnostics + trace hooks ON) and
# greps the produced exe for the VgaTextConsole marker string as a link check
# (mirrors the CLAUDE.md "grep the exe for a marker" rebuild-confirm rule).
#
# Usage:  tools/build_kbd_vga.sh [relwithdebinfo|debug|release]
# Then:   tools/test_kbd_vga.sh ds20
# ============================================================================
set -euo pipefail
SELF_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SELF_DIR/.." && pwd)"
CONFIG="${1:-relwithdebinfo}"

"$SELF_DIR/build_emulatr.sh" "$CONFIG"

EXE="$ROOT/out/build/$CONFIG/Emulatr.exe"
[[ -f "$EXE" ]] || EXE="$ROOT/out/build/$CONFIG/Emulatr"    # non-Windows leaf

echo ""
echo "=== link check: VgaTextConsole present in the exe ==="
if [ -f "$EXE" ]; then
    n=$(grep -a -c 'EMULATR VGA text framebuffer' "$EXE" || true)
    echo "  marker 'EMULATR VGA text framebuffer' count: ${n}  (expect >= 1)"
    if [ "${n:-0}" -ge 1 ]; then
        echo "  OK: keyboard/VGA build linked."
    else
        echo "  WARN: marker not found -- did the build pick up the new files?"
    fi
else
    echo "  WARN: exe not found at $EXE"
fi
echo ""
echo "Next: tools/test_kbd_vga.sh ds20 $CONFIG"
