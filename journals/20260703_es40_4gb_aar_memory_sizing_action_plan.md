<!--
EmulatR V4 -- ES40 4GB Memory-Sizing (AAR) Interface -- Cowork Action Plan
Project: EmulatR (Alpha 21264 / EV6 emulator), V4 active tree
Architect: Timothy Peer.  AI collaboration: Claude / Anthropic.
Date: 2026-07-03
Frontier refs: main 8981e99 -> 2c12e35 (ES40 kFaultAcv loop).
Purpose: hand-off document for Cowork to work the ES40 4GB memory-sizing
path. This is a design/reference brief, NOT generated code. Discuss-
before-code applies: the conditional edits in Section 7 are proposals to
be confirmed against the live tree and signed off before any Edit/Write.
Cowork is the source of truth for current file state and line numbers.
ASCII(128) only.
-->

# ES40 4GB Memory-Sizing (AAR) Interface -- Cowork Action Plan

## 0. How to use this document

This brief exists so the memory-sizing interface can be worked from a
spec-anchored reference rather than from the AXPBox witness. It carries:

- The authoritative 21272 (Tsunami) HRM facts for AAR0-3 and the
  firmware memory-init sequence (Section 3). These are quoted from the
  HRM, not inferred.
- The architectural fork that decides the fix (Section 4).
- The [CONFIRM]/[LOCATE] items Cowork must resolve in the live tree
  before any edit (Section 5).
- The capture plan that turns the staged diag run into a decision
  (Section 6).
- Conditional, proposed surgical edits keyed to the confirm outcome
  (Section 7).
- Test/assertion plan (Section 8) and fidelity discipline (Section 9).

Nothing in Section 7 is landed until the Section 5 items are resolved and
Tim signs off. Values marked _PROVISIONAL do not drive MEMDSC or decode
until confirmed against BOTH the HRM table AND a real firmware trace.

## 1. The defect (frontier restated)

- ES40, 4GB config. Console does not reach the prompt; a kFaultAcv loop
  fires with a garbage R16.
- R16 traces to a legitimate negative SUBQ: R02(1GB) - R00(~3.2GB), where
  R00 is the end-pointer of a count-based memory-fill loop at PC 0x5afac.
- ALU, CSERVE-0x66, and DTB-format are exonerated. The negative result is
  correct for those operands; the operands are wrong.
- Root points upstream at the memory-size / HWRPB MEMDSC layout tied to
  the 4GB config.

Working thesis (to be proven, not assumed): the two operands come from two
different memory-size sources that disagree. One channel reports ~1GB (a
single-array value); another reports the full 4GB-class backing. The loop
count is derived from the larger source while a per-array/per-cluster limit
is the smaller, so the running end-pointer overshoots the limit and the
SUBQ goes negative. The fix is to make the ES40 4GB config present as a
single consistent description -- four 1GB arrays -- through whichever
channel the firmware actually reads.

## 2. Why ES40 is special here

ES40 is the Tsunami 21272 chipset. ES45/DS25 are Titan 21274 (Typhoon
family, deferred). The distinction is load-bearing for this issue because
the array-size encoding differs between the two, and the ES40 path must not
borrow the Typhoon encodings.

## 3. Authoritative HRM reference (21272 Tsunami)

### 3.1 AAR0-3 CSR addressing

    Register   CSR address        Type
    AAR0       801.A000.0100      RW
    AAR1       801.A000.0140      RW
    AAR2       801.A000.0180      RW
    AAR3       801.A000.01C0      RW

The AXPBox-profile shorthand "CSR 0x100 / 0x140 / 0x180 / 0x1c0" is exactly
AAR0..AAR3 (Cchip register-space low offsets).

### 3.2 AAR field layout -- Tsunami (HRM Table 10-14)

    Field   Bits       Type       Description
    RES     <63:35>    MBZ,RAZ    Reserved.
    ADDR    <34:24>    RW         Base address. Bits <34:24> of the
                                  physical byte address of the first byte
                                  in the array. On Tsunami, phys <31:24>
                                  are valid.
    RES     <23:17>    MBZ,RAZ    Reserved.
    DBG     <16>       RW         Debug-interface enable for this port.
    ASIZ    <15:12>    RW         Array size (see 3.3).
    RES     <11:9>     MBZ,RAZ    Reserved.
    SA      <8>        RW         Split array.
    RES     <7:4>      MBZ,RAZ    Reserved.
    ROWS    <3:2>      RW         Number of SDRAM row bits.
                                  0=11, 1=12, 2=13, 3=Reserved.
    BNKS    <1:0>      RW         Number of SDRAM bank bits.
                                  0=1, 1=2, 2=Reserved, 3=Reserved.

Base-address granularity is 16MB (bit 24). ADDR holds phys<34:24> in its
native register position, so for a base B the ADDR contribution to the
register is simply (B & 0x7ff000000).

### 3.3 ASIZ encoding -- Tsunami tops out at 1GB

    ASIZ<15:12>   Size (Tsunami)     Size (Typhoon)
    0000          0 (bank disabled)  0 (bank disabled)
    0001          16MB               16MB
    0010          32MB               32MB
    0011          64MB               64MB
    0100          128MB              128MB
    0101          256MB              256MB
    0110          512MB              512MB
    0111          1GB                1GB
    1000          Reserved           2GB   (Typhoon only)
    1001          Reserved           4GB   (Typhoon only)
    1010          Reserved           8GB   (Typhoon only)
    1011..1111    Reserved           Reserved

Key fact: on Tsunami (ES40) there is NO single-array encoding above 1GB.
The 2/4/8GB codes exist only on Typhoon (ES45/Titan). Any ES40 path that
emits ASIZ >= 0x8 is out of spec and the firmware decode is undefined.

### 3.4 Address-space rule (HRM 9.5)

Each array maps a contiguous region determined by the ADDR (base) and ASIZ
(size) fields. Each region must be naturally aligned, and no two regions
may overlap. With equal-size arrays the natural layout is contiguous from
address 0.

### 3.5 Firmware memory-init sequence (HRM 12.x)

After CSC/STR init, the arrays are sized and programmed by the firmware,
not by any pre-seed on real hardware:

    1. If serial presence detect (SPD) is in use, read SPD from the serial
       ROM via the MPD register using an I2C protocol in software. This
       yields array sizes and SDRAM speeds.
    2. Write MTR with the desired timing.
    3. (32-byte-bus width test, if applicable.)
    4. If SPD is NOT in use, size each array by setting it to its largest
       possible size, then writing and reading back addresses to find the
       highest-order address bit in use.
    5. Write AARn with the determined configuration; disable absent arrays
       (ASIZ = 0000).

The firmware discovers memory by one of two channels: SPD (via MPD) or a
write/read-back probe (step 4). Both end by the firmware programming AARn.

### 3.6 The 4GB ES40 target configuration

A 4GB ES40 on Tsunami is four 1GB arrays, contiguous from 0:

    Reg    ADDR (phys base)   ASIZ         Region
    AAR0   0x0_0000_0000      0x7 (1GB)    0    .. 1GB
    AAR1   0x0_4000_0000      0x7 (1GB)    1GB  .. 2GB
    AAR2   0x0_8000_0000      0x7 (1GB)    2GB  .. 3GB
    AAR3   0x0_C000_0000      0x7 (1GB)    3GB  .. 4GB

Register-value assembly (ADDR native position | ASIZ<<12 | geometry):

    AAR0 = 0x0000_0000 | 0x7000 | GEOM
    AAR1 = 0x4000_0000 | 0x7000 | GEOM
    AAR2 = 0x8000_0000 | 0x7000 | GEOM
    AAR3 = 0xC000_0000 | 0x7000 | GEOM

GEOM = (SA<<8) | (ROWS<<2) | BNKS. For a 1GB nonsplit array, a valid HRM
Table 9-1 organization is 256Mb 32M x 8, B+R+C = 25, nonsplit 32-byte bus:
BNKS=1 (B=2), ROWS=2 (R=13), C=10, SA=0 -> GEOM = 0x009. These geometry
bits are _PROVISIONAL: they affect internal DRAM address decode, NOT the
size the firmware reads (ASIZ). They do not gate the sizing fix. Confirm
the modeled organization before letting GEOM drive access decode.

Important: the firmware reads ADDR and ASIZ to size memory. The size bug is
an ADDR/ASIZ problem. ROWS/BNKS/SA are an access-decode concern and are
tracked separately.

## 4. The architectural fork (this decides the fix)

Which memory-discovery channel does V4 implement for ES40, and which does
the OpenVMS SRM ES40 image actually exercise at the cycle where 0x5afac
runs? Three candidate models:

- Model P (firmware probe): V4 does not program AARs. The firmware runs the
  step-4 write/read-back probe. Correctness then depends on V4's memory
  backing: it must respond across the full configured size and must NOT
  respond (NXM) above it, so the probe terminates at exactly 4GB. If V4
  aliases/wraps out-of-range accesses (e.g. pa & (memSize-1)), the probe
  never sees a boundary and computes a wrong top.

- Model S (SPD present): V4 presents SPD data via the MPD register
  describing four 1GB modules; the firmware reads SPD and programs AARn.

- Model E (pre-seed AARs, AXPBox-style): V4 writes AAR0-3 directly and the
  firmware trusts them. The AXPBox witness reports a single array (AAR0 set,
  AAR1-3 = 0). At 4GB that single array can encode at most 1GB (ASIZ 0x7),
  which is a strong candidate source for the R02 = 1GB operand.

The frontier evidence (a count-based fill/probe loop at runtime, and a 1GB
value colliding with a ~3.2GB value) is consistent with Model P or Model E
producing a split description. The capture plan (Section 6) decides which.

## 5. CONFIRM / LOCATE before any edit

Cowork resolves these against the live tree at
D:\EmulatR\EmulatRAppUniV4\Emulatr before proposing edits:

- [LOCATE] TsunamiCchip AAR0-3 read/write handling. Does V4 store AARn as
  plain RW CSRs, decode ADDR/ASIZ, or ignore them? Path and line range.
- [CONFIRM] Which discovery model (P/S/E) V4 currently implements. Does any
  code write AAR0-3, or present SPD via MPD, or neither?
- [LOCATE] The memSize -> memory-map path. How is configured guest RAM
  represented, and is there any AAR/CSR that reports it independently of
  the AARs? This is the suspected second size source.
- [CONFIRM] Out-of-range access behavior. On a load/store above top-of-
  memory, does V4 alias/wrap, return 0, or raise NXM? (Gates Model P.)
- [LOCATE] Disasm window at 0x5afac (say 0x5af80..0x5afd0). Is this the SPD
  read, the step-4 probe, a MEMDSC-driven bootstrap zero/scrub, or a
  console memory test? What are R00 and R02 loaded from (register/memory
  provenance)?
- [LOCATE] Where MEMDSC clusters are built and from what. Are the cluster
  PFN start/count derived from AAR reads, from memSize, or from a probe
  result?
- [CONFIRM] Exact hex of R00 and R02 at the SUBQ. If R00 == 0xC0000000
  exactly, that is AAR3's base (3GB) and implicates an array-base walk. If
  R00 is a non-1GB-aligned ~3.2GB value, it implicates a memSize-derived
  count. R02 == 0x40000000 confirms a 1GB (per-array or base) quantity.

## 6. Capture plan (staged diag run)

Use the existing diag build (out/build/mac-diag, probes ON) with
EMULATR_VALUE_GATE + EMULATR_TRACE_WINDOW=1. One run, three pulls, in
priority order. If the ring cannot span both the early sizing/SPD work and
the later 0x5afac loop, take the 0x5afac window first -- it forks the
investigation.

1. 0x5afac window: exact R00, R02 hex at the SUBQ, and the load sources for
   both. Kills or confirms the split-source thesis and the array-base-walk
   vs memSize-count fork (Section 5 last item).
2. AAR channel: capture every AAR0-3 read and write during boot, with
   values, and whether they were written by firmware or by V4. Capture MPD
   reads if any. Identifies the active discovery model (P/S/E).
3. Boundary probe: capture the highest physical address the firmware reads
   or writes during sizing, and V4's response at and just above 4GB (data,
   0, or NXM). Confirms whether the probe terminates at 4GB.

Deliver the ring back for analysis before drafting the landed diff.

## 7. Conditional instructional changes (proposals, gated on Section 5)

Do not land any of these until the model is confirmed and Tim signs off.
Line numbers are [LOCATE]; register values follow Section 3.6.

### 7.1 Shared: memSize -> AAR-set decomposition helper

Add a pure helper that maps a configured byte size to the minimal set of
Tsunami-legal arrays:

- Decompose memSize into up-to-four naturally-aligned, non-overlapping,
  contiguous-from-0 regions, each with ASIZ in {0x1..0x7} (16MB..1GB).
- For 4GB: emit AAR0-3 each ASIZ=0x7 at bases 0/1G/2G/3G per Section 3.6.
- Hard-fail (find-or-fail, loud) if the request exceeds 4 arrays or any
  single region would need ASIZ > 0x7 on Tsunami. No silent truncation to
  1GB -- that silent truncation is the current suspected defect.
- Emit ASIZ case labels in hex radix (0x1..0x7), never decimal.

This helper is the single source of truth for the size the AARs, the
memory map, and MEMDSC all derive from.

### 7.2 If Model E (V4 pre-seeds AARs)

Replace the single-array seed with the decomposition helper output. Seed
AAR0-3 with the 4x1GB set. Verify MEMDSC is built from the same AAR set,
not from a separate memSize field. This directly removes the R02=1GB vs
R00=~3.2GB split if the split is AAR(1GB) vs memSize(4GB).

### 7.3 If Model P (firmware probes)

Ensure the memory backing responds across the full configured size and
raises NXM above it, so the step-4 probe terminates at 4GB. Remove any
wrap/alias mask on out-of-range physical accesses. Route non-existent-
memory accesses through the NXM error path rather than returning 0 or
wrapping. Do not pre-seed AARs in this model; let the firmware own them.

### 7.4 If Model S (SPD)

Present MPD/SPD data describing four 1GB modules and let the firmware
program the AARs. Lower priority unless the capture shows the firmware
polling MPD at the relevant cycle.

### 7.5 CSERVE / MEMDSC consistency (all models)

Wherever MEMDSC clusters are minted, derive PFN start/count from the same
AAR-decomposition (Section 7.1), so cluster size == backing size == AAR
size. A single derivation removes the class of split-source bugs entirely.

## 8. Test / assertion plan

- static_assert / doctest CHECK: an ASIZ value->size table matching HRM
  Table 10-14 exactly, including that 0x8..0xf are illegal on Tsunami.
  doctest CHECK only, never REQUIRE (exceptions disabled in V4).
- doctest CHECK: decomposition helper cases -- 4GB -> four ASIZ=0x7 at
  bases 0/1G/2G/3G; 3GB -> three 1GB; 64MB -> one ASIZ=0x3 at base 0;
  memSize > 4GB on Tsunami -> hard-fail.
- Runtime invariant (diag build): sum of enabled AAR-encoded sizes ==
  configured memSize; regions naturally aligned, non-overlapping,
  contiguous from 0.
- Boot-trace check: at 0x5afac, R02 and R00 reconcile (no negative SUBQ),
  R16 is non-negative, the kFaultAcv loop is cleared. Re-run to the next
  halt/prompt and record the new frontier PC.

## 9. Fidelity and convention discipline

- The AAR values and ASIZ codes in Section 3.6 are confirmed against HRM
  Table 10-14. Any AAR base or geometry value not yet matched to a real
  firmware AAR read/write trace stays _PROVISIONAL and must not drive
  MEMDSC or access decode until confirmed. GEOM in Section 3.6 is
  _PROVISIONAL.
- Hard-stop over silent degradation: the decomposition helper fails loudly
  on an unrepresentable config; it never falls back to 264DP-style or
  single-array defaults.
- ASCII(128) only in any artifact. ADR-0001 header on any new source/header
  and on Markdown specs (HTML comment). Include guards, never pragma once,
  on any new header. Hex radix for all ASIZ switch/case labels.
- Discuss-before-code: Section 7 items are proposals. Confirm the Section 5
  unknowns, bring the diff shape with real line numbers back for sign-off,
  then land surgically with header + inline documentation per house style.
