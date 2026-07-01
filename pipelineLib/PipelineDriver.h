// ============================================================================
// pipelineLib/PipelineDriver.h -- sequential pipeline driver for V4 v1
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
// PipelineDriver runs one in-flight slot at a time through all six
// stages -- IF, DE, GR, EX, MEM, WB -- before fetching the next.
// This is the V4 v1 cut: deterministic, single-issue, no
// speculation, no out-of-order, 100 percent mispredicted (any
// branch-divert squashes nothing because nothing was speculated).
// A multi-slot pipeline with bypass forwarding lands later when a
// real workload is exercising perf trade-offs; until then,
// "sequential through six stages" satisfies the contract.
//
// Stage map:
//
//   IF  -- mint a slot from cpu.pc; translate VA to PA via the
//          instruction-stream translator; read 4 bytes from
//          GuestMemory; populate slot.grain.pc and grain.encoded.
//   DE  -- dispatch lookup against grainFactory::g_primaryTable
//          plus the per-kind sub-tables / sparse helpers; populate
//          grain.execFn, grain.semFlags, grain.box.
//   GR  -- build ExecCtx: read regfile (intReg or fpReg per
//          S_ReadsFp / S_ReadsInt) into ctx.opA / opB; copy palMode
//          and cycleCount; install the CpuState* escape hatch.
//   EX  -- invoke grain.execFn(grain, ctx) and store the returned
//          BoxResult on slot.result.
//   MEM -- pipelineLib::MemDrainer::drain(slot, cpu, memory).
//          Applies memEffect, sign-extends loads, commits the
//          regfile write, manages the LDx_L / STx_C reservation.
//   WB  -- advance cpu.pc from {grain.pc, divertTarget, faultCode}.
//          On kFaultHalt set cpu.halted = true.  On any other
//          non-zero faultCode: deliver to PALcode trap entry if
//          palBase is set (entryForFault maps the fault code to the
//          AARM-canonical entry vector; PC <- palBase + offset,
//          excAddr captures faulting PC + pre-trap palMode bit,
//          palMode <- true), else fall back to halt-with-diagnostic.
//          v1 records grain.pc to excAddr and
//          halts (no PAL trap delivery yet -- when PAL lands the
//          retire path becomes "set excAddr, divert to PAL_BASE +
//          vector").  Increment cpu.cycleCount.
//
// Two entry points:
//
//   step  -- runs one slot through all six stages; returns false
//            when the CPU has halted.
//   run   -- loops calling step until halted or maxCycles reached.
//
// ============================================================================

#ifndef PIPELINELIB_PIPELINEDRIVER_H
#define PIPELINELIB_PIPELINEDRIVER_H

#include <cstdint>
#include <cstdio>   // TEMP probes 2026-06-01/02 (fprintf) -- revert with probes
#include <cstdlib>  // TEMP probes 2026-06-02 (getenv, tick-warp) -- revert with probes

#include "coreLib/BoxResult.h"
#include "coreLib/CpuState.h"
#include "coreLib/DispatchEntry.h"
#include "coreLib/Ev6EntryVectors.h"
#include "coreLib/ExecCtx.h"
#include "coreLib/FaultEventLog.h"
#include "coreLib/PalShadow.h"
#include "coreLib/InstructionGrain.h"
#include "coreLib/PipelineSlot.h"
#include "coreLib/VA_types.h"
#include "coreLib/axp_attributes_core.h"

#include "grainFactoryLib/DispatchAccess.h"
#include "grainFactoryLib/generated/DispatchKinds.h"
#include "grainFactoryLib/generated/SemanticFlagsEnum.h"

#include "memoryLib/GuestMemory.h"
#include "memoryLib/ISystemBus.h"
#include "mmuLib/Ev6Translator.h"
#include "mmuLib/TranslationResult.h"
#include "pipelineLib/IFetchOverride.h"
#include "pipelineLib/MemDrainer.h"

#include "traceLib/CommitRecord.h"
#include "traceLib/DecListingSink.h"   // 2026-06-01: setTraceWindowCountdown (window arm)
#include "traceLib/RetireProfiler.h"   // 2026-06-05: always-on boot profiler
#include "traceLib/TraceSink.h"


namespace pipelineLib {

struct PipelineDriver
{
    // -------------------------------------------------------------
    // step
    //
    //   Runs one in-flight slot through all six stages and updates
    //   cpu / memory accordingly.  Returns true if the CPU is still
    //   running afterwards, false if it has halted.
    // -------------------------------------------------------------
    // EMULATR_DEBUG_STEP: leftover debug aid -- disables optimization on the
    // hot step() so the optimizer doesn't elide locals during single-step
    // debugging.  DEFAULT OFF so the 82%-of-runtime hot loop ships at the
    // build's -O2 (it was previously always -O0 here; PERF #0, 2026-06-10).
    // Define EMULATR_DEBUG_STEP (CMake option) to restore the -O0 debug build.
#ifdef EMULATR_DEBUG_STEP
#pragma optimize("", off)
#endif
    AXP_HOT AXP_FLATTEN
    static bool step(coreLib::CpuState&     cpu,
                     memoryLib::ISystemBus& bus, memoryLib::GuestMemory& mem,
                     traceLib::TraceSink*   sink           = nullptr,
                     IFetchOverride*        fetchOverride  = nullptr) noexcept
    {
        if (cpu.halted) {
            return false;
        }

        coreLib::PipelineSlot slot{};
        slot.grain.pc    = cpu.pc;
        slot.cycleIssued = cpu.cycleCount;
        slot.valid       = true;
        uint64_t debug8 = 0;
        uint32_t debug4 = 0;
        uint16_t debug2 = 0;
        uint8_t debug1 = 0;

        // -----------------------------------------------------
        // IF: translate cpu.pc and read the instruction word.
        //
        // The fetch path consults the optional IFetchOverride first.
        // V1's documented use is the SRM-decompressor I-cache
        // coherency: when the firmware's copy loop overwrites its
        // own stub instructions in guest memory, the override serves
        // the original bytes from an immutable buffer so the IBox
        // does not decode garbage.  When override is null or returns
        // false the GuestMemory.read4 path runs as before.
        // -----------------------------------------------------
        coreLib::PAType pa = 0;
        mmuLib::TranslationResult const itr = mmuLib::Ev6Translator::translateInstruction(cpu, cpu.pc, pa);

#if EMULATR_MEMDIAG
        // I-side fetch translation event (cycle window shared with MemDrainer.h).
        // pc carries the PALmode bit in PC<0>; pal= is the decoded mode.  This
        // is the line that exposes the VPN 0x301 -> PFN 0x300 code skew: a
        // native fetch of VA 0x6021e8 resolving to PA 0x6001e8 is the I-side
        // half that drain() cannot see.
        if (cpu.cycleCount >= EMULATR_MEMDIAG_CYC_LO &&
            cpu.cycleCount <= EMULATR_MEMDIAG_CYC_HI) {
            std::fprintf(stderr,
                "MEMDIAG-I cyc=%llu pc=0x%016llx pal=%d itr=%u pa=0x%016llx\n",
                static_cast<unsigned long long>(cpu.cycleCount),
                static_cast<unsigned long long>(cpu.pc),
                static_cast<int>(cpu.inPalMode()),
                static_cast<unsigned>(itr),
                static_cast<unsigned long long>(pa));
        }
#endif

        if (itr != mmuLib::TranslationResult::Success) {
            // I-side translation fault (C4): route through the unified
            // retire() trap-delivery path instead of hard-halting, so ITB
            // misses / IACV deliver to PALcode exactly like D-side faults.
            // The faulting fetch VA is the PC.
            cpu.va      = cpu.pc;            // HW_VA = faulting fetch VA
            cpu.mm_stat = cpu.pc;


            slot.result.faultCode = mmuLib::toFaultCode(itr);
            retire(slot, cpu);
            cpu.cycleCount++;
            return !cpu.halted;
        }


       
        // Pre-fetch hook.  Implementations use this to fire one-shot
        // side effects gated on the about-to-fetch PA -- canonically,
        // SRM PAL image relocation (Step D) when the CPU first reaches
        // the post-decompression entry PC.  Default is a no-op virtual
        // call, predicted-untaken on the hot path.
        if (fetchOverride != nullptr) {
            fetchOverride->onBeforeFetch(pa);
        }

        uint32_t encoded = 0;
        bool fetched = false;
        if (fetchOverride != nullptr) {
            fetched = fetchOverride->tryFetch(pa, encoded);
        }
        if (!fetched) {
            memoryLib::BusResult const fr = bus.fetch(pa, 4);
            if (fr.status != memoryLib::BusStatus::Ok) {
                // I-side fetch bus error (C4): deliver via retire() (MCHK)
                // instead of hard-halting -- one unified trap path.
                cpu.va      = cpu.pc;
                cpu.mm_stat = cpu.pc;
                slot.result.faultCode = coreLib::kFaultBusError;
                retire(slot, cpu);
                cpu.cycleCount++;
                return !cpu.halted;
            }
            encoded = static_cast<uint32_t>(fr.data);
        }

        // 2026-05-31: FETCH-FIXUP removed.  It was based on the disproven
        // "0xd954 byte is corrupt, must be hw_ret(R6)" hypothesis.  The word
        // IS genuinely hw_ret(R2) (source-built decompressor oracle confirmed);
        // R2 is the SCB-dispatch handler PC, now populated by the SCBB
        // S_PalEntry fix (MTPR_SCBB writes guest PT__SCBB).  0xd954 must run
        // its real R2 so we actually exercise that fix, not the dead R6 hack.

        slot.grain.encoded   = encoded;
        slot.grain.primaryOp = static_cast<uint8_t>((encoded >> 26) & 0x3Fu);

        // ---- TEMP relocation/window probe 2026-06-01 (REMOVE after capture) ----
        // Bounded retire-window dump around the UART-write entry captured live at
        // cyc 0xaa461cf2 / PC 0x1c6a80. Logs VA(pc), translated PA, the encoded
        // instruction word, GP(R29), RA(R26). Purpose: match runtime text against
        // the static decompressed ROM (expect PA, not VA, to match) + lock GP.
        // Window = [entry-before, entry+after]; ~2 compares/instr, bounded output.
#if EMULATR_BRINGUP_PROBES
        {
            constexpr uint64_t kProbeEntry = 0xaa461cf2ull;  // captured UART-entry cyc
            constexpr uint64_t kProbeBefore = 20;             // your "20 before"
            constexpr uint64_t kProbeAfter = 500;            // "some after" -- widen freely
            if (cpu.cycleCount + kProbeBefore >= kProbeEntry &&
                cpu.cycleCount <= kProbeEntry + kProbeAfter) {
                std::fprintf(stderr,
                    "PCWIN cyc=%llu pc=0x%016llx pa=0x%016llx enc=0x%08x gp=0x%016llx ra=0x%016llx\n",
                    static_cast<unsigned long long>(cpu.cycleCount),
                    static_cast<unsigned long long>(cpu.pc),
                    static_cast<unsigned long long>(pa),
                    static_cast<unsigned>(encoded),
                    static_cast<unsigned long long>(cpu.intReg[29]),
                    static_cast<unsigned long long>(cpu.intReg[26]));
                std::fflush(stderr);
            }
        }
#endif
        // ---- END TEMP probe ----

        // ---- TEMP one-shot trace-window arm 2026-06-01 (REMOVE after capture) ----
        // Arms DecListingSink's retire-trace window ~20 instr before the UART entry so
        // the next kTraceLen retires are logged to the _srm.trc (decoded mnemonic, reg
        // write, ld/st va/pa/value -- richer than PCWIN). Run with EMULATR_TRACE_WINDOW=1
        // and NO --trace so the global firehose stays off; only this bounded window is
        // captured. One-shot: arms once at/after the threshold, counts down, self-disables.
        {
            constexpr uint64_t kTraceArmCyc = 0xaa461cf2ull - 20;
            constexpr int64_t  kTraceLen    = 600;
            static bool s_traceArmed = false;
            if (!s_traceArmed && cpu.cycleCount >= kTraceArmCyc) {
                s_traceArmed = true;
                traceLib::DecListingSink::setTraceWindowCountdown(kTraceLen);
            }
        }
        // ---- END one-shot trace-window arm ----

        // ---- TEMP tick-delay warp (Task #5) 2026-06-02 -- arm with EMULATR_TICKWARP=1 ----
        // Fast-forwards the console-init tick-counted real-time delays. At the delay-loop
        // compare (PC 0x7c314: CMPLT r0,r6,r0; loop while counter<r6): R5 = counter PA
        // (0x3c970), R6 = target ticks, R0 = current counter (just loaded by 0x7c310).
        // We advance BOTH the tick counter and cycleCount coherently (RSCC stays consistent)
        // so a multi-second wait collapses to one step. Reads R6 live -> handles any target.
        // Self-limiting (loop re-reads target next pass and exits) + thresholded (long waits
        // only). Checksum: advanced cycles must equal skipped ticks * interval. Off unless
        // EMULATR_TICKWARP set. REMOVE/replace when Task #5 hardens into the registry form.
        {
            // QUARANTINED 2026-06-12: these warps jump cycleCount past many tick
            // boundaries in one step AND rewrite the 0x3c970 counter out-of-band
            // -- the confirmed cause of the overnight 0x7f4xx boot corruption.
            // Moved off EMULATR_TICKWARP onto EMULATR_RSCCWARP (off by default).
            // The clean replacement is the single-tick idle-warp in Machine.cpp
            // (EMULATR_IDLEWARP), which advances cycleCount one tick at a time and
            // lets the real ISR increment 0x3c970 -- no out-of-band rewrite.
            static const bool s_tickWarp = (std::getenv("EMULATR_RSCCWARP") != nullptr);
            // One-shot diagnostics: prove (a) the env is seen and (b) the gate PC is hit.
            static bool s_warpArmAnnounced = false;
            if (!s_warpArmAnnounced) {
                s_warpArmAnnounced = true;
                std::fprintf(stderr, "TICKWARP: armed=%d (EMULATR_TICKWARP %s)\n",
                             s_tickWarp ? 1 : 0,
                             s_tickWarp ? "seen" : "NOT set -- use export in bash");
                std::fflush(stderr);
            }
            constexpr uint64_t kTickInterval  = (1ull << 20);   // cycles per tick (2^20)
            constexpr uint64_t kWarpThreshold = 8;              // skip only waits > 8 ticks
            if (cpu.pcAddr() == 0x000000000007c314ull) {
                static bool s_gateHitAnnounced = false;
                if (!s_gateHitAnnounced) {
                    s_gateHitAnnounced = true;
                    std::fprintf(stderr,
                        "TICKWARP: gate 0x7c314 first reached cyc=%llu R0=%llu "
                        "R5=0x%llx R6=%llu\n",
                        static_cast<unsigned long long>(cpu.cycleCount),
                        static_cast<unsigned long long>(cpu.intReg[0]),
                        static_cast<unsigned long long>(cpu.intReg[5]),
                        static_cast<unsigned long long>(cpu.intReg[6]));
                    std::fflush(stderr);
                }
            }
            if (s_tickWarp && cpu.pcAddr() == 0x000000000007c314ull) {
                uint64_t const cntAddr = cpu.intReg[5];
                uint64_t const target  = cpu.intReg[6];
                uint64_t const cur     = cpu.intReg[0];
                if (target > cur && (target - cur) > kWarpThreshold) {
                    uint64_t const delta = target - cur;
                    uint64_t const c0    = cpu.cycleCount;
                    (void)bus.write(cntAddr, target, 4);        // counter -> target ([[nodiscard]])
                    cpu.cycleCount += delta * kTickInterval;    // advance time coherently
                    cpu.warpCycles += delta * kTickInterval;    // WARP accounting (2026-06-30)
                    bool const checksumOk =
                        (cpu.cycleCount - c0) == (delta * kTickInterval);
                    std::fprintf(stderr,
                        "TICKWARP cyc=%llu->%llu addr=0x%llx from=%llu to=%llu "
                        "delta=%llu interval=%llu checksum=%s\n",
                        static_cast<unsigned long long>(c0),
                        static_cast<unsigned long long>(cpu.cycleCount),
                        static_cast<unsigned long long>(cntAddr),
                        static_cast<unsigned long long>(cur),
                        static_cast<unsigned long long>(target),
                        static_cast<unsigned long long>(delta),
                        static_cast<unsigned long long>(kTickInterval),
                        checksumOk ? "OK" : "FAIL");
                    std::fflush(stderr);
                }
            }
        }
        // ---- END TEMP tick-delay warp ----

        // ---- RSCC-deadline warp at 0x7c304 (Task #5) 2026-06-02 -- arm EMULATR_TICKWARP ----
        // Decoded from the DEADLINE dumps: at 0x7c304, R19 = elapsed (currentSCC - startSCC,
        // climbs ~1:1 with cycles), R07 = duration to wait; loop exits when R19 >= R07 (CMPLT
        // at 0x7c304, BEQ at 0x7c308). This is a pure RSCC time-delay (e.g. ~260M cyc).
        // WARP: advance cycleCount by the remaining (R07-R19) so the next rscc read makes
        // elapsed >= duration and the loop exits; bump the 0x3c970 tick counter coherently by
        // remaining/2^20 so uptime-in-ticks stays consistent with cycleCount. Self-limiting
        // (one more iteration exits), thresholded (> 1 tick), checksummed. REMOVE/registry-ize
        // when Task #5 hardens.
        static const bool s_tickWarpRscc = (std::getenv("EMULATR_RSCCWARP") != nullptr); // QUARANTINED (rewrites 0x3c970)
        if (s_tickWarpRscc && cpu.pcAddr() == 0x7c304ull) {
            uint64_t const elapsed  = cpu.intReg[19];
            uint64_t const duration = cpu.intReg[7];
            constexpr uint64_t kRsccWarpThresh = (1ull << 20);   // skip only waits > 1 tick
            if (duration > elapsed && (duration - elapsed) > kRsccWarpThresh) {
                uint64_t const delta  = duration - elapsed;
                uint64_t const c0     = cpu.cycleCount;
                cpu.cycleCount += delta;                          // advance time to the deadline
                cpu.warpCycles += delta;                          // WARP accounting (2026-06-30)
                uint64_t const dticks = delta >> 20;              // ticks coherently skipped
                memoryLib::BusResult const rd = bus.read(0x3c970ull, 4);
                (void)bus.write(0x3c970ull,
                                rd.data + dticks, 4);             // keep tick-count coherent
                bool const checksumOk = (cpu.cycleCount - c0) == delta;
                std::fprintf(stderr,
                    "RSCCWARP cyc=%llu->%llu elapsed=%llu duration=%llu delta=%llu "
                    "ticks+=%llu checksum=%s\n",
                    static_cast<unsigned long long>(c0),
                    static_cast<unsigned long long>(cpu.cycleCount),
                    static_cast<unsigned long long>(elapsed),
                    static_cast<unsigned long long>(duration),
                    static_cast<unsigned long long>(delta),
                    static_cast<unsigned long long>(dticks),
                    checksumOk ? "OK" : "FAIL");
                std::fflush(stderr);
            }
        }
        // ---- END RSCC-deadline warp ----

        // ---- 0x7bef0 software-tick loop register dump (2026-06-08, #21) ----
        // The post-GCT/FRU + pre-dva0 + dva0 stalls are ONE loop: pc=0x7bef0
        // stores 0x3c970=counter+1 once per ~2^18 cyc; NOT covered by the RSCC/
        // tick warps (no rscc wrapper). Dump all 32 int regs on the first 4 hits
        // so the COUNTER (increments by 1 across dumps) and the TARGET (stays
        // constant) registers can be identified -> then warp the loop. Gated on
        // EMULATR_TICKWARP (already passed on warp runs). Strip after #21 lands.
        {
            static const bool s_c970dump = (std::getenv("EMULATR_RSCCWARP") != nullptr); // diagnostic, moved with the quarantined warps
            static int s_c970dumps = 0;
            if (s_c970dump && s_c970dumps < 4 && cpu.pcAddr() == 0x000000000007bef0ull) {
                ++s_c970dumps;
                std::fprintf(stderr, "C970DUMP #%d pc=0x7bef0 cyc=%llu",
                             s_c970dumps,
                             static_cast<unsigned long long>(cpu.cycleCount));
                for (int i = 0; i < 32; ++i) {
                    std::fprintf(stderr, " R%d=0x%llx", i,
                                 static_cast<unsigned long long>(cpu.intReg[i]));
                }
                std::fprintf(stderr, "\n");
                std::fflush(stderr);
            }
        }
        // ---- END 0x7bef0 dump ----

        // ---- General RSCC-spin warp (Task #5) 2026-06-02 -- arm EMULATR_TICKWARP ----
        // The firmware does MANY real-time delays at different PCs, each spinning on the
        // rscc wrapper (0x1c655c) comparing elapsed-SCC to a per-loop duration. Rather than
        // gate every loop, detect the spin generically: the wrapper called repeatedly from
        // the SAME 4KB caller-page (resets when the page changes = real forward progress).
        // Once confirmed (>256 calls), inject one tick (2^20 cyc) of cycleCount per wrapper
        // call so each iteration's SCC read climbs ~1 tick and the loop's own deadline fires
        // in O(duration_ticks) iterations instead of grinding every cycle. Bump 0x3c970
        // coherently. Overshoot <= ~1 tick. The precise 0x7c304 warp above wins where it
        // applies (fires before 256 calls); this is the catch-all for other delay loops.
        {
            static const bool s_spinWarp = (std::getenv("EMULATR_RSCCWARP") != nullptr); // QUARANTINED (rewrites 0x3c970)
            if (s_spinWarp && cpu.pcAddr() == 0x1c655cull) {
                static uint64_t s_lastPage = ~0ull;
                static uint64_t s_spinCnt  = 0;
                uint64_t const page = cpu.intReg[26] & ~0xFFFull;   // caller 4KB page
                if (page == s_lastPage) { ++s_spinCnt; }
                else { s_lastPage = page; s_spinCnt = 0; }
                if (s_spinCnt > 256) {                              // confirmed delay spin
                    constexpr uint64_t kChunk = (1ull << 20);       // inject 1 tick / call
                    cpu.cycleCount += kChunk;
                    memoryLib::BusResult const rd = bus.read(0x3c970ull, 4);
                    (void)bus.write(0x3c970ull, rd.data + 1, 4);    // coherent +1 tick
                    static uint64_t s_spinLog = 0;
                    if ((s_spinLog++ & 0x3FFull) == 0) {            // throttle 1/1024
                        std::fprintf(stderr,
                            "SPINWARP cyc=%llu caller_page=0x%llx injections~%llu\n",
                            static_cast<unsigned long long>(cpu.cycleCount),
                            static_cast<unsigned long long>(page),
                            static_cast<unsigned long long>(s_spinLog));
                        std::fflush(stderr);
                    }
                }
            }
        }
        // ---- END General RSCC-spin warp ----

        // ---- TEMP periodic PC sampler 2026-06-02 -- what is the current grind doing? ----
        // Two delays warped, but the boot keeps finding new grinds. Sample the native PC
        // every ~4M cycles: if it clusters at a few PCs -> another delay/spin loop (warpable);
        // if it ranges widely -> real work (memory test/probe, must run, don't warp). Also
        // shows the caller of the rscc wrapper if that's where it sits. REMOVE after.
#if EMULATR_BRINGUP_PROBES
        if ((cpu.cycleCount & 0x3FFFFFull) == 0) {   // every 4,194,304 cycles
            std::fprintf(stderr, "PCSAMPLE cyc=%llu pc=0x%llx pal=%d ra=0x%llx\n",
                static_cast<unsigned long long>(cpu.cycleCount),
                static_cast<unsigned long long>(cpu.pcAddr()),
                cpu.inPalMode() ? 1 : 0,
                static_cast<unsigned long long>(cpu.intReg[26]));
            std::fflush(stderr);
        }
#endif
        // ---- END periodic PC sampler ----

#if EMULATR_MEMDIAG
        // TEMP DIAG 2026-05-30 (clock-return fetch source) -- REMOVE BEFORE COMMIT.
        // The hw_ret at PC 0xd954 decoded as enc=0x7be2a000 (hw_ret R2 -> R2=0
        // -> halt), but the handler put the resume PC in R6, so the real word
        // should be 0x7be6a000 (hw_ret R6).  Did a stale IFetchOverride serve
        // us the wrong word, or does the bus/memory itself hold it?  Re-read
        // the bus and compare against the served `encoded`.
        if (cpu.pcAddr() == 0x000000000000d954ull) {
            memoryLib::BusResult const busWord = bus.fetch(pa, 4);
            std::fprintf(stderr,
                "MEMDIAG-FETCH cyc=%llu pcAddr=0xd954 pa=0x%016llx "
                "override_present=%d served_by_override=%d encoded=0x%08x "
                "bus_word=0x%08x bus_status=%d\n",
                static_cast<unsigned long long>(cpu.cycleCount),
                static_cast<unsigned long long>(pa),
                static_cast<int>(fetchOverride != nullptr),
                static_cast<int>(fetched),
                static_cast<unsigned>(encoded),
                static_cast<unsigned>(static_cast<uint32_t>(busWord.data)),
                static_cast<int>(busWord.status));
            std::fflush(stderr);
        }
#endif

      

        // -----------------------------------------------------
        // DE: dispatch lookup.
        // -----------------------------------------------------
        coreLib::GrainEntry const& entry = decode(encoded, cpu);
        slot.grain.execFn   = entry.fn;
        slot.grain.semFlags = entry.semFlags;
        slot.grain.box      = entry.box;

        // HW_LD (0x1b) / HW_ST (0x1f) physical-vs-virtual routing.
        // Per EV6 Spec Rev 2.0 sec 4.1.1 / 4.1.2 the Hw-format Type
        // field is encoded[15:13]; the physical variants are Type 000
        // (Physical) and 001 (Physical/Lock | Physical/Cond) -- i.e.
        // encoded[15:14] == 00.  All other Type values (VPTE 010,
        // Virtual 100, WrChk 101, Alt 110, WrChk/Alt 111) are virtual
        // and MUST translate through the DTB.  We stamp S_PhysAddr on
        // the per-instruction grain here, at decode, so MemDrainer
        // routes the physical data access straight to PA -- bypassing
        // translateData independently of cpu.palMode.  This covers the
        // I_CTL[HWE]=1 case: HW_LD/HW_ST may execute with pal=0, where
        // the palMode bypass would not fire and a physical EA would be
        // wrongly translated.  The decision is a property of the
        // encoding, resolved once at decode and carried on semFlags;
        // the executor leaves do not re-derive it.
        if ((slot.grain.primaryOp == 0x1Bu || slot.grain.primaryOp == 0x1Fu)
            && ((encoded >> 14) & 0x3u) == 0u) {
            slot.grain.semFlags |= grainFactory::GrainSem::S_PhysAddr;
        }

        // Capture the mnemonic literal locally for trace -- the entry
        // reference goes out of scope at the end of step(), but the
        // codegen-emitted string literal it points to has static
        // lifetime.  Cheap to keep around for the trace hook below.
        char const* const mnemonic = entry.mnemonic;

        // -----------------------------------------------------
        // GR: operand resolution.
        // -----------------------------------------------------
        coreLib::ExecCtx ctx = buildCtx(slot.grain, cpu);
        ctx.memory = &mem;   // CSERVE intrinsics read/write guest buffers at EX

        // -----------------------------------------------------
        // EX: invoke leaf.
        // -----------------------------------------------------
        slot.result = slot.grain.execFn(slot.grain, ctx);


#if EMULATR_MEMDIAG
        // VPN-gated I-side miss: what faulting fetch VA does the PAL ITB-miss
        // handler get?  If at the 0x601ffc->0x602000 crossing this reports
        // the prior page (VPN 0x300) instead of 0x602000, the handler will
        // compute PFN 0x300 and mis-install VPN 0x301.  Fires regardless of
        // cycle, so it catches the fill even if it predates the cycle window.

        if (cpu.pcAddr() >= 0x000000000000d954 && cpu.pcAddr() <= 0x000000000000d955) {
            std::fprintf(stderr,
                "MEMDIAG-IMISS cyc=%llu pc=0x%016llx pcAddr=0x%016llx "
                "pal=%d itr=%u va<-0x%016llx mm_stat<-0x%016llx\n",
                static_cast<unsigned long long>(cpu.cycleCount),
                static_cast<unsigned long long>(cpu.pc),
                static_cast<unsigned long long>(cpu.pcAddr()),
                static_cast<int>(cpu.inPalMode()),
                static_cast<unsigned>(itr),
                static_cast<unsigned long long>(cpu.va),
                static_cast<unsigned long long>(cpu.mm_stat));
        }
#endif
        // -----------------------------------------------------
        // MEM: drain memEffect + regfile commit.
        // -----------------------------------------------------
        MemDrainer::drain(slot, cpu, bus, mem.lockMonitor());

        // -----------------------------------------------------
        // WB: PC advance + halt / fault intercept.
        // -----------------------------------------------------
      
#if EMULATR_MEMDIAG
        // ----------------------------------------------------------
        // Disassembly capture: SRM idle-loop body + RSCC PAL handler
        // ----------------------------------------------------------
        // 2026-05-29 -- updated to chase R22 (STL target address) in
        // the RSCC idle loop discovered at 0x1c6560..0x1c6568.
        //
        // Captures TWO PC ranges so we see both the user-mode loop
        // and the PAL handler it calls:
        //   user mode: 0x1c6560..0x1c657f  (CALL_PAL #0x9D, STL, RET)
        //   PAL mode:  0xb740..0xb77f      (RSCC handler entry to HW_REI)
        //
        // Adds R21-R25 to the dump so we can identify R22's value at
        // PC 0x1c6564 (the STL instruction's base register).  R22 is
        // the MMIO address the firmware writes the cycle counter to --
        // identifying that address tells us what external event the
        // SRM is spinning on (Cchip MISC, Pchip wakeup, DRAM hint, ...).
        //
        // Once-per-PC firing via 6-bit hashed mask ((pcA >> 2) & 0x3F).
        // Some collisions possible across the two PC ranges but
        // acceptable -- we just need each unique PC's state once.
        //
        // Remove after the wait latch is identified.
        // ----------------------------------------------------------
        {
            uint64_t const pcA = cpu.pcAddr();
            // 2026-05-29 -- muted post-kCcMultiplier fix.  Re-enable by
            // removing the leading 'false &&' to investigate post-banner.
            if (false && slot.grain.pc >= 0x7bad0 && slot.grain.pc < 0x7bb20)
            {
                // 2026-05-29 -- caller body capture for wedge investigation.
                //   caller [0x7bad0..0x7bb20)  -> bits 0-19 (20 slots)
                //
                // We've already characterized the leaf at 0x1c6560-0x1c6568
                // (CALL_PAL #0x9D RSCC, STL R0,0(R22), RET) and the PAL
                // handler at 0xb740-0xb770 (RPCC + scaling + HW_RET).  The
                // wait condition must be in the CALLER -- the firmware
                // calls the leaf from two BSR sites (R26 alternates between
                // 0x7bad8 and 0x7bafc on consecutive iterations), so the
                // routine doing the timing measurement and the loop-back
                // branch lives in this range.
                //
                // What to look for in the captured PRE/POST stream:
                //   - The instructions BEFORE 0x7bad8 set up R22 (= the
                //     STL target) and BSR into the leaf.
                //   - The instructions BETWEEN 0x7bad8 and 0x7bafc do
                //     the measurement -- this is where the wait latch is.
                //   - The instructions AFTER 0x7bafc test something and
                //     branch back (forming the loop) or fall through
                //     (loop terminates).  The test + branch target IS
                //     the answer to "what is the firmware waiting for".
                static std::atomic<uint64_t> s_firedMask{ 0 };
                uint64_t bit = 0;
                if (slot.grain.pc >= 0x7bad0 && slot.grain.pc < 0x7bb20) {
                    bit = uint64_t{ 1 }
                          << ((slot.grain.pc - 0x7bad0) >> 2);
                }
                if ((s_firedMask.fetch_or(bit,
                    std::memory_order_acq_rel)
                    & bit) == 0)
                {
                    std::fprintf(stderr,
                        "MEMDIAG-DISASM-PRE pc=0x%012llx enc=0x%08x "
                        "R0=0x%016llx R1=0x%016llx R2=0x%016llx "
                        "R3=0x%016llx R4=0x%016llx R5=0x%016llx "
                        "R6=0x%016llx R7=0x%016llx "
                        "R16=0x%016llx R17=0x%016llx "
                        "R20=0x%016llx R21=0x%016llx R22=0x%016llx "
                        "R23=0x%016llx R24=0x%016llx R25=0x%016llx "
                        "R26=0x%016llx R27=0x%016llx "
                        "R29=0x%016llx R30=0x%016llx\n",
                        static_cast<unsigned long long>(pcA),
                        static_cast<unsigned>(encoded),
                        static_cast<unsigned long long>(cpu.intReg[0]),
                        static_cast<unsigned long long>(cpu.intReg[1]),
                        static_cast<unsigned long long>(cpu.intReg[2]),
                        static_cast<unsigned long long>(cpu.intReg[3]),
                        static_cast<unsigned long long>(cpu.intReg[4]),
                        static_cast<unsigned long long>(cpu.intReg[5]),
                        static_cast<unsigned long long>(cpu.intReg[6]),
                        static_cast<unsigned long long>(cpu.intReg[7]),
                        static_cast<unsigned long long>(cpu.intReg[16]),
                        static_cast<unsigned long long>(cpu.intReg[17]),
                        static_cast<unsigned long long>(cpu.intReg[20]),
                        static_cast<unsigned long long>(cpu.intReg[21]),
                        static_cast<unsigned long long>(cpu.intReg[22]),
                        static_cast<unsigned long long>(cpu.intReg[23]),
                        static_cast<unsigned long long>(cpu.intReg[24]),
                        static_cast<unsigned long long>(cpu.intReg[25]),
                        static_cast<unsigned long long>(cpu.intReg[26]),
                        static_cast<unsigned long long>(cpu.intReg[27]),
                        static_cast<unsigned long long>(cpu.intReg[29]),
                        static_cast<unsigned long long>(cpu.intReg[30]));
                    std::fflush(stderr);
                }
            }
        }
#endif
        retire(slot, cpu);
#if EMULATR_MEMDIAG
        // ----------------------------------------------------------
        // Disassembly capture POST-retire -- companion to the PRE block
        // above; same gate range, same hashed mask, same register set.
        // Pre/post pairs let us see (a) the state going INTO each
        // instruction (PRE) and (b) the state AFTER it commits (POST).
        // Delta = what the instruction wrote.
        //
        // For RSCC at PC 0x1c6560: POST shows R0 holding the cycle
        // counter value the firmware will store via the following STL.
        // For STL R0,(R22) at PC 0x1c6564: POST should be identical to
        // PRE (the store has no regfile effect) -- this confirms R22
        // didn't change and we can read its true value.
        // ----------------------------------------------------------
        {
            uint64_t const pcA = cpu.pcAddr();
            // 2026-05-29 -- muted post-kCcMultiplier fix.  Re-enable by
            // removing the leading 'false &&' to investigate post-banner.
            if (false && slot.grain.pc >= 0x7bad0 && slot.grain.pc < 0x7bb20)
            {
                // 2026-05-29 -- caller body capture for wedge investigation.
                //   caller [0x7bad0..0x7bb20)  -> bits 0-19 (20 slots)
                //
                // We've already characterized the leaf at 0x1c6560-0x1c6568
                // (CALL_PAL #0x9D RSCC, STL R0,0(R22), RET) and the PAL
                // handler at 0xb740-0xb770 (RPCC + scaling + HW_RET).  The
                // wait condition must be in the CALLER -- the firmware
                // calls the leaf from two BSR sites (R26 alternates between
                // 0x7bad8 and 0x7bafc on consecutive iterations), so the
                // routine doing the timing measurement and the loop-back
                // branch lives in this range.
                //
                // What to look for in the captured PRE/POST stream:
                //   - The instructions BEFORE 0x7bad8 set up R22 (= the
                //     STL target) and BSR into the leaf.
                //   - The instructions BETWEEN 0x7bad8 and 0x7bafc do
                //     the measurement -- this is where the wait latch is.
                //   - The instructions AFTER 0x7bafc test something and
                //     branch back (forming the loop) or fall through
                //     (loop terminates).  The test + branch target IS
                //     the answer to "what is the firmware waiting for".
                static std::atomic<uint64_t> s_firedMask{ 0 };
                uint64_t bit = 0;
                if (slot.grain.pc >= 0x7bad0 && slot.grain.pc < 0x7bb20) {
                    bit = uint64_t{ 1 }
                          << ((slot.grain.pc - 0x7bad0) >> 2);
                }
                if ((s_firedMask.fetch_or(bit,
                    std::memory_order_acq_rel)
                    & bit) == 0)
                {
                    std::fprintf(stderr,
                        "MEMDIAG-DISASM-POST pc=0x%012llx enc=0x%08x "
                        "R0=0x%016llx R1=0x%016llx R2=0x%016llx "
                        "R3=0x%016llx R4=0x%016llx R5=0x%016llx "
                        "R6=0x%016llx R7=0x%016llx "
                        "R16=0x%016llx R17=0x%016llx "
                        "R20=0x%016llx R21=0x%016llx R22=0x%016llx "
                        "R23=0x%016llx R24=0x%016llx R25=0x%016llx "
                        "R26=0x%016llx R27=0x%016llx "
                        "R29=0x%016llx R30=0x%016llx\n",
                        static_cast<unsigned long long>(pcA),
                        static_cast<unsigned>(encoded),
                        static_cast<unsigned long long>(cpu.intReg[0]),
                        static_cast<unsigned long long>(cpu.intReg[1]),
                        static_cast<unsigned long long>(cpu.intReg[2]),
                        static_cast<unsigned long long>(cpu.intReg[3]),
                        static_cast<unsigned long long>(cpu.intReg[4]),
                        static_cast<unsigned long long>(cpu.intReg[5]),
                        static_cast<unsigned long long>(cpu.intReg[6]),
                        static_cast<unsigned long long>(cpu.intReg[7]),
                        static_cast<unsigned long long>(cpu.intReg[16]),
                        static_cast<unsigned long long>(cpu.intReg[17]),
                        static_cast<unsigned long long>(cpu.intReg[20]),
                        static_cast<unsigned long long>(cpu.intReg[21]),
                        static_cast<unsigned long long>(cpu.intReg[22]),
                        static_cast<unsigned long long>(cpu.intReg[23]),
                        static_cast<unsigned long long>(cpu.intReg[24]),
                        static_cast<unsigned long long>(cpu.intReg[25]),
                        static_cast<unsigned long long>(cpu.intReg[26]),
                        static_cast<unsigned long long>(cpu.intReg[27]),
                        static_cast<unsigned long long>(cpu.intReg[29]),
                        static_cast<unsigned long long>(cpu.intReg[30]));
                    std::fflush(stderr);
                }
            }
        }
#endif

        // Cycle tick.  Done after retire so the slot's cycleIssued
        // matches the cycle on which it was minted.
        cpu.cycleCount++;

        // Boot profiler (2026-06-05): always-on retire-PC histogram.
        // One masked shift + increment + relaxed load; see
        // traceLib/RetireProfiler.h for operator controls.
        traceLib::RetireProfiler::record(slot.grain.pc, cpu.cycleCount);

#if EMULATR_MEMDIAG
        // -----------------------------------------------------
        // D-side lexer probe -- 2026-05-27, 0x60111c spin diagnosis.
        // -----------------------------------------------------
        // The table-driven lexer at 0x60111c never reaches a terminal
        // token, and its inputs are D-side reads the I-side fault path
        // above cannot observe.  Capture them at retire of the two load
        // sites the routine touches every pass:
        //   0x6009a8 (extbl)  get-next-byte : R17=input VA, R0=byte
        //   0x60115c (and)    table dispatch: R22=table EA, R1=table
        //                     value, R24=dispatch byte, R10=assembled
        //                     value, R11=shift counter
        // Records stream to lexer_probe.log in the run cwd, capped so
        // the file stays bounded.  The input VA+byte stream shows
        // whether the terminator is ever produced; the table EA+value
        // stream shows whether the state machine is cycling.  If the
        // first records land in pre-spin boot rather than the spin,
        // raise a cycle floor on the gate below.
        {
            uint64_t const insPc = slot.grain.pc & ~uint64_t{0x3};
            if (insPc == 0x6009a8ULL || insPc == 0x60115cULL) {
                static std::FILE*       lexLog   = std::fopen("lexer_probe.log", "w");
                static unsigned long    lexCount = 0;
                constexpr unsigned long kLexCap  = 6000;
                if (lexLog != nullptr && lexCount < kLexCap) {
                    if (insPc == 0x6009a8ULL) {
                        std::fprintf(lexLog,
                            "GETB cyc=%llu inVA=0x%016llx byte=0x%02llx "
                            "r27=0x%016llx\n",
                            static_cast<unsigned long long>(cpu.cycleCount),
                            static_cast<unsigned long long>(cpu.intReg[17]),
                            static_cast<unsigned long long>(cpu.intReg[0] & 0xFFu),
                            static_cast<unsigned long long>(cpu.intReg[27]));
                    } else {
                        std::fprintf(lexLog,
                            "TBL  cyc=%llu tblEA=0x%016llx tblVal=0x%016llx "
                            "disp=0x%02llx asm=0x%016llx cnt=0x%llx\n",
                            static_cast<unsigned long long>(cpu.cycleCount),
                            static_cast<unsigned long long>(cpu.intReg[22]),
                            static_cast<unsigned long long>(cpu.intReg[1]),
                            static_cast<unsigned long long>(cpu.intReg[24] & 0xFFu),
                            static_cast<unsigned long long>(cpu.intReg[10]),
                            static_cast<unsigned long long>(cpu.intReg[11]));
                    }
                    if (++lexCount == kLexCap) {
                        std::fprintf(lexLog,
                            "# lexer_probe: cap %lu reached, stopping\n",
                            kLexCap);
                        std::fclose(lexLog);
                        lexLog = nullptr;
                    } else {
                        std::fflush(lexLog);
                    }
                }
            }
        }
#endif

        // -----------------------------------------------------
        // Trace hook: per-commit + PAL transition emission.
        // -----------------------------------------------------
        // Fired after retire() so postCommitCpu in the CommitRecord
        // reflects both the regfile commit (done at MEM) and the PC
        // advance (done at WB).  PAL entry / exit are detected from
        // the just-retired grain's semantic flags.
        //
        // COMPILE-GATED (EMULATR_TRACE_HOOKS, default OFF -- PERF #1 /
        // diagnostics-default-off rule 2026-06-10): this block runs every
        // retire, so even the `if (sink)` branch is a per-step cost on the
        // hot loop.  Normal/perf builds compile it OUT; build with
        // -DEMULATR_TRACE_HOOKS=ON to capture a trace (sink/--trace then
        // selects what emits).
#if defined(EMULATR_TRACE_HOOKS)
        if (sink) {
            traceLib::CommitRecord rec{};
            rec.cycle    = slot.cycleIssued;
            rec.pc       = slot.grain.pc;
            rec.encoded  = slot.grain.encoded;
            rec.mnemonic = mnemonic;
            rec.result   = &slot.result;
            sink->onCommit(rec, cpu);

            using grainFactory::GrainSem;
            if (grainFactory::has(slot.grain.semFlags, GrainSem::S_PalEntry)) {
                sink->onPalEntry(slot.cycleIssued, cpu.pc, cpu.excAddr);
            }
            if (grainFactory::has(slot.grain.semFlags, GrainSem::S_PalExit)) {
                sink->onPalExit(slot.cycleIssued, cpu.pc);
            }
        }
#else
        (void)sink;
        (void)mnemonic;
#endif

        return !cpu.halted;
    }

#ifdef EMULATR_DEBUG_STEP
#pragma optimize("", on)
#endif

    // -------------------------------------------------------------
    // run
    //
    //   Loop calling step until the CPU halts or maxCycles is
    //   reached.  maxCycles defaults to effectively unlimited;
    //   tests pass a small value to bound the run if the program
    //   is malformed and would otherwise spin forever.
    // -------------------------------------------------------------
    static void run(coreLib::CpuState&     cpu,
                    memoryLib::ISystemBus& bus, memoryLib::GuestMemory& mem,
                    uint64_t                maxCycles      = ~uint64_t{0},
                    traceLib::TraceSink*    sink           = nullptr,
                    IFetchOverride*         fetchOverride  = nullptr) noexcept
    {
        for (uint64_t i = 0; i < maxCycles; ++i) {
            if (!step(cpu, bus, mem, sink, fetchOverride)) {
                break;
            }
        }
    }


private:
    // -------------------------------------------------------------
    // decode
    //
    //   Map a 32-bit instruction word to its dispatch entry.
    //   Reads the primary opcode at encoded[31:26], indexes
    //   g_primaryTable, then routes by DispatchKind:
    //
    //     Direct / HwMfpr / HwLd / HwMtpr / HwRei / HwSt / Reserved
    //         use the primary entry's `direct` GrainEntry.
    //     IntArith / IntLogical / IntShift / IntMul / ItFp /
    //     FltLogical / FpTiExt
    //         7-bit sub-decode at encoded[11:5] indexes the 128-
    //         entry sub-table.
    //     FltIeee
    //         11-bit sub-decode at encoded[15:5] indexes the 2048-
    //         entry sub-table.
    //     JmpClass
    //         2-bit sub-decode at encoded[15:14] indexes the 4-
    //         entry sub-table.
    //     Misc
    //         16-bit sub-decode at encoded[15:0]; sparse via
    //         lookupMisc.
    //     Pal
    //         26-bit sub-decode at encoded[25:0]; sparse via
    //         lookupPalTru64 / lookupPalVms keyed by personality.
    //
    //   Out-of-range sub-decodes and lookup misses fall back to
    //   coreLib::kOpcDecEntry, which raises kFaultOpcDec at WB.
    // -------------------------------------------------------------
    AXP_HOT AXP_FLATTEN
    static coreLib::GrainEntry const& decode(uint32_t                  encoded,
                                              coreLib::CpuState const& cpu) noexcept
    {
        uint8_t const primaryOp = static_cast<uint8_t>((encoded >> 26) & 0x3Fu);
        coreLib::PrimaryEntry const& pe = grainFactory::primaryEntry(primaryOp);

        using grainFactory::DispatchKind;

        switch (pe.kind) {
            case DispatchKind::Direct:
            case DispatchKind::HwMfpr:
            case DispatchKind::HwLd:
            case DispatchKind::HwMtpr:
            case DispatchKind::HwRei:
            case DispatchKind::HwSt:
            case DispatchKind::Reserved:
                return pe.direct;

            case DispatchKind::IntArith:
            case DispatchKind::IntLogical:
            case DispatchKind::IntShift:
            case DispatchKind::IntMul:
            case DispatchKind::ItFp:
            case DispatchKind::FltLogical:
            case DispatchKind::FpTiExt: {
                uint16_t const sub = static_cast<uint16_t>((encoded >> 5) & 0x7Fu);
                if (sub < pe.subTableLen && pe.subTable != nullptr) {
                    return pe.subTable[sub];
                }
                return coreLib::kOpcDecEntry;
            }

            case DispatchKind::FltIeee: {
                uint16_t const sub = static_cast<uint16_t>((encoded >> 5) & 0x7FFu);
                if (sub < pe.subTableLen && pe.subTable != nullptr) {
                    return pe.subTable[sub];
                }
                return coreLib::kOpcDecEntry;
            }

            case DispatchKind::JmpClass: {
                uint16_t const sub = static_cast<uint16_t>((encoded >> 14) & 0x3u);
                if (sub < pe.subTableLen && pe.subTable != nullptr) {
                    return pe.subTable[sub];
                }
                return coreLib::kOpcDecEntry;
            }

            case DispatchKind::Misc: {
                uint32_t const func = encoded & 0xFFFFu;
                coreLib::GrainEntry const* e = grainFactory::lookupMisc(func);
                return e ? *e : coreLib::kOpcDecEntry;
            }

            case DispatchKind::Pal: {
                uint32_t const func = encoded & 0x03FFFFFFu;
                coreLib::GrainEntry const* e =
                    (cpu.palPersonality == 1u) ? grainFactory::lookupPalVms(func)
                    : grainFactory::lookupPalTru64(func);
                return e ? *e : coreLib::kOpcDecEntry;
            }
        }

        return coreLib::kOpcDecEntry;
    }


    // -------------------------------------------------------------
    // buildCtx
    //
    //   Resolve operands per grain.semFlags and pack ExecCtx for
    //   the leaf call.  Reads intReg or fpReg per S_ReadsFp; honours
    //   S_HasLit (8-bit literal at encoded[20:13] zero-extended into
    //   opB).  Always installs cpu pointer, cycleCount, palMode.
    // -------------------------------------------------------------
    AXP_HOT AXP_FLATTEN
    static coreLib::ExecCtx buildCtx(coreLib::InstructionGrain const& grain,
                                      coreLib::CpuState&              cpu) noexcept
    {
        coreLib::ExecCtx ctx{};
        ctx.cpu        = &cpu;
        ctx.cycleCount = cpu.cycleCount;
        ctx.palMode    = cpu.inPalMode();

        uint64_t const flagsBits = static_cast<uint64_t>(grain.semFlags);
        auto const has = [flagsBits](grainFactory::GrainSem bit) -> bool {
            return (flagsBits & static_cast<uint64_t>(bit)) != 0;
        };

        // Ra-side regfile selection: S_ReadsFp says Ra is an FP register.
        // Holds for FP-format arithmetic (both Ra and Rb are FP) AND for
        // FP-mem stores (Ra is the FP value being stored, Rb is the int
        // base -- split-regfile case handled below).
        bool const useFp = has(grainFactory::GrainSem::S_ReadsFp);

        // Mem-format Rb is the integer EA base regardless of any FP
        // involvement on Ra.  STS / STT / STF / STG carry both S_ReadsFp
        // (Ra source) and S_ReadsInt (Rb base); routing Rb through fpReg
        // would read F[Rb] (all zeros at boot) instead of int[Rb].
        bool const isMemFormat = has(grainFactory::GrainSem::S_MemFormat);

        if (has(grainFactory::GrainSem::S_ReadsRa)) {
            uint8_t const ra = static_cast<uint8_t>((grain.encoded >> 21) & 0x1Fu);
            ctx.opA = useFp ? cpu.fpReg[ra] : cpu.intReg[ra];
        }

        if (has(grainFactory::GrainSem::S_ReadsRb)) {
            // Op-format and FP-format encode IMM at bit 12 per
            // instruction instance (every ALU/FP opcode covers both
            // regfile-Rb and 8-bit-literal forms with the same
            // dispatch entry).  Mem-format / Bra-format / Jmp-format
            // / Misc-format do NOT have an IMM bit there -- encoded[12]
            // belongs to the displacement / function-code field, so
            // we gate the literal path on the format flag.
            //
            // Static S_HasLit at the dispatch level was the wrong
            // abstraction here; Alpha's literal/regfile choice is
            // dynamic, not opcode-static.
            bool const isOpFormat =
                   has(grainFactory::GrainSem::S_OpFormat)
                || has(grainFactory::GrainSem::S_FpFormat);
            bool const immBit =
                isOpFormat && ((grain.encoded & (uint32_t{1} << 12)) != 0);

            if (immBit) {
                // 8-bit literal at encoded[20:13], zero-extended.
                ctx.opB = static_cast<uint64_t>((grain.encoded >> 13) & 0xFFu);
            } else {
                uint8_t const rb = static_cast<uint8_t>((grain.encoded >> 16) & 0x1Fu);
                // Mem-format: Rb is ALWAYS the integer EA base.
                // Other formats: follow the unified useFp selection.
                bool const rbFromFp = useFp && !isMemFormat;
                ctx.opB = rbFromFp ? cpu.fpReg[rb] : cpu.intReg[rb];
            }
        }

        return ctx;
    }


    // -------------------------------------------------------------
    // retire
    //
    //   Advance cpu.pc and intercept halts / faults.  Does not
    //   touch the regfile -- that drain happened at MEM.  The
    //   pipeline driver is the only piece that mutates cpu.pc; the
    //   leaves and the drainer never touch it.
    // -------------------------------------------------------------
    AXP_HOT AXP_FLATTEN
    static void retire(coreLib::PipelineSlot const& slot,
                       coreLib::CpuState&            cpu) noexcept
    {
        coreLib::BoxResult const& r = slot.result;

        // HALT: graceful shutdown signal, not a trap.
        if (r.faultCode == coreLib::kFaultHalt) {
            cpu.halted        = true;
            cpu.lastFaultCode = coreLib::kFaultHalt;
            return;
        }

        // Other faults: deliver to PALcode if PALcode is loaded;
        // otherwise halt with the fault captured for post-mortem.
        //
        // PALcode trap delivery sequence (Alpha SRM Section III; EV6
        // HRM 6.8.1):
        //   1. Save faulting PC in excAddr; bit 0 = pre-trap palMode
        //      (so HW_REI's STACKED form restores it on return).
        //   2. Compute entry vector via coreLib::ev6::entryForFault,
        //      which maps V4's kFault* codes to the AARM-canonical
        //      offsets (UNALIGN 0x280, OPCDEC 0x400, MCHK 0x500, etc.).
        //   3. Divert PC to palBase + entry offset.
        //   4. Set palMode = true so PALcode runs privileged.
        //
        // Fallback to halt-with-diagnostic when:
        //   - palBase == 0: PALcode hasn't been loaded yet (V4 v1
        //     boot-without-firmware scenario; jumping to entry offset
        //     would land in low-memory garbage and lose the failure).
        //   - entryForFault returned kEntry_None: the fault is an
        //     emulator-internal stop signal with no architectural
        //     entry (e.g., kFaultHalt was caught above; this guards
        //     against future fault codes that share the no-entry
        //     posture).
        if (r.faultCode != coreLib::kNoFault) {
            cpu.lastFaultCode = r.faultCode;

            // Fault telemetry -> logs/faults.log (offline review, not the
            // multi-GB .trc).  Skip routine TB misses: kFaultDtbMiss /
            // kFaultItbMiss are high-volume paging events and would flood the
            // log; everything else (OPCDEC, unimplemented, privileged,
            // unaligned, ACV, bus error, ...) is the demand/anomaly list.
            if (r.faultCode != coreLib::kFaultDtbMiss &&
                r.faultCode != coreLib::kFaultItbMiss) {
                coreLib::logFaultEvent(cpu.cycleCount, slot.grain.pc,
                                       slot.grain.encoded, r.faultCode,
                                       cpu.inPalMode());
            }

            // FIX 2026-05-27: REMOVED  cpu.va = cpu.mm_stat;
            // The original line was a copy-paste bug -- it overwrote the
            // faulting VA with mm_stat, so HW_MFPR HW_VA / HW_VA_FORM in
            // every D-side PAL handler returned a value derived from
            // mm_stat instead of the faulting effective address.  cpu.va
            // is already latched correctly upstream:
            //   D-side memory faults: MemDrainer.h sets cpu.va = r.memAddr.
            //   I-side faults: translateInstruction sets cpu.va = cpu.pc.
            //   Other faults (OPCDEC / PRIVILEGED / FEN / MCHK): cpu.va is
            //     architecturally don't-care.
            // Symptom this fixed: DTBM_DOUBLE_3 installed DTB_TAG = 0x280
            // (== mm_stat for an LDL fault, opcode 0x28 << 4) instead of
            // the VPTE virtual address, causing an infinite ~28-cycle
            // self-re-entry loop at SROM PCs 0x8300 / 0xd1a1.

            uint64_t const entryOffset =
                coreLib::ev6::entryForFault(r.faultCode);

            if (cpu.palBase == 0 ||
                entryOffset == coreLib::ev6::kEntry_None) {
                // No PALcode to deliver to, or no entry vector defined
                // for this fault class -- halt the run cleanly.
                cpu.excAddr = slot.grain.pc;
                cpu.halted  = true;
                return;
            }

            // PALcode is loaded -- deliver the trap.
            // CHANGE 2026-05-21 (PALmode == PC<0>): excAddr = faulting PC
            // (clean address) with bit0 = pre-trap PALmode, so HW_REI's
            // STACKED form restores the mode on return.  Entering the PAL
            // vector sets PC<0> = 1 instead of a separate palMode = true.
            //
            // ============================================================
            // WORKAROUND 2026-05-28 -- BusError advances excAddr by 4.
            // ============================================================
            // For kFaultBusError, set excAddr = pc + 4 (skip the failing
            // access) instead of the faulting PC.  The MCHK handler reads
            // EXC_ADDR into R23 and uses it as the HW_REI target, so this
            // makes the handler resume at the instruction AFTER the failed
            // access -- the firmware proceeds to its next probe target
            // instead of re-issuing the same failed access.
            //
            // Why this is a workaround, not architecturally correct:
            //   Real EV6 silicon distinguishes TRANSIENT errors (ECC single-
            //   bit, retry-and-succeed) from HARD errors (NXM, no device
            //   responds) via the CBox ERROR_REG shift chain (HRM Section
            //   5.4 / Section 7.10).  For transient errors the PAL handler
            //   re-issues at the same PC; for hard errors it advances to
            //   PC+4.  Our V4 chipset latches MISC.NXM and we OR a single
            //   bit into cBox.errorReg + pre-populate dataReg=0x01 in
            //   MemDrainer.h on BusError -- but those signals only tell the
            //   PAL "some error happened" not "external NXM, advance PC."
            //   The handler currently defaults to TRANSIENT semantics,
            //   re-issues at the faulting PC, and loops indefinitely on
            //   NXM (376K iterations observed at cyc 200M -> 294M).
            //
            // ARCHITECTURAL CORRECTION (TODO -- remove this branch when
            //                           landed):
            //   1. Encode the failing PA into cBox.errorReg per HRM 5.4
            //      C_ADDR / C_STAT bit layout (reverse-engineer from the
            //      decode loop captured in 123456.txt at PCs 0x12369-
            //      0x12381 -- 33-iteration bit-scan that assembles R07
            //      into R04, the chunks at 6-bit positions encode address
            //      and status fields).
            //   2. Set the "external NXM / no master ack" status bit in
            //      the appropriate chunk so the handler's decode at PC
            //      0x12541 dispatches to the hard-error path.
            //   3. Remove the conditional below; let the PAL set R23
            //      itself based on the decoded status.
            //   4. Alternatively, plumb the chipset's MISC.NXM into the
            //      CBox chain directly so the handler reads it via the
            //      normal HW_MFPR HW_C_DATA poll sequence.
            //
            // Removal trigger: when the HRM 5.4 bit layout is known and
            // step (1)/(4) above is implemented; verified by observing
            // the handler set R23 = PC+4 without this branch firing.
            // ============================================================
            {
                // REVERTED 2026-05-30: the blanket "I-side excAddr = cpu.pcAddr()"
                // fix regressed normal ITB misses (hang ~cyc 181.5M in
                // applyMemEffect) -- cpu.pcAddr() is not reliably the faulting
                // fetch VA at every ITB-miss delivery.  Back to slot.grain.pc
                // until the [ITBMISS-PROBE]/[REI-PROBE] data below disambiguates
                // H1 (latch source) vs H2 (HW_REI target) for the post-clock
                // miss only.  Probes are passive (window-gated, no behavior).
                uint64_t basePc = slot.grain.pc & ~uint64_t{1};
                if (r.faultCode == coreLib::kFaultBusError) {
                    // TODO: remove when CBox ERROR_REG chain encodes
                    // HARD-NXM status bits per HRM 5.4 (see comment above).
                    basePc += 4;
                }

                // TEMP DIAG 2026-05-30 (ITB-miss excAddr H1/H2 probe) --
                // REMOVE BEFORE COMMIT.  Wide window around the clock-handler
                // return (event seen at cyc ~189564931 this run, ~189564984 on
                // cold boot -- the window must straddle both).  Capped.
                // Logs grainSrc (current, =slot.grain.pc) vs vaSrc (=cpu.va,
                // the faulting fetch VA translateInstruction latched):
                //   bad miss  -> grain.pc=0, cpu.va=0x1c699c  (cpu.va = the fix)
                //   normal miss-> grain.pc == cpu.va          (cpu.va won't regress)
#if EMULATR_BRINGUP_PROBES
                if (r.faultCode == coreLib::kFaultItbMiss &&
                    cpu.cycleCount >= 189564000ull &&
                    cpu.cycleCount <= 189565200ull) {
                    static int s_itbProbe = 0;
                    bool const staleGrain = ((slot.grain.pc & ~uint64_t{1}) == 0);
                    if (s_itbProbe < 48 || staleGrain) {
                        if (s_itbProbe < 48) ++s_itbProbe;
                        std::fprintf(stderr,
                            "[ITBMISS-PROBE] cyc=%llu cpu.pc=0x%016llx "
                            "grainSrc=0x%016llx vaSrc(cpu.va)=0x%016llx%s\n",
                            static_cast<unsigned long long>(cpu.cycleCount),
                            static_cast<unsigned long long>(cpu.pc),
                            static_cast<unsigned long long>(slot.grain.pc & ~uint64_t{1}),
                            static_cast<unsigned long long>(cpu.va & ~uint64_t{1}),
                            staleGrain ? "  <== STALE-GRAIN (the bug)" : "");
                        std::fflush(stderr);
                        // __debugbreak();  // uncomment to halt live in VS here
                    }
                }
#endif

                cpu.excAddr =
                    basePc | (cpu.inPalMode() ? uint64_t{1} : uint64_t{0});
            }
            // FIX 2026-05-26: route the native->PAL trap transition through
            // palModeEnter so the EV6 I_CTL[SDE] shadow swap (R4-R7 / R20-R23)
            // fires symmetrically with HW_REI's setPalMode on return.  The
            // prior direct "cpu.pc |= 1" raised PALmode WITHOUT swapping, so
            // the PAL miss-handler ran on the native register set and the
            // replayed faulting instruction resolved its base operand from a
            // stale shadow GPR (DS10 store at PC 0x600d3c: R21 = 0x5f0004
            // native vs 0x0f01 shadow -> spurious EA 0x0f01 on the replay).
            // palModeEnter swaps only on a real native->PAL transition (and
            // only when SDE is set), and is a no-op for a trap taken from
            // PAL mode, so nested-PAL faults keep correct swap parity.  Its
            // own "pc |= 1" is intentionally overwritten by the vector write
            // immediately below.
            coreLib::palModeEnter(cpu);
            cpu.pc =
                coreLib::ev6::computeHwExceptionEntry(cpu.palBase,
                                                      entryOffset)
                | uint64_t{1};
            return;
        }

        // No-fault path: divert if requested, else fall through.
        if (r.divert) {
            cpu.pc = r.divertTarget;
        } else {
            cpu.pc = slot.grain.pc + 4;
        }
    }
};

} // namespace pipelineLib

#endif // PIPELINELIB_PIPELINEDRIVER_H
