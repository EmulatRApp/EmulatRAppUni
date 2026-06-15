// ============================================================================
// Tsunami21272_CsrSpec.h -- bitfield specification for chipset CSRs
// ============================================================================
// Project: EmulatR -- Alpha AXP / EV6 Architecture Emulator (V4)
// Copyright (C) 2025, 2026 eNVy Systems, Inc.  All rights reserved.
// Licensed under eNVy Systems Non-Commercial License v1.1
//
// Project Architect: Timothy Peer
// AI Collaboration:  Claude (Anthropic)
//
// Commercial use prohibited without separate license.
// Contact:        peert@envysys.com  |  https://envysys.com
// Documentation:  https://timothypeer.github.io/ASA-EMulatR-Project/
// ============================================================================
//
// PURPOSE:
//   Single source of truth for chipset CSR bit-field layout, R/W
//   semantics, and reset values, across all Tsunami / Typhoon
//   variants.  Companion to `Tsunami21272_RegisterMap.h` (which
//   carries the MMIO offsets); this header carries the per-register
//   *contract* the firmware exercises.
//
//   Phase A deliverable per `journals/CchipPhaseA_Design_Notes.md`.
//   The data here is consumed by Phase B refactor of the
//   TsunamiCchip / TsunamiDchip / TsunamiPchip read/write switches:
//   every spec'd register gets a typed decode path; every unwritten
//   register carries the `TODO(unwired)` discipline.
//
// LAYOUT:
//   Each register has its own nested namespace inside
//   `Tsunami21272::Spec`.  Inside each register's namespace:
//
//     - `inline constexpr FieldSpec <FieldName> { lsb, width, sem, reset };`
//       One entry per HRM-named subfield, with the bit position, the
//       field width, an R/W/W1C/W1S/RO/WO/MBZ semantic tag, and the
//       reset value (masked to width).  Magic numbers do NOT appear
//       at call sites -- only here.
//
//     - For registers with variant differences (REV, CSC widths,
//       AAR sizing), separate `*_Tsunami` and `*_Typhoon` constants
//       record the variant-specific values.
//
//     - A `// HRM: section X.Y.Z, Table N` cross-reference at the
//       top of each register's namespace points back at the HRM
//       page where the table lives.
//
// SEMANTIC TAGS (matches HRM "Type" column):
//   "RO"   -- read-only, ignored on write.
//   "RW"   -- read/write, full word.
//   "WO"   -- write-only.  Read returns UNPREDICTABLE per HRM; V4
//             reads return 0.
//   "W1C"  -- write-1-to-clear.  Bits set in the write value clear
//             the matching bits in the register.
//   "W1S"  -- write-1-to-set.  Bits set in the write value set the
//             matching bits in the register.  HRM also locks some
//             W1S fields once any bit is set; lock rules are
//             called out in the per-field comment.
//   "MBZ"  -- must-be-zero on write.
//   "RAZ"  -- reads-as-zero.
//   "DYN"  -- dynamic-on-read.  Field value is injected by the read
//             handler (e.g., MISC<CPUID>) and not stored.  V4
//             callers must thread the cpuId through `read(off, cpu)`.
//
//   Combined tags such as "MBZ,RAZ" or "R,W1C" mirror the HRM
//   column verbatim.
//
// COVERAGE (resolution (4) of Phase A design doc):
//   Complete per variant -- every HRM-defined register has a spec
//   entry.  Registers whose bitfield table has not yet been
//   transcribed from the HRM carry a `// TODO(spec-extract): ...`
//   comment with the HRM section number.  Phase B extracts each
//   table as it wires the register's decode path.
//
//   MISC, IIC, and CSC are fully populated this commit -- they are
//   the registers Phase C uses directly for the interval-timer
//   work.  Every other register's HRM reference is recorded for
//   Phase B follow-up extraction.
//
// REFERENCES:
//   HRM:            Tsunami/Typhoon 21272 HRM (EC-RE2CA-TE Rev 4.0,
//                   21 October 1999) -- Chapter 10 "Programmer's
//                   Reference," sections 10.2.2.x (Cchip), 10.2.3.x
//                   (Dchip), and Chapter 11 (Pchip).
//   Companion map:  chipsetLib/Tsunami21272_RegisterMap.h
//   Phase A design: journals/CchipPhaseA_Design_Notes.md
// ============================================================================
//
// CHANGE HISTORY:
//   2026-05-14  Initial commit -- Phase A spec scaffolding.  Fully
//               populated: Cchip CSC, MISC, IIC.  TODO-stubbed:
//               remainder of Cchip, all of Dchip, all of Pchip.
//
// TODO REGISTER TABLE (Phase B follow-up extraction from HRM):
//   Cchip:
//     MTR    -- HRM 10.2.2.2, Table 10-11.  Memory timing fields.
//     MPD    -- HRM 10.2.2.4, Table 10-13.  Presence detect pins.
//     AAR0-3 -- HRM 10.2.2.5, Tables 10-14 (Tsunami) and 10-15
//               (Typhoon).  Variant-dependent ASIZ encoding.
//     DIMn   -- HRM 10.2.2.6, Table 10-16.  64-bit device mask.
//     DIRn   -- HRM 10.2.2.7, Table 10-17.  Combinational
//               (DRIR & DIMn); no storage.
//     DRIR   -- HRM 10.2.2.8, Table 10-18.  64-bit raw IRQ.
//     PRBEN  -- HRM 10.2.2.9.  Probe enable.
//     MPR0-3 -- HRM 10.2.2.12, Table 10-22.  Memory programming.
//     MCTL   -- HRM 10.2.2.13.  Tsunami-only; MBZ.
//     TTR    -- HRM 10.2.2.14, Table 10-23.  TIGbus timing.
//     TDR    -- HRM 10.2.2.15, Table 10-24.  Device timing.
//     PWR    -- HRM 10.2.2.x.  Typhoon-only.  Power management.
//     CMONCT*-- HRM 10.2.2.x.  Typhoon-only.  Monitor control/count.
//   Dchip:
//     DSC    -- HRM 10.2.3.1.  Dchip system config.
//     STR    -- HRM 10.2.3.2.  Stripe.
//     DREV   -- HRM 10.2.3.3.  Dchip revision.
//     DSC2   -- HRM 10.2.3.4.  Reserved/future.
//   Pchip (per Pchip0/Pchip1):
//     WSBAn  -- HRM 11.x, PCI DMA window base.
//     WSMn   -- HRM 11.x, PCI DMA window mask.
//     TBAn   -- HRM 11.x, PCI DMA translation base.
//     PCTL   -- HRM 11.x, PCI control.
//     PLAT   -- HRM 11.x, PCI latency.
//     PERROR -- HRM 11.x, W1C PCI error.
//     PERRMSK-- HRM 11.x, PCI error mask.
//     PERRSET-- HRM 11.x, WO PCI error set (diagnostics).
//     TLBIV  -- HRM 11.x, WO TLB invalidate single.
//     TLBIA  -- HRM 11.x, WO TLB invalidate all.
//     PMONCTL-- HRM 11.x, perf monitor control.
//     PMONCNT-- HRM 11.x, perf monitor count.
//     SPRST  -- HRM 11.x, WO PCI soft reset.
//
// When a register's fields are extracted in Phase B, the entry above
// is removed and the per-register namespace below is populated.
// ============================================================================

#ifndef CHIPSETLIB_TSUNAMI21272_CSR_SPEC_H
#define CHIPSETLIB_TSUNAMI21272_CSR_SPEC_H

#include <cstdint>

#include "chipsetLib/TsunamiVariant.h"

namespace Tsunami21272 {
namespace Spec {

// ----------------------------------------------------------------------------
// FieldSpec POD.
//
//   lsb       -- bit position of the least-significant bit of the field.
//   width     -- field width in bits, 1..64.
//   sem       -- HRM semantic tag, e.g., "RO", "RW", "W1C", "DYN".
//                One of the values listed in the file header.
//   reset     -- power-on reset value, masked to `width`.  For DYN
//                fields, the reset value is meaningless (the value is
//                computed on read).
//
// Constexpr POD; aggregate-initialized at spec sites.  Trivially
// copyable, safe in constexpr contexts.
// ----------------------------------------------------------------------------
struct FieldSpec {
    uint8_t     lsb;
    uint8_t     width;
    char const* sem;
    uint64_t    reset;
};

// ----------------------------------------------------------------------------
// Bitfield helpers.  Compile-time mask/shift derivation from a FieldSpec.
// Phase B Cchip code uses these to read/write fields without restating
// the bit positions at the call site.
//
//   mask(f)       -- mask of the field in its 64-bit container.
//   extract(reg, f) -- f-aligned slice of `reg`, zero-extended.
//   deposit(reg, f, val) -- `reg` with the f bits replaced by `val`.
// ----------------------------------------------------------------------------
inline constexpr uint64_t mask(FieldSpec const& f) noexcept
{
    // (1<<width)-1 with the width=64 edge case guarded; we never spec
    // a 64-bit field but defensive in case a future register has one.
    uint64_t const widthMask =
        (f.width >= 64) ? ~uint64_t{0} : ((uint64_t{1} << f.width) - 1);
    return widthMask << f.lsb;
}

inline constexpr uint64_t extract(uint64_t reg, FieldSpec const& f) noexcept
{
    return (reg & mask(f)) >> f.lsb;
}

inline constexpr uint64_t deposit(uint64_t reg, FieldSpec const& f,
                                  uint64_t val) noexcept
{
    uint64_t const m = mask(f);
    uint64_t const widthMask =
        (f.width >= 64) ? ~uint64_t{0} : ((uint64_t{1} << f.width) - 1);
    return (reg & ~m) | ((val & widthMask) << f.lsb);
}

// ============================================================================
// CCHIP REGISTERS
// ============================================================================

namespace Cchip {

// ----------------------------------------------------------------------------
// CSC -- System Configuration Register
//   HRM 10.2.2.1, Tables 10-9 (Tsunami) and 10-10 (Typhoon)
//
// System-wide configuration written once at reset.  CPU present mask
// in the low bits; system parameters (PRQMAX, PDTMAX, FPQ*, TPQ*,
// EFT/FET, BC, DRTP, DWFP, DWTP) in the upper bits.  Read-only after
// the platform writes it at boot.
//
// Variant differences are in PRQMAX width and a few field positions;
// the HRM tables call them out explicitly.  Phase A captures the
// most-touched fields; the rest follow the TODO(spec-extract).
// ----------------------------------------------------------------------------
namespace CSC {

    // CPU present mask -- bit n set means CPU n is populated.  Width 4
    // (one bit per CPU) on Typhoon; width 2 on Tsunami where the upper
    // bits overlap with the BC field.
    inline constexpr FieldSpec PIP        { 0, 4, "RO",  0x00};

    // Bus configuration -- HRM 10.2.2.1 paragraph 2: BC=1 selects four
    // Dchips on one memory bus, the only configuration V4 emulates.
    inline constexpr FieldSpec BC         { 0, 2, "RO",  0x01};

    // Variant cap / mask fields -- HRM Tables 10-9 / 10-10.  Encoded
    // verbatim in the V4 reset() of TsunamiCchip; the spec records
    // the field positions for forward use.
    inline constexpr FieldSpec DWTP       {16, 2, "RO",  0x03};
    inline constexpr FieldSpec DWFP       {18, 2, "RO",  0x03};
    inline constexpr FieldSpec DRTP       {20, 2, "RO",  0x03};
    inline constexpr FieldSpec FET        {26, 4, "RO",  0x02};
    inline constexpr FieldSpec EFT        {31, 1, "RO",  0x01};
    inline constexpr FieldSpec TPQMMAX    {36, 4, "RO",  0x01};
    inline constexpr FieldSpec FPQCMAX    {40, 4, "RO",  0x01};
    inline constexpr FieldSpec FPQPMAX    {44, 4, "RO",  0x01};
    inline constexpr FieldSpec PDTMAX     {48, 4, "RO",  0x01};
    inline constexpr FieldSpec PRQMAX     {52, 4, "RO",  0x02};

    // TODO(spec-extract): IDDW, IDDR, AW, P1P, IDQT, PIP-wide on Typhoon,
    //   and other Tsunami/Typhoon-specific config bits.  HRM Tables
    //   10-9 / 10-10 for the complete table.  These are read-once at
    //   boot; firmware-visible but rarely touched after reset.

} // namespace CSC


// ----------------------------------------------------------------------------
// MISC -- Miscellaneous Register
//   HRM 10.2.2.3, Table 10-12.
//
// The most-touched Cchip CSR.  Hosts:
//   - CPUID injection on read (DYN)
//   - Interval timer pending (ITINTR -- the field this entire Phase
//     A/B/C effort is in service of)
//   - IPI request / pending (IPREQ / IPINTR)
//   - Arbitration try / won / clear (ABT / ABW / ACL)
//   - NXM detection and source (NXM / NXS)
//   - Device IRQ suppression (DEVSUP, used by TIGbus polling)
//   - Chip revision (REV)
//
// Write semantics are field-specific (W1C, W1S, WO).  HRM 10.2.2.3
// paragraph 1: "there are no read side effects, and ... writing a 0
// to any bit has no effect."  Software writes a 1 to the bit it
// wants to act on; no read-modify-write needed.
//
// V4 implementation note: this register is the canonical CAS-loop
// W1C/W1S target.  Phase B will own miscWriteW1C() per the design
// doc Section 4 sketch.
//
// Variant differences:
//   - DEVSUP <43:40>:  <43:42> are Typhoon-only (CPU2, CPU3).
//   - ABT    <23:20>:  <23:22> are Typhoon-only.
//   - ABW    <19:16>:  <19:18> are Typhoon-only.
//   - IPREQ  <15:12>:  <15:14> are Typhoon-only.
//   - IPINTR <11:8>:   <11:10> are Typhoon-only.
//   - ITINTR <7:4>:    <7:6>   are Typhoon-only.
//   - CPUID  <1:0>:    <1>     is  Typhoon-only.
//   - REV    <39:32>:  1 = Tsunami (21272), 8 = Typhoon (21274).
// ----------------------------------------------------------------------------
namespace MISC {

    // Suppress device IRQ (irq<1>) to the CPU corresponding to a 1 in
    // this field until the TIG poll machine completes one full pass.
    // Used by PALcode to drain stale device interrupts.  WO -- reads
    // are UNPREDICTABLE per HRM.
    inline constexpr FieldSpec DEVSUP     {40, 4, "WO",   0x00};

    // Chip revision.  RO; reset value is variant-dependent.
    inline constexpr FieldSpec REV        {32, 8, "RO",   0x00};  // see REV_RESET_*
    inline constexpr uint64_t  REV_RESET_TSUNAMI = 0x01;  // HRM Table 10-12
    inline constexpr uint64_t  REV_RESET_TYPHOON = 0x08;

    // NXM source -- which CPU/Pchip caused the most recent NXM.
    // Locked at NXM-set time; UNPREDICTABLE when NXM == 0.  Cleared
    // when NXM is W1C'd.
    inline constexpr FieldSpec NXS        {29, 3, "RO",   0x00};

    // Nonexistent memory detected.  Sets DRIR<63> when asserted.
    // W1C; clearing NXM also unlocks NXS and triggers re-arm.
    inline constexpr FieldSpec NXM        {28, 1, "W1C",  0x00};

    // Arbitration clear.  WO; writing 1 clears both ABT and ABW
    // and unlocks ABW.  Read returns 0 (no storage).
    inline constexpr FieldSpec ACL        {24, 1, "WO",   0x00};

    // Arbitration try.  W1S per CPU.  Used by SMP init sequence
    // (HRM Section 6.6 / Section 12.x boot synchronisation).
    inline constexpr FieldSpec ABT        {20, 4, "W1S",  0x00};

    // Arbitration won.  W1S per CPU; LOCKED once any bit is set --
    // the first CPU to write wins, subsequent writes ignored.
    // Cleared by writing 1 to ACL.
    inline constexpr FieldSpec ABW        {16, 4, "W1S",  0x00};

    // Interprocessor interrupt request.  WO; writing 1 to bit n
    // sets IPINTR<8+n>, asserts irq<3> to CPU n.
    inline constexpr FieldSpec IPREQ      {12, 4, "WO",   0x00};

    // Interprocessor interrupt pending.  W1C per CPU.  Acked by the
    // receiving CPU writing 1 to its bit; deasserts irq<3>.
    inline constexpr FieldSpec IPINTR     { 8, 4, "W1C",  0x00};

    // Interval timer interrupt pending.  W1C per CPU.  THIS IS THE
    // FIELD the IntervalTimer module drives in Phase C.  Set by the
    // Cchip when the interval-timer pin (or, in V4, the cycle-counted
    // mask test) fires.  Asserts irq<2> to the CPU corresponding to
    // a 1 bit.  Acked by the CPU writing 1 to its bit, which clears
    // the bit and deasserts irq<2>.
    inline constexpr FieldSpec ITINTR     { 4, 4, "W1C",  0x00};

    // CPUID -- dynamically injected on read by the read handler from
    // the CPU's identity.  Not stored.  V4 reads must thread cpuId
    // through `read(offset, cpuId)`; the no-cpuId overload returns
    // CPUID = 0.
    inline constexpr FieldSpec CPUID      { 0, 2, "DYN",  0x00};

    // ------------------------------------------------------------------
    // Composite masks used by the CAS-loop W1C/W1S writer.
    //
    // W1C bits: write-1-clears the matching register bit.  Composed
    // from the three W1C fields above.
    //
    // W1S bits: write-1-sets the matching register bit.  Composed
    // from the two W1S fields above.
    //
    // WO  bits: write-only fields whose effect is "do the action,
    // do not store."  Composed from DEVSUP, ACL, IPREQ.  The writer
    // dispatches these to their side-effect handlers rather than
    // depositing them in the register storage.
    //
    // RO bits: read-only fields whose stored value never changes
    // post-reset.  Phase B's writer masks these off.
    //
    // The remaining bits (RES<63:44>, RES<27:25>, RES<3:2>) are
    // MBZ,RAZ -- write ignored, read returns 0.
    // ------------------------------------------------------------------
    inline constexpr uint64_t W1C_MASK =
          mask(NXM) | mask(IPINTR) | mask(ITINTR);

    inline constexpr uint64_t W1S_MASK =
          mask(ABT) | mask(ABW);

    inline constexpr uint64_t WO_MASK =
          mask(DEVSUP) | mask(ACL) | mask(IPREQ);

    inline constexpr uint64_t RO_MASK =
          mask(REV) | mask(NXS) | mask(CPUID);

} // namespace MISC


// ----------------------------------------------------------------------------
// IICn -- Interval Ignore Count Register (one per CPU)
//   HRM 10.2.2.10, Table 10-20.
//
// Used for 21264 sleep mode.  Software writes a count of how many
// interval-timer ticks to suppress to this CPU.  Each subsequent
// timer tick (per the IntervalTimer module's fire predicate)
// decrements ICNT for each enabled CPU before asserting MISC<ITINTR>
// for that CPU.  When ICNT reaches 0, the next tick goes through.
// One more tick after that goes "negative" (sets OF) so software
// can compute exactly how many ticks were skipped during the sleep
// window.
//
// V4 implementation (Phase C): atomic fetch_sub on the count field;
// the tick handler checks the prior value -- if it was nonzero, the
// tick is suppressed for that CPU; if it was 0, the tick is
// delivered and the count goes to -1 -1 (encoded as 0xFFFFFE...);
// on the *next* tick after that, OF gets set.  This matches the
// HRM "wake-up tick is received, count goes negative, OF set on
// the next interval" sequence.
// ----------------------------------------------------------------------------
namespace IIC {

    // Overflow indicator -- set when ICNT has gone negative.  RO.
    // Cleared only by writing a new positive count to ICNT.
    inline constexpr FieldSpec OF         {24,  1, "RO",  0x00};

    // Ignore count -- 24-bit signed counter.  RW.  Software writes
    // a positive count to start a sleep window; hardware decrements
    // on each tick.  Width 24 -- maximum ~16.7M ticks ignorable.
    // At ES40's ~1144 Hz tick rate that's ~4 hours of sleep, well
    // beyond any realistic firmware scenario.
    inline constexpr FieldSpec ICNT       { 0, 24, "RW",  0x00};

    // Bits <63:25> are MBZ,RAZ.

} // namespace IIC

} // namespace Cchip


// ============================================================================
// DCHIP REGISTERS
// ============================================================================
//
// Dchip surface is small (4 registers).  All four are TODO(spec-extract)
// for Phase B -- the HRM tables are short and will be transcribed
// when the Dchip read/write switch is refactored.
// ============================================================================

namespace Dchip {

    // TODO(spec-extract): DSC, STR, DREV, DSC2.
    // HRM 10.2.3.1 -- 10.2.3.4.
    //
    // Known so far (from V4 reset() and the existing register map):
    //   DREV reset = 0x10 on Tsunami, 0x20 on Typhoon.
    //   DSC, STR, DSC2 all reset to 0; storage stub only today.

    inline constexpr uint64_t DREV_RESET_TSUNAMI = 0x10;
    inline constexpr uint64_t DREV_RESET_TYPHOON = 0x20;

} // namespace Dchip


// ============================================================================
// PCHIP REGISTERS
// ============================================================================
//
// Pchip0 and Pchip1 share the register layout.  Per-Pchip state is
// stored separately at runtime; the spec here is shared.
//
// Phase A delivers the cross-reference table only; the per-field
// bit positions are TODO(spec-extract) for Phase B, since the
// existing V4 Pchip code already covers the load-bearing fields
// (PCTL, WSBA/WSM/TBA, PERROR) functionally.  Phase B promotes them
// to spec-driven decoders with explicit W1C semantics on PERROR.
// ============================================================================

namespace Pchip {

    // TODO(spec-extract): WSBA0-3, WSM0-3, TBA0-3, PCTL, PLAT,
    // PERROR (W1C), PERRMASK, PERRSET (WO), TLBIV (WO), TLBIA (WO),
    // PMONCTL, PMONCNT, SPRST (WO).  HRM Chapter 11.
    //
    // Known so far (existing V4 code in TsunamiPchip.h):
    //   PCTL reset = 0x0 (all features disabled).
    //   PERROR is W1C.
    //   WSBA*<0> = window enable, WSBA*<1> = scatter-gather enable.

} // namespace Pchip


// ============================================================================
// VARIANT SELECTORS
// ============================================================================
//
// Reset values that differ between Tsunami and Typhoon.  Each
// selector is a one-liner switching on `ChipsetVariant`.  Phase B
// callers use these at construction time to populate storage with
// the right initial value.
// ============================================================================

inline constexpr uint64_t resetMiscRev(ChipsetVariant v) noexcept
{
    return (v == ChipsetVariant::Typhoon)
         ? Cchip::MISC::REV_RESET_TYPHOON
         : Cchip::MISC::REV_RESET_TSUNAMI;
}

inline constexpr uint64_t resetDchipDrev(ChipsetVariant v) noexcept
{
    return (v == ChipsetVariant::Typhoon)
         ? Dchip::DREV_RESET_TYPHOON
         : Dchip::DREV_RESET_TSUNAMI;
}

// ============================================================================
// INTERVAL TIMER PROFILE (Phase C consumer)
// ============================================================================
//
// The IntervalTimer module's cycle interval is derived from a
// profile-clock constant.  Phase A places the constant and the
// constexpr rounding here so the rate is visible alongside the
// register spec it drives; Phase C wires the predicate.
//
// Algorithm: target tick rate is ~1024 Hz per HRM Section 6.3.2;
// kCchipIntervalTimerCycles = profileAlphaClockHz / 1024.  Round
// to the nearest power of two so the fire predicate becomes a
// single AND-mask test on cpu.cycleCount, not a counter compare.
// ES40 (600 MHz) -> bit 19 (~1144 Hz); ES45 (1 GHz) -> bit 20
// (~954 Hz).  Both within ~12% of the HRM target; firmware does
// not depend on the exact rate.
//
// Profile escape hatch: today this is one constexpr per build.  A
// future multi-profile build can promote `kProfileAlphaClockHz` to
// a runtime field on Machine and recompute the mask at construction;
// see resolution (2) of the Phase A design doc.
// ============================================================================

inline constexpr int roundLog2Nearest(uint64_t n) noexcept
{
    // Returns N such that 2^N is closest (in log space) to `n`.
    // Handles n=0 by returning 0 -- the caller must ensure n>=1
    // for a meaningful timer interval.
    int bit = 0;
    uint64_t v = 1;
    while (v < n) { ++bit; v <<= 1; }
    // v is now smallest power of two >= n.  If the previous power
    // (v>>1) is closer to n, prefer it.
    if (bit > 0 && (n - (v >> 1)) < (v - n)) {
        --bit;
    }
    return bit;
}

// ----------------------------------------------------------------------------
// Profile clock constant.
// ----------------------------------------------------------------------------
//
// Default ES45 / Typhoon (21274) at 1 GHz.  Phase C bump from the
// original Phase A default of 600 MHz (ES40 / Tsunami 21272) -- the
// V4 binary currently identifies as "Typhoon 21274 (wired)" at the
// chipset construction site, so the timer rate should match the
// profile that's actually executing.  Approach (a) per Phase A
// resolution 2: one constexpr per build; the runtime-field escape
// hatch (Machine member that recomputes the mask at construction)
// is the deferred path for multi-profile-in-one-binary, not built
// today.
//
// To switch back to ES40 600 MHz for an ES40 build, override at
// configure time:  cmake -DEMULATR_PROFILE_ALPHA_CLOCK_HZ=600000000
//
// Effective tick rates worked from `roundLog2Nearest` at the bottom
// of this section:
//   ES45 1 GHz  -> bit 20  -> 1048576 cycles/tick  -> ~953.7 Hz
//   ES40 600 MHz -> bit 19 ->  524288 cycles/tick  -> ~1144   Hz
//   Both within ~12% of the HRM 1024 Hz nominal; OSF/1 PAL idle-wait
//   is rate-agnostic so the drift is not firmware-observable.
// ----------------------------------------------------------------------------
#ifndef EMULATR_PROFILE_ALPHA_CLOCK_HZ
// 2026-06-02 (timer spec v2 sec.5.3 EXPERIMENT): lowered 1000000000 -> 2^28 so the
// interval drops 2^20 -> 2^18 (4x faster ticks, still within the kCchipTimerBit[18,22]
// static_assert). TEST: if the firmware CALIBRATES its clock (timer.c:1468 path), its
// clock belief = interval*1024 = this value, so every real-time delay shrinks ~4x AND
// timer_check stays consistent (it measured our rate). If instead DS10 uses the ISP
// constant (timer.c:1458 HWRPB$_CC_FREQ), delays will NOT shrink -> different lever.
// Original ES45 value was 1000000000ULL; revert if the calibration premise fails.
#define EMULATR_PROFILE_ALPHA_CLOCK_HZ 268435456ULL     // 2^28 (was 1e9 / ES45)
#endif

inline constexpr uint64_t kProfileAlphaClockHz =
    static_cast<uint64_t>(EMULATR_PROFILE_ALPHA_CLOCK_HZ);

// Target tick interval in cycles -- the named constant Tim called
// out: "kCchipIntervalTimerCycles = profileAlphaClockHz / 1024."
inline constexpr uint64_t kCchipIntervalTimerCycles =
    kProfileAlphaClockHz / 1024;

// Rounded power-of-two bit position and mask.  The fire predicate
// in Phase C (chipsetLib/CchipIntervalTimer.h) reads:
//   (cycleCount & kCchipTimerMask) == 0 && cycleCount != 0
// -- one AND + compare-to-zero + AND + compare-to-zero, branch-free
// hot-path test, single integer register pressure.
inline constexpr int      kCchipTimerBit  =
    roundLog2Nearest(kCchipIntervalTimerCycles);

inline constexpr uint64_t kCchipTimerMask =
    (uint64_t{1} << kCchipTimerBit) - 1;

// Build-log sanity: the bit position should fall in a plausible
// range for any profile clock we care about (300 MHz -- 2 GHz).
// ES45 1 GHz lands at bit 20; ES40 600 MHz lands at bit 19.  Triggers
// if a future profile clock falls outside this window so the build
// fails loudly rather than emitting timer ticks at a clearly-wrong
// cadence.
static_assert(kCchipTimerBit >= 18 && kCchipTimerBit <= 22,
              "Tsunami21272 IntervalTimer: bit position out of "
              "expected range; check EMULATR_PROFILE_ALPHA_CLOCK_HZ");

} // namespace Spec
} // namespace Tsunami21272

#endif // CHIPSETLIB_TSUNAMI21272_CSR_SPEC_H
