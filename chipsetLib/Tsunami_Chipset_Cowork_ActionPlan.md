# Tsunami Chipset Build-Up — Cowork Action Plan

**Sibling to:** `Tsunami_Chipset_Architecture.md`
**Executor:** Claude in Cowork (file edits, builds, test runs)
**Architect/reviewer:** Timothy Peer (human, consulted at every gate)
**Goal:** Land Phases 2–6 of the architecture doc so SRM boots through memory sizing and PCI enumeration without spinning.

---

## How to read this plan

Twelve tickets, executed in order. Each ticket has:

- **Scope** — one sentence on what it does.
- **Read first** — files Cowork must view before editing.
- **Deliverables** — concrete edits with file paths and function names.
- **Tests** — what to write, in `chipset_tests/<phase>_*.cpp`.
- **Gate** — the bright line that says "done." Do not start the next ticket until the gate is green.
- **Stop and ask** — points where strategy decisions go back to the architect.

**Cowork must not skip gates.** If a gate fails twice in a row, stop, write a status note, and surface the failure to the architect with the failing test output and the last-known-good state.

---

## Ticket 0 — Build & test infrastructure

### Scope
Confirm the project has a working build and a unit-test harness. If not, propose one and stop for approval before installing it.

### Read first
- `CMakeLists.txt` (or whatever build file is at the repo root)
- Any existing `tests/`, `test/`, or `*_test.cpp` files
- The project README if present

### Deliverables
1. Identify the build system. Report back the answer to: "Does `make` (or `cmake --build .`) succeed from a clean state right now?"
2. Search the tree for an existing test framework. Look for `Catch2`, `gtest`, `doctest`, `boost::test`, or any `add_test(...)` lines in CMake.
3. If **no** framework is present: stop and ask the architect which to install. **Recommended default:** Catch2 v3 (header-only, single include, fits in `third_party/` cleanly).
4. If a framework **is** present: add a new test target `chipset_tests` that compiles a single file `chipset_tests/sanity.cpp` containing one trivial passing assertion. Wire it into the build.
5. **Add five test fixtures** under `chipset_tests/fixtures/`. These are tiny, declared once, used across every later ticket — landing them now prevents per-ticket reinvention:
   - `FakePciDevice.h` — implements `IPciDeviceHandler` with a `std::map<uint8_t, uint32_t>` of programmable config registers and a vector that records every config access. Also a `wasReset` boolean for SPRST testing.
   - `FakeIoPortDevice.h` — implements the I/O port handler interface (whatever shape the ISA bridge eventually defines; stub it now as `read(port, width) -> value` / `write(port, value, width)`).
   - `FakeCpu.h` — minimal stand-in with a `cpuId`, an `ipl`, a `pending_b_irq` bitmask, and a reservation state (`lock_flag`, `lock_address`). Used from Ticket 8 onward but lands now so its API is stable.
   - `DeterministicMemory.h` — flat `std::vector<uint8_t>` with `read(pa, len)` / `write(pa, len)`. Used by DMA and reservation tests.
   - `CycleInjector.h` — a function `runCycles(chipset, N)` that calls `chipset.step()` deterministically. Used from Ticket 4 onward.
   Each fixture gets one sanity test in `chipset_tests/sanity_fixtures.cpp` to prove it compiles and the API does what its header claims.

### Tests
```cpp
// chipset_tests/sanity.cpp
#include <catch2/catch_test_macros.hpp>
TEST_CASE("sanity") { REQUIRE(1 + 1 == 2); }

// chipset_tests/sanity_fixtures.cpp
TEST_CASE("FakePciDevice records config reads") {
    FakePciDevice dev;
    dev.configRegs[0x00] = 0xC6931080;
    REQUIRE(dev.pciConfigRead(0x00, 4) == 0xC6931080);
    REQUIRE(dev.configReads.size() == 1);
}
TEST_CASE("DeterministicMemory round-trips a quadword") {
    DeterministicMemory mem(1 << 20);
    uint64_t v = 0xDEADBEEFCAFEBABEULL;
    mem.write(0x1000, &v, 8);
    uint64_t r = 0;
    mem.read(0x1000, &r, 8);
    REQUIRE(r == v);
}
```

### Gate
- `cmake --build . --target chipset_tests` succeeds.
- Running the test binary prints "All tests passed" (or equivalent).
- The full project still builds.

### Stop and ask
- If the project doesn't build in its current state, stop. The architect needs to know before we layer more changes on top.
- If the test framework choice isn't obvious, stop. We do not pick frameworks autonomously.

---

## Ticket 1 — Wire the orchestrator

### Scope
Move `TsunamiChipset` from "owns three members" to "dispatches MMIO, drives the tick, carries cross-chip signals." This is plumbing — no register semantics change yet.

### Read first
- `TsunamiChipset.cpp` (currently ~20 lines, just a constructor)
- `TsunamiChipset.h` (need to see what's already declared)
- `Tsunami21272_RegisterMap.h` — specifically the `MMIOOffset` namespace at the bottom and the `SparseSpace` helpers
- `TsunamiCchip.h::mmioRead/mmioWrite` and `TsunamiDchip.h::mmioRead/mmioWrite` to see the existing per-realm handler shape

### Deliverables

**1. Add to `TsunamiChipset.h`:**

```cpp
class TsunamiChipset {
public:
    // ... existing constructor ...

    // MMIO dispatch — single entry from the CPU's address translation layer.
    // cpuId is needed for MISC reads (returns per-CPU CPU ID in bits <1:0>);
    // pass 0 for non-SMP test paths.
    uint64_t mmioRead(uint64_t pa, uint8_t width, int cpuId = 0) noexcept;
    void     mmioWrite(uint64_t pa, uint64_t value, uint8_t width, int cpuId = 0) noexcept;

    // Tick — called once per scheduler quantum
    void step(uint64_t cycles) noexcept;

    // Cross-chip wires (called by realms, not by CPU)
    void raisePciInterrupt(int pchipId, int intxLine) noexcept;
    void reportNxm(uint64_t pa, int sourceCode) noexcept;

    // Accessors for the realms themselves
    TsunamiCchip& cchip() noexcept { return m_cchip; }
    TsunamiDchip& dchip() noexcept { return m_dchip; }
    TsunamiPchip& pchip() noexcept { return m_pchip; }

private:
    // ... existing members ...
};
```

**2. Implement in `TsunamiChipset.cpp`:**

```cpp
uint64_t TsunamiChipset::mmioRead(uint64_t pa, uint8_t width) noexcept {
    using namespace Tsunami21272::MMIOOffset;
    using namespace Tsunami21272::Base;
    const uint64_t off = pa - kMMIO_Start;

    if (off >= kCchip_CSR  && off < kCchip_CSR_End)
        return m_cchip.read(off - kCchip_CSR);
    if (off >= kDchip_CSR  && off < kDchip_CSR_End)
        return m_dchip.read(off - kDchip_CSR);
    if (off >= kPchip0_CSR && off < kPchip0_CSR_End)
        return m_pchip.read(off - kPchip0_CSR);   // adjust to actual Pchip read API
    // PCI config, IACK, sparse mem/IO follow the same pattern — fill in
    // per the dispatch table at the bottom of Tsunami21272_RegisterMap.h.

    SPDLOG_WARN("TsunamiChipset::mmioRead unhandled PA=0x{:x}", pa);
    return 0;
}
```

Match `mmioWrite` similarly. Use the dispatch table comment block at the bottom of the register map header as the source of truth for ranges.

**3. Implement `step()`:**

```cpp
void TsunamiChipset::step(uint64_t cycles) noexcept {
    // Phase 4 will add real timer logic to Cchip — for now this is a stub
    // that just calls into a no-op hook so the wiring is in place.
    m_cchip.tickIntervalTimer(cycles);   // add this method to Cchip in Ticket 3
}
```

**4. Stub the cross-chip wires:**

```cpp
void TsunamiChipset::raisePciInterrupt(int pchipId, int intxLine) noexcept {
    // Real DRIR-bit mapping lands in Ticket 3. For now:
    (void)pchipId; (void)intxLine;
}
void TsunamiChipset::reportNxm(uint64_t pa, int sourceCode) noexcept {
    (void)pa; (void)sourceCode;
}
```

### Tests
```cpp
// chipset_tests/ticket01_dispatch.cpp
TEST_CASE("MMIO read at Cchip CSC offset reaches Cchip") {
    TsunamiChipset cs(ChipsetVariant::Tsunami, 1, 1ULL << 30);
    // CSC reset value has CPU0-present bit set
    uint64_t v = cs.mmioRead(0x801A0000000ULL + Tsunami21272::Cchip::CSC, 8);
    REQUIRE((v & 0x1) == 0x1);
}

TEST_CASE("MMIO read at Dchip DREV offset reaches Dchip") {
    TsunamiChipset cs(ChipsetVariant::Tsunami, 1, 1ULL << 30);
    uint64_t v = cs.mmioRead(0x801B0000000ULL + Tsunami21272::Dchip::DREV, 8);
    REQUIRE(v == 0x10);  // Tsunami DREV
}

TEST_CASE("MMIO read in gap region returns 0 without crashing") {
    TsunamiChipset cs(ChipsetVariant::Tsunami, 1, 1ULL << 30);
    uint64_t v = cs.mmioRead(0x80190000000ULL, 8);  // gap between Pchip0 CSR and Cchip
    REQUIRE(v == 0);
}
```

### Gate
- All `ticket01_*` tests pass.
- All prior `chipset_tests` still pass.
- The full project still builds.

### Stop and ask
- If `TsunamiPchip::read` doesn't exist with that signature, stop. Don't invent an API. Surface the actual Pchip read entry point to the architect for ratification.

---

## Ticket 1.5 — Platform → variant binding (first-class)

### Scope
Make chipset variant a first-class concept: every chip accepts it, every chipset has a derivable platform model, and consistency is enforced at construction. This ticket touches no register semantics — it's purely the binding layer that lets the rest of the codebase address variant cleanly. Lands before Ticket 2 because Ticket 2's tests cover both variants, and they should run against a binding layer that's already correct.

### Read first
- **`TsunamiVariant.h`** — contents inferred from usage; confirm or correct before editing anything. Specifically: is `enum ChipsetVariant { Tsunami, Typhoon, Unknown }` defined here? Does `variantInfo(ChipsetVariant)` return a pointer to a struct, and what are its fields (`drev`, `crev`, `chipName`, others)? **Paste the contents into the ticket close-out** so the architect has it for reference.
- `TsunamiChipset.cpp` line 19–22 — the hardcoded `m_model = variant == Typhoon ? "ES45" : "ES40"` ternary, and the `m_pchip()` construction with no variant
- `TsunamiCchip.h` constructor (line 152) and `reset()` — see how variant is used
- `TsunamiDchip.h` constructor (line 71) and `reset()` — see how variant is used
- `TsunamiPchip.h` constructor — currently takes no variant; that changes here

### Deliverables

**1. Report `TsunamiVariant.h` contents** before any edits. If the inferred contract (enum + `variantInfo()` returning a struct with `drev`/`crev`/`chipName`) matches, say so explicitly. If it deviates, stop and ask before proceeding — the rest of the deliverables assume that contract.

**2. Add a `Platform` concept.** Either extend `TsunamiVariant.h` or create a sibling `Platform.h` — the architect picks. Suggested shape:

```cpp
enum class Platform : uint8_t {
    Unknown = 0,
    DS10,    // Tsunami, 1 CPU
    DS20,    // Tsunami, 2 CPUs
    ES40,    // Tsunami, up to 4 CPUs
    DS25,    // Typhoon, 2 CPUs
    ES45,    // Typhoon, up to 4 CPUs
};

struct PlatformInfo {
    Platform        platform;
    ChipsetVariant  variant;
    const char*     modelName;     // "ES45", "DS10", etc.
    int             maxCpus;
    uint64_t        maxMemoryBytes;
};

const PlatformInfo* platformInfo(Platform) noexcept;
ChipsetVariant      platformToVariant(Platform) noexcept;
```

**Do not invent platforms.** Use only the five listed unless the architect adds more. (DS15, Aurora, Goldrush, Sable — confirm scope before adding.)

**3. Change `TsunamiPchip` to accept variant.** Add it to the constructor signature even if the body doesn't branch on it today. Store as `m_variant`. Expose a `variant()` accessor for symmetry with Cchip and Dchip. The Pchip's existing behavior must be byte-identical after this change — variant is inert here, just present.

**4. Rewrite the `TsunamiChipset` constructor.** Provide a platform-keyed primary constructor and retain a variant-keyed overload for tests and existing call sites:

```cpp
// Primary — platform is the user-facing knob
explicit TsunamiChipset(Platform platform,
                        int cpuCount,
                        uint64_t memSizeBytes) noexcept;

// Direct-variant overload — for tests and legacy call sites.
// Sets m_platform = Platform::Unknown.
explicit TsunamiChipset(ChipsetVariant variant,
                        int cpuCount,
                        uint64_t memSizeBytes) noexcept;
```

The platform constructor derives variant via `platformToVariant(platform)` and sets `m_model` from `platformInfo(platform)->modelName`. **The hardcoded ternary on line 20 of `TsunamiChipset.cpp` is gone after this ticket.**

**5. Add accessors to `TsunamiChipset`:**

```cpp
ChipsetVariant variant()  const noexcept { return m_variant; }
Platform       platform() const noexcept { return m_platform; }
const char*    model()    const noexcept { return m_model; }
```

**6. Add a constructor-time consistency check** after the three sub-chips are constructed:

```cpp
// All three chips must agree with the chipset's variant
if (m_cchip.variant() != m_variant ||
    m_dchip.variant() != m_variant ||
    m_pchip.variant() != m_variant) {
    SPDLOG_CRITICAL("TsunamiChipset: sub-chip variant mismatch");
    std::abort();
}
```

A `static_assert` won't work here (variant is runtime), but a release-build-friendly guard is fine. The architect may prefer a fatal log + abort, or an exception, or just a regular `assert()`. Default to log + abort.

### Tests

```cpp
// chipset_tests/ticket01_5_variant_binding.cpp

TEST_CASE("platformToVariant: Tsunami platforms") {
    REQUIRE(platformToVariant(Platform::DS10) == ChipsetVariant::Tsunami);
    REQUIRE(platformToVariant(Platform::DS20) == ChipsetVariant::Tsunami);
    REQUIRE(platformToVariant(Platform::ES40) == ChipsetVariant::Tsunami);
}

TEST_CASE("platformToVariant: Typhoon platforms") {
    REQUIRE(platformToVariant(Platform::DS25) == ChipsetVariant::Typhoon);
    REQUIRE(platformToVariant(Platform::ES45) == ChipsetVariant::Typhoon);
}

TEST_CASE("platformInfo: ES45 reports correct model and caps") {
    const auto* info = platformInfo(Platform::ES45);
    REQUIRE(info != nullptr);
    REQUIRE(std::string(info->modelName) == "ES45");
    REQUIRE(info->variant == ChipsetVariant::Typhoon);
    REQUIRE(info->maxCpus == 4);
}

TEST_CASE("Chipset from ES45 reports Typhoon variant and ES45 platform") {
    TsunamiChipset cs(Platform::ES45, 4, 8ULL << 30);
    REQUIRE(cs.variant() == ChipsetVariant::Typhoon);
    REQUIRE(cs.platform() == Platform::ES45);
    REQUIRE(std::string(cs.model()) == "ES45");
}

TEST_CASE("Chipset from ES40 reports Tsunami variant and ES40 platform") {
    TsunamiChipset cs(Platform::ES40, 2, 4ULL << 30);
    REQUIRE(cs.variant() == ChipsetVariant::Tsunami);
    REQUIRE(cs.platform() == Platform::ES40);
}

TEST_CASE("All three sub-chips agree on variant") {
    TsunamiChipset cs(Platform::ES45, 4, 8ULL << 30);
    REQUIRE(cs.cchip().variant() == ChipsetVariant::Typhoon);
    REQUIRE(cs.dchip().variant() == ChipsetVariant::Typhoon);
    REQUIRE(cs.pchip().variant() == ChipsetVariant::Typhoon);
}

TEST_CASE("DREV reflects platform-derived variant via MMIO") {
    TsunamiChipset es40(Platform::ES40, 2, 4ULL << 30);
    REQUIRE(es40.mmioRead(0x801B0000000ULL + Tsunami21272::Dchip::DREV, 8) == 0x10);

    TsunamiChipset es45(Platform::ES45, 4, 8ULL << 30);
    REQUIRE(es45.mmioRead(0x801B0000000ULL + Tsunami21272::Dchip::DREV, 8) == 0x20);
}

TEST_CASE("Variant-keyed overload still works (regression)") {
    TsunamiChipset cs(ChipsetVariant::Tsunami, 1, 1ULL << 30);
    REQUIRE(cs.variant() == ChipsetVariant::Tsunami);
    REQUIRE(cs.platform() == Platform::Unknown);
}

TEST_CASE("Ticket 1 dispatch tests still pass with new constructor") {
    // Sanity: variant binding does not alter MMIO dispatch behavior
    TsunamiChipset cs(Platform::ES40, 1, 1ULL << 30);
    uint64_t v = cs.mmioRead(0x801A0000000ULL + Tsunami21272::Cchip::CSC, 8);
    REQUIRE((v & 0x1) == 0x1);
}
```

### Gate
- All `ticket01_5_*` tests pass.
- **All prior tests still pass** (Ticket 0, Ticket 1). This is the regression bar — variant binding must be additive, not mutating.
- The hardcoded `m_model` ternary in `TsunamiChipset.cpp` is gone, replaced by a `platformInfo()` lookup.
- The full project still builds.

### Stop and ask
- After reading `TsunamiVariant.h`: if it has a fundamentally different shape than the inferred contract (no `variantInfo()` function, struct fields differ, enum has more values than expected), **stop and surface the actual contents** before doing anything else. The deliverables above assume the inferred shape.
- The list of five platforms is a starting point. Confirm with the architect which models are in scope before merging the `Platform` enum. Adding platforms later is easy; removing them after callers depend on them is not.
- The architect picks: **extend `TsunamiVariant.h` or create `Platform.h`?** Default recommendation: extend `TsunamiVariant.h` only if it's short and clearly about chip+platform identity; otherwise create `Platform.h` and have it include `TsunamiVariant.h`. Either way, every chip header still includes only `TsunamiVariant.h`.
- The consistency check shape (log + abort vs. exception vs. `assert()`) is an architect preference. Default to log + abort. Do not silently continue with mismatched variants.

---

## Ticket 2 — Phase 2: AAR encoding byte-correctness

### Scope
Fix the `0x1283x` spin. Audit `TsunamiCchip::computeAAR` against HRM Tables 10-14 (Tsunami) and 10-15 (Typhoon). Add the diagnostic trace hook from the existing scaffold.

### Read first
- `TsunamiCchip.h::computeAAR` (lines ~455–486 in current file)
- `TsunamiCchip.h::reset` (the AAR0–3 population loop, ~181–205)
- `Tsunami_Chip_Analysis_Scaffold.md` — sections on AAR layout and the trace hook
- HRM Table 10-14 and 10-15 in `tsunami_typhoon_21272_hrm.txt` (search for "AAR" near line 15400)

### Deliverables

**1. Validate the existing `computeAAR` against the HRM tables.** Specifically check:
- `ADDR` field is bits `[34:24]` of the base PA, shifted to bit position 24 in the register (currently `(baseAddr >> 24) & 0x7FF`).
- `ASIZ[15:12]` encoding matches HRM exactly for both variants. The Tsunami encoding tops out at `0x7 = 1 GB`. The Typhoon encoding adds `0x8 = 2 GB`, `0x9 = 4 GB`, `0xA = 8 GB`.
- `ROWS[3:2]` and `BNKS[1:0]` should reflect the actual SDRAM organization (current code hardcodes `rows=2, bnks=1` — confirm this matches what SRM expects for the modeled DIMM layout, or make it configurable).
- Bit 16 (DBG) is 0 unless explicitly set.

**2. If any bit is misplaced, fix it.** Update with a comment citing the HRM table line number.

**3. Add the trace hook.** In `TsunamiChipset::mmioWrite`, after dispatch to Cchip, log the decoded AAR fields whenever the offset is AAR0–3:

```cpp
if (off >= kCchip_CSR && off < kCchip_CSR_End) {
    const uint64_t cchipOff = off - kCchip_CSR;
    m_cchip.write(cchipOff, value);
    if (cchipOff >= Tsunami21272::Cchip::AAR0 &&
        cchipOff <= Tsunami21272::Cchip::AAR3) {
        const int idx = (cchipOff - Tsunami21272::Cchip::AAR0) / 0x40;
        SPDLOG_INFO("AAR{} write: raw=0x{:016x} ADDR=0x{:x} ASIZ=0x{:x}",
                    idx, value, (value >> 24) & 0x7FF, (value >> 12) & 0xF);
    }
    return;
}
```

**4. SRM normally only *reads* AAR0–3 (they're set by Cchip reset).** But the hook is symmetric: log on every read, too, so you see what the SRM saw, byte-for-byte.

### Tests
```cpp
// chipset_tests/ticket02_aar_encoding.cpp

TEST_CASE("Tsunami 1GB system: AAR0 encodes 1GB at base 0, AAR1-3 disabled") {
    TsunamiCchip c(ChipsetVariant::Tsunami, 1, 1ULL << 30);
    const uint64_t aar0 = c.read(Tsunami21272::Cchip::AAR0);
    REQUIRE(((aar0 >> 12) & 0xF) == 0x7);   // ASIZ = 1GB per HRM 10-14
    REQUIRE(((aar0 >> 24) & 0x7FF) == 0);    // base 0
    REQUIRE(c.read(Tsunami21272::Cchip::AAR1) == 0);  // disabled
    REQUIRE(c.read(Tsunami21272::Cchip::AAR2) == 0);
    REQUIRE(c.read(Tsunami21272::Cchip::AAR3) == 0);
}

TEST_CASE("Tsunami 4GB system: 4×1GB arrays, bases at 0, 1G, 2G, 3G") {
    TsunamiCchip c(ChipsetVariant::Tsunami, 1, 4ULL << 30);
    for (int i = 0; i < 4; ++i) {
        const uint64_t aar = c.read(Tsunami21272::Cchip::AAR0 + i * 0x40);
        REQUIRE(((aar >> 12) & 0xF) == 0x7);
        REQUIRE(((aar >> 24) & 0x7FF) == (uint64_t(i) << 6));  // 1GB step = bit 30
    }
}

TEST_CASE("Typhoon 32GB system: 4×8GB arrays") {
    TsunamiCchip c(ChipsetVariant::Typhoon, 4, 32ULL << 30);
    for (int i = 0; i < 4; ++i) {
        const uint64_t aar = c.read(Tsunami21272::Cchip::AAR0 + i * 0x40);
        REQUIRE(((aar >> 12) & 0xF) == 0xA);  // ASIZ = 8GB per HRM 10-15
    }
}

TEST_CASE("AAR base+size never wraps past max PA") {
    for (uint64_t mem : {1ULL<<30, 2ULL<<30, 4ULL<<30, 8ULL<<30}) {
        TsunamiCchip c(ChipsetVariant::Typhoon, 1, mem);
        for (int i = 0; i < 4; ++i) {
            const uint64_t aar = c.read(Tsunami21272::Cchip::AAR0 + i * 0x40);
            if (aar == 0) continue;
            const uint64_t base = ((aar >> 24) & 0x7FF) << 24;
            const uint64_t asiz = (aar >> 12) & 0xF;
            const uint64_t size = (asiz == 0) ? 0 : (16ULL << 20) << (asiz - 1);
            REQUIRE(base + size <= (1ULL << 35));  // 35-bit PA space
        }
    }
}
```

### Gate
- All `ticket02_*` tests pass.
- All prior tests still pass.
- **Boot SRM and observe:** the R6 sizing loop converges. The `0x1283x` PC value either stops appearing or appears once and moves on. Capture the boot log and attach to the ticket close-out.

### Stop and ask
- If the HRM tables disagree with the existing `computeAAR` in a way that isn't obviously a bit-position mistake (for example, if the table calls out `ROWS` encodings tied to specific DIMM SKUs), stop. We need the architect to pick a DIMM model before we encode it.

---

## Ticket 3 — Phase 3: Interrupt matrix validation

### Scope
The Cchip's interrupt routing logic is already implemented (DRIR → DIR via `DRIR & DIM[n]`). Validate it with tests, wire the cross-chip `raisePciInterrupt` to actually call `assertInterrupt`, and confirm `b_irq<n>` reaches the CPU model.

### Read first
- `TsunamiCchip.h::assertInterrupt / deassertInterrupt / readDIR`
- Whatever the CPU model uses to consume interrupts — search for `b_irq` or `IPL` in the CPU source

### Deliverables

**1. Implement the PCI-to-DRIR mapping table** (HRM §6.3 has the canonical layout, but a common simplification is bits 32–55 of DRIR are for Pchip device interrupts, with Pchip0 occupying [39:32] and Pchip1 [47:40]):

```cpp
// In TsunamiChipset.cpp
void TsunamiChipset::raisePciInterrupt(int pchipId, int intxLine) noexcept {
    // HRM §6.3: device interrupts occupy DRIR[55:32].
    // Pchip0 INTA-D → DRIR[35:32], Pchip1 INTA-D → DRIR[39:36], etc.
    // Confirm exact mapping against HRM before merging.
    const int drirBit = 32 + (pchipId * 4) + (intxLine & 0x3);
    m_cchip.assertInterrupt(drirBit);
}
```

Add a `lowerPciInterrupt` counterpart calling `deassertInterrupt`.

**2. Confirm the CPU model checks `readDIR(cpuId)` against its IPL gate.** This may require no change if it already does; if not, add a per-instruction or per-quantum poll.

### Tests
```cpp
// chipset_tests/ticket03_interrupts.cpp

TEST_CASE("DRIR assert with matching DIM sets DIR") {
    TsunamiCchip c;
    c.write(Tsunami21272::Cchip::DIM0, 1ULL << 33);
    c.assertInterrupt(33);
    REQUIRE(c.readDIR(0) == (1ULL << 33));
    REQUIRE(c.readDIR(1) == 0);  // CPU1 has DIM1=0, masks it
}

TEST_CASE("DRIR assert with mismatched DIM does NOT set DIR") {
    TsunamiCchip c;
    c.write(Tsunami21272::Cchip::DIM0, 1ULL << 33);
    c.assertInterrupt(34);  // different bit
    REQUIRE(c.readDIR(0) == 0);
}

TEST_CASE("Deassert clears DIR for all CPUs") {
    TsunamiCchip c;
    c.write(Tsunami21272::Cchip::DIM0, 0xFFFFFFFFFFFFFFFFULL);
    c.write(Tsunami21272::Cchip::DIM1, 0xFFFFFFFFFFFFFFFFULL);
    c.assertInterrupt(40);
    REQUIRE(c.readDIR(0) != 0);
    REQUIRE(c.readDIR(1) != 0);
    c.deassertInterrupt(40);
    REQUIRE(c.readDIR(0) == 0);
    REQUIRE(c.readDIR(1) == 0);
}

TEST_CASE("PCI interrupt from Pchip0 INTA lands in DRIR[32]") {
    TsunamiChipset cs(ChipsetVariant::Tsunami, 1, 1ULL << 30);
    cs.cchip().write(Tsunami21272::Cchip::DIM0, 1ULL << 32);
    cs.raisePciInterrupt(0, 0);  // Pchip0 INTA
    REQUIRE(cs.cchip().readDIR(0) == (1ULL << 32));
}
```

### Gate
- All `ticket03_*` tests pass.
- All prior tests still pass.

### Stop and ask
- The exact PCI-INTx → DRIR bit mapping is platform-specific (ES40 vs DS20 vs DS10 wire devices to different bits). If the architect has a specific platform target, get the mapping table before merging the test that pins specific bit numbers.

---

## Ticket 4 — Phase 4: Interval timer

### Scope
Make `step()` real. Every N cycles, the Cchip asserts the timer line in DRIR, gated by `IIC[n]`.

### Read first
- `TsunamiCchip.h` — confirm there is currently no `tickIntervalTimer` method
- The CPU loop's scheduler quantum size (so we know what `cycles` will look like coming in)
- HRM §6.3 for the TIG timer line — which DRIR bit is it? Convention is bit 55 or bit 31; check.

### Deliverables

**1. Add to `TsunamiCchip.h`:**

```cpp
public:
    void tickIntervalTimer(uint64_t cycles) noexcept;

private:
    uint64_t m_timerTicks{0};
    uint64_t m_timerInterval{10000};  // configurable; ~10K cycles ≈ 10µs at 1GHz
    int      m_timerDrirBit{55};       // confirm against HRM
```

```cpp
// In TsunamiCchip.h or .cpp
void TsunamiCchip::tickIntervalTimer(uint64_t cycles) noexcept {
    m_timerTicks += cycles;
    if (m_timerTicks >= m_timerInterval) {
        m_timerTicks -= m_timerInterval;
        // Assert the timer line. IIC[n] gating happens at the per-CPU mask
        // (DIM[n]) — the OS configures DIM[n] to include the timer bit when
        // it wants timer interrupts on that CPU.
        assertInterrupt(m_timerDrirBit);
    }
}
```

**2. Add the IIC suppression check** if your model requires it (some implementations gate at the timer source, not at DIM — read HRM §6.3 carefully before deciding):

```cpp
// Optional: IIC-gated suppression
if (m_iic[0].load(std::memory_order_relaxed) > 0) {
    m_iic[0].fetch_sub(1, std::memory_order_relaxed);
    return;
}
assertInterrupt(m_timerDrirBit);
```

### Tests
```cpp
// chipset_tests/ticket04_timer.cpp

TEST_CASE("Timer fires after exactly interval cycles") {
    TsunamiCchip c;
    c.write(Tsunami21272::Cchip::DIM0, 1ULL << 55);  // unmask timer for CPU0
    c.tickIntervalTimer(9999);
    REQUIRE(c.readDIR(0) == 0);
    c.tickIntervalTimer(1);  // now at 10000
    REQUIRE(c.readDIR(0) == (1ULL << 55));
}

TEST_CASE("Timer fires repeatedly at interval boundaries") {
    TsunamiCchip c;
    c.write(Tsunami21272::Cchip::DIM0, 1ULL << 55);
    c.tickIntervalTimer(25000);  // 2.5 intervals
    REQUIRE(c.readDIR(0) != 0);
    c.deassertInterrupt(55);
    c.tickIntervalTimer(5000);   // brings total to 30000 = 3 intervals
    REQUIRE(c.readDIR(0) != 0);
}

TEST_CASE("TsunamiChipset::step propagates to Cchip timer") {
    TsunamiChipset cs(ChipsetVariant::Tsunami, 1, 1ULL << 30);
    cs.cchip().write(Tsunami21272::Cchip::DIM0, 1ULL << 55);
    cs.step(10000);
    REQUIRE(cs.cchip().readDIR(0) == (1ULL << 55));
}
```

### Gate
- All `ticket04_*` tests pass.
- All prior tests still pass.
- **If a Linux or kernel boot test exists:** observe `jiffies`/`xtime` advancing in the boot log. Otherwise, this gate is just the tests.

### Stop and ask
- Confirm the timer DRIR bit (55 is a guess; HRM §6.3 has the authoritative bit number). Do not merge code that asserts an arbitrary bit without architect sign-off on the mapping.
- The default `m_timerInterval = 10000` is a placeholder. Real Alpha systems run the timer at ~1024 Hz (HZ in the kernel). Compute the right cycle count for the modeled clock frequency and confirm with the architect.

---

## Ticket 5 — Phases 5–6: Pchip live state + Cypress ISA bridge

### Scope
Make Pchip CSRs hold their last write, route PCI config space to registered devices, and register the Cypress CY82C693 at slot 0 of Pchip0 so SRM PCI enumeration completes.

### Read first
- All of `TsunamiPchip.h` (700 lines) — understand the existing device registration model
- `Cypress_CY82C693ISABridge.h` (currently a skeleton with a comment)
- The SRM crash log if it shows the PCI scan stalling at slot 0
- The PCI 2.2 spec section on config space header type 0 (just the first 16 bytes: vendor, device, command, status, revision, class code, BIST/header/latency/cache)

### Deliverables

**1. In `Cypress_CY82C693ISABridge.h`,** implement `IPciDeviceHandler` returning:

| Register     | Value        | Meaning                              |
|--------------|--------------|--------------------------------------|
| `0x00` vendor| `0x1080`     | Cypress Semiconductor                |
| `0x02` device| `0xC693`     | CY82C693                             |
| `0x04` command| `0x0007`    | I/O + Mem + Bus master enabled       |
| `0x06` status | `0x0280`    | Fast back-to-back + medium DEVSEL    |
| `0x08` rev    | `0x01`      | Revision 1                            |
| `0x09–0x0B` class| `0x060100`| Bridge / ISA bridge                 |
| `0x0E` header | `0x80`      | Multi-function header type 0         |
| BARs `0x10+`  | `0x00000000`| No BARs (ISA bridge has fixed I/O)   |

Confirm the vendor/device IDs against the real Cypress datasheet — some SRM revisions check both ID and class code together.

**2. In `TsunamiPchip.h`,** confirm there's a `registerDevice(int slot, int func, IPciDeviceHandler*)` API. If not, add one. The Type 0 config dispatch table at offset `0x801.FE00.0000` decodes:
```
device = (pa >> 11) & 0x1F    // slot 0..31
function = (pa >> 8) & 0x07
reg = pa & 0xFC
```

Return `0xFFFFFFFF` for any (device, function) pair that has no registered handler — this is what SRM expects for empty slots, and is also what the existing scaffold note in `Cypress_CY82C693ISABridge.h` is warning about.

**3. Wire the bridge in at construction.** In `TsunamiChipset` or wherever the chipset is built:

```cpp
m_pchip.registerDevice(0, 0, &m_cypressBridge);  // slot 0, function 0
```

**4. Add a PCI enumerator** (Functional Surface 17) for diagnostics and future test code:

```cpp
// On TsunamiPchip
void forEachDevice(std::function<void(int slot, int func, IPciDeviceHandler*)> fn) const;
```

Iterate the internal registration map in stable (slot, function) order. This is not on the SRM-facing path — it's for the test harness and for `dump-state` style diagnostics.

**5. Implement SPRST (Surface 20).** A write to the Pchip's SPRST register iterates registered devices and calls `pciReset()` on each. The `IPciDeviceHandler` interface gains a `pciReset()` method (default empty implementation is fine; `FakePciDevice` already records it).

```cpp
case Pchip::SPRST:
    forEachDevice([](int, int, IPciDeviceHandler* h){ h->pciReset(); });
    break;
```

### Tests
```cpp
// chipset_tests/ticket05_pci_enum.cpp

TEST_CASE("Empty PCI slot returns 0xFFFFFFFF") {
    TsunamiChipset cs(ChipsetVariant::Tsunami, 1, 1ULL << 30);
    // Read vendor ID of slot 5 (no device registered)
    const uint64_t pa = 0x801FE000000ULL + (5 << 11) + 0x00;
    REQUIRE((cs.mmioRead(pa, 4) & 0xFFFFFFFF) == 0xFFFFFFFFULL);
}

TEST_CASE("Cypress ISA bridge identifies at slot 0") {
    TsunamiChipset cs(ChipsetVariant::Tsunami, 1, 1ULL << 30);
    // Wire in the bridge — adjust to actual registration API
    const uint64_t pa = 0x801FE000000ULL + (0 << 11) + 0x00;
    const uint32_t vendev = cs.mmioRead(pa, 4) & 0xFFFFFFFF;
    REQUIRE((vendev & 0xFFFF) == 0x1080);          // Cypress vendor
    REQUIRE(((vendev >> 16) & 0xFFFF) == 0xC693);  // CY82C693
}

TEST_CASE("Cypress ISA bridge reports ISA bridge class code") {
    TsunamiChipset cs(ChipsetVariant::Tsunami, 1, 1ULL << 30);
    const uint64_t pa = 0x801FE000000ULL + (0 << 11) + 0x08;
    const uint32_t classRev = cs.mmioRead(pa, 4) & 0xFFFFFFFF;
    REQUIRE(((classRev >> 8) & 0xFFFFFF) == 0x060100);  // ISA bridge
}

TEST_CASE("PCI enumerator yields registered devices in stable order") {
    TsunamiChipset cs(ChipsetVariant::Tsunami, 1, 1ULL << 30);
    FakePciDevice a, b;
    cs.pchip().registerDevice(5, 0, &a);
    cs.pchip().registerDevice(2, 0, &b);
    std::vector<int> slots;
    cs.pchip().forEachDevice([&](int s, int, IPciDeviceHandler*){ slots.push_back(s); });
    // Slot 0 (Cypress) + 2 + 5
    REQUIRE(slots == std::vector<int>{0, 2, 5});
}

TEST_CASE("SPRST write resets all registered devices") {
    TsunamiChipset cs(ChipsetVariant::Tsunami, 1, 1ULL << 30);
    FakePciDevice a;
    cs.pchip().registerDevice(3, 0, &a);
    REQUIRE(a.wasReset == false);
    cs.mmioWrite(0x801'8000'0000ULL + Tsunami21272::Pchip::SPRST, 1, 8);
    REQUIRE(a.wasReset == true);
}
```

### Gate
- All `ticket05_*` tests pass.
- All prior tests still pass.
- **Boot SRM and observe:** PCI enumeration completes (or at least progresses past slot 0). Capture the new boot log.

### Stop and ask
- If the SRM scans for any specific PCI device beyond the ISA bridge (e.g., a SCSI HBA at slot 5), the architect needs to decide whether to stub those devices now or let them return `0xFFFFFFFF`. Default: return all-ones and let SRM treat them as absent.

---

## Phase 1 milestone

**After Ticket 5, the chipset is "SRM-bootable":** memory sizing converges, PCI enumeration completes, the Cypress ISA bridge is identified. Tickets 6–10 below are the second horizon — they make the chipset *OS-functional*. They can land in any order after Ticket 5, subject to the dependency notes at each ticket. The architect may also choose to land only some of them before declaring the chipset "good enough" for whatever the next milestone is.

---

## Ticket 6 — ISA I/O dispatch and UART hookup

### Scope
Wire the Cypress ISA bridge as the I/O port dispatcher behind the Pchip's PCI I/O space. Register the COM1/COM2 UART devices for the standard PC port addresses. Validate the sparse I/O address decode round-trip. After this ticket, console output from SRM/OS reaches the host.

Implements Surfaces 3 (PCI I/O dispatch) and 23 (UART hookup).

### Read first
- `Cypress_CY82C693ISABridge.h` — the current skeleton
- `Tsunami21272_RegisterMap.h::SparseSpace` — `decodePciAddr`, `decodeByteLane`, `decodeXferLen` are already there
- `TsunamiPchip.h` — how it currently routes (or doesn't) the I/O windows at `0x801.FC00.0000` (dense) and `0x801.4000.0000` (sparse)
- Whatever serial device module already exists in the project — search for `UART`, `NS16550`, `16550`, `serial`, `Com1`, or `Com2`
- HRM §10.1.3 — PIO address translation rules for sparse vs. dense

### Deliverables

**1. Define `IIoPortHandler`.** A two-method interface for any device that owns a range of PC I/O ports:

```cpp
struct IIoPortHandler {
    virtual ~IIoPortHandler() = default;
    virtual uint32_t ioRead(uint16_t port, uint8_t width) = 0;
    virtual void     ioWrite(uint16_t port, uint32_t value, uint8_t width) = 0;
};
```

**2. Extend the Cypress bridge to dispatch I/O ports.** It becomes the port-router for the entire ISA bus:

```cpp
// On Cypress_CY82C693ISABridge
void registerPortRange(uint16_t start, uint16_t end, IIoPortHandler* h);
uint32_t isaIoRead(uint16_t port, uint8_t width);
void     isaIoWrite(uint16_t port, uint32_t value, uint8_t width);
```

Overlapping port ranges are an error (assert at registration). Unregistered ports return `0xFF` (ISA floating-line convention) on read and silently drop writes.

**3. Implement sparse I/O decode in the Pchip.** Use the existing `SparseSpace::decodePciAddr / decodeByteLane / decodeXferLen` helpers. A sparse I/O access becomes:

```cpp
uint64_t TsunamiPchip::readSparseIO(uint64_t mmioOffset) noexcept {
    using namespace Tsunami21272::SparseSpace;
    const uint16_t port    = (uint16_t)decodePciByteAddr(mmioOffset);
    const uint8_t  xferLen = xferLenToBytes(decodeXferLen(mmioOffset));
    return m_isaBridge->isaIoRead(port, xferLen);
}
```

Plus the symmetric `writeSparseIO`. The dispatcher in `TsunamiChipset::mmioRead/mmioWrite` already routes sparse I/O addresses to the Pchip (Ticket 1) — confirm that path exists and reaches `readSparseIO`.

**4. Register the serial devices.** In the chipset wiring (or wherever the platform is assembled):

```cpp
m_cypressBridge.registerPortRange(0x3F8, 0x3FF, &m_com1);  // COM1
m_cypressBridge.registerPortRange(0x2F8, 0x2FF, &m_com2);  // COM2
```

If the project's existing UART module needs adaptation to `IIoPortHandler`, do that adaptation. Do not invent a UART from scratch — that's out of scope; this ticket only wires what exists.

**5. Route the UART IRQ.** COM1 maps to ISA IRQ 4, COM2 to ISA IRQ 3. The bridge's `raiseIsaInterrupt(irq)` cascades through to `chipset.raiseIsaInterrupt(irq)` and lands in a specific DRIR bit per HRM §6.3 — confirm the bit number against HRM before pinning a test.

### Tests
```cpp
// chipset_tests/ticket06_isa_uart.cpp

TEST_CASE("Sparse I/O PA encodes and decodes a port symmetrically") {
    using namespace Tsunami21272::SparseSpace;
    // Construct a sparse PA for port 0x3F8, byte lane 0, byte access
    const uint16_t port = 0x3F8;
    const uint64_t pa   = kPchip0_SparseIO + ((uint64_t)port << 7) | 0;
    REQUIRE(decodePciByteAddr(pa - kPchip0_SparseIO) == port);
    REQUIRE(decodeXferLen(pa - kPchip0_SparseIO) == 0);  // byte
}

TEST_CASE("Read of UART LSR via sparse I/O reaches the UART handler") {
    TsunamiChipset cs(Platform::ES40, 1, 1ULL << 30);
    FakeIoPortDevice uart;
    uart.responses[0x3FD] = 0x60;  // THRE + TEMT
    cs.cypress().registerPortRange(0x3F8, 0x3FF, &uart);
    // Sparse I/O PA for port 0x3FD, byte read
    const uint64_t pa = 0x801'4000'0000ULL + ((uint64_t)0x3FD << 7);
    REQUIRE((cs.mmioRead(pa, 1) & 0xFF) == 0x60);
    REQUIRE(uart.reads.size() == 1);
    REQUIRE(uart.reads[0].port == 0x3FD);
}

TEST_CASE("Write to UART THR delivers the byte to the UART") {
    TsunamiChipset cs(Platform::ES40, 1, 1ULL << 30);
    FakeIoPortDevice uart;
    cs.cypress().registerPortRange(0x3F8, 0x3FF, &uart);
    const uint64_t pa = 0x801'4000'0000ULL + ((uint64_t)0x3F8 << 7);
    cs.mmioWrite(pa, 'A', 1);
    REQUIRE(uart.writes.size() == 1);
    REQUIRE(uart.writes[0].port == 0x3F8);
    REQUIRE(uart.writes[0].value == 'A');
}

TEST_CASE("Unregistered port returns 0xFF") {
    TsunamiChipset cs(Platform::ES40, 1, 1ULL << 30);
    const uint64_t pa = 0x801'4000'0000ULL + ((uint64_t)0x1234 << 7);
    REQUIRE((cs.mmioRead(pa, 1) & 0xFF) == 0xFF);
}

TEST_CASE("UART IRQ raises ISA IRQ 4 and lands in DRIR") {
    TsunamiChipset cs(Platform::ES40, 1, 1ULL << 30);
    cs.cchip().write(Tsunami21272::Cchip::DIM0, ~0ULL);  // unmask all
    cs.raiseIsaInterrupt(4);
    // Confirmed DRIR bit per HRM — adjust after Stop-and-ask
    REQUIRE(cs.cchip().readDIR(0) != 0);
}
```

### Gate
- All `ticket06_*` tests pass.
- All prior tests still pass.
- **Boot SRM and observe:** console banner appears on the host UART terminal. This is the most visible single milestone in the whole plan.

### Stop and ask
- Confirm the project's existing UART module's API before writing the `IIoPortHandler` adapter. If the UART has a different shape (e.g., methods named `serialIn`/`serialOut`), the adapter goes one direction or the other based on what's easier to change.
- The exact ISA-IRQ → DRIR bit mapping per HRM §6.3. The test above leaves it as "non-zero" pending architect input.
- Dense vs sparse I/O for the UART: real SRM uses sparse for byte-granular UART access, but if your existing memory translation routes UART through dense I/O, confirm the dispatch table accounts for both.

---

## Ticket 7 — DMA windows (direct-mapped and scatter-gather)

### Scope
Implement the Pchip's four WSBA/WSM/TBA windows as live address translation. Honor the SG bit for scatter-gather translation with PTE fetch and an 8-entry TLB. Handle the PCTL.HOLE exclusion. After this ticket, any registered PCI device's DMA reaches system memory through the documented translation path.

Implements Surfaces 4 (PCI memory dispatch), 11 (direct-mapped DMA), 12 (scatter-gather DMA).

### Read first
- `TsunamiPchip.h` — current WSBA/WSM/TBA storage; confirm no translation logic exists yet
- HRM Table 105 (Direct-Mapped) and Table 106 (Scatter-Gather)
- HRM §10.1.4.1 (window hole), §10.1.4.2 (direct), §10.1.4.3 (scatter-gather)
- The `DeterministicMemory` fixture from Ticket 0 — this ticket needs to read/write through it

### Deliverables

**1. Translation API on the Pchip:**

```cpp
struct DmaTranslation {
    uint64_t sysAddr;
    bool     valid;
    bool     scatterGather;   // true if SG was used
    int      windowIndex;     // 0–3, which window matched
};
DmaTranslation translatePciAddress(uint32_t pciAddr) const noexcept;
```

**2. Direct-mapped path** per HRM Table 105. Window size derives from the contiguous low-1-bits in `WSM[n]<31:20>`. The translation is `TBA[n]<34:n> : pciAddr<n-1:2>` for window-size `2^n`.

**3. Scatter-gather path** per HRM Table 106. When `WSBA[n].SG = 1`:
   - Compute PTE address: `TBA[n]<34:k> : pciAddr<size_log-1:13>` where `k` depends on window size.
   - Fetch 8 bytes from system memory at PTE address.
   - PTE bit 0 is the valid bit; if clear, raise SGE.
   - Otherwise system address = `pte<22:1> : pciAddr<12:0>`.
   - Cache the PTE in the TLB (8 entries, LRU eviction).

**4. Window hole.** `PCTL.HOLE` set excludes PCI addresses `0x000.8000`–`0x000.FFFFF` from all windows. A miss against all windows + hole produces `valid = false`.

**5. TLB management.** `TLBIV` (write-only) invalidates one entry by PCI address; `TLBIA` invalidates all. Implement as a simple 8-entry array with sequential search — performance is not a concern here, correctness is.

**6. DMA read/write paths.** `pchip.dmaRead(pciAddr, len)` and `pchip.dmaWrite(pciAddr, data, len)` translate then access system memory. These are the device-facing API; PCI devices call them.

### Tests
```cpp
// chipset_tests/ticket07_dma.cpp

TEST_CASE("Direct-mapped 1MB window translates correctly") {
    TsunamiChipset cs(Platform::ES40, 1, 1ULL << 30);
    // Program window 0: 1 MB direct-mapped at PCI base 0, sysmem base 0x40000000
    cs.mmioWrite(0x801'8000'0000ULL + Pchip::WSBA0, 0x1, 8);      // enable, no SG
    cs.mmioWrite(0x801'8000'0000ULL + Pchip::WSM0,  0x00000000ULL, 8);  // 1 MB
    cs.mmioWrite(0x801'8000'0000ULL + Pchip::TBA0,  0x40000000ULL, 8);
    auto t = cs.pchip().translatePciAddress(0x12340);
    REQUIRE(t.valid);
    REQUIRE(t.scatterGather == false);
    REQUIRE(t.sysAddr == 0x40012340);
}

TEST_CASE("Disabled window does not match") {
    TsunamiChipset cs(Platform::ES40, 1, 1ULL << 30);
    cs.mmioWrite(0x801'8000'0000ULL + Pchip::WSBA0, 0x0, 8);  // disabled
    REQUIRE(cs.pchip().translatePciAddress(0x12340).valid == false);
}

TEST_CASE("Window hole excludes 512K-1M from all windows when PCTL.HOLE set") {
    TsunamiChipset cs(Platform::ES40, 1, 1ULL << 30);
    cs.mmioWrite(0x801'8000'0000ULL + Pchip::PCTL,  Pchip::kPCTL_Hole, 8);
    cs.mmioWrite(0x801'8000'0000ULL + Pchip::WSBA0, 0x1, 8);
    cs.mmioWrite(0x801'8000'0000ULL + Pchip::WSM0,  0x00F00000ULL, 8);  // 16 MB
    cs.mmioWrite(0x801'8000'0000ULL + Pchip::TBA0,  0x40000000ULL, 8);
    REQUIRE(cs.pchip().translatePciAddress(0x000A0000).valid == false);  // in hole
    REQUIRE(cs.pchip().translatePciAddress(0x00200000).valid == true);   // outside hole
}

TEST_CASE("Scatter-gather translation walks PTE in system memory") {
    TsunamiChipset cs(Platform::ES40, 1, 1ULL << 30);
    DeterministicMemory mem(1 << 28);
    cs.attachMemory(&mem);  // hypothetical wiring
    // Place a valid PTE at sysmem 0x10000 mapping PCI page 0 to sysmem page 0x80000000
    const uint64_t pte = ((0x80000000ULL >> 13) << 1) | 0x1;  // valid bit set
    mem.write(0x10000, &pte, 8);
    cs.mmioWrite(0x801'8000'0000ULL + Pchip::WSBA0, 0x3, 8);    // enable + SG
    cs.mmioWrite(0x801'8000'0000ULL + Pchip::WSM0,  0x00000000ULL, 8);  // 1 MB
    cs.mmioWrite(0x801'8000'0000ULL + Pchip::TBA0,  0x10000ULL, 8);
    auto t = cs.pchip().translatePciAddress(0x100);  // page 0, offset 0x100
    REQUIRE(t.valid);
    REQUIRE(t.scatterGather);
    REQUIRE(t.sysAddr == 0x80000100ULL);
}

TEST_CASE("Invalid PTE raises SGE error") {
    // similar setup but PTE valid bit = 0; assert PERROR.SGE set, translate returns invalid
}

TEST_CASE("TLBIA invalidates all TLB entries") {
    // translate twice (warm TLB), TLBIA, translate again; observe PTE re-fetch
}
```

### Gate
- All `ticket07_*` tests pass.
- All prior tests still pass.
- If a PCI device with DMA is wired in the system (e.g., the network or storage stubs), a smoke test of "device DMAs N bytes, system memory contains those bytes at translated address" passes.

### Stop and ask
- The PTE format above (`bit 0` = valid, `bits 22:1` = page number) matches HRM §10.1.4.3 — confirm before pinning tests. Some implementations use `bit 0` for something else.
- TLB eviction policy (LRU vs FIFO vs round-robin) is not HRM-specified; pick the simplest (FIFO) unless the architect prefers otherwise.
- `PCTL.PTEVRFY` causes the Pchip to fetch each PTE twice and compare — implement now or defer? Defer is fine.

---

## Ticket 8 — Inter-processor interrupts (IPI)

### Scope
Make writes to `IIC[n]` deliver an interrupt to CPU n. Per HRM, the target CPU sees this on a specific b_irq line. After this ticket, an SMP kernel can wake secondary CPUs and send cross-CPU interrupts.

Implements Surface 8 (IPI).

### Read first
- `TsunamiCchip.h::sendIPI` — current implementation just stores the value
- HRM §6.3 — interrupt sources including IPI; identify which DRIR bit (if any) IPI uses, vs. a dedicated per-CPU line
- The `FakeCpu` fixture's interrupt-poll surface

### Deliverables

**1. Refine IPI semantics.** When CPU A writes `IIC[B]`, the Cchip must make CPU B observe a pending IPI through its normal interrupt poll. The two viable approaches:
   - **Approach A:** allocate a dedicated DRIR bit for IPI (e.g., bit 31); IIC[n] writes set that bit, but the bit is only visible to CPU n (variant of the existing `readDIR` that takes IPI into account).
   - **Approach B:** keep IIC[n] as a separate register and add a per-CPU IPI-pending flag that the CPU's IPL poll consults alongside `readDIR`.
   
   The HRM is the tiebreaker. Pick the approach the HRM actually documents — do not invent.

**2. Acknowledgment path.** A CPU reading or writing IIC[n] (per HRM convention) clears the pending state. Implement that.

**3. Wire to FakeCpu.** Add an `ipiPending` flag to the fixture; the Cchip flips it when IIC writes target that CPU.

### Tests
```cpp
// chipset_tests/ticket08_ipi.cpp

TEST_CASE("Write to IIC[1] makes CPU 1 see IPI pending") {
    TsunamiChipset cs(Platform::ES45, 4, 8ULL << 30);
    // CPU 0 sends IPI to CPU 1
    cs.mmioWrite(0x801'A000'0000ULL + Tsunami21272::Cchip::IIC1, 0x1, 8, /*cpuId=*/0);
    // CPU 1 polls
    REQUIRE(cs.cchip().hasPendingIpi(1) == true);
    REQUIRE(cs.cchip().hasPendingIpi(0) == false);
}

TEST_CASE("IPI ack clears the pending state") {
    TsunamiChipset cs(Platform::ES45, 4, 8ULL << 30);
    cs.mmioWrite(0x801'A000'0000ULL + Tsunami21272::Cchip::IIC1, 0x1, 8, 0);
    cs.cchip().acknowledgeIpi(1);
    REQUIRE(cs.cchip().hasPendingIpi(1) == false);
}

TEST_CASE("IPI to CPU n does not affect other CPUs") {
    TsunamiChipset cs(Platform::ES45, 4, 8ULL << 30);
    cs.mmioWrite(0x801'A000'0000ULL + Tsunami21272::Cchip::IIC2, 0x1, 8, 0);
    REQUIRE(cs.cchip().hasPendingIpi(0) == false);
    REQUIRE(cs.cchip().hasPendingIpi(1) == false);
    REQUIRE(cs.cchip().hasPendingIpi(2) == true);
    REQUIRE(cs.cchip().hasPendingIpi(3) == false);
}
```

### Gate
- All `ticket08_*` tests pass.
- All prior tests still pass.
- If the CPU model can run, an SMP boot lands secondary CPUs at their wakeup vectors (this may be Ticket 9-dependent if the CPU loop needs reservation snoop first).

### Stop and ask
- Approach A vs B above. Pick based on HRM, but get architect sign-off.
- The IPL associated with IPI delivery (typically IPL 22 or IPL 3 — HRM §6.3 has the table).

---

## Ticket 9 — Reservation snoop (LDx_L/STx_C coherence)

### Scope
Wire the system-bus snoop broadcast: any memory write — by a CPU or by a DMA cycle — must invalidate other CPUs' lock reservations if they match. Without this, SMP code that uses LDx_L / STx_C atomics is incorrect. After this ticket, SMP kernels and userspace can rely on Alpha atomics.

Implements Surface 13 (reservation snoop). Cross-cuts CPU model and memory subsystem; this ticket builds the broadcast point in the chipset and the snoop API surface.

### Read first
- The CPU model's reservation tracking — search for `lock_flag`, `lock_address`, `LDQ_L`, `STQ_C`, `LDL_L`, `STL_C`
- How the CPU model currently performs memory writes (is there a single chokepoint, or do writes happen in multiple paths?)
- HRM §6 — coherence and snoop on Cchip
- The `FakeCpu` fixture's reservation state

### Deliverables

**1. Snoop broadcast API on the chipset:**

```cpp
// Called by every memory write — CPU stores AND DMA writes
void broadcastWriteSnoop(int writerCpuId, uint64_t pa, uint8_t len) noexcept;
```

Iterates registered CPUs; for each non-writer CPU, if its `lock_address` overlaps `[pa, pa+len)`, clears its `lock_flag`. The CPU model must expose a hook for this clear — `cpu.snoopWrite(pa, len)`.

**2. CPU registration with the chipset:**

```cpp
void TsunamiChipset::registerCpu(int cpuId, ICpuSnoopHandler* cpu);
```

`ICpuSnoopHandler` has one method: `snoopWrite(uint64_t pa, uint8_t len)`. The CPU model implements this; `FakeCpu` already has a `lock_flag` / `lock_address` pair so it implements naturally.

**3. Wire the broadcast into the memory write path.** Every memory store from the CPU and every DMA write from the Pchip calls `chipset.broadcastWriteSnoop(...)` *before* (or atomically with) committing the write. The exact ordering matters less than the invariant: by the time a CPU observes the new value, all locks that could be invalidated *are* invalidated.

**4. Reservation granularity.** Per HRM, the reservation is per cache line (64 bytes on Alpha). A write to any byte in the cache line clears matching locks. Code this carefully — overly fine granularity (single byte) is benign but slow; overly coarse (page) is incorrect.

### Tests
```cpp
// chipset_tests/ticket09_reservation.cpp

TEST_CASE("Lock on CPU 0; CPU 1 writes elsewhere; lock survives") {
    TsunamiChipset cs(Platform::ES45, 4, 8ULL << 30);
    FakeCpu cpu0(0), cpu1(1);
    cs.registerCpu(0, &cpu0);
    cs.registerCpu(1, &cpu1);
    cpu0.takeReservation(0x10000);
    cs.broadcastWriteSnoop(1, 0x20000, 8);
    REQUIRE(cpu0.lock_flag == true);
}

TEST_CASE("Lock on CPU 0; CPU 1 writes same line; lock clears") {
    TsunamiChipset cs(Platform::ES45, 4, 8ULL << 30);
    FakeCpu cpu0(0), cpu1(1);
    cs.registerCpu(0, &cpu0);
    cs.registerCpu(1, &cpu1);
    cpu0.takeReservation(0x10000);
    cs.broadcastWriteSnoop(1, 0x10020, 8);  // same 64-byte line
    REQUIRE(cpu0.lock_flag == false);
}

TEST_CASE("CPU 0's own write does not clear its own lock") {
    TsunamiChipset cs(Platform::ES45, 4, 8ULL << 30);
    FakeCpu cpu0(0);
    cs.registerCpu(0, &cpu0);
    cpu0.takeReservation(0x10000);
    cs.broadcastWriteSnoop(0, 0x10000, 8);  // CPU 0 writes its own locked line
    // HRM says implementation-defined; pick "survives" and assert
    REQUIRE(cpu0.lock_flag == true);
}

TEST_CASE("DMA write from Pchip clears matching CPU lock") {
    TsunamiChipset cs(Platform::ES45, 4, 8ULL << 30);
    FakeCpu cpu0(0);
    cs.registerCpu(0, &cpu0);
    cpu0.takeReservation(0x80000000);
    // Pchip DMA writes through translated address — wire the broadcast there
    cs.pchip().simulateDmaWrite(0x80000000, 8);
    REQUIRE(cpu0.lock_flag == false);
}
```

### Gate
- All `ticket09_*` tests pass.
- All prior tests still pass.
- SMP kernel boot does not exhibit livelock or lost-update bugs in any LDx_L/STx_C loop.

### Stop and ask
- "CPU writes its own locked line" semantics — HRM allows implementation choice. Default: lock survives; confirm with architect.
- Cache line size constant — Alpha EV6 is 64 bytes; confirm.
- The exact hook location for memory writes (CPU's STQ path, DMA's write path). This may require touching CPU code; if so, stop and flag scope.

---

## Ticket 10 — Error capture and reporting

### Scope
Implement W1C error capture in Pchip `PERROR`, cross-realm promotion of NXM to Cchip `MISC.NXM_SRC`, and PERRMASK-gated error interrupts. After this ticket, the system reports errors cleanly instead of silently dropping them.

Implements Surface 22 (error capture and reporting).

### Read first
- `TsunamiPchip.h` — current PERROR / PERRMASK / PERRSET storage (last-write-wins right now)
- `TsunamiCchip.h` — MISC register and its NXM_SRC field at bits `[47:44]`
- HRM §8.8 — Pchip error handling
- HRM §6.6 — Cchip-detected errors and reporting

### Deliverables

**1. Error type enum:**

```cpp
enum class PchipError : uint8_t {
    NDS = 0,   // No device select (master abort)
    TA  = 1,   // Target abort
    RDPE,      // Read data parity error
    PERR,      // PCI PERR# asserted
    SGE,       // Scatter-gather error
    APE,       // Address parity error
    SERR,      // PCI SERR# asserted
    DCRTO,     // Delayed completion retry timeout
    // ...per HRM table; full list per §8.8
};
```

**2. Latching capture in Pchip:**

```cpp
void TsunamiPchip::captureError(PchipError type, uint64_t pa) noexcept {
    if (m_errorLatched) return;  // first-error-wins
    m_errorLatched = true;
    m_perror = (1ULL << (int)type);
    m_perrorPa = pa;
    // If PERRMASK has this bit clear (unmasked), assert error interrupt
    if ((m_perrmask & (1ULL << (int)type)) == 0) {
        m_chipset->reportPchipError(/*pchipId*/ 0);  // → DRIR bit per HRM
    }
}
```

**3. W1C semantics on PERROR.** Writing 1 to a PERROR bit clears it; writing 0 has no effect. When all bits clear, `m_errorLatched = false` so the next error captures.

**4. Master abort detection.** When a PCI config read or memory read targets an unregistered slot/BAR, the Pchip captures `PchipError::NDS` with the failing PA.

**5. NXM promotion.** When the Pchip cannot complete a CPU-side PIO (e.g., access to an unmapped region), call `chipset.reportNxm(pa, sourceCode)` which writes `MISC.NXM_SRC` on the Cchip. The `reportNxm` stub from Ticket 1 finally becomes real here.

**6. PERRSET for diagnostic injection.** Write to PERRSET force-sets PERROR bits — used by SRM/OS to test error paths.

### Tests
```cpp
// chipset_tests/ticket10_errors.cpp

TEST_CASE("Master abort on read of empty slot captures NDS") {
    TsunamiChipset cs(Platform::ES40, 1, 1ULL << 30);
    // Read vendor ID of slot 5 (no device) — triggers master abort
    (void)cs.mmioRead(0x801'FE00'0000ULL + (5 << 11) + 0x00, 4);
    uint64_t perror = cs.mmioRead(0x801'8000'0000ULL + Pchip::PERROR, 8);
    REQUIRE((perror & (1ULL << (int)PchipError::NDS)) != 0);
}

TEST_CASE("W1C clears PERROR; second error then captures") {
    TsunamiChipset cs(Platform::ES40, 1, 1ULL << 30);
    cs.pchip().captureError(PchipError::NDS, 0x1234);
    REQUIRE(cs.mmioRead(0x801'8000'0000ULL + Pchip::PERROR, 8) != 0);
    // W1C clear
    cs.mmioWrite(0x801'8000'0000ULL + Pchip::PERROR, (1ULL << (int)PchipError::NDS), 8);
    REQUIRE(cs.mmioRead(0x801'8000'0000ULL + Pchip::PERROR, 8) == 0);
    // Second error captures
    cs.pchip().captureError(PchipError::TA, 0x5678);
    REQUIRE((cs.mmioRead(0x801'8000'0000ULL + Pchip::PERROR, 8) & (1ULL << (int)PchipError::TA)) != 0);
}

TEST_CASE("Second error before clear is ignored") {
    TsunamiChipset cs(Platform::ES40, 1, 1ULL << 30);
    cs.pchip().captureError(PchipError::NDS, 0x1234);
    cs.pchip().captureError(PchipError::TA, 0x5678);
    uint64_t perror = cs.mmioRead(0x801'8000'0000ULL + Pchip::PERROR, 8);
    REQUIRE((perror & (1ULL << (int)PchipError::NDS)) != 0);
    REQUIRE((perror & (1ULL << (int)PchipError::TA)) == 0);  // second lost
}

TEST_CASE("Unmasked error raises Cchip interrupt") {
    TsunamiChipset cs(Platform::ES40, 1, 1ULL << 30);
    cs.cchip().write(Tsunami21272::Cchip::DIM0, ~0ULL);  // unmask everything
    cs.mmioWrite(0x801'8000'0000ULL + Pchip::PERRMASK, 0, 8);  // unmask all errors
    cs.pchip().captureError(PchipError::NDS, 0x1234);
    REQUIRE(cs.cchip().readDIR(0) != 0);
}

TEST_CASE("NXM source promotes to Cchip MISC") {
    TsunamiChipset cs(Platform::ES40, 1, 1ULL << 30);
    cs.reportNxm(0xDEADBEEF, /*sourceCode=*/ 0x3);
    uint64_t misc = cs.mmioRead(0x801'A000'0000ULL + Tsunami21272::Cchip::MISC, 8);
    REQUIRE(((misc >> 44) & 0xF) == 0x3);  // NXM_SRC bits per HRM
}
```

### Gate
- All `ticket10_*` tests pass.
- All prior tests still pass.
- SRM or OS does not hang on a PCI master abort to an empty slot — the error is captured, optionally raised as an interrupt, and reported through the normal error path.

### Stop and ask
- Which DRIR bit catches Pchip errors per HRM §6.3 — likely bit 16 or so but confirm.
- NXM source code values (3-bit or 4-bit field, which encodes which engine flagged it). HRM Table 10-12 has the encoding.
- The full PERROR bit list. The enum above is illustrative; the real list is in HRM §8.8.

---

## Cross-cutting practices

### After every ticket
1. Run the full `chipset_tests` binary, not just the new tests. Regressions are easier to catch immediately than later.
2. If a SRM boot test exists, run it. Capture the boot log delta.
3. Commit with a message of the form `chipset: ticket N — <one-line scope>`. One commit per ticket.

### Documenting HRM deviations
Create `docs/hrm_deviations.md` if it doesn't exist. Every time the code deviates from the literal HRM text (because the HRM has known errata, or because the SRM works around a hardware bug), append an entry: HRM section, what the HRM says, what we do, why.

### When something doesn't compile
- Stop immediately. Do not paper over a missing API with a stub that returns 0 just to make the build pass.
- Surface the actual error to the architect with the file/line.
- This is most likely in Ticket 1 (Pchip read API may not match) and Ticket 5 (device registration API may not exist).

### When a test fails
- Run it once more in isolation.
- If it still fails: stop. Do not modify the test to make it pass. The test pins an invariant — if the invariant is wrong, the architect needs to weigh in.

---

## Definition of done — Phase 1 (SRM-bootable, Tickets 0–5)

The architect should be able to verify after Ticket 5:

1. `cmake --build . && ./chipset_tests` returns green across all Phase 1 ticket suites (0, 1, 1.5, 2, 3, 4, 5).
2. Booting the SRM in the emulator no longer spins at `0x1283x`. R6 sizing loop converges.
3. SRM PCI enumeration progresses past slot 0 Pchip0 (Cypress ISA bridge identified).
4. `TsunamiChipset.cpp` has: `mmioRead`, `mmioWrite`, `step`, `raisePciInterrupt`, `reportNxm`, and the platform-keyed constructor.
5. `TsunamiChipset`, `TsunamiCchip`, `TsunamiDchip`, `TsunamiPchip` all accept and expose a variant; the hardcoded model-name ternary is gone.
6. Five test fixtures (`FakePciDevice`, `FakeIoPortDevice`, `FakeCpu`, `DeterministicMemory`, `CycleInjector`) compile, are exercised by sanity tests, and are available for use by later tickets.
7. `docs/hrm_deviations.md` records any places we diverged from the literal HRM.

## Definition of done — Phase 2 (OS-functional, Tickets 6–10)

After Ticket 10:

8. All Phase 2 ticket suites (6, 7, 8, 9, 10) pass.
9. SRM console output appears on the host UART terminal (Ticket 6).
10. A registered PCI device can DMA to system memory and the data lands at the translated address (Ticket 7).
11. SMP kernel boot wakes secondary CPUs via IPI (Ticket 8).
12. SMP atomics (LDx_L / STx_C) do not exhibit lost updates under cross-CPU pressure (Ticket 9).
13. PCI master abort to an empty slot is captured in PERROR and the system continues running (Ticket 10).

After Phase 2 the chipset is functionally complete for an ES40/ES45 single-Pchip configuration. Dual-Pchip (Pchip1) support, full ACPI power management (Phase 10 of the architecture doc), and performance counters become tickets in their own right but are not blockers for OS operation.

---

## What this plan deliberately does not do

- **Does not implement the storage devices, network devices, or other PCI peripherals.** Those modules live outside the chipset. The chipset only ensures the bus, IRQ, and DMA paths reach them correctly.
- **Does not implement the UART itself.** Ticket 6 wires whatever UART module already exists in the project; if there isn't one, that's a separate ticket and the architect needs to flag it.
- **Does not implement the SRM image or boot loader.** Those are upstream.
- **Does not refactor any existing working code.** The twelve tickets are additive. If something is already correct, the ticket adds tests and trace hooks only.
- **Does not pick a test framework autonomously.** Cowork stops at Ticket 0 if there isn't one.
- **Does not invent platforms, register layouts, or interrupt mappings.** Every authoritative claim comes from the HRM. Stop-and-ask points are placed at every spot where the HRM needs to be consulted before pinning behavior.
- **Does not promise that Phase 2 completes before any specific operating system boots.** The phase order is by chipset surface, not by OS dependency. Tru64 and Linux/Alpha exercise slightly different surfaces, and which OS lights up first depends on the device set, not the chipset.
