// ============================================================================
// pipelineLib/MemDrainer.h -- MEM-stage drainer for V4 v1 pipeline
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
// MemDrainer is the seam where a leaf-produced BoxResult meets the
// memory subsystem and the architectural register file.  One static
// entry point, MemDrainer::drain, called by the pipeline driver per
// slot at the MEM stage.  Pulls memEffect off the BoxResult, calls
// the EV6 translator, calls GuestMemory, applies the load sign-
// extension rule from BoxResult.h's drain map, commits the regfile
// write, manages the LDx_L / STx_C reservation pair, and surfaces
// faults via faultCode without delivering them (trap delivery is the
// pipeline driver's WB-stage concern).
//
// Drain order per slot:
//
//   1. memEffect: if memSize > 0, translate VA -> PA, call
//      GuestMemory, write the load fill into regWriteValue or
//      publish the store data; on translation / GuestMemory error,
//      set faultCode and skip the regfile commit.
//   2. reservation: LDL_L / LDQ_L (S_Locked + S_Load) sets the
//      reservation; STL_C / STQ_C (S_Locked + S_Store) checks-and-
//      clears it and sets regWriteValue to 1 / 0 per success.
//   3. regfile commit: if regWriteIdx != kNoRegWrite, write
//      regWriteValue into intReg[regWriteIdx] or fpReg[...] per
//      regWriteIsFp.  R31 / F31 commit is suppressed by virtue of
//      kNoRegWrite == 31 -- so callers can blindly route through
//      this path.
//
// The drainer does NOT touch CpuState::pc or CpuState::halted; those
// are the WB stage's responsibility (PC advance, halt intercept).
// The drainer does NOT call the leaf -- that's the EX stage's job.
// The drainer does NOT report success / failure separately to the
// caller; it mutates slot.result and CpuState in place, and the
// pipeline driver reads the post-drain BoxResult to decide WB.
//
// ============================================================================

#ifndef PIPELINELIB_MEMDRAINER_H
#define PIPELINELIB_MEMDRAINER_H

#include <cstdint>
#include <cstdio>
#include <cstdlib>   // std::getenv -- PA 0x10 conditional-break gate (2026-06-03)
#if defined(_MSC_VER)
#include <intrin.h>  // __debugbreak -- PA 0x10 conditional break (2026-06-03)
#endif

#include "coreLib/BoxResult.h"
#include "coreLib/CpuState.h"
#include "coreLib/PipelineSlot.h"
#include "coreLib/VA_types.h"
#include "coreLib/axp_attributes_core.h"
#include "fBoxLib/grains/FpFormat.h"
#include "grainFactoryLib/generated/SemanticFlagsEnum.h"
#include "memoryLib/GuestMemory.h"
#include "memoryLib/ISystemBus.h"
#include "mmuLib/Ev6Translator.h"
#include "mmuLib/TranslationResult.h"

// ---------------------------------------------------------------------------
// MEMDIAG -- surgical, cycle-gated translation diagnostics (temporary probe).
// Build with -DEMULATR_MEMDIAG=1 to enable; off by default (zero hot-path
// cost -- the gate folds away).  The window brackets the DS10 DTB-miss /
// HALT sequence (native LDQ fault at cyc 4194515, HALT at cyc 4194618), so
// it captures the fault, the PAL refill handler, the retry, the entry into
// the VA 0x602xxx region, the BSR fetch, and the halt in one bounded burst.
// PipelineDriver.h includes this header, so its I-side fetch gate shares the
// same window verbatim.  The #ifndef wrappers let a build flag override LO/HI
// without editing the source.
// ---------------------------------------------------------------------------
#ifndef EMULATR_MEMDIAG
#define EMULATR_MEMDIAG 0   // dormant diagnostic scaffold (set 1 to re-enable)
#endif
#ifndef EMULATR_MEMDIAG_CYC_LO
#define EMULATR_MEMDIAG_CYC_LO 4194450ULL
#endif
#ifndef EMULATR_MEMDIAG_CYC_HI
#define EMULATR_MEMDIAG_CYC_HI 4194650ULL
#endif

namespace pipelineLib {

// EV6 cache line is 64 bytes.  The reservation tracks granularity at
// this size; LDL_L of any address within a 64-byte block reserves
// the whole block, and STL_C succeeds only if the same block is
// still reserved.
constexpr uint64_t kCacheLineMask = ~uint64_t{0x3FULL};


// ---------------------------------------------------------------------------
// formatLoadValue -- apply BoxResult.h's drain-map sign and FP-format rules.
// ---------------------------------------------------------------------------
// Integer loads (regWriteIsFp == false):
//   memSize == 1: zero-extend (LDBU)
//   memSize == 2: zero-extend (LDWU)
//   memSize == 4: sign-extend longword -> quadword (LDL / LDL_L)
//   memSize == 8: pass through (LDQ / LDQ_L / LDQ_U)
//
// FP loads (regWriteIsFp == true):
//   memSize == 4, S_VaxFp clear  -> LDS (IEEE S-floating, 32->64 expand)
//   memSize == 4, S_VaxFp set    -> LDF (VAX F-floating, 32->64 expand)
//   memSize == 8, S_VaxFp clear  -> LDT (IEEE T-floating, identity)
//   memSize == 8, S_VaxFp set    -> LDG (VAX G-floating, word swap)
//
// S_VaxFp comes off the leaf's semFlags -- the TSV row for LDF / LDG sets
// it, LDS / LDT leave it clear.  fBoxLib/grains/FpFormat.h owns the four
// conversion helpers; the drainer just dispatches on size + flag.
[[nodiscard]] AXP_HOT AXP_FLATTEN
constexpr uint64_t formatLoadValue(uint64_t                       raw,
                                   uint8_t                        memSize,
                                   bool                           regWriteIsFp,
                                   grainFactory::GrainSem         semFlags) noexcept
{
    if (regWriteIsFp) {
        bool const isVax = grainFactory::has(
            semFlags, grainFactory::GrainSem::S_VaxFp);

        if (memSize == 4) {
            uint32_t const mem32 = static_cast<uint32_t>(raw);
            return isVax ? fBox::convertF_FloatingToRegister(mem32)
                         : fBox::convertS_FloatingToRegister(mem32);
        }
        if (memSize == 8) {
            return isVax ? fBox::convertG_FloatingToRegister(raw)
                         : fBox::convertT_FloatingToRegister(raw);
        }
        return raw;     // defensive: unsupported FP load size
    }

    // Integer path.
    if (memSize == 4) {
        return static_cast<uint64_t>(
            static_cast<int64_t>(static_cast<int32_t>(raw)));
    }
    return raw;
}


// Backwards-compatible thin wrapper retained for any caller that still
// uses the old signature.  Equivalent to formatLoadValue with no S_VaxFp.
[[nodiscard]] AXP_HOT AXP_FLATTEN
constexpr uint64_t signExtendLoadFill(uint64_t raw,
                                      uint8_t  memSize,
                                      bool     regWriteIsFp) noexcept
{
    return formatLoadValue(raw, memSize, regWriteIsFp,
                           static_cast<grainFactory::GrainSem>(0));
}


// ---------------------------------------------------------------------------
// MemDrainer -- single entry point.
// ---------------------------------------------------------------------------
struct MemDrainer
{
    // -------------------------------------------------------------
    // drain
    //
    //   Apply slot.result against cpu and memory.  Mutates
    //   slot.result.regWriteValue (for loads) and slot.result.faultCode
    //   (on translation / memory failure) and CpuState (regfile
    //   commit, reservation update, mm_stat on faults).  The
    //   pipeline driver consumes the post-drain BoxResult at WB.
    // -------------------------------------------------------------
    AXP_HOT AXP_FLATTEN
    static void drain(coreLib::PipelineSlot&    slot,
                      coreLib::CpuState&        cpu,
                      memoryLib::ISystemBus&    bus,
                      memoryLib::LockMonitor&   locks) noexcept
    {
        coreLib::BoxResult& r = slot.result;

        // If the leaf already raised a fault at EX (kFaultUnimplemented,
        // kFaultHalt, kFaultOpcDec, kFaultPrivileged), skip everything;
        // the pipeline driver handles delivery at WB.
        if (r.faultCode != coreLib::kNoFault) {
            return;
        }

        // ---------------------------------------------------------
        // 1. Memory effect.
        // ---------------------------------------------------------
        if (r.memSize != coreLib::kNoMemEffect) {
            applyMemEffect(slot, cpu, bus, locks);

            // applyMemEffect may have set faultCode; if so, abort
            // before regfile commit and reservation.
            if (r.faultCode != coreLib::kNoFault) {
                return;
            }
        }

        // ---------------------------------------------------------
        // 2. Regfile commit.
        // ---------------------------------------------------------
        // R31 / F31 are kNoRegWrite; the check below short-circuits.
        if (r.regWriteIdx != coreLib::kNoRegWrite) {
            // DIAGNOSTIC: catch leaves that return a regfile commit
            // when their semantic flags do not declare one.  S_WritesRa
            // (loads, BSR/JSR return PC, STx_C success indicator) and
            // S_WritesRc (Op-format / Fp-format destination) are the
            // two architectural commit channels.  If neither is set
            // but regWriteIdx is non-kNoRegWrite, the BoxResult is
            // leaking a spurious commit -- catches the exact bug
            // shape Tim is hypothesizing for HW_MTPR.  One untaken
            // branch on the hot path; near-zero perf cost.
            using grainFactory::GrainSem;
            if (!grainFactory::has(r.semFlags,
                    GrainSem::S_WritesRa | GrainSem::S_WritesRc))
            {
                /*
                std::fprintf(stderr,
                    "ASSERT: spurious regfile commit -- pc=0x%016llx "
                    "encoded=0x%08x regWriteIdx=%u value=0x%016llx "
                    "semFlags=0x%016llx cycle=%llu\n",
                    static_cast<unsigned long long>(slot.grain.pc),
                    static_cast<unsigned>(slot.grain.encoded),
                    static_cast<unsigned>(r.regWriteIdx),
                    static_cast<unsigned long long>(r.regWriteValue),
                    static_cast<unsigned long long>(r.semFlags),
                    static_cast<unsigned long long>(cpu.cycleCount));
                    */
            }

            if (r.regWriteIsFp) {
                cpu.fpReg[r.regWriteIdx] = r.regWriteValue;
            } else {
                cpu.intReg[r.regWriteIdx] = r.regWriteValue;
            }
        }

        // R31 must always read as 0 (RAZ; writes ignored).  Cheap
        // structural invariant: one branch, predicted untaken.  Catches
        // any pipeline bug that ends up writing R31 -- e.g. a leaf
        // returning regWriteIdx=31 with a non-zero value AND an
        // off-by-one MEM-drainer that committed it.
        if (cpu.intReg[31] != 0) {
#if EMULATR_BRINGUP_PROBES
            std::fprintf(stderr,
                "ASSERT: R31 != 0 -- pc=0x%016llx encoded=0x%08x "
                "intReg[31]=0x%016llx cycle=%llu\n",
                static_cast<unsigned long long>(slot.grain.pc),
                static_cast<unsigned>(slot.grain.encoded),
                static_cast<unsigned long long>(cpu.intReg[31]),
                static_cast<unsigned long long>(cpu.cycleCount));
#endif
            cpu.intReg[31] = 0;   // re-zero so subsequent reads stay clean
        }
    }


private:
    // -------------------------------------------------------------
    // applyMemEffect
    //
    //   Translate, access GuestMemory, populate regWriteValue on
    //   loads, set faultCode on failure.  For STL_C / STQ_C, also
    //   consult the per-CPU reservation and rewrite regWriteValue
    //   to 1 / 0.  For LDL_L / LDQ_L, set the reservation after a
    //   successful read.
    // -------------------------------------------------------------
    AXP_HOT AXP_FLATTEN
    static void applyMemEffect(coreLib::PipelineSlot&  slot,
                               coreLib::CpuState&      cpu,
                               memoryLib::ISystemBus&  bus,
                               memoryLib::LockMonitor& locks) noexcept
    {
        coreLib::BoxResult& r = slot.result;

        // Translate VA to PA.  Always alignment-checked: the
        // alignment rule for size > 1 is naturally aligned, and the
        // leaf has already pre-aligned LDQ_U / STQ_U via its own
        // EA mask.  size == 1 (LDBU / STB) trivially passes.
        //
        // S_PhysAddr bypass: HW_LD / HW_ST / CALL_PAL LDQP+STQP
        // explicitly address physical memory (the EA from the leaf is
        // already a PA).  Skip the translator entirely when this flag
        // is set; alignment is the caller's responsibility (PALcode
        // pre-aligns LDQP/STQP per OSF spec, HW_LD/HW_ST carry their
        // own alignment hint bits).
        bool const physAddr = grainFactory::has(
            r.semFlags, grainFactory::GrainSem::S_PhysAddr);

        coreLib::PAType pa = 0;
        mmuLib::TranslationResult tr = mmuLib::TranslationResult::Success;
        if (physAddr) {
            pa = r.memAddr;
        } else {
            coreLib::AccessKind const access = r.memIsStore
                ? coreLib::AccessKind::DataWrite
                : coreLib::AccessKind::DataRead;
            tr = mmuLib::Ev6Translator::translateDataAligned(
                cpu, r.memAddr, r.memSize, access, pa);
        }

#if EMULATR_MEMDIAG
        // D-side translation event.  phys=1 means the S_PhysAddr bypass ran
        // (pa==va by construction); phys=0 means translateDataAligned ran and
        // tr/pa are its verdict.  This is the line that settles whether the
        // cyc-4194515 fault is a real native DTB miss (pal=0 tr=DtbMiss) or a
        // synthetic one, and what PFN the D-side resolves VPN 0x301 to.
        if (cpu.cycleCount >= EMULATR_MEMDIAG_CYC_LO &&
            cpu.cycleCount <= EMULATR_MEMDIAG_CYC_HI) {
            std::fprintf(stderr,
                "MEMDIAG-D cyc=%llu pc=0x%016llx enc=0x%08x %s%u "
                "va=0x%016llx pal=%d phys=%d tr=%u pa=0x%016llx fault=%u\n",
                static_cast<unsigned long long>(cpu.cycleCount),
                static_cast<unsigned long long>(slot.grain.pc),
                static_cast<unsigned>(slot.grain.encoded),
                r.memIsStore ? "st" : "ld", static_cast<unsigned>(r.memSize),
                static_cast<unsigned long long>(r.memAddr),
                static_cast<int>(cpu.inPalMode()), static_cast<int>(physAddr),
                static_cast<unsigned>(tr),
                static_cast<unsigned long long>(pa),
                static_cast<unsigned>(mmuLib::toFaultCode(tr)));
        }
#endif

        if (!physAddr && tr != mmuLib::TranslationResult::Success) {
            r.faultCode = mmuLib::toFaultCode(tr);
            // A VPTE-format HW_LD (the PALcode page-table-walk PTE fetch:
            // opcode 0x1B, encoded<15:13> == 010) that DTB-misses is a DOUBLE
            // miss -- the page-table page itself is unmapped.  EV6 vectors that
            // to DTBM_DOUBLE, not DTBM_SINGLE; routing it to SINGLE makes the
            // single-miss handler re-enter itself forever (its own VPTE load
            // re-misses).  Matches AXPBox virt2phys (VPTE -> DTBM_DOUBLE_3) and
            // SimH alpha_ev5_tlb single/double split.
            if (r.faultCode == coreLib::kFaultDtbMiss
                && slot.grain.primaryOp == 0x1Bu
                && ((slot.grain.encoded >> 13) & 0x7u) == 0x2u) {
                r.faultCode = coreLib::kFaultDtbMissDouble;
            }
            // EV6 MM_STAT is a STATUS word (HRM 5.x / ev6_defs.mar), NOT the
            // address:  bit0 WR (store), bit1 ACV (access violation), bit2 FOR
            // (PTE fault-on-read), bit3 FOW (PTE fault-on-write), bits9:4 OPCODE
            // of the faulting instruction.  The VMS/OSF DFAULT handler
            // (vmspal_ent_dfault) branches on these bits to classify the fault
            // and pick the TB-fill / read-vs-write action.  Previously this
            // stored the faulting VA into mm_stat, so every bit test read
            // address garbage; the real VA belongs in HW_VA (cpu.va), set below.
            cpu.mm_stat =
                  (r.memIsStore ? uint64_t{1} : uint64_t{0})                                   // WR
                | (tr == mmuLib::TranslationResult::AccessViolation ? (uint64_t{1} << 1) : 0)  // ACV
                | (tr == mmuLib::TranslationResult::FaultOnRead     ? (uint64_t{1} << 2) : 0)  // FOR
                | (tr == mmuLib::TranslationResult::FaultOnWrite    ? (uint64_t{1} << 3) : 0)  // FOW
                | ((static_cast<uint64_t>(slot.grain.encoded >> 26) & 0x3Fu) << 4);           // OPCODE
            // HW_VA (IPR scbd 0xc2 = EV6__VA) latches the faulting D-stream VA
            // on a data MM fault; VA_FORM (scbd 0xc3) is derived from it plus
            // VA_CTL[VPTB] for the page-table walk.  Previously unset, so
            // VA_FORM computed 0, the walk loaded an invalid PTE from [0], and
            // re-faulted forever (2026-05-27 SROM 0x8301-0x8321 page-walk spin).
            cpu.va = r.memAddr;
#if EMULATR_MEMDIAG
            // D-side fault probe.  Filters out the SROM DTBM_SINGLE handler's
            // own VPTE re-load at palBase+0x321 so we see the ORIGINAL upstream
            // access, not the handler thrashing.  Also decodes Ra/Rb and dumps
            // the base register value (intReg[Rb]) -- when va is suspiciously
            // low (e.g. 0 or 0x1b0), this reveals the null/garbage base
            // pointer that produced the EA, which is the actual upstream bug
            // (the page-walk machinery just exposes it).
            {
                static unsigned long s_dfaultDiag = 0;
                constexpr unsigned long kCap = 256;
                uint64_t const insPcAddr   = slot.grain.pc & ~uint64_t{0x3};
                // Byte-aligned address of the SROM DTBM_SINGLE handler's VPTE
                // re-load (palBase + 0x320; PALmode adds bit 0 producing 0x...321
                // at the raw PC, but insPcAddr is already mask-aligned).
                uint64_t const handlerVpte = cpu.palBase + uint64_t{0x320};
                bool     const isVpteReload = (insPcAddr == handlerVpte);
                if (!isVpteReload && s_dfaultDiag < kCap) {
                    ++s_dfaultDiag;
                    uint32_t const enc = slot.grain.encoded;
                    unsigned const ra  = static_cast<unsigned>((enc >> 21) & 0x1Fu);
                    unsigned const rb  = static_cast<unsigned>((enc >> 16) & 0x1Fu);
                    // Displacement width depends on opcode: HW_LD (0x1B) and
                    // HW_ST (0x1F) use a 13-bit disp (bits 12:0) -- bits 15:13
                    // are the Type field (VPTE / phys / virt etc.); normal
                    // load/store opcodes use a 16-bit disp (bits 15:0).
                    int disp = 0;
                    if (slot.grain.primaryOp == 0x1Bu || slot.grain.primaryOp == 0x1Fu) {
                        int32_t const d13 = static_cast<int32_t>(enc & 0x1FFFu);
                        disp = (d13 & 0x1000) ? (d13 - 0x2000) : d13;
                    } else {
                        disp = static_cast<int16_t>(enc & 0xFFFFu);
                    }
                    uint64_t const rbVal  = cpu.intReg[rb];
                    std::fprintf(stderr,
                        "MEMDIAG-DFAULT cyc=%llu pc=0x%016llx enc=0x%08x op=0x%02x "
                        "Ra=R%u Rb=R%u(0x%016llx) disp=%d fault=%u "
                        "va<-0x%016llx va_ctl=0x%016llx excAddr=0x%016llx\n",
                        static_cast<unsigned long long>(cpu.cycleCount),
                        static_cast<unsigned long long>(slot.grain.pc),
                        static_cast<unsigned>(enc),
                        static_cast<unsigned>(slot.grain.primaryOp),
                        ra, rb,
                        static_cast<unsigned long long>(rbVal),
                        disp,
                        static_cast<unsigned>(r.faultCode),
                        static_cast<unsigned long long>(cpu.va),
                        static_cast<unsigned long long>(cpu.va_ctl),
                        static_cast<unsigned long long>(cpu.excAddr));
                }
            }
#endif
            return;
        }

        // Carry the resolved PA onto the result so the retire trace can
        // show both the effective/virtual address (memAddr) and the
        // physical address actually accessed (memPhysAddr).  For
        // S_PhysAddr accesses pa == memAddr by construction.
        r.memPhysAddr = pa;

        // Locked read / write?  S_Locked in semFlags signals the
        // LDx_L / STx_C variants.
        bool const isLocked = (static_cast<uint64_t>(r.semFlags)
            & static_cast<uint64_t>(grainFactory::GrainSem::S_Locked)) != 0;

        if (r.memIsStore) {
            applyStoreEffect(r, cpu, bus, locks, pa, isLocked);
        } else {
            applyLoadEffect(r, cpu, bus, locks, pa, isLocked);
        }
    }


    // -------------------------------------------------------------
    // applyLoadEffect
    //
    //   Read the appropriate width from GuestMemory, sign-extend per
    //   the BoxResult.h drain map, and populate regWriteValue.  On
    //   bus error, set faultCode and stash mm_stat.  For LDL_L /
    //   LDQ_L (isLocked), set the per-CPU reservation after a
    //   successful read.
    // -------------------------------------------------------------
    AXP_HOT AXP_FLATTEN
    static void applyLoadEffect(coreLib::BoxResult&      r,
                                coreLib::CpuState&       cpu,
                                memoryLib::ISystemBus&   bus,
                                memoryLib::LockMonitor&  locks,
                                coreLib::PAType          pa,
                                bool                     isLocked) noexcept
    {
        memoryLib::BusResult br = bus.read(pa, r.memSize);

        // ---- PLATFORM LEVER: ISP vs SILICON execution path (2026-06-19) ----
        // One knob, one truth: env -> 0xBFFC answer -> firmware platform().
        // apisrm pc264.c platform() reads *(int32*)0xBFFC: == 0xCAFEBEEF =>
        // ISP_MODEL (pre-silicon SIMULATOR paths -- the firmware skips the
        // real-hardware timing/probe/IDE steps V4 does not yet model, so the
        // SRM reaches >>>); any other value => REAL_HW (the faithful AXPBox-
        // level path, currently unmodeled -> stalls).  EmulatR claims ISP by
        // presenting the flag HERE as a READ-INTERCEPT, never a deposit: 0xBFFC
        // sits at image offset 0x3FFC and is overwritten by the SRM self-
        // decompressor, so a one-time store would not survive to platform().
        // EMULATR_PLATFORM: unset or "isp" => ISP (default; reaches >>>);
        // "silicon" => REAL_HW (no intercept; the REAL_HW-readiness probe path).
        // Supersedes EMULATR_CPU1_ALIVE.  Do NOT add a second host sim/silicon
        // flag -- the firmware's own platform() is the single source of truth.
        // The LDL sign-extends 0xCAFEBEEF to match the firmware's constant.
        {
            static bool const s_isp = [] {
                char const* const p = std::getenv("EMULATR_PLATFORM");
                if (p == nullptr) return true;          // default: ISP path
                // exact "silicon" selects REAL_HW; any other value stays ISP
                char const* const sel = "silicon";
                for (int i = 0;; ++i) {
                    if (p[i] != sel[i]) return true;    // differs   -> ISP
                    if (sel[i] == '\0') return false;   // full match -> SILICON
                }
            }();
            if (s_isp && pa == 0x000000000000BFFCull) {
                br.data = 0xCAFEBEEFull;                // platform() => ISP_MODEL
            }
        }
        // ---- TEMP load-watch on 0x3c970 (find the tick-counter poll loop) 2026-06-02 ----
        // The tick-delay grinds without warping; log the PC that READS 0x3c970 (the poll
        // loop) plus the loaded value and R6 (the would-be target). Prints when the PC or
        // R6 changes, capped, so we see: single big-target wait vs many small waits vs a
        // different poll PC than 0x7c314. REMOVE after we locate the loop.
#if EMULATR_BRINGUP_PROBES
        if (pa == 0x000000000003c970ull) {
            static uint64_t s_lwLastPc = ~0ull;
            static uint64_t s_lwLastR6 = ~0ull;
            static int      s_lwCount  = 0;
            if (s_lwCount < 300 &&
                (cpu.pc != s_lwLastPc || cpu.intReg[6] != s_lwLastR6)) {
                s_lwLastPc = cpu.pc;
                s_lwLastR6 = cpu.intReg[6];
                ++s_lwCount;
                std::fprintf(stderr,
                    "C970-LOADWATCH cyc=%llu pc=0x%016llx v=0x%llx R6=0x%llx R5=0x%llx\n",
                    static_cast<unsigned long long>(cpu.cycleCount),
                    static_cast<unsigned long long>(cpu.pc),
                    static_cast<unsigned long long>(br.data),
                    static_cast<unsigned long long>(cpu.intReg[6]),
                    static_cast<unsigned long long>(cpu.intReg[5]));
                std::fflush(stderr);
            }
        }
#endif
        // ---- END TEMP load-watch ----

        // ---- TEMP fclose-chain watch 2026-06-03 -- REMOVE once the toy
        // fclose NULL-dispatch is fixed.  fclose (native entry 0x5a250,
        // named via the embedded symbol table) dispatches the driver close
        // through fp->ip->dva->close; the cold-boot PC=0 halt was this
        // chain hitting a NULL link and JSRing to 0 (via the innocent
        // cns$r0=INITIAL_PCBB value at PA 0x10).  Capture the four chain
        // loads so ONE cold boot names the dead link:
        //   PC 0x5a278  LDL R4,0x68(R16)   fp->ip      (R16 = fp)
        //   PC 0x5a2c4  LDL R4,0x4(R4)     ip->dva
        //   PC 0x5a2d0  LDL R27,0x10(R4)   dva->close  (procedure value)
        //   PC 0x5a2d4  LDQ R26,0x8(R27)   PV+8        (code entry)
        // Native-mode PCs (no PAL bit).  Unthrottled by design: we need
        // the LAST fclose before a halt; volume is a few lines per fclose.
#if EMULATR_BRINGUP_PROBES
        {
            uint64_t const wpc = cpu.pc;
            if (wpc == 0x5a278ull || wpc == 0x5a2c4ull ||
                wpc == 0x5a2d0ull || wpc == 0x5a2d4ull) {
                std::fprintf(stderr,
                    "FCLOSE-WATCH cyc=%llu pc=0x%05llx pa=0x%010llx "
                    "v=0x%016llx R16=0x%010llx R3=0x%010llx\n",
                    static_cast<unsigned long long>(cpu.cycleCount),
                    static_cast<unsigned long long>(wpc),
                    static_cast<unsigned long long>(pa),
                    static_cast<unsigned long long>(br.data),
                    static_cast<unsigned long long>(cpu.intReg[16]),
                    static_cast<unsigned long long>(cpu.intReg[3]));
                std::fflush(stderr);
            }
        }
#endif
        // ---- END TEMP fclose-chain watch ----
        if (br.status != memoryLib::BusStatus::Ok) {
            r.faultCode = coreLib::kFaultBusError;
            cpu.mm_stat = r.memAddr;
            // Bridge: signal "external bus error" into the CBox ERROR_REG
            // chain so the PAL MCHK handler's sys__cbox poll sees non-zero
            // bits and identifies the error class.  Without this, the
            // chain reads all zeros and the handler falls through to its
            // "unknown error class -> halt" recovery path (R2=0 -> PC=0
            // -> CALL_PAL HALT).
            //
            // 2026-05-28 update: also pre-populate dataReg.  Observed PAL
            // sys__cbox writes 0 to HW_C_SHFT (no shift trigger in our
            // model), then reads HW_C_DATA expecting the current visible
            // 6-bit window.  Real silicon likely either (a) shifts on any
            // C_SHFT write, or (b) auto-snapshots dataReg from chain top
            // when error events arrive.  Setting dataReg directly here
            // matches (b) semantics and is the minimum-perturbation
            // experiment to verify the PAL's decision branches on a
            // non-zero dataReg.  Companion latch in chipset reportNxm/
            // latchNxm sets MISC.NXM.  Specific bit positions per HRM 5.4
            // may need refinement after observing the PAL's response.
            cpu.cBox.errorReg |= 0x1ULL;
            cpu.cBox.dataReg = 0x01;
            return;
        }
        uint64_t const raw = br.data;

#if EMULATR_MEMDIAG
        // TEMP LOAD-WATCH 2026-05-30 -- REMOVE BEFORE COMMIT.  The PAL-takeover
        // copy at PC 0x600938 writes the corrupt byte (0xd956: 0xe2 vs 0xe6)
        // into PA 0xd950 around cyc 5528602.  Capture every load in that copy
        // window to see the SOURCE byte: if a load returns ...e2... the source
        // is already corrupt (decompression bug); if ...e6... the copy mangled
        // it (a byte-extract/insert/ALU slip).  Tight cyc gate (this run's
        // timing; deterministic across cold boots).
        if (cpu.cycleCount >= 5528490ull && cpu.cycleCount <= 5528660ull) {
            std::fprintf(stderr,
                "LOAD-WATCH cyc=%llu pc=0x%016llx sz=%u pa=0x%016llx raw=0x%016llx\n",
                static_cast<unsigned long long>(cpu.cycleCount),
                static_cast<unsigned long long>(cpu.pc),
                static_cast<unsigned>(r.memSize),
                static_cast<unsigned long long>(pa),
                static_cast<unsigned long long>(raw));
            std::fflush(stderr);
        }

        // TEMP TICK-LOADWATCH 2026-05-31 -- REMOVE BEFORE COMMIT.  Pair to the
        // store-side TICK-STOREWATCH below.  PA 0x1d0d6c = pcb$q_cputime
        // (pcb 0x1d0ca0 + 0xcc), the first counter the clock ISR bumps.  The
        // generic +1 scan showed it is written 3->4 EVERY tick but never
        // 4->5 -- i.e. the next read returns 3 again.  This logs every LOAD of
        // that PA (value + faulting VA) so we can see (a) whether a load ever
        // returns 4, and (b) whether the load VA matches the store VA.  If the
        // store and load VAs differ, this is a translation/aliasing bug (ISR
        // increments a phantom PA); if same VA + load still returns 3, it is a
        // store-visibility/coherence bug in the bus/GuestMemory path.
        if (pa == 0x00000000001d0d6cull) {
            std::fprintf(stderr,
                "TICK-LOADWATCH cyc=%llu pc=0x%016llx va=0x%016llx pa=0x%016llx "
                "asn=0x%llx ptbr=0x%llx mode=%d pal=%d raw=0x%016llx\n",
                static_cast<unsigned long long>(cpu.cycleCount),
                static_cast<unsigned long long>(cpu.pc),
                static_cast<unsigned long long>(r.memAddr),
                static_cast<unsigned long long>(pa),
                static_cast<unsigned long long>(cpu.asn),
                static_cast<unsigned long long>(cpu.ptbr),
                static_cast<int>(cpu.mode),
                cpu.inPalMode() ? 1 : 0,
                static_cast<unsigned long long>(raw));
            std::fflush(stderr);
        }
#endif

        r.regWriteValue = formatLoadValue(raw, r.memSize, r.regWriteIsFp,
                                          r.semFlags);

        if (isLocked) {
            // LDL_L / LDQ_L: arm THIS CPU's reservation in the
            // LockMonitor SSOT at cache-line granularity.  A subsequent
            // STL_C / STQ_C by the same CPU to the same line succeeds;
            // a store by any CPU (or DMA) to that line clears it via
            // LockMonitor::clearLine.
            locks.set(static_cast<int>(cpu.cpuSlot), pa);
        }
    }


    // -------------------------------------------------------------
    // applyStoreEffect
    //
    //   For an unconditional store: write memData at pa.  For a
    //   conditional store (STL_C / STQ_C, isLocked): check the
    //   reservation, perform the write only on hit, and rewrite
    //   regWriteValue to 1 / 0 per success.  Always clear
    //   hasReservation -- success or failure -- on the conditional
    //   path.
    // -------------------------------------------------------------
    AXP_HOT AXP_FLATTEN
    static void applyStoreEffect(coreLib::BoxResult&      r,
                                 coreLib::CpuState&       cpu,
                                 memoryLib::ISystemBus&   bus,
                                 memoryLib::LockMonitor&  locks,
                                 coreLib::PAType          pa,
                                 bool                     isLocked) noexcept
    {
        int const lockSlot = static_cast<int>(cpu.cpuSlot);
        if (isLocked) {
            // STL_C / STQ_C.  Reservation valid iff THIS CPU still holds
            // a LockMonitor reservation on this cache line.
            bool const valid = locks.check(lockSlot, pa);

            locks.clear(lockSlot);          // cleared regardless of outcome

            if (!valid) {
                // Lock lost: skip the publish, success indicator = 0.
                r.regWriteValue = 0;
                return;
            }
            // Fall through to the publish below.  regWriteValue is
            // overwritten with 1 after a successful store.
        }

#if EMULATR_MEMDIAG
        // TEMP STORE-WATCH 2026-05-30 -- REMOVE BEFORE COMMIT.  Catch any guest
        // store landing in the aligned quad (PA 0xd950-0xd957) that holds the
        // corrupt OSF PAL word at 0xd954 (hw_ret R2 vs R6).  UNGATED by cycle
        // window so it fires during early self-decompression too.  v= reveals
        // whether the wrong byte (0xd956: 0xe2 vs 0xe6) arrives via the store
        // value (a decode/decompress/ALU slip) or is clobbered later.  If this
        // NEVER fires, the bytes were placed host-side by SrmLoader (no guest
        // store) -- check the loader/firmware image instead.
        if (pa >= 0x0000000000000d950ull && pa <= 0x0000000000000d957ull) {
            std::fprintf(stderr,
                "STORE-WATCH cyc=%llu pc=0x%016llx sz=%u pa=0x%016llx v=0x%016llx\n",
                static_cast<unsigned long long>(cpu.cycleCount),
                static_cast<unsigned long long>(cpu.pc),
                static_cast<unsigned>(r.memSize),
                static_cast<unsigned long long>(pa),
                static_cast<unsigned long long>(r.memData));
            std::fflush(stderr);
        }

        // TEMP TICK-STOREWATCH 2026-05-31 -- REMOVE BEFORE COMMIT.  Pair to the
        // TICK-LOADWATCH on the load path.  PA 0x1d0d6c = pcb$q_cputime; the
        // clock ISR writes it 3->4 every tick but the next read returns 3.
        // Log every STORE of that PA (value + faulting VA) so store VA/PA can
        // be compared with the load VA/PA: VA mismatch => translation aliasing
        // (ISR stores to a phantom PA); VA match but load returns stale =>
        // store-visibility/coherence bug.
        if (pa == 0x00000000001d0d6cull) {
            std::fprintf(stderr,
                "TICK-STOREWATCH cyc=%llu pc=0x%016llx va=0x%016llx pa=0x%016llx "
                "asn=0x%llx ptbr=0x%llx mode=%d pal=%d sz=%u v=0x%016llx\n",
                static_cast<unsigned long long>(cpu.cycleCount),
                static_cast<unsigned long long>(cpu.pc),
                static_cast<unsigned long long>(r.memAddr),
                static_cast<unsigned long long>(pa),
                static_cast<unsigned long long>(cpu.asn),
                static_cast<unsigned long long>(cpu.ptbr),
                static_cast<int>(cpu.mode),
                cpu.inPalMode() ? 1 : 0,
                static_cast<unsigned>(r.memSize),
                static_cast<unsigned long long>(r.memData));
            std::fflush(stderr);
        }

        // ---- TEMP store-watch on 0x3c970 (poll-counter producer) 2026-06-01 ----
        // The console-init wait loop at 0x7c2xx spins until mem[0x3c970] reaches 2;
        // it sits at 1 and nothing was seen writing it in the break-trace window.
        // Log every store to that PA -- PC, value, size, faulting VA, palMode -- so
        // we can identify the PRODUCER that should bump 1->2 (ENV/EEPROM? a device
        // probe? an ISR?) and watch when/whether it advances. Cheap: one compare per
        // store, not gated by EMULATR_MEMDIAG so it runs in the normal build.
        // REMOVE once the producer is identified.
        if (pa == 0x000000000003c970ull) {
            std::fprintf(stderr,
                "C970-STOREWATCH cyc=%llu pc=0x%016llx va=0x%016llx pa=0x%016llx "
                "sz=%u v=0x%016llx pal=%d\n",
                static_cast<unsigned long long>(cpu.cycleCount),
                static_cast<unsigned long long>(cpu.pc),
                static_cast<unsigned long long>(r.memAddr),
                static_cast<unsigned long long>(pa),
                static_cast<unsigned>(r.memSize),
                static_cast<unsigned long long>(r.memData),
                cpu.inPalMode() ? 1 : 0);
            std::fflush(stderr);
        }
        // ---- END TEMP store-watch ----

        // ---- TEMP store-watch on the GCT/FRU config tree 2026-06-06 (task #11) ----
        // #11 verdict is BUILD-SIDE: the firmware-built GCT at 0x3f32000 contains a
        // cyclic link (terminal node 0x3f32ac0 rings back to head 0x3f32040 forever).
        // The firmware SELF-builds this tree ("initializing GCT/FRU at 3f32000") by
        // enumerating hardware V4 provides; the cycle is a side-effect of a missing
        // or wrong construction INPUT, not a GCT builder V4 owns.  Log every store
        // into the tree's 4 KiB page during the cold-boot construction phase so we
        // see WHICH pc writes each node-link field, the stored VALUE, and R26 (RA =
        // the building function) -- exposing the bad link being laid down and its
        // source.  Env-gated (EMULATR_GCT_WATCH=1) so normal/restore runs stay
        // silent; arm it only on the diagnostic cold boot.  Construction happens
        // late (during from_init, ~21.5B cyc), so expect output near the snapshot
        // anchor, not early.  REMOVE once the construction input is fixed.
        {
            static bool const s_gctWatch =
                (std::getenv("EMULATR_GCT_WATCH") != nullptr);
            if (s_gctWatch &&
                pa >= 0x000000000003f32000ull && pa <= 0x000000000003f33fffull) {
                std::fprintf(stderr,
                    "GCT-STOREWATCH cyc=%llu pc=0x%016llx va=0x%016llx pa=0x%016llx "
                    "sz=%u v=0x%016llx pal=%d ra=0x%016llx\n",
                    static_cast<unsigned long long>(cpu.cycleCount),
                    static_cast<unsigned long long>(cpu.pc),
                    static_cast<unsigned long long>(r.memAddr),
                    static_cast<unsigned long long>(pa),
                    static_cast<unsigned>(r.memSize),
                    static_cast<unsigned long long>(r.memData),
                    cpu.inPalMode() ? 1 : 0,
                    static_cast<unsigned long long>(cpu.intReg[26]));
                std::fflush(stderr);
            }
        }
        // ---- END GCT store-watch ----

        // ---- STORE-WATCH + conditional break: PA 0x10 bad-descriptor-ptr hunt
        //      2026-06-03 (cold-boot PC=0 halt root) -- REMOVE once found ----
        // Root of the clean-cold-boot halt (full-trace 20260603-111611_srm.trc):
        //   0x5a2d0  LDL R27 <- *(PA 0x10) = 0x8c00   (ptr lands in PAL space!)
        //   0x5a2d4  LDQ R26 <- *(0x8c08)  = 0        (descriptor entry = 0)
        //   0x5a2d8  JSR R26,(R26)  R26=0 -> PC 0 -> CALL_PAL HALT
        // PA 0x10 holds the WRONG descriptor pointer; it is planted EARLIER in
        // the cold boot (image has 0 at offset 0x10; nothing writes it inside
        // the 21.43B snapshot window). Log every store landing in the 0x10 quad
        // AND the 0x8c00 descriptor -- PC, VA, PA, size, VALUE, palMode, RA --
        // to identify the producer that writes 0x8c00. Cheap: a couple compares
        // per store; runs in the normal build (NOT gated by EMULATR_MEMDIAG).
        //
        // CONDITIONAL DEBUG BREAK: set EMULATR_BREAK_ON_PA10 to __debugbreak()
        // AFTER logging when a store targets the PA 0x10 quad, so an attached VS
        // debugger pops at the storing instruction (walk one frame up to read
        // the source register / call site). Default OFF -- never set in a
        // headless run (it pops the OS JIT prompt). To narrow to the bad write
        // only, change the guard to also require r.memData == 0x8c00.
        if ((pa >= 0x0000000000000010ull && pa <= 0x0000000000000017ull) ||
            (pa >= 0x0000000000008c00ull && pa <= 0x0000000000008c0full)) {
            std::fprintf(stderr,
                "PA10-STOREWATCH cyc=%llu pc=0x%016llx va=0x%016llx pa=0x%016llx "
                "sz=%u v=0x%016llx pal=%d ra=0x%016llx\n",
                static_cast<unsigned long long>(cpu.cycleCount),
                static_cast<unsigned long long>(cpu.pc),
                static_cast<unsigned long long>(r.memAddr),
                static_cast<unsigned long long>(pa),
                static_cast<unsigned>(r.memSize),
                static_cast<unsigned long long>(r.memData),
                cpu.inPalMode() ? 1 : 0,
                static_cast<unsigned long long>(cpu.intReg[26]));
            std::fflush(stderr);

            static bool const s_breakOnPa10 =
                (std::getenv("EMULATR_BREAK_ON_PA10") != nullptr);
            if (s_breakOnPa10 &&
                pa >= 0x0000000000000010ull && pa <= 0x0000000000000017ull) {
#if defined(_MSC_VER)
                __debugbreak();
#else
                std::fprintf(stderr,
                    "PA10-STOREWATCH: break requested but __debugbreak "
                    "unavailable on this toolchain\n");
                std::fflush(stderr);
#endif
            }
        }
        // ---- END PA 0x10 store-watch ----

        // ---- TEMP fp->ip store-watch 2026-06-03 -- REMOVE with the
        // fclose-chain watch.  The halt run's toy FILE block sat at PA
        // 0x1cc4c0 (R3/R16/R22 in the halt register dump); its inode
        // pointer slot fp->ip is fp+0x68 = 0x1cc528.  Log every store
        // into that slot to catch a clobber between fopen and the fatal
        // fclose.  Only meaningful for runs that reproduce the stub-era
        // trajectory (the ToyRtc landing alongside this watch shifts
        // allocation order); harmless noise otherwise.
        if (pa >= 0x00000000001cc528ull && pa <= 0x00000000001cc52full) {
            std::fprintf(stderr,
                "FPIP-STOREWATCH cyc=%llu pc=0x%016llx pa=0x%010llx "
                "sz=%u v=0x%016llx pal=%d ra=0x%010llx\n",
                static_cast<unsigned long long>(cpu.cycleCount),
                static_cast<unsigned long long>(cpu.pc),
                static_cast<unsigned long long>(pa),
                static_cast<unsigned>(r.memSize),
                static_cast<unsigned long long>(r.memData),
                cpu.inPalMode() ? 1 : 0,
                static_cast<unsigned long long>(cpu.intReg[26]));
            std::fflush(stderr);
        }
        // ---- END fp->ip store-watch ----
#endif
        // ---- TEMP START-WATCH (VALUE) 2026-06-21 -- REMOVE BEFORE COMMIT ----
        // Base-pinning for secondary-CPU bring-up: catch start_secondary(id)
        // staging the entry address (PAL$PAL_BASE+1 = palBase|1 at the store
        // moment) into PAL$CPU0_START_BASE[id], wherever the linkage base
        // resolves.  Keyed by VALUE so it self-locates regardless of base;
        // covers the relocation epochs palBase can be in when start_secondaries
        // runs (0x900000 stub / 0x600000 target / SDL 0x8000).  The hit paired
        // with the immediately-following TIG 0xC00029 write is the slot.  Gated
        // by EMULATR_START_WATCH (cached getenv); (void)0 when unset.  See
        // journals/20260621_storewatch_cpu0_start_base_base_pinning.md.
        {
            static bool const s_startWatch =
                (std::getenv("EMULATR_START_WATCH") != nullptr);
            if (s_startWatch) {
                // (a) candidate slot store: staged entry value palBase|1.  NOTE:
                // 0x8001 (compile-time PAL$PAL_BASE+1) is ubiquitous, so a (val)
                // hit alone is NOT the slot -- only the one whose cyc immediately
                // precedes a (kick) line below is start_secondary's slot store.
                if (r.memSize == 8 &&
                    (r.memData == 0x0000000000900001ull ||
                     r.memData == 0x0000000000600001ull ||
                     r.memData == 0x0000000000008001ull)) {
                    std::fprintf(stderr,
                        "START-WATCH(val) cyc=%llu pc=0x%016llx pa=0x%016llx "
                        "v=0x%016llx pal=%d palBase=0x%016llx\n",
                        static_cast<unsigned long long>(cpu.cycleCount),
                        static_cast<unsigned long long>(cpu.pc),
                        static_cast<unsigned long long>(pa),
                        static_cast<unsigned long long>(r.memData),
                        cpu.inPalMode() ? 1 : 0,
                        static_cast<unsigned long long>(cpu.palBase));
                    std::fflush(stderr);
                }
                // (b) THE unique start_secondary signal: the TIG CPU-START kick
                // (outtig 0xC00028+id -> store to kIpcr0..3 PA 0x801_3000_0A00 +
                // id*0x40).  Its presence proves start_secondary ran; the (val)
                // store just before it is the slot, and that pa is the answer.
                if (pa >= 0x0000080130000A00ull && pa <= 0x0000080130000AC0ull) {
                    std::fprintf(stderr,
                        "START-WATCH(kick) cyc=%llu pc=0x%016llx pa=0x%016llx "
                        "id=%u v=0x%016llx pal=%d palBase=0x%016llx\n",
                        static_cast<unsigned long long>(cpu.cycleCount),
                        static_cast<unsigned long long>(cpu.pc),
                        static_cast<unsigned long long>(pa),
                        static_cast<unsigned>((pa - 0x0000080130000A00ull) / 0x40ull),
                        static_cast<unsigned long long>(r.memData),
                        cpu.inPalMode() ? 1 : 0,
                        static_cast<unsigned long long>(cpu.palBase));
                    std::fflush(stderr);
                }
            }
        }
        // ---- END START-WATCH ----
        memoryLib::BusResult const br = bus.write(pa, r.memData, r.memSize);
        if (br.status != memoryLib::BusStatus::Ok) {
            r.faultCode = coreLib::kFaultBusError;
            cpu.mm_stat = r.memAddr;
            // Bridge: signal "external bus error" into the CBox ERROR_REG
            // chain so the PAL MCHK handler's sys__cbox poll sees non-zero
            // bits and identifies the error class.  Without this, the
            // chain reads all zeros and the handler falls through to its
            // "unknown error class -> halt" recovery path (R2=0 -> PC=0
            // -> CALL_PAL HALT).
            //
            // 2026-05-28 update: also pre-populate dataReg.  Observed PAL
            // sys__cbox writes 0 to HW_C_SHFT (no shift trigger in our
            // model), then reads HW_C_DATA expecting the current visible
            // 6-bit window.  Real silicon likely either (a) shifts on any
            // C_SHFT write, or (b) auto-snapshots dataReg from chain top
            // when error events arrive.  Setting dataReg directly here
            // matches (b) semantics and is the minimum-perturbation
            // experiment to verify the PAL's decision branches on a
            // non-zero dataReg.  Companion latch in chipset reportNxm/
            // latchNxm sets MISC.NXM.  Specific bit positions per HRM 5.4
            // may need refinement after observing the PAL's response.
            cpu.cBox.errorReg |= 0x1ULL;
            cpu.cBox.dataReg = 0x01;
            return;
        }

        // Store published successfully.  Per Alpha LL/SC semantics, a
        // store to a reserved cache line clears the reservation of every
        // OTHER CPU on that line (the storing CPU is excepted so its own
        // plain store does not self-invalidate -- a successful STx_C has
        // already cleared its own reservation above via locks.clear).
        // With one active CPU exceptCpu==lockSlot makes this a no-op, so
        // the single-CPU boot stays byte-identical; with a second agent
        // it is the cross-CPU invalidation that makes contended STx_C
        // correct.
        //
        // TODO(Phase 4, 2nd agent): when MemDrainer runs under
        //   ThreadedDriver with >1 agent, this shared-state mutation must
        //   be STAGED in step() and applied in syncPhase (see SmpHarness
        //   staged-Effect discipline) so determinism_equivalence stays
        //   bit-identical; inline mutation is safe only while exactly one
        //   agent steps the single LockMonitor.
        // TODO(DMA): device/DMA writes that bypass MemDrainer must also
        //   call locks.clearLine(pa, -1); no DMA path exists on the
        //   single-CPU boot yet.
        locks.clearLine(pa, lockSlot);

        if (isLocked) {
            // Successful conditional store: success indicator = 1.
            r.regWriteValue = 1;
        }
    }
};

} // namespace pipelineLib

#endif // PIPELINELIB_MEMDRAINER_H
