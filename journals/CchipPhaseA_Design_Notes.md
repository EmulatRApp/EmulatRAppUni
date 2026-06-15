<!--
============================================================================
CchipPhaseA_Design_Notes.md -- Cchip Interval Timer + Uniform CSR Surface
============================================================================
Project: EmulatR -- Alpha AXP / EV6 Architecture Emulator (V4)
Copyright (C) 2025, 2026 eNVy Systems, Inc.  All rights reserved.
Licensed under eNVy Systems Non-Commercial License v1.1

Project Architect: Timothy Peer
AI Collaboration:  Claude (Anthropic)

Commercial use prohibited without separate license.
Contact:        peert@envysys.com  |  https://envysys.com
Documentation:  https://timothypeer.github.io/ASA-EMulatR-Project/
============================================================================
-->

# Cchip Interval Timer + Uniform CSR Surface -- Phase A Design Notes

**Status:** APPROVED 2026-05-14 with the resolutions captured in the
"Resolutions" section below.  Three-phase plan agreed with Tim this
session; Phase A defines specification and scaffolding with no
behavior change, Phase B applies the spec uniformly to the .h
implementation, Phase C layers the interval-timer behavior on top.
This document is the Phase A deliverable now cleared to land.

**Predecessor context:** the 2026-05-13 idle-wait-interrupt hypothesis
landed (one-shot synthetic INTERRUPT injection moved firmware
forward; sys__cbox is an idle wait for the Tsunami Cchip interval
timer on b_irq<2>).  The 2026-05-14 timer-interval design settled
(fixed cycle interval, profile-derived, host-independent,
deterministic).  Open tasks #27-#30 in the handoff at
`D:\EmulatR\traces\session_handoff_20260514.md`; this document
absorbs and broadens #28 in light of Tim's "the chipset surface is
woefully incomplete" reframing.

**Reframing this session:** the question Tim asked -- model TIGbus
explicitly, or stay collapsed -- resolved by HRM 6.3 reading.  TIGbus
is a Cchip-internal block, not a peer; it polls 64 device-IRQ lines
into DRIR on the input side and drives b_irq<3:0> through a board-
side register on the output side.  V4 collapses both ends because
the polling cycle and serial-out cycle are not guest-visible.  The
interval timer's source pin `i_intim_l` enters the Cchip outside
TIGbus and is what the new IntervalTimer module emulates; b_irq<2>
delivery is collapsed to a direct cpu-field poke matching the
already-validated synthetic-injection recipe.

The expansion Tim proposed -- and which this document specs -- is to
land a uniform CSR surface against the HRM contract before adding
the timer behavior, rather than bolt the timer into the current
gappy surface.  Pays back the second register-specific behavior we
add (TTR/TDR/STR/MISC bitfields/IIC ignore-count), is continuous
with V4's diagnostic-rich house style (BreakpointSink, retire-compact
trace), and keeps Phase C small and clean.

---

## Section 1: Diagnostic Guard Macro

**Goal:** every CSR access narrates itself uniformly when diagnostics
are on; zero overhead when off.

**Macro:** `EMULATR_CHIPSET_DIAG`, compile-time on/off, controlled by
CMake.  Defaults ON for `relwithdebinfo` and `debug`, OFF for
`release`.  Overridable from the command line via
`-DEMULATR_CHIPSET_DIAG=ON|OFF`.

**Code shape:** the diagnostic emit expands to a thin function call
when on, and to an empty inline when off, so the compiler discards
both the call site and the formatted-string argument computations
at the call sites.  Verify with `objdump -d` on a release build of
one Cchip TU once Phase A lands.

```cpp
// In a new header chipsetLib/CsrDiag.h:

#ifndef CHIPSETLIB_CSR_DIAG_H
#define CHIPSETLIB_CSR_DIAG_H

#include <cstdint>
#include <cstdio>

namespace chipsetLib {

#ifdef EMULATR_CHIPSET_DIAG

// One canonical sink for CSR access events.  Real implementation in
// CsrDiag.cpp; this header carries the inline wrappers and the
// runtime-mute global.  Wrappers take their string args by const
// char* so the compiler can place them in .rodata and the call site
// is a single load + jmp when on.
void csrLogAccess(char const* chipName,
                  char const* regName,
                  bool        isWrite,
                  uint64_t    rawValue,
                  uint64_t    offset,
                  int         cpuId,
                  uint64_t    cycleCount) noexcept;

// Runtime mute -- set true at startup from env var
// EMULATR_CHIPSET_DIAG_OFF=1 or CLI flag; consulted by csrLogAccess
// so the macro-on build can still silence output without rebuild.
extern bool g_csrDiagMuted;

#define CSR_LOG_R(chip, name, val, off, cpu, cyc) \
    ::chipsetLib::csrLogAccess(chip, name, false, (val), (off), (cpu), (cyc))
#define CSR_LOG_W(chip, name, val, off, cpu, cyc) \
    ::chipsetLib::csrLogAccess(chip, name, true,  (val), (off), (cpu), (cyc))

#else // EMULATR_CHIPSET_DIAG

#define CSR_LOG_R(chip, name, val, off, cpu, cyc) ((void)0)
#define CSR_LOG_W(chip, name, val, off, cpu, cyc) ((void)0)

#endif // EMULATR_CHIPSET_DIAG

} // namespace chipsetLib

#endif // CHIPSETLIB_CSR_DIAG_H
```

**CMake side:** in the root `CMakeLists.txt`, after the existing
build-type detection block:

```cmake
option(EMULATR_CHIPSET_DIAG
       "Enable per-CSR-access diagnostic logging in the chipset"
       $<IF:$<CONFIG:Release>,OFF,ON>)

if(EMULATR_CHIPSET_DIAG)
    target_compile_definitions(Emulatr      PRIVATE EMULATR_CHIPSET_DIAG=1)
    target_compile_definitions(Emulatr_tests PRIVATE EMULATR_CHIPSET_DIAG=1)
endif()
```

**Runtime mute layer:** when the macro is on, the sink consults
`g_csrDiagMuted` (a single relaxed-load bool in the hot path); when
muted, the call returns immediately with no fprintf.  Set from
`main()` based on either an env var (`getenv("EMULATR_CHIPSET_DIAG_OFF")`)
or a CLI flag added to AppOptions.  Both supported; CLI wins if
both present.  This layer is the "tuning knob" we reach for when
chasing one register-specific thing and don't want the rest of the
chipset narrating.

**Two-level discipline:** macro is the on/off (zero cost in release);
runtime mute is the volume (live tunable when on).  Same pattern can
be reused for future diagnostic streams (Pchip CSR, MMU walker, etc.).

---

## Section 2: CSR Specification Header

**File:** `chipsetLib/Tsunami21272_CsrSpec.h`, companion to the
existing `Tsunami21272_RegisterMap.h` (which stays the offset map).
Spec describes per-register **contract** (bits, R/W/W1C/RO flags,
reset value, storage class); RegisterMap describes per-register
**address** (the MMIO offset constants).

**What goes in the spec for each register:**

- A typed field struct enumerating bit positions, widths, and
  semantics (R, W, W1C, RO, RW).  One field constant per HRM-named
  subfield; magic numbers do not appear at call sites.
- The reset value.  Drawn from HRM Chapter 10 tables and the chip
  revision/variant (Tsunami DREV=0x10, Typhoon DREV=0x20).
- The storage class: plain uint64_t (RO-after-init), atomic uint64_t
  (shared mutable or per-CPU), or computed-on-read (no storage).
- The atomic-memory-ordering policy: relaxed for the common case
  (single-emulated-CPU semantics today), seq-cst for cross-CPU
  visibility points that the HRM specifies.

**Example walk-through, Cchip MISC.**  HRM Table 10-19 gives the
field layout; the spec encodes it as:

```cpp
namespace Tsunami21272 { namespace CchipMiscSpec {

// HRM 10.2.2.3 -- MISC bit fields.
// Each constant is { lsb, width, semantics, reset }.
// Magic numbers belong here, never at call sites.

struct FieldSpec {
    uint8_t  lsb;
    uint8_t  width;
    char const* semantics;   // "R", "W1C", "RW", "RO"
    uint64_t reset;          // initial value, masked to width
};

inline constexpr FieldSpec REV     {32, 8, "RO",   0x10}; // chip rev
inline constexpr FieldSpec NXM_SRC {44, 4, "RO",   0x00}; // who hit NXM
inline constexpr FieldSpec NXM     {28, 1, "W1C",  0x00}; // NXM seen
inline constexpr FieldSpec ABT     {27, 1, "W1C",  0x00}; // abort
inline constexpr FieldSpec ABW     {26, 1, "W1C",  0x00}; // abort window
inline constexpr FieldSpec IPREQ   {12, 4, "W",    0x00}; // IPI request
inline constexpr FieldSpec IPINTR  { 8, 4, "W1C",  0x00}; // IPI pending
inline constexpr FieldSpec ITINTR  { 4, 4, "W1C",  0x00}; // timer pending
inline constexpr FieldSpec DEVSUP  {24, 1, "RW",   0x00}; // dev-IRQ suppress
inline constexpr FieldSpec CPUID   { 0, 2, "RO",   0x00}; // reader's CPUID

// Typed accessors derived from FieldSpec, generated either by hand
// or by a small constexpr helper.  See section 4 sketch.

} } // namespace
```

**Storage class decisions for Cchip, derived from HRM:**

- RO-after-init (plain uint64_t): CSC, MTR, MPD, AAR0-3, MPR0-3,
  PRBEN (settable once at boot then conventionally not touched).
  Set in `reset()`, never atomically.
- Shared mutable atomic: DRIR (devices + CPUs both touch), MISC's
  shared bits (NXM, ABT, ABW, DEVSUP, IPREQ when used as a CPU
  selector).  Memory order release on writer, acquire/relaxed on
  reader.
- Per-CPU atomic: DIM0..3, IIC0..3, MPR0..3 already, plus MISC's
  per-CPU bit slices (ITINTR<4+n>, IPINTR<8+n>).  The MISC slices
  are the tricky case -- the whole register is one address but the
  semantics are per-CPU within fields.  Two implementation options
  for review:
    1. Single atomic uint64_t with field-aware compare-and-swap on
       W1C writes; read returns the full word and the call site
       extracts its own CPU's bits.
    2. Split into a per-CPU std::atomic<uint8_t> array for
       ITINTR/IPINTR and a single atomic for the shared bits, then
       compose on `read(MISC)`.

  Option (1) matches silicon (MISC is one register at one address);
  option (2) is simpler to reason about but adds a compose step on
  every MISC read.  Recommendation: option (1), with a CAS loop on
  the W1C path; the CAS rarely contends because there is only one
  emulated CPU touching its own bits at a time.

**Dchip:** spec covers DSC (RO), STR (RW), DREV (RO), DSC2 (RO).
All four are platform-shared, no per-CPU semantics.  All currently
fall through to UNKNOWN on access -- Phase B fixes that.

**Pchip:** existing PCTL/PERROR/PERRMASK/WSBA/WSM/TBA already have
decoded paths; the spec formalizes their reset values and W1C bits
(PERROR is W1C).  Less surface than the Cchip work.

**Spec content for Phase A is structural only.**  The actual field-
by-field bit-position table is filled in **as a Phase A deliverable**
from HRM Chapter 10 tables 10-9 through 10-30 (Cchip), 10-31 through
10-34 (Dchip), Chapter 11 (Pchip).  No behavior change yet; the spec
sits unused until Phase B consumes it.

---

## Section 3: IntervalTimer Module

**Cycle interval:** the algorithm settled in the prior message --
`kCchipIntervalTimerCycles = profileAlphaClockHz / 1024` -- becomes
the input to a constexpr rounding step that yields a single-bit
mask, so the per-cycle hot path is one AND and one compare:

```cpp
// In chipsetLib/CchipIntervalTimer.h (Phase C; spec only in Phase A).

constexpr int roundLog2Nearest(uint64_t n) noexcept {
    int bit = 0;
    uint64_t v = 1;
    while (v < n) { ++bit; v <<= 1; }
    // v is now smallest power of two >= n; round to nearest in log space.
    if (bit > 0 && (n - (v >> 1)) < (v - n)) --bit;
    return bit;
}

// Profile constant -- ES40 = 600 MHz, ES45 = 1 GHz.
constexpr uint64_t kProfileAlphaClockHz = 600'000'000ULL;

// kCchipIntervalTimerCycles is the **target** interval; the
// effective interval is the nearest power of two to it.
constexpr uint64_t kCchipIntervalTimerCycles = kProfileAlphaClockHz / 1024;

constexpr int      kCchipTimerBit  = roundLog2Nearest(kCchipIntervalTimerCycles);
constexpr uint64_t kCchipTimerMask = (uint64_t{1} << kCchipTimerBit) - 1;

// Static-asserts to make the rate visible in the build log if we
// ever change the profile clock.
static_assert(kCchipTimerBit >= 18 && kCchipTimerBit <= 22,
              "interval-timer bit out of expected range for known profiles");
```

**Concrete numbers:**

- ES40 (600 MHz): target 585,937 cycles/tick, rounded to 2^19 =
  524,288 cycles/tick.  Effective rate 600M / 524288 = ~1144 Hz.
  Mask-test fire condition: low 19 bits of cycleCount all zero.
- ES45 (1 GHz): target 976,562 cycles/tick, rounded to 2^20 =
  1,048,576 cycles/tick.  Effective rate ~954 Hz.  Mask-test fire
  condition: low 20 bits of cycleCount all zero.

Both within ~12% of the 1024 Hz HRM target; firmware tolerates the
deviation because the idle-wait loop is rate-agnostic.

**Tick predicate:**

```cpp
// Called once per retire from Machine::run, after step(), before
// the existing synthetic-injection block.  When the predicate
// fires, the IntervalTimer asserts MISC<ITINTR> for all CPUs and
// the chipset's in-Cchip b_irq<2> latch -- Machine then polls
// chipset().cchip().pendingIrq2(cpuId) and runs the divert recipe.
// Gate on cycleCount != 0 (and palBase != 0 in the divert step)
// to suppress the boot-time edge case.

inline bool intervalTimerShouldFire(uint64_t cycleCount) noexcept {
    return cycleCount != 0 && (cycleCount & kCchipTimerMask) == 0;
}
```

**No internal counter state.**  The cycle counter IS the counter; the
IntervalTimer module has no member variables for tracking time.  Its
only mutable state is the MISC<ITINTR> bits in the Cchip and the
IIC*n* ignore-count fields.

**IIC ignore-count semantics:** HRM 6.3.2 says "software can suppress
interval timer interrupts for n cycles by writing n into IIC*n*."
Phase C interprets this as: on each fire, before asserting MISC<ITINTR>
for CPU *n*, check `iic[n].fetch_sub(1, relaxed)`; if non-zero before
the decrement, skip the assert for this CPU.  Atomic-fetch-sub keeps
the path lock-free.  The IIC*n* register has additional fields per
the HRM (mode bits, etc.); the count semantics live in the low bits,
spec to be filled in from HRM Table 10-30.

**Resume-from-predig behavior:** predig snapshot at cycle 178M is
mid-interval (cycleCount low 19 bits at that point are 0xC2B80,
nonzero), so resume does not phantom-fire.  Next aligned tick
fires at the next 2^N boundary, ~258K cycles into the resumed run
on ES40.  This is the correct behavior -- snapshots preserve the
counter alignment.

**Tests for Phase A (spec only):**

- `static_assert` chain confirming the rounded bit position matches
  the profile clock for ES40 and ES45.
- A small doctest exercising `roundLog2Nearest` against a table of
  known values.  Doctest convention: `CHECK` only, never `REQUIRE`
  (exceptions disabled in V4 build).

---

## Section 4: Cchip Read/Write Switch Refactor Sketch

**Goal of Phase B (sketched here so Phase A spec exposes the right
seams):** replace the bare UNKNOWN-handler fall-through with a
spec-driven decoder, so every HRM-defined offset gets explicit
storage + bitfield-aware R/W behavior + diagnostics.

**Shape:**

```cpp
// chipsetLib/TsunamiCchip.h (Phase B refactor):

inline uint64_t TsunamiCchip::read(uint64_t offset, int cpuId) const noexcept
{
    using namespace Tsunami21272;
    using namespace Tsunami21272::CchipMiscSpec;

    switch (offset) {
    case Cchip::CSC:  CSR_LOG_R("Cchip", "CSC",  m_csc, offset, cpuId, m_cycleCount);
                      return m_csc;
    case Cchip::MISC: {
        // MISC read is special: it injects the reader's CPUID into
        // bits [1:0] per HRM 10.2.2.3.  Atomic load of the
        // shared/per-CPU subfields, then compose with CPUID.
        uint64_t const raw   = m_misc.load(std::memory_order_acquire);
        uint64_t const value = (raw & ~uint64_t{0x3}) | (cpuId & 0x3);
        CSR_LOG_R("Cchip", "MISC", value, offset, cpuId, m_cycleCount);
        return value;
    }
    case Cchip::TTR:  CSR_LOG_R("Cchip", "TTR",  m_ttr, offset, cpuId, m_cycleCount);
                      return m_ttr;
    case Cchip::TDR:  CSR_LOG_R("Cchip", "TDR",  m_tdr, offset, cpuId, m_cycleCount);
                      return m_tdr;
    // ... rest of the spec-covered registers ...
    default:
        CSR_LOG_R("Cchip", "UNKNOWN", 0, offset, cpuId, m_cycleCount);
        return 0;
    }
}

inline void TsunamiCchip::write(uint64_t offset, uint64_t value, int cpuId) noexcept
{
    using namespace Tsunami21272;

    switch (offset) {
    case Cchip::MISC: {
        // W1C bits per CchipMiscSpec: NXM, ABT, ABW, ITINTR<4+n>, IPINTR<8+n>.
        // CAS loop preserves bits not in the W1C set; clears bits the
        // write set in the W1C set.
        miscWriteW1C(value, cpuId);
        CSR_LOG_W("Cchip", "MISC", value, offset, cpuId, m_cycleCount);
        break;
    }
    case Cchip::TTR:  m_ttr = value;
                      CSR_LOG_W("Cchip", "TTR", value, offset, cpuId, m_cycleCount);
                      break;
    case Cchip::TDR:  m_tdr = value;
                      CSR_LOG_W("Cchip", "TDR", value, offset, cpuId, m_cycleCount);
                      break;
    // ... rest of the spec-covered registers ...
    default:
        CSR_LOG_W("Cchip", "UNKNOWN", value, offset, cpuId, m_cycleCount);
        break;
    }
}
```

**MISC W1C helper -- Phase B body:**

```cpp
inline void TsunamiCchip::miscWriteW1C(uint64_t writeVal, int cpuId) noexcept
{
    // W1C mask -- bits the write is allowed to clear.  Composed from
    // CchipMiscSpec field semantics.
    constexpr uint64_t kMiscW1CMask =
        (uint64_t{0xF}  <<  4) |   // ITINTR<7:4>
        (uint64_t{0xF}  <<  8) |   // IPINTR<11:8>
        (uint64_t{0x1}  << 26) |   // ABW
        (uint64_t{0x1}  << 27) |   // ABT
        (uint64_t{0x1}  << 28);    // NXM

    constexpr uint64_t kMiscRWMask =
        (uint64_t{0xF}  << 12) |   // IPREQ -- writeable, sets pending IPIs
        (uint64_t{0x1}  << 24);    // DEVSUP

    // The IPREQ field is the W-side of the IPI mechanism; writing
    // bits here is interpreted as "request an IPI to those CPUs".
    // Phase C wires that to the b_irq<3> assertion path.

    // CAS loop -- preserve all bits not in W1C set; clear W1C bits
    // that the writer marked; apply RW bits verbatim.
    uint64_t old = m_misc.load(std::memory_order_acquire);
    for (;;) {
        uint64_t const clearedBits = old & ~(writeVal & kMiscW1CMask);
        uint64_t const rwBits      = (clearedBits & ~kMiscRWMask) |
                                     (writeVal & kMiscRWMask);
        if (m_misc.compare_exchange_weak(old, rwBits,
                                         std::memory_order_acq_rel,
                                         std::memory_order_acquire)) {
            break;
        }
    }

    // Side effects -- Phase C wires these.  Phase B keeps the storage
    // pure and leaves the side-effect hooks empty.
    //   - If ITINTR<4+cpuId> was cleared, deassert b_irq<2> for cpuId.
    //   - If IPINTR<8+cpuId> was cleared, deassert b_irq<3> for cpuId.
    //   - If IPREQ field bits were set, assert b_irq<3> for those CPUs
    //     and set the corresponding IPINTR bits.
}
```

**TigController inner class -- open for Phase B decision.**  Two
options, defer to Phase B body when shape is more visible:

1. Flat: `m_drir` stays a direct member of `TsunamiCchip`, no inner
   class.  Matches what V4 has today.
2. Nested: `TsunamiCchip::TigController m_tig` owns the 64-line
   input shadow (= `m_drir`) and the polling-result aggregation
   logic.  Future flash ROM access or MISC<DEVSUP> one-poll
   suppression slots in cleanly.

Recommendation: defer.  Phase A doesn't need to decide.  When Phase B
fills in the read/write switch and we see how many register
accessors touch DRIR vs other Cchip state, the right shape becomes
obvious.  Either choice is a refactor away from the other.

---

## Phase A Concrete Deliverables

What Phase A actually ships:

1. New file `chipsetLib/CsrDiag.h` -- the diagnostic-guard macro
   and the inline wrappers.
2. New file `chipsetLib/CsrDiag.cpp` -- the `csrLogAccess` body
   and the `g_csrDiagMuted` global.
3. New file `chipsetLib/Tsunami21272_CsrSpec.h` -- bit-position
   FieldSpec tables for every register of every chipset variant
   we support: Cchip (HRM 10.2.2.x), Dchip (HRM 10.2.3.x), and
   both Pchip0 and Pchip1 (HRM Chapter 11), for both Tsunami and
   Typhoon variant differences.  Per resolution (4) the spec is
   complete per variant; behavior depth follows the unwired-TODO
   discipline in Phase B.
4. CMake `option(EMULATR_CHIPSET_DIAG ...)` block + per-target
   `target_compile_definitions`.
5. Doctest exercises in `tests/chipsetLib/test_csr_spec.cpp`:
   reset-value consistency, FieldSpec sanity (lsb + width <= 64,
   etc.), `roundLog2Nearest` against a known table.

No behavior change.  All existing tests still pass.  All existing
TsunamiCchip read/write switch entries unchanged.  The spec sits
unused until Phase B consumes it.

Estimated effort: one focused session for the spec extraction from
HRM tables, ~half a session for the diagnostic-guard infrastructure
and CMake plumbing.  Test additions are mechanical.

---

## Resolutions (2026-05-14)

All five open questions resolved this session.  Phase A is cleared
to land per these decisions.

**1. MISC storage shape -- RESOLVED: option (1).**  Single atomic
uint64_t with a CAS loop on the W1C path.  Rationale: matches the
silicon (MISC is one register at one address); CAS rarely contends
because there is only one emulated CPU touching its own bits at a
time.  The CAS-loop sketch in Section 4 is the reference shape.

**2. Profile clock Hz constant -- RESOLVED: approach (a).**  One
constexpr per build today.  ES45 joins later by adding a CMake
define that selects between profile constexprs.  Section 3's
`kProfileAlphaClockHz` keeps the comment noting the approach-(b)
escape hatch (runtime field on Machine/TsunamiChipset) for the day
multi-profile-in-one-binary becomes load-bearing.

**3. TigController nested vs flat -- RESOLVED: defer to Phase B.**
Phase A does not decide.  When Phase B fills in the read/write
switch and the DRIR-vs-other-Cchip-state ratio becomes visible, the
right shape settles by inspection.  Either choice is a refactor
away from the other.

**4. HRM coverage scope -- RESOLVED: complete per variant, with the
unwired-TODO discipline.**  Phase A specifies storage and surface
for *every* HRM-defined register of every chipset variant we
support (Cchip, Dchip, Pchip0, Pchip1 on Tsunami and Typhoon).
Registers whose behavior is needed today for chipset interface
compliance and execution get full RW/W1C/RO behavior.  Registers
not yet exercised by firmware get storage + spec entry + a `TODO`
header note and a `// TODO(unwired): ...` line comment at the
read/write decoder site explaining what wiring is missing.  This
gives us a complete surface against the HRM contract with a
machine-greppable map of where behavior is shallow, so a future
session can pull a TODO forward when firmware first touches it.

**5. `read()` signature -- RESOLVED: overload (a).**  Keep
`read(offset)` for trace/diagnostic callers that have no CPU
context, and add `read(offset, cpuId)` for the MMIO dispatch path
that does.  Non-invasive to existing call sites; the diagnostic
sink stays simple.  The overload delegates to the cpuId-aware form
with `cpuId = -1` (treated as "no CPU context" by registers like
MISC whose read result depends on CPUID).

---

## Unwired-TODO Discipline (Tim's house rule, Phase B onward)

The decision in resolution (4) becomes a discipline that governs
every register we touch in Phase B and beyond.  The rule has two
parts -- one in the header, one at the call site -- mirroring the
project's existing header+line documentation rule.

**At the header level.**  Each chipset header (`TsunamiCchip.h`,
`TsunamiDchip.h`, `TsunamiPchip.h`) gets a "TODO Register Table"
block in the file header listing every register the file owns
whose behavior is not yet fully wired.  Format:

```
// ============================================================================
// TODO Register Table -- unwired or partially-wired behavior
// ============================================================================
// MTR    -- storage RW; no SDRAM timing model.  Wire when memory
//           training becomes load-bearing.
// TTR    -- storage RW; no TIGbus timing effect.  Wire when flash
//           ROM access goes live.
// IIC0-3 -- ignore-count decrement Phase C; Phase B leaves storage
//           pass-through.
// ...
// ============================================================================
```

When Phase C (or any later session) wires a TODO entry, the entry
is removed from the table in the same edit, and the file-header
change-history block records the removal with a date and rationale.

**At the line level.**  Each unwired register's read/write decoder
site carries a single-line comment in the canonical form
`// TODO(unwired): <one-line summary>` pointing back at the
header-block entry.  Greppable: `grep -rn "TODO(unwired)" chipsetLib/`
gives the whole map.  When the wiring lands, the line comment is
removed in the same edit that removes the header table entry.

**Why this matters:**  the alternative -- letting unwired registers
fall to the UNKNOWN handler at the bottom of the switch -- hides
which registers exist in the HRM but lack model behavior, and
mixes "register not defined" with "register defined but unwired"
in the same stderr noise stream.  The TODO discipline separates
those two failure modes cleanly.

**Workflow integration:**  every edit that touches a chipset
register file updates the relevant TODO table entry (or removes
it).  This is the line-level half of the existing house rule from
`feedback_workflow_rules.md` ("documentation at both header and
source line"), specialized for the chipset surface.

---

## Cross-References

- Handoff: `D:\EmulatR\traces\session_handoff_20260514.md`
- Idle-wait hypothesis memory note:
  `project_idle_wait_interrupt_hypothesis.md`
- Cchip implementation: `chipsetLib/TsunamiCchip.h`
- Cchip register-offset map: `chipsetLib/Tsunami21272_RegisterMap.h`
- One-shot injection (recipe Phase C reuses): `Machine.cpp:530-591`
- AXPBox behavior reference: `D:\EmulatR\axpbox` (clean-room only)
- HRM (Tsunami/Typhoon 21272): EC-RE2CA-TE Rev 4.0, 21 Oct 1999

