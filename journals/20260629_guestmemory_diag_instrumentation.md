<!--
EmulatR V4 -- GuestMemory diagnostic instrumentation (sink-level store-watch + retire-trace arm)
Project: EmulatR (Alpha 21264 / EV67 emulator), V4 active tree
Architect: Timothy Peer.  AI collaboration: Claude / Anthropic.
Date: 2026-06-29.
ASCII(128) only.
-->

# GuestMemory Diagnostic Instrumentation -- sink-level store-watch + retire-trace arm

**Date:** 2026-06-29
**Status:** DESIGN + IMPLEMENTATION SPEC. EmulatR-only. Compile-guarded
(`EMULATR_DIAGNOSTIC_LOGGING`: present in Debug/RelWithDebInfo, ABSENT in Release),
runtime default-OFF, observe-only.

---

## 0. Why

EmulatR is the tool of record for firmware execution analysis -- more reliable than
Ghidra/IDA on the V7.3-2 image, and the only source of a faithful executed-instruction
stream. Two recent findings forced this:

1. The `MemDrainer` store-watch (`EMULATR_PA_WATCH`) NEVER fires on the HWRPB-build
   stores, even though the scan proves SYSVAR=0x405 lands at PA 0x2058. The HWRPB-build
   stores reach DRAM via a path that bypasses the `MemDrainer` drain-watch point. So
   that watch is blind to exactly the stores we need.
2. Silicon-path runs take 30-40 min; iterating with narrow one-off levers is too
   expensive. We want to capture a broad, reusable artifact in a single pass.

The fix for (1): put the store-watch at the **universal sink** -- `GuestMemory`, where
*every* committed DRAM byte lands (the chipset arbiter routes all DRAM writes here).
Nothing can bypass it. The answer to (2): instrument freely but under a compile guard so
Release is byte-for-byte unaffected (the bloat compiles away) -- faithfulness never moves.

## 1. Discipline (non-negotiable)

- **Compile-guarded:** all new code under `#if defined(EMULATR_DIAGNOSTIC_LOGGING)`.
  Defined in Debug + RelWithDebInfo, NOT in Release (matches the existing macro-guard
  convention; see the "CMake Macro Guards" H&M topic). In Release the instruments are
  zero bytes / zero cycles / no branch.
- **Runtime default-OFF:** each instrument is silent until its env lever is set, so a
  RelWithDebInfo build with the levers unset behaves like Release.
- **Observe-only:** the hooks log/arm; they NEVER mutate guest state. Because the guest
  is cycle-deterministic regardless of build, a RelWithDebInfo capture reflects the exact
  guest behavior Release would produce -- only host wall-clock differs.

## 2. The two instruments (both at the GuestMemory commit sink)

### 2a. Sink-level store-watch -- `EMULATR_GMEM_WATCH=<pa>`
Logs every store whose byte range overlaps the watched quadword, at the `GuestMemory`
sink. Catches what the `MemDrainer` watch misses (e.g. the HWRPB SYSVAR store at 0x2058).
Output (stderr): `GMEM-WATCH(0x<pa>) STORE pa=0x.. sz=N v=0x..`.

### 2b. Store-triggered retire-trace arm -- `EMULATR_TRACE_ARM_PA=<pa>` [+ VAL] [+ INSTRS]
When a store to `EMULATR_TRACE_ARM_PA` (optionally only when the stored value ==
`EMULATR_TRACE_ARM_VAL`) commits, arm the existing windowed retire trace for
`EMULATR_TRACE_ARM_INSTRS` retired instructions (default 8,000,000) by calling
`traceLib::DecListingSink::setTraceWindowCountdown(N)`. Reuses the existing `.trc`
emission machinery -- no new trace format. Output marker (stderr):
`GMEM-TRACE-ARM pa=0x.. v=0x.. -> retire window N instrs`.

**HWRPB-build capture (the immediate use):** arm on the HWRPB self-pointer store that
opens `build_hwrpb` (apisrm hwrpb.c:371 `hwrpb->BASE = virt_to_phys(hwrpb)` => store of
0x2000 to PA 0x2000, immediately after the `memset` zero-fill). The window then captures
`get_sysvar()` -> `fopen("iic_ocp0")` -> the member decision -> the SYSVAR store, as a
real executed-instruction `.trc`.

## 3. Required environment levers (prior to + during execution)

PREREQUISITE: build **RelWithDebInfo** (or Debug). In a Release build these instruments
are compiled out and the levers do nothing.

| Lever | Role | When |
| ----- | ---- | ---- |
| `EMULATR_TRACE_WINDOW=1` | construct the windowed `DecListingSink` (.trc open, emission window-gated). REQUIRED for 2b. | prior |
| `EMULATR_RETIRE_TRACE_DIR=<dir>` | output directory for the `.trc` / breakpoint / profiler files | prior |
| `EMULATR_TRACE_ARM_PA=0x2000` | arm trigger = HWRPB self-pointer store (build_hwrpb entry) | prior |
| `EMULATR_TRACE_ARM_VAL=0x2000` | only when stored value == 0x2000 (skips the memset zero) | prior |
| `EMULATR_TRACE_ARM_INSTRS=8000000` | retire-window length (tune to cover get_sysvar) | prior |
| `EMULATR_GMEM_WATCH=0x2058` | also log the SYSVAR store at the sink | prior |
| `EMULATR_IIC_TRACE=1` | correlate the fopen("iic_ocp0") path with bus activity | prior |
| `EMULATR_FLASH_ROM=ds20_flash.rom` + `rm -f ds20_flash.rom` | factory-fresh cold NVRAM | prior |
| `EMULATR_PLATFORM=silicon` | optional: silicon path (unset = ISP) | prior |

Run recipe (ISP example), from the RelWithDebInfo run dir:
```
source tools/vsenv.sh
cmake --build out/build/<relwithdebinfo-dir>          # NOT a Release config
export EMULATR_TRACE_WINDOW=1
export EMULATR_RETIRE_TRACE_DIR=./traces
export EMULATR_TRACE_ARM_PA=0x2000
export EMULATR_TRACE_ARM_VAL=0x2000
export EMULATR_TRACE_ARM_INSTRS=8000000
export EMULATR_GMEM_WATCH=0x2058
export EMULATR_IIC_TRACE=1
export EMULATR_FLASH_ROM=ds20_flash.rom; rm -f ds20_flash.rom
unset EMULATR_PLATFORM
./run_fw.sh ds20 cold 2>&1 | tee fw_ds20_hwrpbtrace.out
```
Read-off: the `.trc` in `EMULATR_RETIRE_TRACE_DIR` holds the executed stream from
build_hwrpb entry; grep it for the iic_ocp0 open path and the branch after `fopen`. The
`GMEM-WATCH(0x2058)` line gives the SYSVAR store; correlate against `addr=0x40` in the IIC
trace to settle whether get_sysvar's open reached the bus.

## 4. Exact diff (implement this)

File: `memoryLib/GuestMemory.cpp` (EmulatR-only; all additions guarded).

1. After the existing includes (`#include <cstring>`), add:
```cpp
#if defined(EMULATR_DIAGNOSTIC_LOGGING)
#  include <cstdlib>                       // std::getenv / std::strtoull
#  include "traceLib/DecListingSink.h"     // setTraceWindowCountdown (retire-window arm)
#endif
```
2. Inside `namespace memoryLib {`, BEFORE `GuestMemory::writeBlock`, add the guarded
   file-local helper `gmemDiagOnStore(pa, value, size)` (static; internal linkage):
   reads the four env levers once (function-local statics), does the overlap-logged
   store-watch, and the value-matched trace-arm via
   `traceLib::DecListingSink::setTraceWindowCountdown(...)`. (Full body below.)
3. Call it from each writer, just before `return MemStatus::Ok;` (or per chunk in
   writeBlock), each call wrapped in `#if defined(EMULATR_DIAGNOSTIC_LOGGING)`:
   - `write1`: `gmemDiagOnStore(pa, value, 1);`
   - `write2`: `gmemDiagOnStore(pa, value, 2);`
   - `write4`: `gmemDiagOnStore(pa, value, 4);`
   - `write8`: `gmemDiagOnStore(pa, value, 8);`
   - `writeBlock`: inside the loop after `markDirty(pidx);`,
     `gmemDiagOnStore(addr, 0, static_cast<unsigned>(chunk));`

Helper body:
```cpp
#if defined(EMULATR_DIAGNOSTIC_LOGGING)
    // Observe-only diagnostic hook at the universal DRAM commit sink.  Catches
    // stores that bypass the MemDrainer drain-watch (e.g. the HWRPB build).
    static inline void gmemDiagOnStore(uint64_t pa, uint64_t value,
                                       unsigned size) noexcept {
        static uint64_t const s_watchPa = []() -> uint64_t {
            char const* e = std::getenv("EMULATR_GMEM_WATCH");
            return (e && *e) ? std::strtoull(e, nullptr, 0) : 0ULL; }();
        static uint64_t const s_armPa = []() -> uint64_t {
            char const* e = std::getenv("EMULATR_TRACE_ARM_PA");
            return (e && *e) ? std::strtoull(e, nullptr, 0) : 0ULL; }();
        static bool const s_armValSet =
            (std::getenv("EMULATR_TRACE_ARM_VAL") != nullptr);
        static uint64_t const s_armVal = []() -> uint64_t {
            char const* e = std::getenv("EMULATR_TRACE_ARM_VAL");
            return (e && *e) ? std::strtoull(e, nullptr, 0) : 0ULL; }();
        static int64_t const s_armInstrs = []() -> int64_t {
            char const* e = std::getenv("EMULATR_TRACE_ARM_INSTRS");
            return (e && *e) ? std::strtoll(e, nullptr, 0) : 8000000LL; }();

        if (s_watchPa != 0ULL) {
            uint64_t const qw  = s_watchPa & ~7ULL;
            uint64_t const sHi = pa + (size ? size : 1u);
            if (pa < qw + 8ULL && sHi > qw) {
                std::fprintf(stderr,
                    "GMEM-WATCH(0x%llx) STORE pa=0x%llx sz=%u v=0x%llx\n",
                    (unsigned long long)s_watchPa, (unsigned long long)pa,
                    size, (unsigned long long)value);
                std::fflush(stderr);
            }
        }
        if (s_armPa != 0ULL && pa == s_armPa &&
            (!s_armValSet || value == s_armVal)) {
            traceLib::DecListingSink::setTraceWindowCountdown(s_armInstrs);
            std::fprintf(stderr,
                "GMEM-TRACE-ARM pa=0x%llx v=0x%llx -> retire window %lld instrs\n",
                (unsigned long long)pa, (unsigned long long)value,
                (long long)s_armInstrs);
            std::fflush(stderr);
        }
    }
#endif
```

## 5. Build / verify note
- Build config MUST define `EMULATR_DIAGNOSTIC_LOGGING` (Debug or RelWithDebInfo).
  In Release the additions vanish. Confirm by setting `EMULATR_GMEM_WATCH=0x2058` on a
  cold boot: a `GMEM-WATCH` line proves the instrument is compiled in.
- Cannot compile in the Cowork sandbox (no MSVC/Qt); build natively. If the
  `traceLib/DecListingSink.h` include creates an unexpected memoryLib->traceLib link
  cycle, fall back to hoisting `setTraceWindowCountdown`'s atomic into a tiny standalone
  header; not expected (DecListingSink is a leaf sink).
- Run `Emulatr_tests` after building (expect green; the additions are guarded + observe-only).

## 6. Faithfulness statement
Release is untouched (guarded out). RelWithDebInfo with levers unset == Release behavior.
Instruments are observe-only, so any captured `.trc` is the genuine deterministic guest
stream. This is the first reusable piece of the EmulatR tool-of-record instrumentation.

## 7. ADDENDUM 2026-06-29b -- earlier arm (IIC) + store-disarm (capture iic_init)

The 0x2000-armed window proved get_sysvar returns member 1 with NO IIC bus traffic ->
fopen("iic_ocp0") fails at device-TABLE LOOKUP (never reaches an open/probe); the device
was never registered by `iic_init` (runs earlier, ~cyc 185M, the addr=0x40 dir=R that
returned status != 1). To capture THAT, we arm earlier and bound the window.

KEY CONSTRAINT: the GuestMemory sink only sees DRAM commits. The IIC controller
(PCF8584) is MMIO -- it routes to the device model, NOT through GuestMemory -- so we
cannot arm on an IIC PA via the store-watch. The arm must come from the IIC model.

Two new compile-guarded (`EMULATR_DIAGNOSTIC_LOGGING`), observe-only levers:

1. IIC-model arm -- `deviceLib/Tsunami/IicPcf8584.h`, in `startTransaction`:
   `EMULATR_TRACE_ARM_ON_IIC=<node|0|1>`. Fires once, on the first START transaction
   (0/1 = any address; else only when `(addr & 0xFE) == (node & 0xFE)`, e.g. `0x40` for
   the first status-controller probe). Calls `setTraceWindowCountdown(EMULATR_TRACE_ARM_INSTRS)`.
   Marker: `IIC-TRACE-ARM node=0x.. -> retire window N instrs`. (The boot-time iic_acks
   latch is host-side configureDevices, not a guest START, so it will NOT false-trigger;
   the first guest START is iic_init at ~cyc 185M.)
2. Store-disarm -- `memoryLib/GuestMemory.cpp`, in `gmemDiagOnStore`:
   `EMULATR_TRACE_DISARM_PA=<pa>` (+ optional `EMULATR_TRACE_DISARM_VAL=<v>`). A store to
   <pa> sets the countdown to 0 (closes the window). Marker:
   `GMEM-TRACE-DISARM pa=0x.. v=0x.. -> retire window closed`. Use `0x2000`/`VAL=0x2000`
   (the HWRPB base store = build_hwrpb entry) as a hard backstop bound.

VOLUME NOTE: iic_init (~185M) -> build_hwrpb (~222M) is ~37M cycles of unrelated POST.
Spanning the whole thing to the 0x2000 disarm is ~35-40M lines (~9 GB). For the
registration question only the few-thousand instrs around the 0x40 probe matter, so
prefer a modest `EMULATR_TRACE_ARM_INSTRS` (2-4M) and keep DISARM_PA=0x2000 as a backstop.

Recipe (iic_init registration capture), from the RelWithDebInfo run dir
(build with `-DEMULATR_TRACE_HOOKS=ON`):
```
export EMULATR_TRACE_WINDOW=1
export EMULATR_RETIRE_TRACE_DIR=./traces
export EMULATR_TRACE_ARM_ON_IIC=0x40        # arm on first 0x40 (status) probe = iic_init
export EMULATR_TRACE_ARM_INSTRS=4000000     # ~4M instrs covers the registration sequence
export EMULATR_TRACE_DISARM_PA=0x2000       # backstop: close at build_hwrpb entry
export EMULATR_TRACE_DISARM_VAL=0x2000
export EMULATR_GMEM_WATCH=0x2058
export EMULATR_IIC_TRACE=1
export EMULATR_FLASH_ROM=ds20_flash.rom; rm -f ds20_flash.rom
unset EMULATR_PLATFORM
./run_fw.sh ds20 cold 2>&1 | tee fw_ds20_iicinit_trace.out
```
Read-off: the `.trc` now holds iic_init's `iic_rw_common` probe of node 0x40 -- look for
the `rec_count`/status the registration gates on (the path that decides whether iic_ocp0
is registered). That is the actual fix site for the DS20 -> AlphaPC 264DP badge fallback.
