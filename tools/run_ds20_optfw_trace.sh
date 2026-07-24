#!/usr/bin/env bash
# Capture the DS20 option-firmware -> LFU decision with the DIAG retire facility.
# PC / Windows (Git Bash / MSYS2) port of run_ds20_optfw_trace.sh.
#
# The b_irq<1> PCI-poll storm (0x1ade64) is stable at cyc~12.343B across runs;
# the firmware runs the "Checking ... option firmware" check + LFU branch right
# after.  Gate DIAG on the cycle window just past the storm so we capture the
# decision (load memAddrs = the flag/NVRAM field it reads; the branch = the LFU
# choice).  Small output, straight into the run log -- no 64 GB .trc scan.
#
# This is a thin wrapper: it only arms the DIAG env vars and hands off to the
# sibling run_ds20_showdev.sh, which self-locates the run dir and cd's into it.
# So no project-root path math is needed here (the old ../../.. was the bug).
#
# Location: emulatrappuniv5/tools/  (beside run_ds20_showdev.sh).
# Run from Git Bash / MSYS2.  Invoked through `bash` since NTFS drops the +x bit.
# ASCII(128) only.
set -u

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SHOWDEV="$HERE/run_ds20_showdev.sh"   # sibling; it self-locates the run dir + cd's

export EMULATR_DIAG_PCLO=0x0
export EMULATR_DIAG_PCHI=0xffffffffffffffff   # all PCs; the cycle gate localizes it
export EMULATR_DIAG_CYCLO=12343100000          # just past the b_irq storm end
export EMULATR_DIAG_CYCHI=12380000000          # ~37M-cycle window
export EMULATR_DIAG_CAP=4000                    # up to 4000 retired insns in-window
export EMULATR_DIAG_CYCHI=12365000000
export EMULATR_DIAG_CAP=8000
export MAXCYC=20000000000

echo "[optfw-trace] DIAG armed cyc 12.3431B-12.380B (option-firmware decision), cap 4000"
echo "[optfw-trace] let DS20 boot to 'Checking ... option firmware'; DIAG-PC lines land in the run log."

if [[ ! -f "$SHOWDEV" ]]; then
  echo "[optfw-trace] ERROR: run_ds20_showdev.sh not found beside this script: $SHOWDEV" >&2
  echo "[optfw-trace] Place this script in emulatrappuniv5/tools/ (next to the run_ds20_*.sh)." >&2
  exit 1
fi
exec bash "$SHOWDEV" "$@"
