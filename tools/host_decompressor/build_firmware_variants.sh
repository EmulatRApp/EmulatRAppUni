#!/bin/sh
# build_firmware_variants.sh
# Build the oracle once, then decompress every listed firmware variant,
# writing a distinct image per variant to out/decompressed_<name>.bin and
# printing a per-variant summary (size + hw_ret R2/R6 signature).
#
# Run from the MSYS2 MinGW64 shell:  sh build_firmware_variants.sh
# Edit FIRMWARES below to add/remove images. Forward-slash paths; the MinGW
# oracle.exe accepts them. Missing files are skipped with a notice, and a
# single bad image does not abort the batch.
set -e
cd "$(dirname "$0")"

FIRMWARES="
/d/EmulatR/EmulatRAppUniV4/Emulatr/firmware/Es45/es45_v7_2.exe
/d/EmulatR/EmulatRAppUniV4/Emulatr/firmware/Es45/es45_v7_3.exe
/d/EmulatR/EmulatRAppUniV4/Emulatr/firmware/ds10_v7_3.exe
/d/EmulatR/firmware/es40_v7_2.exe
"

echo "== building oracle.exe =="
cc -O2 -o oracle.exe src/oracle.c src/inflate.c
echo

mkdir -p out
for fw in $FIRMWARES; do
    name=$(basename "$fw")
    base=${name%.*}
    if [ ! -f "$fw" ]; then
        echo "SKIP (not found): $fw"
        echo
        continue
    fi
    echo "== $name =="
    if ./oracle.exe "$fw" "out/decompressed_${base}.bin"; then
        :
    else
        echo "  FAILED: $name"
    fi
    echo
done
echo "done -- images in out/"
