# Decompressor pass-2 destination overlaps PAL text -- CONFIRMED 2026-05-19

Findings note from the 2026-05-19 noon session.  Captures the evidence
that closes tasks #20, #24, and #27 and reframes the remaining work.

## Headline

The "HW_MTPR spin loop at 0x6003ec" investigated for two days was a
trace-formatter mis-classification.  Primary opcode 0x1b is HW_LD
(not HW_MTPR); primary opcode 0x1f is HW_ST.  The real code at
0x6003ec is a memcpy loop, and the firmware's destination region
overlaps PAL text at PAL_BASE=0x600000 under the V4 default 64 MiB
memory configuration.  Over millions of iterations the loop writes
over its own instruction bytes.  That is the wall.

## Evidence

Predig snapshot captured at cyc 5,242,886 via:

    Emulatr.exe --firmware firmware/es45_v7_3.exe \
                --snapshot-on-pc 0x6003ec --snapshot-name-tag spin_entry \
                --max-cycles 20000000

File: `snapshots\predig_spin_entry_cyc5242886.axpsnap`, 68 MB.

Subsequent run with `--dump-disasm 0x6003e0:32 --max-cycles 1`
autoloaded the snapshot and rendered the actual bytes:

    0x6003e0  e8800003  BLT       R04, +12
    0x6003e4  6c1a1000  HW_LD     R00, 0x000(R26)
    0x6003e8  c3e0000a  BR        R31, +40
    0x6003ec  6c621000  HW_LD     R03, 0x000(R02)
    0x6003f0  e8800003  BLT       R04, +12
    0x6003f4  20420008  LDA       R02, 0x0008(R02)        ; src += 8
    0x6003f8  40211521  IntArith  R01, #0x08, R01         ; SUBQ R01,#8,R01
    0x6003fc  7c601000  HW_ST     R03, 0x000(R00)
    0x600400  e8800003  BLT       R04, +12
    0x600404  40011400  IntArith  R00, #0x08, R00         ; ADDQ R00,#8,R00
    0x600408  f43ffff8  BNE       R01, -32                ; -> 0x6003ec
    0x60040c  6c1a1000  HW_LD     R00, 0x000(R26)         ; post-loop probe
    0x600410  e8800003  BLT       R04, +12
    0x600414  47e0041e  IntLogical R31, R00, R30          ; BIS: R30 := R00
    0x600418  201a05c0  LDA       R00, 0x05c0(R26)        ; R00 := R26 + 0x5c0
    0x60041c  6be04000  JmpClass  R31, (R00)              ; JMP to (R00)

Pre-fire register state:
- R00 = 0x5e7d5a   (dst pointer, low 6 MiB)
- R01 = 0x5e7d55   (remaining count)
- R02 = 0x602298   (src pointer)
- R04 = 0x5f3388   (flag; positive so BLT skips never taken)
- R26 = 0x601440   (base pointer; possibly HWRPB or per-CPU table)
- R29 = R30 = 0x5ff960  (stack)

## Mathematical crossover

R0 starts at 0x5e7d5a, advances by 8 each 6-cycle iteration.  After
`(0x600000 - 0x5e7d5a) / 8 = 6230` iterations -- about 37,400 cycles
past predig fire -- R0 reaches 0x600000 and the HW_ST at 0x6003fc
starts overwriting PAL text.  By cyc 200M the loop is executing
post-corruption bytes; the "HW_MTPR" mnemonic seen in the
200M-cycle trace was the trace formatter rendering whichever
opcode happened to land at 0x6003ec after several hundred million
self-overwrites.

## Why the diagnosis took two days

1.  The 200M-cycle trace tail (the data we had before today) showed
    instructions at 0x6003ec that the trace formatter classified as
    "HW_MTPR".  That label survived into every memory note and
    investigation plan.
2.  The first --dump-disasm probe ran on cold boot (no snapshot
    present) so it showed zeros for the post-decompression region.
3.  Only after Run 1 captured the predig snapshot at cyc 5.24M and
    Run 2 autoloaded it did we see the actual firmware-written
    bytes -- which proved the trace-time label wrong.

## Snapshot residual that needs separate investigation

`lastFault=kFaultOpcDec at excAddr=0x602a45` is state carried in
the predig snapshot from before its capture.  At the moment predig
fired, the CPU had recently OPCDEC'd at PC 0x602a44 (excAddr | PAL
bit).  This indicates an unimplemented opcode encountered during
pass-1 PAL initialization, somewhere between Step D at cyc 4.19M
and reaching the memcpy at cyc 5.24M.  Separate from the overlap
issue, worth a follow-up.

## Operational signals that ARE working

- SrmLoader-AXPBox model: palBase=0x900000, initialPalBase=0x900000,
  targetPalBase=0x600000 correctly seeded.
- Step D fires at PA 0x6005c0 at cyc 4,194,404 (predicted).
- HW_MTPR HW_PAL_BASE transition 0x900000 -> 0x600000 at cyc 4,194,406.
- Interval-timer SUPPRESSED throttle correctly emits during PAL
  relocation window (suppressed[0..3] at cyc 1M/2M/3M/4M).
- Interval-timer divert at cyc 5M onward, every 1,048,576 cycles,
  savedPc=0x600401, target=0x600100 (palBase + 0x100).  PAL handler
  RETIs cleanly back to the spin -- the path is correct.
- UNALIGN-FIXUP throttle: 16 loud, then summary line "17 total" --
  matches the design.  File sink writes to logs/unaligned.log.
- Predig snapshot save (68 MB), autoload, and resume all clean.

## Candidate fixes for the overlap

1.  **Bump default memSize.**  64 MiB default puts PAL_BASE=0x600000
    right above the firmware's natural destination region.  A larger
    memory may give the decompressor room above PAL text.  Cheapest
    to test: `--mem 268435456` (256 MiB) and re-snapshot.
2.  **Relocate PAL_BASE.**  AXPBox uses 0x600000 for ES40; we copied
    it.  Real ES45 may compute PAL_BASE from a HWRPB field or a
    chipset CSR we are not seeding.
3.  **AXPBox cross-check.**  If AXPBox boots this same firmware
    image with 64 MiB, the fix lies elsewhere (different decompressor
    entry state, missing init step, register seeding).

The memSize-sensitivity probe is the fastest decisive experiment --
re-run the snapshot capture at 256 MiB and observe R0 at predig fire.
If R0 has shifted, memSize is the lever.  If R0 stays at 0x5e7d5a,
the firmware computes destination from something else.

## Drop-into-memory note

A condensed version of these findings belongs in auto-memory as
`project_decompressor_pal_overlap_confirmed.md`.  The condensed text
is at the head of this file's predecessor in
`outputs\decompressor_pal_overlap_memory_draft.md` (this session's
scratchpad).  Future Claude should also UPDATE
`project_srmloader_axpbox_model.md` to note that the SrmLoader fix
itself is validated -- the remaining wall is not in the loader but
in the firmware-PAL-text overlap.  And the entry
`project_phase_b_c_landed.md` can be augmented to note the cyc 200M
"wall" was self-corruption, not a true interrupt-handling failure.
