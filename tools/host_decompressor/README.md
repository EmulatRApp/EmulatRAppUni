# host_decompressor -- native SRM firmware decompressor (oracle)

Native (host-compiled) build of the original DEC SRM Huffman/inflate
decompressor, used as a TRUSTED reference for the compressed Alpha
firmware images (es45_v7_3.exe and siblings).

## Why this exists

The guest firmware self-decompresses on the emulated CPU at every cold
boot (~4M cycles; the spin observed at PC 0x60111c). To verify that
EmulatR's CPU produces the correct decompressed image -- without trusting
either EmulatR's own CPU or AXPBox -- we build the SAME algorithm DEC
used, from the SAME source the firmware was compiled from, as a native
host program. No emulation is involved.

This settled the 2026-05-30 "single-bit PAL corruption" question: the
instruction at PC 0xd954 (enc 0x7be2a000 = hw_ret R2) is GENUINE firmware,
not a decompression corruption. The reference image produced here matches
EmulatR's decompressed output byte for byte. See the project memory note
"project_pal_byte_corruption_hunt" (RESOLVED) for the full chain.

## Contrast with AXPBox

AXPBox's decompressed.rom is NOT a host decompression: its LoadROM runs
the GUEST decompressor on AXPBox's emulated CPU and caches the result, so
it still depends on a correct CPU and a contrived entry sequence
(set_pc(0x900001), single-step). This hive uses the real algorithm in
native code -- strictly better as an oracle.

## Sources (src/)

Pinned copies of the read-only originals in
  Processor Support\Palcode\palcode\apisrm\apisrm\ref\
DO NOT edit these; edit the originals' provenance only there.
  inflate.c          -- Mark Adler inflate, version c10p1 (the engine)
  decom.c            -- DEC wrapper: main() + decompress(); REFERENCE ONLY,
                        not compiled (oracle.c replaces it)
  decomp.h           -- types and ROM/non-ROM switches
  ev6_huf_decom.m64  -- the guest asm startup that CALLS decompress();
                        REFERENCE ONLY. Note: this asm also performs CPU/PAL
                        state setup (PAL_BASE, I_CTL SDE bits, ITB/DTB and
                        icache flush, shadow regs, save/restore of SROM
                        params R16-R21). Any "host decompress in EmulatR"
                        path must preserve that end-state -- see the
                        deferred-work entry in D:\EmulatR\CLAUDE.md.
  oracle.c           -- our harness: finds WimC, sets inptr/outptr/
                        compressedSize, calls inflate(), dumps the image.

## Build and run

  sh build.sh                                  (Linux/gcc)
  ./oracle <path-to-compressed-firmware.exe>

Output is written to ./ref_decompressed.bin and a summary is printed.
A known-good run of es45_v7_3.exe is cached in out/.

## Compressed-image layout (es45_v7_3.exe)

  WimC magic ("WimC" = 0x436D6957) found by linear scan; here at file
  offset 0x2400. Header longwords from the magic:
    [0] 0x436D6957  "WimC"
    [1] compressedSize  (0x1a6164)
    [2] 0
    [3] 0
    [4] decompress target  (0x8000)
    [5..] compressed data  (starts at WimC + 20 = file 0x2414)
  Decompressed output: 0x30cc00 bytes (3,197,952).

## Known-good signature (regression guard)

In the decompressed image the V4-runtime quad at PA 0xd950
(0x7be2a000_b4de0030) appears as bytes "30 00 de b4 00 a0 e2 7b":
  "..b4de0030 + 7be2a000" (hw_ret R2)  x3
  "..b4de0030 + 7be6a000" (hw_ret R6)  x0
0xe6/R6 never appears in that neighborhood. If a future change makes the
guest CPU produce R6 (0xe6) there, it is a CPU regression, not a fix.

## TODO (deferred)

Optional EmulatR integration as an alternate decompression path
(--decompress=inline|host|cache). Recommended design is the
intercept-hybrid: let the guest huf asm run for its CPU/PAL side-effects,
intercept entry to the decompress() C function, host-fill the output and
set R0=decompressed base, advance PC to the call return. Sequenced AFTER
the SRM reaches >>>. Full rationale in D:\EmulatR\CLAUDE.md.
