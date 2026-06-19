# Design v2: wire the Tsunami Cchip IPI delivery (IPREQ -> IPINTR -> b_irq<3>)

Status: DESIGN, HRM-cross-checked + source-checked (two claude-web passes).
2026-06-18. VERDICT: storage/set/clear wiring is HRM-faithful -- LAND IT.
Delivery values (EI bit, IPL, IPL rank) are PAL-decode-only -- provisional.

## Root cause (confirmed)

DS20 cold boot hot-idles forever, no `>>>`. Mode-3 IPI self-test: V4's Tsunami
Cchip defines MISC IPREQ/IPINTR but the delivery side effects are
`TODO(unwired)` (TsunamiCchip.h:127-130). A CPU0->CPU0 IPI in POST writes IPREQ,
waits forever for IPINTR/b_irq<3>. Needs NO 2nd CPU. DS10 single-socket firmware
skips the self-test -> reaches `>>>`.

## HRM facts (21272 HRM Table 10-12 / Sec 6.3, ES45 EK-ES450-SV Table D-10)

- MISC @ `801.A000.0080` (offset 0x80 confirmed). Fields:
  `IPREQ <15:12> WO`, `IPINTR <11:8> R,W1C`, `ITINTR <7:4> R,W1C` -- bit-exact.
- HRM's own causal chain, verbatim: IPREQ<12+n> write "sets the corresponding
  bit in the IPINTR"; IPINTR "Pin irq<3> is asserted to the CPU corresponding to
  a 1 in this field." So IPREQ -> IPINTR -> b_irq<3> is the HRM's decomposition.
- MISC "designed so there are no read side effects, writing 0 has no effect...
  need not be concerned with read-modify-write" -> the CAS/no-RMW model is
  explicitly licensed.
- IPREQ<12+n> -> IPINTR<8+n>: the `>> 4` shift is correct, masked to <11:8>.
- 0x380/0x3C0 are `IIC0/IIC1` = Interval Ignore Count (timer-suppress, Sec 6.3.2),
  NOT IPI. The `Tsunami21272_RegisterMap.h` label "Interprocessor Interrupt
  (CPU0/1)" at those offsets is a V4 BUG -> canonicalize to IIC0/IIC1 (separate
  cleanup, not this ticket).

## The change -- mirror the wired interval-timer (b_irq<2>) path

### A. chipsetLib/TsunamiCchip.h

1. **Storage + reset.** Add `std::array<std::atomic<bool>, kMaxCPUs>
   m_pendingIrq3;`, zeroed in `reset()` (next to the m_pendingIrq2 zero at ~360).
   NOT serialized (transient edge state) -- mirrors m_pendingIrq2. No
   kChipsetVersion bump.

2. **Accessors.** `pendingIrq3(int cpuId)` / `clearPendingIrq3(int cpuId)` --
   bodies identical to the Irq2 pair.

3. **IPREQ -> IPINTR set, INSIDE the CAS loop**, placed AFTER the ABT/ABW block
   (~1273) and BEFORE the `newVal` line (~1288):

       uint64_t const ipreqSet = writeVal & mask(MISC::IPREQ);
       if (ipreqSet != 0) {
           stagedW1CW1S |= (ipreqSet >> 4) & mask(MISC::IPINTR);
       }

   CRITICAL -- mirror the SHIFT, not the GATE. The adjacent ABT->ABW promote is
   *conditionally gated* (`if (newlyTried != 0 && oldAbt == 0)`, ~1265-1272)
   because it is arbitration. IPREQ->IPINTR is UNCONDITIONAL per HRM ("writing a
   1 here sets the corresponding bit"). Do NOT clone the `oldAbt == 0` guard --
   if you do, a second IPI-while-pending silently fails to re-assert. The shift
   distance (4) happens to match ABT(20-23)->ABW(16-19); the gate does not carry.
   (IPREQ is WO so it does not persist: `newVal = (stagedW1CW1S & ~WO_MASK) |
   (old & WO_MASK)` at ~1288 drops it; IPINTR is not in WO_MASK so the OR
   survives. Verified against source.)

4. **b_irq<3> assert, AFTER the CAS.** Bound the SET loop by `m_cpuCount`, NOT
   kMaxCPUs -- mirror `fireIntervalTimer` (`n < m_cpuCount && n < kMaxCPUs`,
   ~619):

       uint64_t const ipreqFire = writeVal & mask(MISC::IPREQ);
       if (ipreqFire != 0) {
           for (int n = 0; n < m_cpuCount && n < kMaxCPUs; ++n)
               if (ipreqFire & (uint64_t{1} << (MISC::IPREQ.lsb + n)))
                   m_pendingIrq3[n].store(true, std::memory_order_release);
       }

   Why m_cpuCount: on DS20 (cpuCount=1) a stray IPREQ to a non-configured target
   bit would otherwise latch m_pendingIrq3[1..3] that `Machine::run` never polls
   (it reads pendingIrq3(0) only) -> a stuck, never-cleared latch. m_cpuCount
   bound matches the timer and is more faithful.

5. **IPINTR W1C -> clear latch, AFTER the CAS**, adjacent to the ITINTR-clear
   loop (~1314). Clone it; kMaxCPUs bound is correct here (clearing a
   non-configured CPU's latch is harmless, matches ITINTR):

       uint64_t const ipintrClears = writeVal & mask(MISC::IPINTR);
       if (ipintrClears != 0) {
           for (int n = 0; n < kMaxCPUs; ++n)
               if (ipintrClears & (uint64_t{1} << (MISC::IPINTR.lsb + n)))
                   clearPendingIrq3(n);
       }

   (W1C_MASK already contains IPINTR<11:8> (~1196), so the CAS already cleared
   the stored bit -- this only drops the latch, exactly like ITINTR.)

6. **Delete the two IPI `TODO(unwired)` lines (~1353-1356)** as they are wired.

LATCH KEYING: keep the `writeVal & IPREQ` / `writeVal & IPINTR` form. That is the
file's house pattern -- the NXM->DRIR hook (~1337) and the ITINTR clears (~1314)
all key off `writeVal`, not resolved storage. (Supersedes an earlier suggestion
to derive the latch from resolved IPINTR; resolved-state is cleaner in the
abstract but would make IPI the one odd side effect in the function.)

OPTIONAL part-accuracy (minor): on Tsunami, IPREQ<15:14>/IPINTR<11:10>/
ITINTR<7:6> are Typhoon-only. A faithful 2-CPU Tsunami masks IPREQ to <13:12>
(Typhoon/Titan: <15:12>). Firmware on a 2-socket box won't write <15:14>, and
the m_cpuCount set-loop bound already makes a stray write benign, so this is
family-accurate-vs-part-accurate polish, not required.

### B. systemLib/Machine.cpp

Add a `b_irq<3>` IPI divert poll mirroring the `b_irq<2>` block (~1246),
SINGLE-CPU (`pendingIrq3(0)`), sharing `divertedThisCycle`:

       if (!divertedThisCycle
           && canAcceptInterrupt(/*IPL*/ 20)            // PROVISIONAL -- see VERIFY
           && m_chipset.cchip().pendingIrq3(0)) {
           stageInterruptDivert(m_cpu, uint64_t{1} << 36);   // EI[3] PROVISIONAL
           m_chipset.cchip().clearPendingIrq3(0);
           divertedThisCycle = true;
       }

This polls CPU0 only -- correct and sufficient for the CPU0->CPU0 self-test.
Placement among the four diverts depends on the IPI IPL rank (see VERIFY).

## VERIFY #1 -- RESOLVED 2026-06-18 (authoritative, from PALcode source)

Decoded NOT by disassembly but from the firmware's own build source:
`Processor Support/Palcode/.../apisrm/ref/ev6_osf_pc264_pal.mar` (the PC264 =
DS10/DS20 OSF PAL).  Its IPL table (lines 730-760) gives, for pc264_system:
`IRQ_IP = 8` (bit 3 of the ISUM IE field), `IRQ_CLK=4, IRQ_DEV=2, IRQ_ERR=1`,
and `EV6__IER__EIEN__S = 33`.  Therefore:

- **Cause bit = EI[3] = 1<<36** (bit 33 + 3).  Confirmed three ways: this PAL
  source, V4's canAcceptInterrupt map (line 659: irq_h[3] -> IER bit 36), and
  the b_irq<n>->EI[n] pattern.  NOT provisional.
- **Gate = canAcceptInterrupt(21).**  canAcceptInterrupt's Phase-D body IS
  wired (Machine.cpp ~671): `ierBit = 57 - irqLevel`, returns `(ier & (1<<ierBit))`.
  irqLevel 21 -> bit 36 = the interprocessor EIEN bit.  Passing 20 would select
  bit 37 (perf counter 0) -- WRONG.  So the gate reads the guest's ACTUAL
  interprocessor enable, fully faithful to the OSF IPL_TABLE.
- **IPL / rank:** interprocessor shares the clock's IPL (both IPL 5; device IPL
  4, error IPL 7).  So IPI is NOT the lowest (the design v2 "lowest/last"
  framing was wrong, as the c-web reviewer suspected).  Rank is moot in
  practice: the gate is the per-source IER bit, so the firmware's EIEN mask
  decides priority.  Section B placed the IPI poll right after the timer poll
  (same IPL tier), guarded by `divertedThisCycle`.
- Caveat: this is the OSF PC264 PAL; a VMS variant would use different IPL
  *numbers* but the same IER-bit gate, so canAcceptInterrupt(21) (bit 36) holds
  regardless of OSF-vs-VMS.

LANDED 2026-06-18: A.1-A.6 in TsunamiCchip.h + B (canAcceptInterrupt(21),
stageInterruptDivert(1<<36)) in Machine.cpp.  Awaiting MSVC build + DS20 re-run.

## (historical) VERIFY items before the decode -- now resolved above

1. **EI bit, IPL value, AND IPL rank -- all from the PAL decode, not the Cchip
   HRM.** `EI[3]=1<<36` is assumed from the b_irq<n>->EI[n] pattern; IPL=20 and
   "lowest priority / checked last" are assumed from V4's in-file table.
   - The 21272 HRM contains ZERO "IPL" references; Sec 6.3.1 is device/error
     `b_irq<1:0>` only. The b_irq->IPL map is a 21264 + PALcode fact. The dead
     "HRM 6.3.1" citation exists in BOTH this design AND the source file
     (TsunamiCchip.h:504-509) -- fix the attribution in both to point at the PAL
     / 21264 HRM interrupt section.
   - IPI = lowest IPL is the most load-bearing unverified claim and is UNUSUAL:
     Tru64 PAL often ranks IPI ABOVE device/timer (it carries TLB-shootdown /
     reschedule). If DS20 PAL ranks IPI higher, the "checked last" placement
     inverts and a co-pending timer would wrongly preempt an IPI at runtime.
   - Does NOT affect the DS20 boot fix: the CPU0->CPU0 self-test has nothing
     co-pending, so delivery happens at any rank. This bites later SMP runtime.
   - Confirm by decoding the DS20 PAL IP-interrupt SCB entry / ISUM EI-nibble
     test in the decompressed image (`sys__int_ipi`). [[feedback_provisional_ipr_scbd]]

## Recorded decision: CSC<IPENA> enable mask intentionally NOT modeled

HRM Sec 6.3.3 names `CSC<IPENA>` "the enable mask for interprocessor
interrupts", but it is ABSENT from both CSC register tables (10-9 Tsunami, 10-10
Typhoon) -- a known 21272-HRM prose-vs-table inconsistency. Modeling delivery
UNCONDITIONALLY (no IPENA gate) is safe for the DS20 unblock: an unmodeled gate
can only cause an early/spurious IPI, never a hang, and POST's self-test wants
delivery enabled. CSC is RW/store-what's-written, so any value POST writes reads
back. Logged as a watch-item, not a blocker.

## Generalization -- corrected, do not oversell

- ES40 is **Tsunami (21272)**, a sibling of DS20 -- same MISC, covered by this
  change directly. ES45 / DS25 are **Titan (21274)**; the ES45 Service Guide
  (Table D-10) shows identical IPI lanes (incl. 4-CPU IPI refs), so the REGISTER
  layout carries across Tsunami/Typhoon/Titan.
- Per-CPU DISPATCH does NOT generalize as written: the Machine poll is
  single-CPU `pendingIrq3(0)`. Storage/register half is family-general; dispatch
  is CPU0-only -- the correct scope for "no second CPU." Real SMP turns the poll
  into a per-CPU loop and re-enters the `LDx_L`/`STx_C` interlock cliff (parked).
- Before trusting store-watch offsets on Titan, confirm the 21274 Cchip CSR base
  maps MISC to 0x80 in the same window -- field-identical != offset-identical.

## Confirm-it-is-the-blocker first (cheap, one register)

Store-watch MISC @ **0x80 only** (drop 0x380/0x3c0 -- those are IIC timer-
suppress and prove nothing): a write into IPREQ<15:12> followed by a poll-read
of IPINTR<11:8> is positive proof of mode-3, and is the same capture that
collapses the callback/rendezvous/timeout branches. Alternatively, land the
patch and re-run -- DS20 advancing past console-idle confirms it.

## Latent snapshot gap (note, do NOT fix this ticket)

`serialize()` (~1134) writes `m_misc` (holds IPINTR<11:8>) but the latch is not
serialized, so a snapshot taken with an IPI pending-but-unacked restores with
IPINTR set in m_misc and m_pendingIrq3 zeroed -- register says pending, latch
says nothing, deliver poll keys off the latch -> IPI lost across restore. This
ALREADY exists for ITINTR/Irq2; the change just makes it symmetric. Irrelevant
to DS20 cold boot (no mid-IPI snapshots). Cheap future closure for both: rebuild
m_pendingIrq2/3[n] from the restored m_misc ITINTR/IPINTR bits in `deserialize()`.

## Edge-vs-level model (stated assumption)

The latch is edge-consumed (set on request, cleared on divert); hardware is
level (irq<3> stays asserted while IPINTR<8+n> set, drops on W1C). Diverge only
if a handler RTIs WITHOUT W1C-ing IPINTR -- hardware re-interrupts, the model
does not. Correct `sys__int_ipi` always W1Cs before RTI, so this never bites
real firmware. Same posture the working timer (ITINTR/Irq2) path already has;
consistency is the saving grace.

## Net

Sections A.1-A.6 + B are HRM-faithful and safe to land for the DS20 unblock.
The three corrections that matter mechanically: (1) IPREQ set is UNGATED -- do
not clone the ABT/ABW guard; (2) the b_irq<3> SET loop is bounded by m_cpuCount,
not kMaxCPUs; (3) fix the dead "HRM 6.3.1" IPL citation in both spec and source.
DELIVERY (EI[3]=1<<36, IPL value + rank) stays PAL-decode-only under VERIFY #1.
