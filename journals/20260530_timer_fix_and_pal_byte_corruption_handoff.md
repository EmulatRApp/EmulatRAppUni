# 2026-05-30 -- Timer interrupt fix LANDED + downstream PAL-byte-corruption hunt

Session handoff. Two distinct results: (1) the "no timer interrupts" blocker is
FIXED and working; (2) it unmasked a single corrupt byte in the loaded OSF PAL
image, root-caused down to the firmware self-decompression / OS-PAL-takeover
copy, with the exact divergent instruction not yet pinned.

--------------------------------------------------------------------------------
1. TIMER INTERRUPT FIX -- LANDED, CONFIRMED WORKING
--------------------------------------------------------------------------------
Root cause of `*** no timer interrupts on CPU 0 ***`: the shared
`stageInterruptDivert` recipe staged ISUM external-interrupt bit EI[0]
(= 1<<33 = IRQ_ERR) for the interval timer, so SRM ran `sys__int_err` (find no
error, dismiss) instead of `sys__int_clk` (count the tick).

Fix (systemLib/Machine.cpp): parameterized `stageInterruptDivert(cpu, isumMask)`;
timer site passes `1<<35` (EI[2] = IRQ_CLK), error/injection sites keep `1<<33`.
Confirmed in-trace: `sys__int_clk` now runs and acks `MISC<ITINTR<4>>` (writes
0x10 to Cchip MISC at 0x801A0000080). Full RCA: journals/
RCA_no_timer_interrupts_20260530.txt.  Memory: [[project_timer_interrupt_ei2_fix]].

EV6/Tsunami facts established (carry forward): EV6__ISUM__EI__S = 33;
EI[0]=IRQ_ERR/IRQ<0>/ipl7, EI[1]=IRQ_DEV/IRQ<1>/ipl4, EI[2]=IRQ_CLK/IRQ<2>/ipl5,
EI[3]=IRQ_IP/IRQ<3>. Interval timer cleared by W1C of MISC<ITINTR> = MISC<7:4>
(CPU0 = bit 4), NOT via HW_INT_CLR. HW_INT_CLR-as-noop is correct for the
Tsunami timer.

--------------------------------------------------------------------------------
2. THE DOWNSTREAM HALT -- root-cause chain
--------------------------------------------------------------------------------
With the timer now delivering as EI[2], `sys__int_clk` runs for the FIRST time
ever (it was never reached before the fix).  On its return it halts:

  - Clock handler return at PC 0xd954 is `HW_REI` enc=**0x7be2a000** = hw_ret(R2).
  - R2 = 0 (loaded from impure slot 0x600 which is 0, then BIC R2 = 0 at 0xd949).
  - hw_ret(R2) -> PC 0 -> the zero word at PC 0 decodes as CALL_PAL HALT.
  - The subsequent "ITB miss" at PC 0 is a SYMPTOM (genuine fetch at 0), not the
    cause -- earlier ITB-miss-excAddr theory (H1) was DISPROVEN.

The resume PC (0x1c699c) is present in excAddr, R23, and R6 (R6 loaded from
impure slot 0x150 = the slot the handler just stored the PC to, at PC 0x110d5).
So either the instruction should be hw_ret(R6) = 0x7be6a000 (one byte different:
byte[0xd956] = 0xe2 where it should be 0xe6, bit 2 cleared), OR the firmware
genuinely does hw_ret(R2) and slot 0x600 should hold the PC but V4 never wrote it.

--------------------------------------------------------------------------------
3. WHERE THE BAD BYTE COMES FROM
--------------------------------------------------------------------------------
STORE-WATCH (MemDrainer.h, temp) caught the write: PC **0x600938**, cyc 5528602,
byte-assembles the quad at PA 0xd950 = 0x7be2a000_b4de0030; byte 6 (0xd956)
arrives as 0xe2.  This is the OS-PAL-takeover copy relocating the OSF PAL to
palBase 0x8000 (0xd954 = 0x8000 + 0x5954), at cyc ~5.5M -- AFTER the Step-D
relocation trigger (cyc 4.19M).  No store to 0xd95x appears anywhere in the
retire trace before this (the early self-decompression at cyc <4M is pre-trace).

--------------------------------------------------------------------------------
4. AXPBox model (the oracle) and the decompressed.rom files
--------------------------------------------------------------------------------
AXPBox (D:\EmulatR\axpbox) boots the same SRM correctly.  Its System.cpp
LoadROM runs the GUEST Huffman decompressor as a pre-pass (set_pc(0x900001),
SingleStep until clean_pc < 0x200000) on AXPBox's CPU, then CACHES the result to
decompressed.rom (header = big-endian PC + PAL_BASE via endian_64, then 0x200000
memory dump) and loads the cache on subsequent runs.  So AXPBox runs the same
opcodes -- the divergence must be an INSTRUCTION SEMANTICS difference, not an
opcode AXPBox skips.

The decompressed.rom files in {EmulatRAppUni,EmulatrPOC,.}/firmware are
V-LINEAGE (little-endian header: pc=0x8001, pal_base=0x600000) -- produced by the
V emulator's own (suspect) decompressor, so they have the bug baked in and are
NOT a trusted reference.  They show PA 0xd954 = 0x7be2a000 (matches V4 runtime);
PA 0xd7f4 = 0x7be6a000 (so the correct hw_ret R6 word exists elsewhere -- both
are legitimate firmware instructions; cannot conclude from pattern alone).

V4 config loads `es45_v7_3.exe` (compressed); the guest self-decompresses on
V4's CPU every cold boot (~4M cyc before Step D).

--------------------------------------------------------------------------------
5. SUSPECTS RULED OUT (by inspection / trace)
--------------------------------------------------------------------------------
  - execHwRei (PalEntries.cpp): correct -- faithfully reads Rb=2 from a genuine
    hw_ret(R2) word; the bit-12/15 "stacked" theories were dead ends (the OSF
    hw_rei macro sets Rb=31; this word has Rb=2; both bootstrap 0x7be0a000 and
    this share bit12=0/bit15=1, so neither bit discriminates).
  - Instruction decode + fetch: g.encoded matches guest memory (read8 = 0x7be2a000).
  - GuestMemory sparse paging: 64KiB per-page VirtualAlloc + zero sentinel; dest
    quad 0xd950-0xd957 is mid-page-0 (no boundary); aligned accesses never cross.
  - Byte-lane ops (coreLib/alpha_int_byteops.h): EXTBL/INSBL/MSKBL/ZAP/ZAPNOT/
    EXT*/INS*/MSK* + stqUMergeLane -- all audited, all correct.
  - Shift ops (eBoxLib/grains/IntArith.cpp): SLL/SRL/SRA mask count with
    & 0x3F -- correct.
  - LDQ_U/STQ_U: force-align EA (clear low 3 bits) -- correct.

--------------------------------------------------------------------------------
6. TWO OPEN HYPOTHESES (need ground truth to decide)
--------------------------------------------------------------------------------
  H-CORRUPT: byte 0xd956 should be 0xe6 (hw_ret R6).  A subtler V4 instruction
    bug (CMPULT/CMOV/logical/ADD-SUB carry/a load path) or the decompression
    algorithm flips one bit.  Fix = that instruction.
  H-CORRECT: 0x7be2a000 (hw_ret R2) is the real firmware; the bug is upstream --
    impure slot 0x600 should hold the resume PC but V4's interrupt-entry never
    populated it (only 0x150 got the PC).  Fix = the register-save/control-flow
    divergence in the INTERRUPT entry path.  Evidence for this: the handler
    deliberately BICs R2=0 right before hw_ret(R2), which is nonsensical if R2 is
    the target.

--------------------------------------------------------------------------------
7. DECISIVE NEXT STEP
--------------------------------------------------------------------------------
Produce a TRUSTED decompressed es45 image: run AXPBox with rom.srm =
es45_v7_3.exe so it caches its own decompressed.rom (big-endian header).  Diff
the memory region against the V-lineage decompressed.rom.
  - AXPBox 0xe6 vs V 0xe2 at PA 0xd954 -> corruption confirmed; the full set of
    differing bytes gives scope (one slip vs systemic); trace the divergent
    instruction (now known NOT to be byte/shift ops).
  - Match (both 0xe2) -> not a corruption; pivot to the upstream 0x600 populate
    (H-CORRECT).

Also pending: the cold-boot FETCH-FIXUP run (serve 0x7be6a000 at 0xd954) to
confirm end-to-end that hw_ret(R6) lets the clock handler return cleanly and the
boot crosses the timer self-test (`no timer interrupts` clears, divert[1]+).

--------------------------------------------------------------------------------
8. TEMP DIAGNOSTICS IN TREE -- REMOVE BEFORE COMMIT
--------------------------------------------------------------------------------
  - pipelineLib/PipelineDriver.h: FETCH-FIXUP (0xd954 -> 0x7be6a000),
    ITBMISS-PROBE (cyc window).
  - pipelineLib/MemDrainer.h: STORE-WATCH (PA 0xd950-0xd957), LOAD-WATCH
    (cyc 5528490-5528660).
  - palBoxLib/grains/PalEntries.cpp: REI-PROBE (PAL->native, cyc window, logs
    enc/Ra/Rb/R6/R23).
Plus Tim's I-side fault restructure in PipelineDriver.h (route ITB miss through
retire() with cpu.va = cpu.pc) -- KEEP, it is correct.

Also landed this session (unrelated to the corruption): SRMConsoleDevice doStop
re-entrancy AV fix + onDrainTx capture-under-lock (the "app crash" on halt).
