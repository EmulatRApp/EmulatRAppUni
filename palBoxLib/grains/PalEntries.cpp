// ============================================================================
// palBoxLib/grains/PalEntries.cpp -- palBox HW_xxx and CALL_PAL leaves (v1)
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
// Hand-written leaf functions for the palBox dispatch arm.  First-
// wave scope is the eight wired leaves declared in
// grainFactoryLib/generated/GrainsForward.h's palBox section:
//
//   HW_MFPR  (opcode 0x19)             read internal processor reg
//   HW_MTPR  (opcode 0x1D)             write internal processor reg
//   HW_REI   (opcode 0x1E)             return from PAL; resume EXC_ADDR
//   HALT     CALL_PAL (both)           stop the processor
//   BPT      CALL_PAL (Tru64)          breakpoint trap
//   BPT      CALL_PAL (VMS)            breakpoint trap
//   CHME     CALL_PAL (VMS only)       change mode to executive
//   CHMK     CALL_PAL (Tru64 only)     change mode to kernel
//
// V4 v1 stance:
//
//   Of the eight, only HALT has a real body in this wave.  The
//   other seven raise kFaultUnimplemented.  This is the minimum-
//   viable cut consistent with the V4 scope discipline -- defer
//   non-blocking PAL work until a consumer demands it.  In v1
//   nothing loads PALcode, so HW_MFPR / HW_MTPR / HW_REI / BPT /
//   CHME / CHMK are unreachable from user-facing test code, and a
//   stub that raises kFaultUnimplemented surfaces immediately if a
//   test accidentally hits one without inventing CpuState plumbing
//   that would be discarded once a real PAL design lands.
//
//   HALT is the exception because v1 user code may legitimately
//   want to stop a test cleanly via "CALL_PAL HALT".  Its body
//   sets faultCode = kFaultHalt; the pipeline driver intercepts
//   that at WB and terminates the run.  No register or memory
//   effect.  Both PAL personalities share this body (the codegen
//   does not suffix execHalt because the row carries both
//   S_PalTru64 and S_PalVms).
//
// Prerequisites for the stubbed leaves:
//
//   HW_MFPR / HW_MTPR  need a non-const CpuState reachable through
//                      ExecCtx, plus the IPR catalog mapping
//                      encoded[15:8] to a per-CPU field.  V1 iprLib
//                      has the enumeration; we port it when loading
//                      PALcode is on the table.
//
//   HW_REI             needs a read of EXC_ADDR plus an update of
//                      PS<PALmode>; same CpuState dependency, plus
//                      a way to clear PAL mode in the divert path.
//
//   BPT / CHME / CHMK  need the PAL vector table giving the entry
//                      point for each CALL_PAL function.  V1
//                      palLib_EV6/global_PalVectorTable holds the
//                      canonical addresses.  The leaf packs
//                      divertTarget = vectorTable[func] plus a
//                      mode-change effect once that surface lands.
//
// ============================================================================


#include "coreLib/BoxResult.h"
#include "coreLib/CpuState.h"
#include "coreLib/Ev6EntryVectors.h"
#include "coreLib/ExecCtx.h"
#include "coreLib/HW_IPR.h"
#include "coreLib/InstructionGrain.h"
#include "coreLib/IprFields.h"
#include "coreLib/PalShadow.h"
#include "coreLib/axp_attributes_core.h"

#include "grainFactoryLib/generated/SemanticFlagsEnum.h"
#include "mmuLib/CboxEventLog.h"
#include "pteLib/Ev6PteFormat.h"

// CSERVE C2: terminal-I/O routes through the (already-compiled) console
// manager.  ConsoleManager is global-namespace (Qt); global_ConsoleManager()
// returns the process singleton with a StdoutConsoleBackend registered on OPA0.
#include "deviceLib/ConsoleManager.h"
#include "deviceLib/global_ConsoleManager.h"
#include "deviceLib/Hwrpb.h"          // deviceLib::hwrpb::Hwpcb layout (SWPCTX)
#include "deviceLib/HwpcbContext.h"   // loadCpuFromHwpcb / storeCpuToHwpcb (SWPCTX)
#include "memoryLib/GuestMemory.h"   // CSERVE PUTS reads its buffer via ExecCtx::memory

#include <cstdint>
#include <cstdio>

// MEMDIAG -- temporary TB-fill probe.  Shares the on/off switch name with
// MemDrainer.h but is defined locally because this TU does not include that
// header.  Logs every ITB/DTB fill whose tag VA falls in the DS10 region
// under scrutiny (0x600000..0x607fff), so we capture the staged tag + PTE
// and the decoded PFN regardless of which cycle the fill happens in.  Tells
// us whether the firmware installs VPN 0x301 -> PFN 0x300 (it was fed a bad
// faulting VA) or stages identity and V4's insert corrupts it.  Revert to 0
// here AND in MemDrainer.h when the probe is done.
#ifndef EMULATR_MEMDIAG
#define EMULATR_MEMDIAG 0   // dormant TB-fill diagnostic scaffold (set 1 to re-enable)
#endif

namespace palBox {

// TEMP DIAGNOSTIC (SDE shadow-swap ledger) forward decls -- REMOVE WITH the
// DIVERT-REI block.  Storage + sdeLog body live in the palDiag namespace near
// execHwRei (~1921); execHwMtpr (HW_I_CTL, above that point) needs them
// forward-declared.  See project_ds20_wall_sde_shadow_choreography.
namespace palDiag {
extern bool g_sdeTraceArmed;
extern int  g_sdeTraceWindows;
void sdeLog(char const* tag, coreLib::CpuState const& cpu) noexcept;
} // namespace palDiag

using coreLib::BoxResult;
using coreLib::CpuState;
using coreLib::ExecCtx;
using coreLib::HW_IPR;
using coreLib::InstructionGrain;

// Forward declaration: the generic CALL_PAL dispatcher is defined further
// down in this TU (near the HW_xxx region) but is called by hand-written
// CALL_PAL leaves that appear above its definition (execChme_vms and the
// bulk-delegating S_PalEntry leaves).  Declaring it here gives those
// earlier leaves a visible name to call without reordering the file.
AXP_HOT AXP_FLATTEN
auto execCallPalDispatch(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult;


// ---------------------------------------------------------------------------
// Encoding helpers.
// ---------------------------------------------------------------------------
// Hw-format HW_MFPR / HW_MTPR encoding (per Digital PALcode macros
// in apisrm/ref/ev6_huf_decom.m64, hw_mfpr / hw_mtpr macros):
//   bits[31:26]  primary opcode (0x19 MFPR, 0x1D MTPR)
//   bits[25:21]  Ra -- destination GPR on MFPR; R31 (unused) on MTPR
//   bits[20:16]  Rb -- R31 (unused) on MFPR; source GPR on MTPR
//   bits[15:8]   scbd (8-bit IPR selector)
//   bits[7:0]    function/extension (currently unused)
//
// IMPORTANT: HW_MTPR sources from Rb (bits 20:16), not Ra.  Earlier
// V4 (and inherited V1) erroneously read Ra; the macro above hardwires
// Ra to ^x1f (R31) on every hw_mtpr emit, so the bug was invisible
// from a code-reading standpoint but silently wrote 0 to every IPR.
// The TSV row therefore sets S_ReadsRb (NOT S_ReadsRa) for HW_MTPR
// so the pipeline populates c.opB with R[Rb], and the leaf reads
// c.opB as the IPR write value.  HW_MFPR has no GPR source -- it
// writes Ra and reads no operand.
//
// The HW_IPR enum adds 0x0100 to scbd to namespace it away from the
// PALcode-visible function codes (PAL_MFPR / PAL_MTPR).  PAL_TEMP
// disambiguation (raw scbd 0x40..0x57 -> PT0..PT23 vs HW_PCTX at
// scbd 0x40) is left to a future iprLib port; the leaf below already
// holds PAL_TEMP cases and isPalTemp / palTempIndex helpers, plus
// CpuState::palTemp[24] storage, so once iprSelector grows the
// disambiguation logic the PT path goes live without further edits.
//
// V1 reference: when this lands, raw scbd 0x40..0x57 should map to
// HW_PAL_TEMP_n via the +0x01C0 namespace offset (yielding
// 0x0200..0x0217), with HW_PCTX (raw scbd 0x40, +0x0100 offset)
// reached only when the encoding signals the non-PAL_TEMP arm.  The
// 21264 HRM disambiguator is bit-encoding state we have not ported
// yet; until then HW_MFPR / HW_MTPR with raw scbd 0x40 always
// resolves to HW_PCTX, and raw scbd 0x41..0x57 fall through to the
// default (kFaultUnimplemented) -- which is the right diagnostic
// behaviour: a halt with the encoding visible in the lookback ring.

[[nodiscard]] AXP_HOT AXP_FLATTEN
constexpr HW_IPR iprSelector(InstructionGrain const& g) noexcept
{
    const uint16_t scbd = static_cast<uint16_t>((g.encoded >> 8) & 0xFFu);
    // Raw scbd 0x40..0x5F is the PAL_TEMP range (PT0..PT31).  V4
    // namespaces these at 0x01C0 + scbd = 0x0200..0x021F so they don't
    // collide with the regular hardware IPR range (0x0100..).  EV6
    // shadows HW_PCTX at raw scbd 0x40 with the disambiguator in the
    // encoding's function bits; V4 has not ported that disambiguator
    // yet, so raw scbd 0x40 resolves to HW_PAL_TEMP_0 (PT0) rather
    // than HW_PCTX.  HW_PCTX is rarely accessed via HW_MFPR/HW_MTPR
    // by PALcode (it's used by hardware during context switch), so
    // this shadowing is acceptable until the function-bit
    // disambiguator lands.
    if (scbd >= 0x40u && scbd <= 0x5Fu) {
        return static_cast<HW_IPR>(0x01C0u + scbd);
    }
    return static_cast<HW_IPR>(0x0100u + scbd);
}

[[nodiscard]] AXP_HOT AXP_FLATTEN
constexpr uint8_t raIndex(InstructionGrain const& g) noexcept
{
    return static_cast<uint8_t>((g.encoded >> 21) & 0x1Fu);
}


#pragma region CALL_PAL Stubs (PalVectorTable prerequisite)

// ----------------------------------------------------------------------------
// BPT (Tru64) -- breakpoint trap, Tru64 personality.  TODO: pack
// divertTarget = palVectorTable[BPT_tru64] once the vector table
// surface lands; for now stub at kFaultUnimplemented.
// ----------------------------------------------------------------------------
AXP_HOT AXP_FLATTEN
auto execBpt_tru64(InstructionGrain const& g, [[maybe_unused]] ExecCtx const& c) noexcept -> BoxResult
{
    BoxResult r;
    r.semFlags  = g.semFlags;
    r.faultCode = coreLib::kFaultUnimplemented;
    return r;
}

// ----------------------------------------------------------------------------
// BPT (VMS) -- breakpoint trap, VMS personality.  Same stub shape;
// distinct from BPT_tru64 because the canonical entry address
// differs between personalities.
// ----------------------------------------------------------------------------
AXP_HOT AXP_FLATTEN
auto execBpt_vms(InstructionGrain const& g, [[maybe_unused]] ExecCtx const& c) noexcept -> BoxResult
{
    BoxResult r;
    r.semFlags  = g.semFlags;
    r.faultCode = coreLib::kFaultUnimplemented;
    return r;
}

// ----------------------------------------------------------------------------
// CHME (VMS only) -- change mode to executive.  CALL_PAL func 0x82
// (unprivileged), VMS personality only.  Diverts into PALcode at
// palBase + 0x3080 (unprivileged vector formula 0x3000 | ((func & 0x3F)
// << 6), per HRM 6.8.1).  Mode change and stack swap happen in PALcode.
// Body delegates to execCallPalDispatch like every other S_PalEntry
// row; a per-function home is preserved here in case CHME ever needs
// inline-execute (S_PalIntrinsic) semantics.
// ----------------------------------------------------------------------------
AXP_HOT AXP_FLATTEN
auto execChme_vms(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult
{
    return execCallPalDispatch(g, c);
}

// ----------------------------------------------------------------------------
// CHMK (Tru64) -- change mode to kernel.  Tru64 syscall path; same
// stub shape as CHME.
// ----------------------------------------------------------------------------
AXP_HOT AXP_FLATTEN
auto execChmk_tru64(InstructionGrain const& g, [[maybe_unused]] ExecCtx const& c) noexcept -> BoxResult
{
    BoxResult r;
    r.semFlags  = g.semFlags;
    r.faultCode = coreLib::kFaultUnimplemented;
    return r;
}

#pragma endregion CALL_PAL Stubs (PalVectorTable prerequisite)


#pragma region CALL_PAL CSERVE intrinsic

// ----------------------------------------------------------------------------
// CSERVE -- console / firmware service intrinsic.  CALL_PAL function
// 0x09, valid under both PAL personalities.
//
// Architectural posture (S_PalIntrinsic, NOT S_PalEntry):
//
//   CSERVE is a CALL_PAL opcode but does NOT enter PALcode.  V1's
//   handler reads R16/R17/R18/R19 in C++, computes a result, writes
//   R0, and the pipeline retires to PC+4.  No PC redirect, no PAL
//   environment entry, no PALmode flip.  This is the canonical
//   inline-executed CALL_PAL function and the reason S_PalIntrinsic
//   exists as its own flag separate from S_PalEntry.
//
// V1 ABI (palLib_EV6/Pal_Service.h):
//
//   R16[7:0]  -- function code (0x01=GETC, 0x02=PUTC, 0x07=CONSOLE_OPEN,
//                0x09=PUTS, 0x0C=GETS, 0x20=GET_ENV, 0x21=SET_ENV,
//                0x22=SAVE_ENV, 0x23=CLEAR_ENV, 0x30=GET_TIME,
//                0x31=SET_TIME, 0x32=GET_TIME_OFFSET, ...)
//                NOTE: V1 also listed 0x44=WRITE_PATTERN and 0x65=Bcache
//                here; both are RETIRED mislabels.  The SRM firmware runs
//                the OpenVMS EV6 PAL, where 0x44 is MTPR_EXC_ADDR and
//                0x65 is MP_WORK_REQUEST (authoritative dispatch below).
//   R16[63:8] -- reserved
//   R17       -- arg1 (function-specific)
//   R18       -- arg2 (function-specific)
//   R19       -- arg3 (function-specific)
//   R0        -- return value (function-specific)
//
// ASA-standard function code mapping (Alpha Architecture guide,
// Console section -- saved verbatim in
// memory/reference_cserve_and_initial_vm_regions.md):
//
//   Terminal I/O routines (0x00..0x0F):
//     0x01 getc           0x02 puts            0x03 reset_term
//     0x04 set_term_int   0x05 set_term_ctl    0x06 process_keycode
//     0x07-0x0F reserved
//
//   Generic I/O device routines (0x10..0x1F):
//     0x10 open  0x11 close  0x12 ioctl  0x13 read  0x14 write
//     0x15-0x1F reserved
//
//   Environment variable routines (0x20..0x2F):
//     0x20 set_env  0x21 reset_env  0x22 get_env  0x23 save_env
//
//   Miscellaneous routines (0x30+):
//     0x30 pswitch  0x31 fixup  0x32 bios_emul   others reserved
//
// RETIRED V1 mislabels (vendor fiction, outside ASA): V1 called 0x44
// "WRITE_PATTERN" and 0x65 "Bcache/chipset hw init".  Neither is what
// the real SRM .exe issues -- under the OpenVMS EV6 PAL the firmware
// runs, 0x44 = MTPR_EXC_ADDR (68) and 0x65 = MP_WORK_REQUEST (101).
//
// PS<CM> == 1 (executive mode) requires CSERVE to raise OPCDEC; from
// Kernel/PAL the action is implementation-dependent.  V4 currently
// does not enforce the PS check -- TODO when mode-aware privilege
// validation lands.
//
// V4 behaviour (VMS personality):
//
//   Function 0x44 (MTPR_EXC_ADDR) loads EXC_ADDR = R17 and diverts the
//   pipeline to R17 -- the huf_decom switch: console hand-off; it does
//   NOT write R0.  Function 0x65 (MP_WORK_REQUEST) has no secondary CPU
//   to signal on the V4 uniprocessor model, so it falls through to the
//   tolerant default: R0 untouched, no fault, no divert.  Unrecognized
//   function codes are likewise TOLERATED (no-op, R0 untouched, no
//   fault), matching silicon -- the "CSERVE Defaulted" diagnostic still
//   names the function for the trace lookback ring.  V4 no longer raises
//   kFaultUnimplemented for unknown CSERVE; that was an artificial
//   fatality that diverged from real hardware.
//
//   Console I/O (PUTC/PUTS/GETC/GETS), env-store (GET_ENV/SET_ENV),
//   and TOY clock (GET_TIME/SET_TIME) all need consoleManager /
//   srmEnvStore / clock plumbing that does not exist in V4 yet --
//   they land incrementally as the trace surfaces them.
// ----------------------------------------------------------------------------
AXP_HOT AXP_FLATTEN
auto execCserve(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult
{
    BoxResult r;
    r.semFlags = g.semFlags;

    // R16 carries the CSERVE function code in the low 8 bits.  Read
    // directly from the regfile via the CpuState back-pointer; CSERVE
    // does not flow operands through ExecCtx::opA / opB because its
    // CALL_PAL encoding has no Ra / Rb fields (the function code is
    // entirely in encoded[25:0]).
    uint64_t const funcCode = c.cpu->intReg[16] & 0xFFu;

    // ------------------------------------------------------------------
    // CSERVE entry-state profile (Tim's debug request, 2026-05-20).
    // Dump the COMPLETE integer-register context plus PC/excAddr/palBase/
    // palMode/mode/cycle on every CSERVE invocation, so the expected
    // entry/execution contract for each function code is fully visible.
    // ASCII-only, written to stderr to interleave with the console stream.
    // funcCode is the raw low byte of R16 == the apisrm Namespace-4
    // $cserve_def value (pal_def.sdl / ev6_pc264_pal_defs).  The real
    // EV6 OSF PAL implements only 16/17 (LDLP/STLP), 18/19 (LDBP/STBP),
    // 64 HALT, 65 WHAMI, 66 START, 67 CALLBACK; every other code returns
    // with nothing done (hw_ret p23).  FP regs are omitted -- CSERVE is
    // an integer-ABI primitive and never touches the FP file.
    // ------------------------------------------------------------------
    // Namespace note (2026-06-04): func 0x01/0x02 are NOT LDQP/STQP here.
    // Those codes belong to the MILO/OSF EBxx CSERVE namespace
    // (milo .../eb164/cserve.h CSERVE_K_LDQP=0x01).  The PC264/DS10
    // namespace is authoritative from apisrm ev6_pc264_pal_defs.mar:59-76
    // and has NO function below SET_HWE=8; physical access primitives are
    // LDLP/STLP at 0x10/0x11 (longword), already dispatched.  An 0x01 hit
    // therefore correctly takes the tolerant no-op default -- do not
    // "fix" it to LDQP (cross-namespace contamination; see the
    // provisional-values house rule).
    // ------------------------------------------------------------------
    [[maybe_unused]] auto cserveFuncName = [](uint64_t f) noexcept -> char const* {
        switch (f) {
            case 0x08: return "SET_HWE";
            case 0x09: return "CLEAR_HWE";
            case 0x0A: return "WRITE_BAD_CHECK_BITS";
            case 0x0B: return "CONFIGURE_MEMORY";
            case 0x0C: return "SIZE_SIMMS";
            case 0x0D: return "CONFIGURE_SIMMS";
            case 0x10: return "LDLP";
            case 0x11: return "STLP";
            case 0x12: return "LDBP";
            case 0x13: return "STBP";
            case 0x32: return "MEDU_HALT_ACTION";
            case 0x33: return "MEDU_WDOG_INT_RD";
            case 0x34: return "MEDU_INT_ENABLE";
            case 0x35: return "MEDU_INT_DISABLE";
            case 0x36: return "WRITE_BAD_ECC";
            case 0x37: return "WRITE_BAD_TAG";
            case 0x3F: return "GET_BASE";
            case 0x40: return "HALT";
            case 0x41: return "WHAMI";
            case 0x42: return "START";
            case 0x43: return "CALLBACK";
            case 0x44: return "MTPR_EXC_ADDR";
            case 0x45: return "JUMP_TO_ARC";
            case 0x65: return "MP_WORK_REQUEST";
            default:   return "(reserved / no-op)";
        }
    };

#if EMULATR_BRINGUP_PROBES
    std::fprintf(stderr,
        "CSERVE entry: func=%llu (0x%llx) %s  pc=0x%llx grainPc=0x%llx "
        "excAddr=0x%llx palBase=0x%llx palMode=%d mode=%u cyc=%llu\n",
        static_cast<unsigned long long>(funcCode),
        static_cast<unsigned long long>(funcCode),
        cserveFuncName(funcCode),
        static_cast<unsigned long long>(c.cpu->pc),
        static_cast<unsigned long long>(g.pc),
        static_cast<unsigned long long>(c.cpu->excAddr),
        static_cast<unsigned long long>(c.cpu->palBase),
        static_cast<int>(c.cpu->inPalMode()),
        static_cast<unsigned>(c.cpu->mode),
        static_cast<unsigned long long>(c.cpu->cycleCount));
    for (int i = 0; i < 32; i += 4) {
        std::fprintf(stderr,
            "  R%02d=0x%016llx R%02d=0x%016llx R%02d=0x%016llx R%02d=0x%016llx\n",
            i,     static_cast<unsigned long long>(c.cpu->intReg[i]),
            i + 1, static_cast<unsigned long long>(c.cpu->intReg[i + 1]),
            i + 2, static_cast<unsigned long long>(c.cpu->intReg[i + 2]),
            i + 3, static_cast<unsigned long long>(c.cpu->intReg[i + 3]));
    }
#endif

    switch (funcCode) {

        // ====================================================================
        // EV6 OSF PAL CSERVE primitive set -- authoritative apisrm $cserve_def
        // (apisrm/ref/pal_def.sdl + ev6_pc264_pal_defs.sdl), as dispatched by
        // sys__cserve in apisrm/ref/ev6_osf_pc264_pal.mar.  The real PAL is a
        // THIN primitive shim: it implements only physical load/store and a
        // few MP/identity calls, and lets every other function code fall
        // through to `hw_ret (p23)` -- "return, nothing done", R0 left as the
        // caller set it.
        //
        // This REPLACES V1's invented "Namespace 5" scheme (GETC/PUTC/PUTS/
        // GETS at 0x01/0x02/0x09/0x0C, env 0x20-0x23, TOY 0x30-0x32, and the
        // mislabeled 0x44 "WRITE_PATTERN" / 0x65 "Bcache").  Those codes are
        // not what the real SRM .exe issues -- 0x44 is MTPR_EXC_ADDR (68) and
        // 0x65 is MP_WORK_REQUEST (101), both of which the real PAL no-ops.
        // Console I/O is the UART (#79) + SRM callback ABI, NOT a CSERVE
        // service.  See ROSETTA_STONE.md and memory/halt-60222c-srm-panic.
        //
        // funcCode is the raw low byte of R16 (the decimal $cserve_def value).
        // ====================================================================

        case 0x08: {   // CSERVE$SET_HWE
            // Enable hardware error reporting/machine-check controls.
            // Real PAL touches per-CPU/system error state.
            // V4: currently ignored.
            return r;
        }

        case 0x09: {   // CSERVE$CLEAR_HWE
            // Disable/clear hardware error reporting state.
            // V4: currently ignored.
            return r;
        }
        case 0x10: {   // CSERVE$LDLP -- load longword physical
            // sys__cserve cfw_ldlp: `mb; hw_ldl/p r0, 0(r17); mb`.
            // R17 = physical address; R0 = sign-extended 32-bit value.
            uint32_t v = 0;
            if (c.memory != nullptr) {
                (void)c.memory->read4(
                    static_cast<coreLib::PAType>(c.cpu->intReg[17]), v);
            }
            r.regWriteIdx   = 0;     // R0
            r.regWriteIsFp  = false;
            r.regWriteValue = static_cast<uint64_t>(
                static_cast<int64_t>(static_cast<int32_t>(v)));  // sext32
            return r;
        }

        case 0x11: {   // CSERVE$STLP -- store longword physical
            // sys__cserve cfw_stlp: `mb; hw_stl/p r18, 0(r17); mb`.
            // R17 = physical address; R18 = 32-bit value.  No R0 result.
            if (c.memory != nullptr) {
                (void)c.memory->write4(
                    static_cast<coreLib::PAType>(c.cpu->intReg[17]),
                    static_cast<uint32_t>(c.cpu->intReg[18] & 0xFFFFFFFFu));
            }
            return r;                // R0 untouched
        }

        case 0x12: {   // CSERVE$LDBP -- load byte physical
            // sys__cserve cfw_ldbp: superpage `ldbu r0, 0(<PA from r17>)`.
            // R17 = physical address; R0 = zero-extended byte.
            uint8_t v = 0;
            if (c.memory != nullptr) {
                (void)c.memory->read1(
                    static_cast<coreLib::PAType>(c.cpu->intReg[17]), v);
            }
            r.regWriteIdx   = 0;     // R0
            r.regWriteIsFp  = false;
            r.regWriteValue = static_cast<uint64_t>(v);          // zext8
            return r;
        }

        case 0x13: {   // CSERVE$STBP -- store byte physical
            // sys__cserve cfw_stbp: superpage `stb r18, 0(<PA from r17>)`.
            // R17 = physical address; R18 = byte value.  No R0 result.
            if (c.memory != nullptr) {
                (void)c.memory->write1(
                    static_cast<coreLib::PAType>(c.cpu->intReg[17]),
                    static_cast<uint8_t>(c.cpu->intReg[18] & 0xFFu));
            }
            return r;                // R0 untouched
        }
        case 0x44: {   // CSERVE$MTPR_EXC_ADDR -- console hand-off continuation
            // huf_decom switch: (ev6_huf_decom.m64 l.308-311)
            //   lda    r17,<10$-start>-offset+1(r0)   ; PA of next instr | PAL bit
            //   cserve cserve$mtpr_exc_addr, r17       ; load EXC_ADDR, return in PAL
            // Loads EXC_ADDR = R17 (= physical PA of the continuation block with
            // bit 0 = 1 enforcing PALmode) and diverts the pipeline to R17,
            // switching the running PC into physical space for the subsequent
            // ic_flush -> hw_ret_stall -> 4MB relocate -> jsr hand-off into the
            // decompressed console.  Same shape as execHwRei register form
            // (PALmode == PC<0>; WB copies divertTarget incl. bit 0, so no
            // explicit setPalMode is needed -- this is a PAL->PAL transfer).
            uint64_t const targetVector = c.cpu->intReg[17];   // R17
            #if !defined(NDEBUG)
            if ((targetVector & 0x1ULL) == 0ULL) {
                std::fprintf(stderr, "WARNING: CSERVE 0x44 targeted non-PAL address "
                                     "0x%016llx at cycle %llu\n",
                             static_cast<unsigned long long>(targetVector),
                             static_cast<unsigned long long>(c.cpu->cycleCount));
            }
            #endif
            c.cpu->excAddr = targetVector;     // MTPR EXC_ADDR (bit 0 = PALmode)
            r.divertTarget = targetVector;     // return-in-PAL to R17, applied at WB
            r.divert       = true;
            return r;
        }

        case 0x3F: {   // CSERVE$GET_BASE
            r.faultCode = coreLib::kFaultHalt;
            return r;
        }
        case 0x40: {   // CSERVE$HALT -- machine halt
            // sys__cserve cfw_halt: sets HALT__HW_HALT and enters the
            // console.  V4: deliver kFaultHalt; the run loop stops with
            // StopReason::HaltedClean.
            r.faultCode = coreLib::kFaultHalt;
            return r;
        }

        case 0x41: {   // CSERVE$WHAMI -- get current CPU id
            // sys__cserve cfw_whami: `hw_ldq/p r0, PT__WHAMI(p_temp)`.
            // T5: return the executing agent's real SMP slot (cpuSlot) -- the one
            // "which CPU" source of truth.  Single agent => 0, byte-identical to
            // the prior hardcoded 0; per-CPU once SMP lands.
            r.regWriteIdx   = 0;     // R0
            r.regWriteIsFp  = false;
            r.regWriteValue = c.cpu->cpuSlot;
            return r;
        }

        case 0x42: {   // CSERVE$START -- start / release a secondary CPU
            // sys__cserve cfw_start: `br sys__exit_console`.  On a UP model
            // there are no secondaries to start; return with nothing done.
            return r;                // R0 untouched
        }

        case 0x43: {   // CSERVE$CALLBACK -- console <- OS callback transition
            // sys__cserve cfw_callback: sets a callback flag and HALTs into
            // the console (trap__update_pcb_and_halt).  This is an OS-driven
            // transition, not part of SRM cold-boot bring-up.  Until console
            // re-entry is modelled, return with nothing done; the entry-state
            // ledger above already records every reach of this code.
            return r;                // R0 untouched
        }

        default: {
            // Every other CSERVE function code: the real EV6 OSF PAL falls
            // through to `hw_ret (p23)` -- return, nothing done, R0 left as
            // the caller set it.  This is the load-bearing fidelity fix.
            // Unknown CSERVE is EXPECTED and TOLERATED on real hardware, NOT
            // fatal.  V4 previously raised kFaultUnimplemented (artificial
            // fatality), and V1's 0x44/0x65 stubs clobbered R0=0; both are
            // wrong.  Leave R0 untouched (BoxResult defaults regWriteIdx ==
            // kNoRegWrite) and return.  Codes WITHOUT an explicit case above
            // that correctly land here and are no-op'd: 0x0A WRITE_BAD_CHECK_BITS,
            // 0x0B-0x0D memory config, 0x32-0x37 MEDU/error-inject,
            // 0x45 JUMP_TO_ARC, 0x65 MP_WORK_REQUEST.  (0x08/0x09, 0x3F, and
            // 0x40-0x44 now have explicit cases and no longer reach default.)

#if EMULATR_BRINGUP_PROBES
            std::fprintf(stderr,
                "CSERVE Defaulted - UnImplemented: func=%llu (0x%llx) %s  pc=0x%llx grainPc=0x%llx "
                "excAddr=0x%llx palBase=0x%llx palMode=%d mode=%u cyc=%llu\n",
                static_cast<unsigned long long>(funcCode),
                static_cast<unsigned long long>(funcCode),
                cserveFuncName(funcCode),
                static_cast<unsigned long long>(c.cpu->pc),
                static_cast<unsigned long long>(g.pc),
                static_cast<unsigned long long>(c.cpu->excAddr),
                static_cast<unsigned long long>(c.cpu->palBase),
                static_cast<int>(c.cpu->inPalMode()),
                static_cast<unsigned>(c.cpu->mode),
                static_cast<unsigned long long>(c.cpu->cycleCount));
#endif
            return r;                // R0 untouched, no fault
        }
    }
}

#pragma endregion CALL_PAL CSERVE intrinsic


#pragma region CALL_PAL LDQP / STQP intrinsics (physical-address load/store)

// ----------------------------------------------------------------------------
// LDQP -- Load Quadword Physical.  CALL_PAL function 0x03.
// VMS-only per AARM C-15 (Tru64/Linux leave 0x03 unassigned); the
// GrainMaster row carries S_PalVms alone, so codegen derives the leaf
// name execLdqp_vms.
// 2026-06-05: renamed execLdqp -> execLdqp_vms to match codegen's
// single-personality suffix rule (same fix class as execMfprScbb_vms,
// 2026-05-29).  The unsuffixed name stopped matching handwritten.tsv
// when the row was corrected from S_PalTru64|S_PalVms to S_PalVms, so
// DispatchTables silently bound the generated kFaultUnimplemented stub
// -> OPCDEC -> SCB 0x420 -> SRM "unexpected exception/interrupt through
// vector 420" crash loop in getbit64 (showmem bitmap walk, show config).
// S_PalIntrinsic posture: the leaf packs a physical-address memEffect
// and an R0 commit; MEM-drainer honours S_PhysAddr to skip translation.
//
// Architectural semantics (OSF/Tru64 PAL spec):
//   R16 (a0) -- physical address; MUST be quadword aligned (R16<2:0> == 0).
//                Unaligned R16 raises kFaultUnaligned with mm_stat = R16.
//   R0  (v0) -- 64-bit value loaded from mem[R16]
//
// Used by PALcode and SRM bootstrap to access PAL data tables and
// machine-context blocks living in physical memory below palBase.
// ----------------------------------------------------------------------------
AXP_HOT AXP_FLATTEN
auto execLdqp_vms([[maybe_unused]] InstructionGrain const& g,
                  ExecCtx const&                            c) noexcept -> BoxResult
{
    BoxResult r;
    r.semFlags = g.semFlags;

    uint64_t const ea = c.cpu->intReg[16];   // R16 (a0)

    // Alignment check.  S_PhysAddr bypasses the MEM-drainer's
    // translator (which is where alignment is normally enforced for
    // VA-translated accesses), so the leaf must enforce it directly
    // per OSF/Tru64 spec.  Unaligned EA -> kFaultUnaligned, no
    // memEffect, no register commit.  mm_stat is captured by WB-stage
    // trap delivery; setting it here would race with the drainer's
    // own mm_stat update on translated paths, so we leave it.
    // Gated on CpuState::unalignTrapEnabled for the same reason the
    // translator path is: the V4 v1 default suppresses the trap to
    // unblock firmware bring-up; tests that need the trap mechanism
    // verified set the flag true before exercising.
    if ((ea & 0x7ULL) != 0 && c.cpu->unalignTrapEnabled) {
        r.faultCode = coreLib::kFaultUnaligned;
        c.cpu->mm_stat = ea;
        return r;
    }

    // Carry S_PhysAddr + S_Load forward so MEM-drainer (a) bypasses
    // translation, and (b) routes through the load-side branch.
    r.semFlags     = r.semFlags
                   | grainFactory::GrainSem::S_PhysAddr
                   | grainFactory::GrainSem::S_Load;
    r.memAddr      = ea;
    r.memSize      = 8;
    r.memIsStore   = false;
    r.regWriteIdx  = 0;                   // R0 (v0) receives the fill
    r.regWriteIsFp = false;
    return r;
}

// ----------------------------------------------------------------------------
// STQP -- Store Quadword Physical.  CALL_PAL function 0x04.
// VMS-only per AARM C-15; renamed execStqp -> execStqp_vms 2026-06-05
// for the same codegen suffix-match reason as execLdqp_vms above.
// Symmetric to LDQP; writes R17 (a1) into mem[R16].  No regfile
// commit.  Same quadword-alignment requirement on R16 (R16<2:0> == 0).
// ----------------------------------------------------------------------------
AXP_HOT AXP_FLATTEN
auto execStqp_vms([[maybe_unused]] InstructionGrain const& g,
                  ExecCtx const&                            c) noexcept -> BoxResult
{
    BoxResult r;
    r.semFlags = g.semFlags;

    uint64_t const ea = c.cpu->intReg[16];   // R16 (a0)

    // Gated on CpuState::unalignTrapEnabled; see execLdqp_vms for rationale.
    if ((ea & 0x7ULL) != 0 && c.cpu->unalignTrapEnabled) {
        r.faultCode = coreLib::kFaultUnaligned;
        c.cpu->mm_stat = ea;
        return r;
    }

    r.semFlags     = r.semFlags
                   | grainFactory::GrainSem::S_PhysAddr
                   | grainFactory::GrainSem::S_Store;
    r.memAddr      = ea;
    r.memData      = c.cpu->intReg[17];   // R17 (a1) -- value to store
    r.memSize      = 8;
    r.memIsStore   = true;
    return r;
}

#pragma endregion CALL_PAL LDQP / STQP intrinsics (physical-address load/store)


#pragma region CALL_PAL VPTB intrinsics (MFPR_VPTB / MTPR_VPTB)

// ----------------------------------------------------------------------------
// MFPR_VPTB -- read Virtual Page Table Base.  CALL_PAL function 0x29,
// valid under both PAL personalities.  S_PalIntrinsic posture: the
// leaf reads cpu.vptb directly and packs an R0 commit; no divert,
// no PALmode flip, retire to PC+4.
//
// V4 v1 cut: cpu.vptb is just storage that MTPR_VPTB writes and
// MFPR_VPTB reads.  No page walker consumes it yet.  The OS and
// PALcode use VPTB as the pointer to the top-level page table; once
// V4 grows TLB-miss handling, the same field will be the page
// walker's input.
//
// Architecturally MFPR_VPTB writes Ra, but the Alpha PAL convention
// for CALL_PAL function reads is to write R0 (v0).  The Alpha
// instruction encoding for CALL_PAL has no Ra/Rb fields (function
// code occupies bits[25:0]), so the leaf hard-codes regWriteIdx = 0.
// ----------------------------------------------------------------------------
// 2026-06-05: renamed execMfprVptb -> execMfprVptb_vms (row is
// S_PalVms-only; same codegen suffix-match fix class as execLdqp_vms).
AXP_HOT AXP_FLATTEN
auto execMfprVptb_vms([[maybe_unused]] InstructionGrain const& g,
                      ExecCtx const&                            c) noexcept -> BoxResult
{
    BoxResult r;
    r.semFlags      = g.semFlags;
    r.regWriteIdx   = 0;            // R0 (v0) -- Alpha PAL "function read" convention
    r.regWriteIsFp  = false;
    r.regWriteValue = c.cpu->vptb;
    return r;
}

// ----------------------------------------------------------------------------
// MTPR_VPTB -- write Virtual Page Table Base.  CALL_PAL function 0x2A.
// Alpha PAL convention for CALL_PAL function writes is to read R16
// (a0) for the value.  Stores into cpu.vptb; no register commit.
// ----------------------------------------------------------------------------
// 2026-06-05: renamed execMtprVptb -> execMtprVptb_vms (see MFPR_VPTB note).
AXP_HOT AXP_FLATTEN
auto execMtprVptb_vms([[maybe_unused]] InstructionGrain const& g,
                      ExecCtx const&                            c) noexcept -> BoxResult
{
    BoxResult r;
    r.semFlags = g.semFlags;
    c.cpu->vptb = c.cpu->intReg[16];   // R16 (a0) is the standard CALL_PAL arg
    return r;
}

#pragma endregion CALL_PAL VPTB intrinsics (MFPR_VPTB / MTPR_VPTB)


#pragma region CALL_PAL SCBB intrinsics (MFPR_SCBB / MTPR_SCBB)

// ----------------------------------------------------------------------------
// MFPR_SCBB -- read System Control Block Base.  CALL_PAL function 0x16,
// VMS only.  S_PalIntrinsic posture: read cpu.scbb directly, pack R0,
// retire to PC+4.  No PAL transfer, no mode change.
//
// Architectural background (AARM Section 14.6):
//   The SCB is the OS-managed dispatch table holding kernel-mode entry
//   points for exception, interrupt, and machine-check delivery.  PALcode
//   (when loaded) walks the SCB after handling a trap at palBase + entry
//   vector to forward control to the OS handler.  SCBB is the physical
//   address (or PFN, platform-dependent) of the SCB.
//
// V4 v1 cut: cpu.scbb is plain backing storage that MTPR_SCBB writes and
// MFPR_SCBB reads.  V4 itself does not perform the SCB walk -- that's
// PALcode's job (see deviceLib/Scb.h for the byte-precise layout that V4
// leaves use to overlay an SCB region of guest memory if they need to).
//
// ABI: Alpha CALL_PAL "function read" convention -- no Ra/Rb in the
// encoding (function code occupies bits[25:0]); result lands in R0 (v0).
// ----------------------------------------------------------------------------
// 2026-05-29: renamed execMfprScbb -> execMfprScbb_vms to match codegen's
// _vms-suffixed dispatch symbol; symbol added to handwritten.tsv so codegen
// no longer emits the conflicting stub.  Body unchanged.
AXP_HOT AXP_FLATTEN
auto execMfprScbb_vms([[maybe_unused]] InstructionGrain const& g,
                      ExecCtx const&                            c) noexcept -> BoxResult
{
    // 2026-05-31: was a broken intrinsic (returned cpu.scbb, which the VMS
    // PAL never reads back -- the interrupt dispatch reads PT__SCBB from
    // guest memory at p21+0x170).  Now an S_PalEntry leaf that delegates to
    // the guest PAL MFPR_SCBB handler, like execMfprPcbb_vms et al.
    return execCallPalDispatch(g, c);
}

// ----------------------------------------------------------------------------
// MTPR_SCBB -- write System Control Block Base.  CALL_PAL function 0x17,
// VMS only.  Reads R16 (a0) for the new SCBB value.  Stores into cpu.scbb;
// no register commit.  No SCB validation -- the caller (kernel) is
// responsible for ensuring the SCB is page-aligned and properly sized
// (8K..32K bytes per AARM 14.6).
// ----------------------------------------------------------------------------
// 2026-05-29: renamed execMtprScbb -> execMtprScbb_vms to match codegen's
// _vms-suffixed dispatch symbol; symbol added to handwritten.tsv so codegen
// no longer emits the conflicting stub.  Body unchanged.
AXP_HOT AXP_FLATTEN
auto execMtprScbb_vms([[maybe_unused]] InstructionGrain const& g,
                      ExecCtx const&                            c) noexcept -> BoxResult
{
    // 2026-05-31: was a broken intrinsic (stored R16 into cpu.scbb, which the
    // VMS PAL dispatch never reads).  Now an S_PalEntry leaf that delegates to
    // the guest PAL MTPR_SCBB handler, which does hw_stq/p p6,PT__SCBB(p_temp)
    // -- writing the value to the guest memory the dispatch actually reads.
    return execCallPalDispatch(g, c);
}

#pragma endregion CALL_PAL SCBB intrinsics (MFPR_SCBB / MTPR_SCBB)


#pragma region CALL_PAL WTINT intrinsic

// ----------------------------------------------------------------------------
// WTINT -- wait for interrupt.  CALL_PAL function 0x3F (OSF/Tru64
// privileged), valid under both PAL personalities.  S_PalIntrinsic
// posture: inline-executed, no PAL transfer, retire to PC+4.
//
// Architectural semantics: the OS issues WTINT to yield the CPU until
// any interrupt fires.  Return value in R0 indicates outcome -- 0 for
// "interrupt arrived", non-zero for error / no interrupt source.
//
// V4 v1 cut: V4 has no interrupt model, no timer, no IPL machinery.
// Returning 0 immediately tells the bootstrap "an interrupt happened,
// proceed".  This matches the behaviour the SRM bootstrap typically
// assumes during init; if the bootstrap needs a real interrupt-driven
// loop later, we revisit when we add the interrupt subsystem.
// ----------------------------------------------------------------------------
AXP_HOT AXP_FLATTEN
auto execWtint([[maybe_unused]] InstructionGrain const& g,
               [[maybe_unused]] ExecCtx const&          c) noexcept -> BoxResult
{
    BoxResult r;
    r.semFlags      = g.semFlags;
    r.regWriteIdx   = 0;     // R0 (v0)
    r.regWriteIsFp  = false;
    r.regWriteValue = 0;     // "interrupt arrived"
    return r;
}

#pragma endregion CALL_PAL WTINT intrinsic


#pragma region CALL_PAL MFPR_WHAMI intrinsic

// ----------------------------------------------------------------------------
// MFPR_WHAMI -- read CPU identifier ("Who Am I?").  CALL_PAL function
// 0x3F (OSF/Tru64 privileged), valid under both PAL personalities.
// S_PalIntrinsic posture.
//
// Architectural semantics: returns the executing CPU's hardware ID
// in R0.  Used by SMP-aware OS code to route per-CPU work and by
// SRM bootstrap to pick a "primary" CPU.
//
// T5: returns the executing agent's real SMP slot (CpuState::cpuSlot) -- the
// single "which CPU" source.  Single agent => 0 (byte-identical to the prior
// hardcoded 0); per-CPU once SMP lands.
// ----------------------------------------------------------------------------
AXP_HOT AXP_FLATTEN
auto execMfprWhami([[maybe_unused]] InstructionGrain const& g,
                   ExecCtx const&                           c) noexcept -> BoxResult
{
    BoxResult r;
    r.semFlags      = g.semFlags;
    r.regWriteIdx   = 0;     // R0 (v0)
    r.regWriteIsFp  = false;
    r.regWriteValue = c.cpu->cpuSlot;   // real SMP slot (0 for agent0)
    return r;
}

#pragma endregion CALL_PAL MFPR_WHAMI intrinsic


#pragma region CALL_PAL SWPCTX intrinsic (process-context swap)

// ----------------------------------------------------------------------------
// SWPCTX -- swap process context.  CALL_PAL function 0x05 (VMS).
// (Tru64/Linux have a separate "swpctx" at 0x30 with the same semantic
// but different opcode -- handled by execSwpctxOsf below.)
//
// Architectural semantics (AARM Section 26 + palcode_dsgn_gde.txt):
//
//   R16 (a0) -- physical address of the NEW HWPCB to install.
//   R0  (v0) -- previous PTBR value (the OS uses this to release the
//               old process's page-table memory).
//
// PALcode actions:
//
//   1. Save current architectural process state into the OLD HWPCB at
//      cpu->pcbb (KSP/ESP/SSP/USP, PTBR, ASN, ASTEN_SR, FEN, CC,
//      and the 7-quadword PALcode-private SCRATCH region).
//   2. Load the same fields from the NEW HWPCB at R16 into CpuState.
//   3. Update PCBB <- R16.
//   4. If new HWPCB's PTBR<63> = 1, switch this process to physical
//      mode (per palcode_dsgn_gde.txt section on swpctx, line 2517).
//   5. Return the previous PTBR in R0.
//
// V4 v1 stance:
//
//   The leaf-side guest-memory accessor doesn't yet support the
//   ~16 synchronous quadword reads + ~16 quadword writes that this
//   operation requires (V4's deferred-memEffect pattern handles only
//   one access per leaf).  Stub with kFaultUnimplemented and capture
//   the encoding in the diagnostic ring so any caller halts loudly
//   with full context.
//
//   Reference implementation, when leaf-side memory access lands:
//
//     #include "deviceLib/HwpcbContext.h"   // shuttle helpers
//     using deviceLib::hwrpb::Hwpcb;
//     using deviceLib::hwrpb::loadCpuFromHwpcb;
//     using deviceLib::hwrpb::storeCpuToHwpcb;
//
//     uint64_t const newPcbbPa = c.cpu->intReg[16];
//     uint64_t const oldPcbbPa = c.cpu->pcbb;
//     uint64_t const oldPtbr   = c.cpu->ptbr;
//
//     // 1. Read new context from guest memory at R16.
//     Hwpcb newCtx;
//     c.cpu->mem.read(newPcbbPa, &newCtx, sizeof(Hwpcb));
//
//     // 2. Save current CpuState into an Hwpcb image and write back
//     //    to the old PCBB (so the previous process can be resumed).
//     Hwpcb oldCtx;
//     storeCpuToHwpcb(oldCtx, *c.cpu);
//     c.cpu->mem.write(oldPcbbPa, &oldCtx, sizeof(Hwpcb));
//
//     // 3. Install the new context.  loadCpuFromHwpcb copies all
//     //    architectural fields and strips PTBR<63> (physical-mode flag).
//     loadCpuFromHwpcb(*c.cpu, newCtx);
//     c.cpu->pcbb = newPcbbPa;
//
//     // 4. Per palcode_dsgn_gde.txt: PTBR<63>=1 selects physical mode
//     //    for this process.  V4 doesn't yet track per-process physical
//     //    mode on CpuState; if needed, capture (newCtx.ptbr >> 63) & 1.
//
//     // 5. Return the previous PTBR in R0.
//     r.regWriteIdx   = 0;
//     r.regWriteIsFp  = false;
//     r.regWriteValue = oldPtbr;
//     return r;
//
// Prerequisites (tracked here for the next-steps board):
//
//   1. CpuState shadow registers (ksp/esp/ssp/usp/fen/asten_sr/pcbb).
//      DONE -- see coreLib/CpuState.h.
//   2. Hwpcb <-> CpuState shuttle helpers.
//      DONE -- see deviceLib/HwpcbContext.h.
//   3. Leaf-side guest-memory accessor on ExecCtx (read N qwords +
//      write N qwords against a guest physical address synchronously).
//      OPEN -- the deferred-memEffect pattern handles only one access
//      per leaf; SWPCTX needs ~16 reads + 16 writes in one shot.
//   4. PerCpuSlot reachability: the OS reads its PCBB by walking the
//      HWRPB; firmware must publish the correct PerCpuSlot::hwpcb at
//      HWRPB-build time so the OS finds a coherent initial context.
//      PARTIAL -- HWRPB is now populated by FirmwareDeviceManager
//      Phase 0 (Step 3 commit); the per-CPU SLOT array carries fresh
//      Hwpcb instances.  Still pending: deploying the buffer into
//      guest memory at boot (Machine orchestrator memcpy).
// ----------------------------------------------------------------------------
AXP_HOT AXP_FLATTEN
auto execSwpctxVms(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult
{
    // Implements palBoxLib/swpctx_spec.md (VMS PALcode SWPCTX).  Step
    // numbers below refer to spec section 5; HWPCB field offsets to
    // section 4.  Accesses are PHYSICAL, quadword-aligned via
    // ExecCtx::memory (SimH ReadPQ/WritePQ semantics); R16 / cpu.pcbb are
    // physical HWPCB addresses, NOT translated.  No runtime alignment
    // check (spec 6.1: the bus masks the low 3 bits; debug-assert only,
    // deferred until a DCHECK macro lands -- spec 11.2).
    BoxResult r;
    r.semFlags = g.semFlags;

    // Spec 11.3 -- bare unit harness with no memory bus: SWPCTX is a no-op.
    if (c.memory == nullptr) {
        return r;
    }

    using deviceLib::hwrpb::Hwpcb;
    using deviceLib::hwrpb::loadCpuFromHwpcb;
    using deviceLib::hwrpb::storeCpuToHwpcb;

    // Step 1 -- capture old PTBR (R0 return) BEFORE any state change.
    uint64_t const oldPtbr = c.cpu->ptbr;
    uint64_t const oldPcbb = c.cpu->pcbb;
    uint64_t const newPcbb = c.cpu->intReg[16];   // R16 = new HWPCB phys addr

    // Step 2 -- snapshot live state.  The running R30 IS the architectural
    // KSP whenever PALcode executes (PAL entry forces kernel mode), so it
    // overrides whatever storeCpuToHwpcb copied from cpu.ksp.
    Hwpcb oldCtx{};
    storeCpuToHwpcb(oldCtx, *c.cpu);
    oldCtx.ksp = c.cpu->intReg[30];

    // Step 3 -- write old HWPCB (9 quadwords).  MUST precede step 4: for a
    // self-switch (newPcbb == oldPcbb) the reads in step 4 must observe the
    // values just saved (spec 5, ordering note).
    (void)c.memory->write8(static_cast<coreLib::PAType>(oldPcbb + 0x00), oldCtx.ksp);
    (void)c.memory->write8(static_cast<coreLib::PAType>(oldPcbb + 0x08), oldCtx.esp);
    (void)c.memory->write8(static_cast<coreLib::PAType>(oldPcbb + 0x10), oldCtx.ssp);
    (void)c.memory->write8(static_cast<coreLib::PAType>(oldPcbb + 0x18), oldCtx.usp);
    (void)c.memory->write8(static_cast<coreLib::PAType>(oldPcbb + 0x20), oldCtx.ptbr);
    (void)c.memory->write8(static_cast<coreLib::PAType>(oldPcbb + 0x28), oldCtx.asn);
    (void)c.memory->write8(static_cast<coreLib::PAType>(oldPcbb + 0x30), oldCtx.asten_sr);
    (void)c.memory->write8(static_cast<coreLib::PAType>(oldPcbb + 0x38), oldCtx.fen);
    (void)c.memory->write8(static_cast<coreLib::PAType>(oldPcbb + 0x40), oldCtx.cc);

    // Step 4 -- read new HWPCB (9 quadwords).  NXM return-value handling is
    // a known gap (spec 7 / 11.1): a non-memory R16 leaves newCtx zero and
    // a zero context is installed.
    Hwpcb newCtx{};
    (void)c.memory->read8(static_cast<coreLib::PAType>(newPcbb + 0x00), newCtx.ksp);
    (void)c.memory->read8(static_cast<coreLib::PAType>(newPcbb + 0x08), newCtx.esp);
    (void)c.memory->read8(static_cast<coreLib::PAType>(newPcbb + 0x10), newCtx.ssp);
    (void)c.memory->read8(static_cast<coreLib::PAType>(newPcbb + 0x18), newCtx.usp);
    (void)c.memory->read8(static_cast<coreLib::PAType>(newPcbb + 0x20), newCtx.ptbr);
    (void)c.memory->read8(static_cast<coreLib::PAType>(newPcbb + 0x28), newCtx.asn);
    (void)c.memory->read8(static_cast<coreLib::PAType>(newPcbb + 0x30), newCtx.asten_sr);
    (void)c.memory->read8(static_cast<coreLib::PAType>(newPcbb + 0x38), newCtx.fen);
    (void)c.memory->read8(static_cast<coreLib::PAType>(newPcbb + 0x40), newCtx.cc);

    // Step 5 -- install new context, switch PCBB, and load the running SP.
    // cpu.intReg[30] = newCtx.ksp is the step the AARM pseudocode elides and
    // SimH makes explicit (alpha_pal_vms.c:1430); without it the kernel
    // stack stays inactive (SP=0 -> the top-of-PA sweep this fixes).
    loadCpuFromHwpcb(*c.cpu, newCtx);
    c.cpu->pcbb       = newPcbb;
    c.cpu->intReg[30] = newCtx.ksp;

    // Step 6 -- report old PTBR in R0 (spec 3; matches S_WritesRa).
    r.regWriteIdx   = 0;
    r.regWriteIsFp  = false;
    r.regWriteValue = oldPtbr;
    return r;
}

// ----------------------------------------------------------------------------
// execSwpctxOsf -- swap process context, OSF/Tru64 + Linux flavor.
// CALL_PAL function 0x30 (Tru64+Linux).  Same operation as execSwpctxVms;
// just a different function-code encoding per the VMS-vs-OSF personality
// split (AARM Table C-15 lines 47741 + 47656).
//
// Note: V4's TSV row at 0x30 currently carries divergent semantics
// (VMS: MFPR_VIRBND; Tru64+Linux: swpctx).  When V4 grows personality-
// aware leaf dispatch, the row's leaf will fan out: under VMS personality
// it returns VIRBND; under Tru64/Linux it calls into this body.  Until
// that fan-out exists, the TSV row stays at S_PalEntry (default divert)
// and this leaf is a forward-looking stub.
// ----------------------------------------------------------------------------
AXP_HOT AXP_FLATTEN
auto execSwpctxOsf(InstructionGrain const& g, [[maybe_unused]] ExecCtx const& c) noexcept -> BoxResult
{
    // Identical body to execSwpctxVms; kept as a separate symbol so the
    // codegen can wire it to the 0x30 dispatch slot when personality fan-
    // out lands.
    BoxResult r;
    r.semFlags  = g.semFlags;
    r.faultCode = coreLib::kFaultUnimplemented;
    return r;
}

#pragma endregion CALL_PAL SWPCTX intrinsic (process-context swap)

#pragma region CALL_PAL HALT (real)

// ----------------------------------------------------------------------------
// HALT -- CALL_PAL function 0x00 in both PAL personalities.  Stops
// the processor.  Pipeline driver intercepts kFaultHalt at WB and
// terminates the run cleanly.  No register or memory effect.
// ----------------------------------------------------------------------------
AXP_HOT AXP_FLATTEN
auto execHalt(InstructionGrain const& g, [[maybe_unused]] ExecCtx const& c) noexcept -> BoxResult
{
    BoxResult r;
    r.semFlags = g.semFlags;
    r.faultCode = coreLib::kFaultHalt;
    return r;
}

#pragma endregion CALL_PAL HALT (real)


#pragma region CALL_PAL generic dispatch (divert into PALcode)

// ----------------------------------------------------------------------------
// execCallPalDispatch -- generic CALL_PAL dispatcher.  Catches every
// CALL_PAL function code not pinned to its own dispatch entry (HALT,
// CSERVE, BPT, CHME, CHMK).  Computes the EV6 PALcode entry vector
// per the Alpha 21264 HRM Table 6-8 / Section 6.8.1, sets up the
// PAL-entry environment (excAddr, palMode), and packs a divert into
// BoxResult.  PALcode bytes loaded at palBase do the actual work.
//
// Entry-vector formula (mirrors V1 coreLib/global_registermaster_hot.h
// computeExceptionVector and the EV6 HRM Section 6.8.1):
//
//   Privileged   (func 0x00-0x3F):  vectorOffset = 0x2000 | (func << 6)
//   Unprivileged (func 0x80-0xBF):  vectorOffset = 0x3000 | ((func & 0x3F) << 6)
//   entryPC = (palBase & ~0x7FFFULL) | vectorOffset
//
// Bit 0 of the architectural entry PC is the PALmode marker (PC<0> = 1
// means "in PAL").  V4 tracks PAL mode separately via cpu->palMode and
// keeps the divert target bit-0-aligned, so the marker is folded into
// cpu->palMode = true rather than into divertTarget.
//
// Return-path encoding (excAddr):
//
//   excAddr = (g.pc + 4) | (cpu->palMode ? 1 : 0)
//
// Bit 0 of excAddr captures the PALmode of the interrupted context.
// HW_REI's STACKED form reads excAddr, splits bit 0 into the resumed
// palMode, and clears it from divertTarget -- so the transition CALL_PAL
// (PAL = 0) -> PALcode (PAL = 1) -> HW_REI -> resumed (PAL = 0) is
// correct without any further bookkeeping.  Nested CALL_PAL from
// PALmode (rare but legal) preserves bit 0 = 1 so HW_REI stays in PAL.
//
// Defaults arms of lookupPalTru64 / lookupPalVms point a static
// GrainEntry at this leaf with personality-appropriate flags
// (S_PalTru64 vs S_PalVms).  Per-function rows pinned in the
// dispatch table (HALT, CSERVE, ...) take priority over the default.
// ----------------------------------------------------------------------------
AXP_HOT AXP_FLATTEN
auto execCallPalDispatch(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult
{
    BoxResult r;
    r.semFlags = g.semFlags;

    // Function code is the low 26 bits of the encoding (bits[25:0]).
    // computeCallPalEntry handles bit-7 (priv vs unpriv base), the
    // 0x40 entry spacing, and the 32K palBase alignment in one place.
    // Source of truth: coreLib/Ev6EntryVectors.h (matches ev6_defs.mar
    // and EV6 HRM Section 6.8.1).
    uint32_t const funcCode = g.encoded & 0x03FFFFFFu;
    uint64_t const entryPC  =
        coreLib::ev6::computeCallPalEntry(c.cpu->palBase, funcCode);

    // Compute the linkage value: return PC plus the caller's palMode
    // in bit 0 (so HW_REI can restore it).  STACKED HW_REI reads
    // EXC_ADDR; non-STACKED HW_REI Rxx reads the linkage register --
    // both forms see the same value.
    uint64_t const returnPc      = g.pc + 4;
    uint64_t const linkageValue  = returnPc
                                 | (c.cpu->inPalMode() ? uint64_t{1} : uint64_t{0});
    c.cpu->excAddr = linkageValue;

    // Route through palModeEnter so the EV6 PAL shadow swap (R4-R7,
    // R20-R23) fires when I_CTL[SDE<1>] is set.  No-op if palMode
    // was already true (nested CALL_PAL from PAL stays in PAL with
    // no additional swap).  Crucial: the swap must happen BEFORE
    // the linkage-register write below so the value lands in the
    // PAL-mode view of R23/R27 (which is what the PAL handler will
    // see), not in the user-context view (which is now stashed in
    // shadow storage).
    coreLib::palModeEnter(*c.cpu);

    // CALL_PAL linkage register: per HRM 5.2.14, the linkage register
    // is R23 when I_CTL[CALL_PAL_R23] is set, else R27.  The choice
    // pairs with I_CTL[SDE] so the linkage register is one of the
    // PAL shadow registers (R23 with SDE<1>; R27 with SDE<0>).
    // PAL handlers that use the non-stacked HW_REI form (HW_REI Rxx)
    // read the return PC from this register; V4 previously only set
    // EXC_ADDR and so the non-stacked form would have read stale
    // values.  Setting the linkage register restores HRM-correct
    // CALL_PAL semantics.  The value INCLUDES the palMode bit in
    // position [0] just like excAddr, so HW_REI Rxx and HW_REI
    // STACKED produce identical resume semantics.
    //
    // The write happens AFTER palModeEnter (see comment above), so
    // it modifies the PAL-mode view of the linkage register.
    uint8_t const linkageReg = coreLib::iCtlCallPalLinkageReg(c.cpu->i_ctl);
    c.cpu->intReg[linkageReg] = linkageValue;

    r.divertTarget = entryPC | uint64_t{1};   // enter PAL: PC<0>=1 (else handler runs native)
    r.divert       = true;
    return r;
}

#pragma endregion CALL_PAL generic dispatch (divert into PALcode)


#pragma region CALL_PAL FEN pair (VMS) -- divert into PALcode

// ----------------------------------------------------------------------------
// FEN pair (VMS personality) -- CALL_PAL function codes 0x0B / 0x0C.
//
//   MFPR_FEN  func 0x0B  -- read  floating-point enable into R0
//   MTPR_FEN  func 0x0C  -- write floating-point enable from R16
//
// Architectural posture: S_PalEntry (NOT S_PalIntrinsic).  Both CALL_PALs
// are delivered into PALcode at palBase + (0x2000 | (func << 6)):
//
//   MFPR_FEN  -> palBase + 0x22C0
//   MTPR_FEN  -> palBase + 0x2300
//
// per HRM Section 6.8.1.  PALcode does the actual read/write of the
// FEN field (typically held in the PALcode impure area, mirrored
// against I_CTL[FPE] or an equivalent CpuState shadow); V4 does not
// synthesize the FPE bit transition inline.
//
// Convention-wise these leaves are hand-written (listed in
// handwritten.tsv so codegen does not emit kFaultUnimplemented stubs)
// but their bodies simply delegate to execCallPalDispatch, which
// already implements the divert correctly for every privileged
// CALL_PAL function code.  Having dedicated leaves gives each function
// code a per-function home: if a future profile wants to short-circuit
// either side as an S_PalIntrinsic (update cpu.fpcr / cpu.fen inline
// and retire without diverting), the body changes here without
// touching the generic dispatcher.
//
// First firmware hit: DS10 SRM at PC 0x1c6208 (MTPR_FEN) during early
// boot, 2026-05-27.  Prior to these leaves existing, V4 returned
// kFaultUnimplemented and the SROM OPCDEC handler ran with a
// not-yet-initialised stack pointer, ending in NXM at PA 0x1ffffffffc0.
// MFPR_FEN is implemented as the matching pair in anticipation of the
// firmware reading the FEN bit shortly after the write.
// ----------------------------------------------------------------------------
AXP_HOT AXP_FLATTEN
auto execMfprFen_vms(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult
{
    return execCallPalDispatch(g, c);
}

AXP_HOT AXP_FLATTEN
auto execMtprFen_vms(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult
{
    return execCallPalDispatch(g, c);
}

#pragma endregion CALL_PAL FEN pair (VMS)





#pragma region HW_xxx Stubs (CpuState prerequisite)

// ----------------------------------------------------------------------------
// HW_MFPR -- read internal processor register.  Looks up the IPR
// selector at encoded[15:8] (with 0x0100 offset added by iprSelector
// to namespace into the HW_IPR enum), reads the corresponding
// CpuState field, and packs the value into BoxResult for commit to
// Ra at MEM-stage drain.
//
// Coverage policy:
//
//   The switch is exhaustive over the V1 HW_IPR enum (taken as
//   authoritative).  Three classes of cases:
//
//     1. CpuState-backed   -- real read of the corresponding field.
//     2. Unbacked, silent  -- return 0.  Permissive stub for IPRs
//                             PALcode reads but where v1 has no
//                             storage yet (interrupt-state IPRs,
//                             D-cache status, action regs, etc.).
//                             Adds storage later is a one-field
//                             CpuState extension plus the case body.
//     3. Default           -- truly unknown selector (raw scbd not
//                             in the V1 enum at all).  Raises
//                             kFaultUnimplemented; the trace
//                             lookback ring captures the failing
//                             encoding for post-mortem diagnosis.
//
//   Trace honesty: the disassembler already renders the IPR name in
//   the operands column of every committed HW_MFPR / HW_MTPR, so
//   "which case was hit" is visible per-cycle in the DEC channel
//   when TRACE_INSTR fires.  PAL-window mode + onRunEnd dump
//   captures it at the stop boundary too.
// ----------------------------------------------------------------------------
AXP_HOT AXP_FLATTEN
auto execHwMfpr(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult
{
    BoxResult r;
    r.semFlags = g.semFlags;

    coreLib::HW_IPR const sel = iprSelector(g);
    uint64_t value = 0;

    // PAL_TEMP range first: a tight range check is cheaper than 24
    // case labels and the PT slots are heavily exercised by every
    // PAL window.
    if (coreLib::isPalTemp(sel)) {
        value = c.cpu->palTemp[coreLib::palTempIndex(sel)];
        r.regWriteIdx = raIndex(g);
        r.regWriteIsFp = false;
        r.regWriteValue = value;
        return r;
    }

    switch (sel) {
        // ---- CpuState-backed reads ----
    case coreLib::HW_EXC_ADDR: value = c.cpu->excAddr;    break;
    case coreLib::HW_PAL_BASE: value = c.cpu->palBase;    break;
    case coreLib::HW_I_CTL:    value = c.cpu->i_ctl;      break;
    case coreLib::HW_M_CTL:    value = c.cpu->m_ctl;      break;
    case coreLib::HW_MM_STAT:  value = c.cpu->mm_stat;    break;
    case coreLib::HW_VA_CTL:   value = c.cpu->va_ctl;     break;
    // 2026-05-29: HW_CC must match execRpcc -- both are the architectural
    // view of one counter (per IntArith.cpp note "RPCC and HW_CC are two
    // architectural views of one counter").  Multiply by kCcMultiplier so
    // the firmware's RSCC handler's scale-down division resolves quickly.
    // See CpuState.h kCcMultiplier comment for full rationale.
    case coreLib::HW_CC:       value = (c.cpu->cycleCount + c.cpu->ccOffset)
                                     * coreLib::CpuState::kCcMultiplier; break;
    case coreLib::HW_CM:       value = static_cast<uint64_t>(c.cpu->mode); break;

        // HW_ISUM (Interrupt Summary, scbd 0x0D).  Backed by
        // cpu->isum so the trap delivery path can stage the cause
        // bits the OSF/1 INTERRUPT entry vector (ev6_osf_pal.mar)
        // decodes -- SI/PC/CR/SL/EI per EV6__ISUM__*__S in
        // ev6_defs.mar.  Returning 0 here made the handler take
        // trap__interrupt_dismiss after every injection, leaving
        // SRM stuck in the MCHK idle loop.  See CpuState::isum.
    case coreLib::HW_ISUM:     value = c.cpu->isum;       break;

        // ---- Computed VA-form registers (HRM 5.1.4) ----
        // VA_FORM / IVA_FORM return the virtual address of the PTE that maps
        // the faulting address in the self-mapped page table (VPTB | VPN<<3).
        // PALcode reads these on a TB miss to locate and load the PTE; both
        // reference emulators (EV5 SIMH FMT_*VA_*, EV6 AXPBox vmspal walk)
        // compute them.  Previously silent-zero, which starved the firmware's
        // page-table walk.  I-side: EXC_ADDR + I_CTL[VPTB].  D-side: the
        // faulting data VA (cpu.va, the HW_VA register) + VA_CTL[VPTB].
    case coreLib::HW_IVA_FORM:
        value = coreLib::computeVaForm(coreLib::iCtlVptb(c.cpu->i_ctl),
            c.cpu->excAddr,
            coreLib::iCtlIsVaForm32(c.cpu->i_ctl));
#if EMULATR_MEMDIAG
        {
            // MEMDIAG-VAFORM probe: confirm what PALcode actually sees
            // when it reads HW_IVA_FORM during an ITB miss.  Capped to
            // bound log volume; remove when VA_FORM/IVA_FORM model is
            // proven against the DS10 SROM TB-miss handler.
            static unsigned long s_ivaFormDiag = 0;
            constexpr unsigned long kCap = 64;
            if (s_ivaFormDiag < kCap) {
                ++s_ivaFormDiag;
                std::fprintf(stderr,
                    "MEMDIAG-VAFORM cyc=%llu pc=0x%016llx ipr=IVA_FORM "
                    "excAddr=0x%016llx i_ctl=0x%016llx value=0x%016llx\n",
                    static_cast<unsigned long long>(c.cpu->cycleCount),
                    static_cast<unsigned long long>(g.pc),
                    static_cast<unsigned long long>(c.cpu->excAddr),
                    static_cast<unsigned long long>(c.cpu->i_ctl),
                    static_cast<unsigned long long>(value));
            }
        }
#endif
        break;
    case coreLib::HW_VA_FORM:
        value = coreLib::computeVaForm(c.cpu->va_ctl & uint64_t{ 0xFFFFFFFFC0000000 },
            c.cpu->va,
            ((c.cpu->va_ctl >> 2) & uint64_t{ 1 }) != 0);
#if EMULATR_MEMDIAG
        {
            // MEMDIAG-VAFORM probe: PALcode reads HW_VA_FORM in the
            // DTBM_SINGLE entry to obtain p4 = VPTE address.  If p4
            // ends up == mm_stat (e.g. 0x280 for an LDL fault), then
            // cpu.va or cpu.va_ctl is the wrong input -- this probe
            // dumps both so we can tell.
            static unsigned long s_vaFormDiag = 0;
            constexpr unsigned long kCap = 64;
            if (s_vaFormDiag < kCap) {
                ++s_vaFormDiag;
                std::fprintf(stderr,
                    "MEMDIAG-VAFORM cyc=%llu pc=0x%016llx ipr=VA_FORM "
                    "va=0x%016llx va_ctl=0x%016llx mm_stat=0x%016llx "
                    "value=0x%016llx\n",
                    static_cast<unsigned long long>(c.cpu->cycleCount),
                    static_cast<unsigned long long>(g.pc),
                    static_cast<unsigned long long>(c.cpu->va),
                    static_cast<unsigned long long>(c.cpu->va_ctl),
                    static_cast<unsigned long long>(c.cpu->mm_stat),
                    static_cast<unsigned long long>(value));
            }
        }
#endif
        break;

        // ---- TB PTE_TEMP read-back (HRM 5.2.3 / 5.3.3; scbd PROVISIONAL) ----
    case coreLib::HW_ITB_PTE_TEMP_PROVISIONAL: value = c.cpu->itbPteTemp; break;
    case coreLib::HW_DTB_PTE_TEMP_PROVISIONAL: value = c.cpu->dtbPteTemp; break;

        // ---- IBox: ITB / exception state / control (silent-zero) ----
    case coreLib::HW_ITB_TAG:      // write-only in HW; PALcode rarely reads
    case coreLib::HW_ITB_PTE:      // ditto
    case coreLib::HW_ITB_IAP:      // action register
    case coreLib::HW_ITB_IA:       // action register
    case coreLib::HW_ITB_IS:       // action register
        // HW_IER / HW_IER_CM (Interrupt Enable + Current Mode, scbd
        // 0x010A / 0x010B).  Per Alpha 21264 EV6 HRM Section 5.2.8:
        // IER_CM is a combined register; IPR-index bits<1:0> control
        // which sub-fields are written / read:
        //
        //   index bit<1>  ->  IER field (the enable bits at 33..38, 31..28, etc.)
        //   index bit<0>  ->  CM (Current Mode) field, at bits [4:3] of the data
        //
        // So 0x010A (bits<1:0> = 10) selects IER only.
        //    0x010B (bits<1:0> = 11) selects both -- combined value.
        //
        // CM bit position in the DATA word: bits 4:3 per HRM 5.2.8 diagram.
        // (21164 had CM at bits 1:0; 21264 moved it.)  V1 confirms via mask
        // 0x18 (bits 4|3) in pal_service.h HW_IER write.
        //
        // Storage convention: cpu.ier holds only the IER bits (bits 3,4
        // cleared since they belong to CM); cpu.mode holds CM as a
        // Mode_Privilege enum.  HW_IER reads return cpu.ier alone;
        // HW_IER_CM reads OR cpu.mode (shifted to bits 4:3) into the
        // returned value.
        //
        // Machine::canAcceptInterrupt(irqLevel) gates on the
        // appropriate IER bit when arbitrating chipset divert
        // requests -- so the firmware controls when each interrupt
        // source is allowed to deliver.  Cold-boot reset value of
        // cpu.ier is 0 (all masked).
    case coreLib::HW_IER:
        value = c.cpu->ier;
        break;
    case coreLib::HW_IER_CM:
        value = coreLib::ierCmCompose(c.cpu->ier, c.cpu->mode);
        break;
    case coreLib::HW_SIRR:         // software interrupt request
    case coreLib::HW_INT_CLR:      // write-only
    case coreLib::HW_EXC_SUM:      // FP exception summary
    case coreLib::HW_IC_FLUSH_ASM: // action
    case coreLib::HW_IC_FLUSH:     // action
    case coreLib::HW_PCTR_CTL:     // perf counter control
    case coreLib::HW_CLR_MAP:      // action
    case coreLib::HW_I_STAT:       // IBox status
    case coreLib::HW_SLEEP:        // action
        value = 0; break;

        // ---- MBox: DTB / D-cache / process context (silent-zero) ----
    case coreLib::HW_DTB_TAG0:
    case coreLib::HW_DTB_PTE0:
    case coreLib::HW_DTB_IAP:
    case coreLib::HW_DTB_IA:
    case coreLib::HW_DTB_IS0:
    case coreLib::HW_DTB_ASN0:
    case coreLib::HW_DTB_ALTMODE:
    case coreLib::HW_DC_CTL:
    case coreLib::HW_DC_STAT:
    case coreLib::HW_PCTX:
    case coreLib::HW_DTB_TAG1:
    case coreLib::HW_DTB_PTE1:
    case coreLib::HW_DTB_IS1:
    case coreLib::HW_DTB_ASN1:
        value = 0; break;

        // ---- CBox CSR / IPR reads (HRM section 5.4) ----
        // HW_MFPR HW_C_DATA returns the visible 6-bit C_DATA register,
        // which was loaded by the most recent HW_MTPR HW_C_SHFT trigger
        // pulling 6 bits out of the ERROR_REG chain.  HW_MFPR does NOT
        // auto-advance the chain (only C_SHFT writes do).
        // HW_MFPR HW_C_SHFT is undefined per spec -- return zero.
        // See coreLib/CBoxState.h for the full model.
    case coreLib::HW_C_DATA: {
        // REMOVED 2026-05-28: __debugbreak() guard "errorReg should always be
        // 0 on a clean boot."  Obsolete now that MemDrainer.h intentionally
        // sets cBox.errorReg on BusError so the PAL MCHK handler's sys__cbox
        // chain poll sees non-zero and identifies the error class.
        value = static_cast<uint64_t>(c.cpu->cBox.dataReg);
        mmuLib::logCboxEvent(c.cpu->cycleCount, c.cpu->pc,
            mmuLib::CboxOp::Read,
            static_cast<uint16_t>(sel),
            value, c.cpu->cBox.errorReg);
        break;
    }
    case coreLib::HW_C_SHFT: {
        // REMOVED 2026-05-28: __debugbreak() guard (same rationale as
        // HW_C_DATA above).
        value = 0;
        mmuLib::logCboxEvent(c.cpu->cycleCount, c.cpu->pc,
            mmuLib::CboxOp::Read,
            static_cast<uint16_t>(sel),
            value, c.cpu->cBox.errorReg);
        break;
    }

                           // HW_VA -- faulting virtual address (read-only).  Populated by the
                           // trap-delivery path (cpu.va) so the PALcode TB-miss / fault handler
                           // reads the faulting address via HW_MFPR HW_VA.
    case coreLib::HW_VA:           value = c.cpu->va; break;

        // ---- CBox / Misc (silent-zero) ----
    case coreLib::HW_CC_CTL:       // counter control + offset
        value = 0; break;

        // PAL_TEMP range handled above by isPalTemp gate; the labels
        // are still listed in the enum but cannot reach here.
    case coreLib::HW_PAL_TEMP_0:  case coreLib::HW_PAL_TEMP_1:
    case coreLib::HW_PAL_TEMP_2:  case coreLib::HW_PAL_TEMP_3:
    case coreLib::HW_PAL_TEMP_4:  case coreLib::HW_PAL_TEMP_5:
    case coreLib::HW_PAL_TEMP_6:  case coreLib::HW_PAL_TEMP_7:
    case coreLib::HW_PAL_TEMP_8:  case coreLib::HW_PAL_TEMP_9:
    case coreLib::HW_PAL_TEMP_10: case coreLib::HW_PAL_TEMP_11:
    case coreLib::HW_PAL_TEMP_12: case coreLib::HW_PAL_TEMP_13:
    case coreLib::HW_PAL_TEMP_14: case coreLib::HW_PAL_TEMP_15:
    case coreLib::HW_PAL_TEMP_16: case coreLib::HW_PAL_TEMP_17:
    case coreLib::HW_PAL_TEMP_18: case coreLib::HW_PAL_TEMP_19:
    case coreLib::HW_PAL_TEMP_20: case coreLib::HW_PAL_TEMP_21:
    case coreLib::HW_PAL_TEMP_22: case coreLib::HW_PAL_TEMP_23:
    case coreLib::HW_PAL_TEMP_24: case coreLib::HW_PAL_TEMP_25:
    case coreLib::HW_PAL_TEMP_26: case coreLib::HW_PAL_TEMP_27:
    case coreLib::HW_PAL_TEMP_28: case coreLib::HW_PAL_TEMP_29:
    case coreLib::HW_PAL_TEMP_30: case coreLib::HW_PAL_TEMP_31:
        value = c.cpu->palTemp[coreLib::palTempIndex(sel)]; break;

    default:
        // Truly unknown selector -- raw scbd is not in the V1
        // HW_IPR enum.  Halt with a fault; the lookback ring
        // captures the failing encoding for diagnosis.
        r.faultCode = coreLib::kFaultUnimplemented;
        return r;
    }

    r.regWriteIdx = raIndex(g);
    r.regWriteIsFp = false;
    r.regWriteValue = value;
    return r;
}

// ----------------------------------------------------------------------------
// HW_MTPR -- write internal processor register.  Reads Rb (c.opB),
// writes to the CpuState field selected by encoded[15:8].  This is
// the documented exception to the "leaves do not mutate CpuState
// directly" contract -- IPR mutation is PAL-side state and does not
// flow through the BoxResult commit path.
//
// Operand: the source GPR for HW_MTPR is encoded in Rb (bits 20:16),
// NOT Ra.  See the encoding comment near iprSelector above for the
// PALcode-macro reference.  The TSV row sets S_ReadsRb so the
// pipeline populates c.opB; we read c.opB here.
//
// Coverage policy mirrors execHwMfpr: switch is exhaustive over the
// V1 HW_IPR enum.  Backed IPRs land in their CpuState field; unbacked
// IPRs silently swallow the write (no-op); selectors outside the
// enum raise kFaultUnimplemented and halt with the lookback intact.
// ----------------------------------------------------------------------------
AXP_HOT AXP_FLATTEN
auto execHwMtpr(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult
{
    BoxResult r;
    r.semFlags = g.semFlags;

    coreLib::HW_IPR const sel = iprSelector(g);

    // PAL_TEMP range first: range check is cheaper than case labels
    // and PT slots are written on every PAL entry.
    if (coreLib::isPalTemp(sel)) {
        c.cpu->palTemp[coreLib::palTempIndex(sel)] = c.opB;
        return r;
    }

#if EMULATR_MEMDIAG
    // MMU-control MTPR probe.  Logs writes to the IPRs whose state governs TB
    // fills and page-table-walk addressing, so we can answer:
    //   (a) does the SROM ever set VPTB via I_CTL/M_CTL/VA_CTL (bits 47:30 on
    //       I_CTL, 63:30 on VA_CTL)?  If so, V4 may have dropped it; if not,
    //       the design here doesn't rely on a VPTB walk at all.
    //   (b) does the SROM manually install TB entries via ITB_TAG/PTE and
    //       DTB_TAG/PTE for the pages it's about to use (e.g. 0x24000)?
    // Capped to keep the log bounded.
    {
        bool isMmuCtl = false;
        switch (sel) {
            case coreLib::HW_I_CTL:
            case coreLib::HW_M_CTL:
            case coreLib::HW_VA_CTL:
            case coreLib::HW_ITB_TAG:
            case coreLib::HW_ITB_PTE:
            case coreLib::HW_DTB_TAG0:
            case coreLib::HW_DTB_PTE0:
            case coreLib::HW_DTB_TAG1:
            case coreLib::HW_DTB_PTE1:
                isMmuCtl = true;
                break;
            default:
                break;
        }
        if (isMmuCtl) {
            static unsigned long s_mtprDiag = 0;
            constexpr unsigned long kCap = 256;
            if (s_mtprDiag < kCap) {
                ++s_mtprDiag;
                // VPTB extracts: I_CTL[VPTB] = bits 47:30 (18 bits);
                // VA_CTL[VPTB] = bits 63:30 (34 bits).
                uint64_t const vptbHint =
                      (sel == coreLib::HW_I_CTL)  ? ((c.opB >> 30) & 0x3FFFFull)
                    : (sel == coreLib::HW_VA_CTL) ? ((c.opB >> 30) & 0x3FFFFFFFFull)
                    : 0ull;
                std::fprintf(stderr,
                    "MEMDIAG-MTPR cyc=%llu pc=0x%016llx ipr=0x%04x "
                    "value=0x%016llx vptbHint=0x%llx\n",
                    static_cast<unsigned long long>(c.cpu->cycleCount),
                    static_cast<unsigned long long>(g.pc),
                    static_cast<unsigned>(sel),
                    static_cast<unsigned long long>(c.opB),
                    static_cast<unsigned long long>(vptbHint));
            }
        }
    }
#endif

    switch (sel) {


    // ---- CpuState-backed writes ----
    case coreLib::HW_EXC_ADDR: c.cpu->excAddr = c.opB;                                          break;
    case coreLib::HW_PAL_BASE: {
        // DIAGNOSTIC: log every HW_PAL_BASE write -- retained as a
        // sanity check during bring-up after the Ra/Rb operand-source
        // fix.  Includes R26 (RA), R27 (pv), R29 (GP), R30 (SP)
        // snapshots so we can correlate against the source register
        // identified in the encoded instruction.  Rare event; cost
        // negligible.  Remove once the post-fix runs prove stable.
#if !defined(AXP_EXEC_TRACE) && EMULATR_BRINGUP_PROBES
        std::fprintf(stderr,
            "DEBUG: HW_MTPR HW_PAL_BASE pc=0x%016llx  "
            "old=0x%016llx -> new=0x%016llx  cycle=%llu\n"
            "  pre:  R26=0x%016llx R27=0x%016llx "
            "R29=0x%016llx R30=0x%016llx\n"
            "  encoded=0x%08x  Ra=R%u  Rb=R%u  scbd=0x%02x  func=0x%02x\n",
            static_cast<unsigned long long>(g.pc),
            static_cast<unsigned long long>(c.cpu->palBase),
            static_cast<unsigned long long>(c.opB),
            static_cast<unsigned long long>(c.cpu->cycleCount),
            static_cast<unsigned long long>(c.cpu->intReg[26]),
            static_cast<unsigned long long>(c.cpu->intReg[27]),
            static_cast<unsigned long long>(c.cpu->intReg[29]),
            static_cast<unsigned long long>(c.cpu->intReg[30]),
            static_cast<unsigned>(g.encoded),
            static_cast<unsigned>((g.encoded >> 21) & 0x1Fu),
            static_cast<unsigned>((g.encoded >> 16) & 0x1Fu),
            static_cast<unsigned>((g.encoded >> 8) & 0xFFu),
            static_cast<unsigned>(g.encoded & 0xFFu));
#endif
        // HRM 5.2.13: bits [63:44] and [14:0] are RAZ/MBZ.  Mask
        // before storing so subsequent HW_MFPR HW_PAL_BASE returns
        // architecturally-correct zeros in those reserved positions.
        // Real hardware ignores writes to those bits; V4 prior to
        // this fix stored them verbatim and would have surfaced
        // garbage on read-back.  No live consumer has tripped the
        // divergence (firmware always writes spec-clean 32 KiB-aligned
        // values), but the masking is defensive correctness.
        c.cpu->palBase = coreLib::palBaseSanitize(c.opB);
        break;
    }
    // I_CTL / M_CTL writes.  Besides storing the raw register, re-derive the
    // superpage-enable field the translator actually consults (cpu.i_spe /
    // cpu.m_spe).  These were never updated here, so firmware enabling SPE had
    // no effect -- Ev6Translator::tryKsegTranslate saw spe=0, every kseg access
    // TB-missed and page-walked, and with VPTB unset the walk spun forever.
    // EV6 HRM / ev6_defs.mar: I_CTL[SPE] = bits<5:3>, M_CTL[SPE] = bits<3:1>.
    case coreLib::HW_I_CTL: {
        // 2026-06-03: SDE<1> edges while IN PAL mode must swap the shadow
        // bank immediately.  Invariant: live bank == shadow iff
        // (palMode && SDE<1>); palModeEnter/Leave cover the palMode edges,
        // this covers the SDE edges (VMS PAL clears SDE around its
        // interrupt-frame save/restore -- ev6_vms_pal.mar "zap sde").
        bool const wasOn = coreLib::iCtlSdeHigh(c.cpu->i_ctl);
        bool const nowOn = coreLib::iCtlSdeHigh(c.opB);
        c.cpu->i_ctl = c.opB;
        c.cpu->i_spe = static_cast<uint8_t>((c.opB >> 3) & 0x7u);
#if EMULATR_BRINGUP_PROBES
        // TEMP (SDE swap ledger): record the in-PAL SDE toggle edges (the
        // "zap sde"/"restore sde" pair the VMS clock ISR does each tick).
        if (palDiag::g_sdeTraceArmed && c.cpu->inPalMode() && wasOn != nowOn)
            palDiag::sdeLog(nowOn ? "ictl-set-pre" : "ictl-clr-pre", *c.cpu);
#endif
        if (c.cpu->inPalMode() && wasOn != nowOn) {
            coreLib::swapPalShadowRegs(*c.cpu);
        }
#if EMULATR_BRINGUP_PROBES
        if (palDiag::g_sdeTraceArmed && c.cpu->inPalMode() && wasOn != nowOn)
            palDiag::sdeLog("ictl-postswap", *c.cpu);
#endif
        break;
    }
    case coreLib::HW_M_CTL:
        c.cpu->m_ctl = c.opB;
        c.cpu->m_spe = static_cast<uint8_t>((c.opB >> 1) & 0x7u);
        break;
        // HW_IER / HW_IER_CM writes (scbd 0x010A / 0x010B).
        //
        // Per Alpha 21264 EV6 HRM Section 5.2.8: IER_CM is a combined
        // register; IPR-index bits<1:0> select which sub-fields the
        // write updates:
        //
        //   0x010A (bits<1:0> = 10): write IER only; CM preserved.
        //   0x010B (bits<1:0> = 11): write BOTH IER and CM.
        //
        // CM bit position in the DATA word: bits 4:3 per HRM Section 5.2.8.
        // Mask 0x18 = bits 3|4.  V1 confirms this in palLib_ev6/pal_service.h
        // (uses `value & ~0x18ULL`).
        //
        // Storage convention: cpu.ier holds only the IER bits (bits 3,4
        // always cleared since they belong to CM); cpu.mode holds CM
        // as a Mode_Privilege enum.  Both writes mask off bits 3,4 from
        // the IER store; HW_IER_CM additionally extracts (opB>>3) & 0x3
        // into cpu.mode.
        //
        // Phase D consumer: Machine::canAcceptInterrupt(irqLevel)
        // reads cpu.ier and refuses divert when the matching IER bit
        // is clear.  Cold-boot reset value 0 masks every source.
    case coreLib::HW_IER:
        c.cpu->ier = coreLib::ierCmIerPortion(c.opB);
        break;
    case coreLib::HW_IER_CM:
        c.cpu->ier = coreLib::ierCmIerPortion(c.opB);
        c.cpu->mode = coreLib::ierCmExtractMode(c.opB);
        break;
    case coreLib::HW_MM_STAT:  c.cpu->mm_stat = c.opB;                                          break;
    case coreLib::HW_VA_CTL:   c.cpu->va_ctl = c.opB;                                          break;
        /* NOLINT(clang-diagnostic-invalid-utf8)
         * Derived ticking -- cpu.ccOffset is the only stored field;
         * HW_MFPR HW_CC returns uint32_t(cpu.cycleCount + cpu.ccOffset),
         * HW_MTPR HW_CC sets cpu.ccOffset = written - cpu.cycleCount. No extra increment cost. \
         * Architecturally indistinguishable from a real free-running counter that was set by HW_MTPR.
         */
    case coreLib::HW_CC: {
        c.cpu->ccOffset = c.opB - c.cpu->cycleCount;
    }                                       break;
    case coreLib::HW_CM:       c.cpu->mode = static_cast<coreLib::Mode_Privilege>(c.opB & 0x3ULL); break;

        // ---- ITB fill / invalidate (C2b: software-managed TLB) ----
        // ITB_TAG is write-only staging; the VA it holds is consumed when
        // ITB_PTE is written (HRM 5.2.1 round-robin fill).  I-side ASN comes
        // from PCTX (cpu.asn), not a dedicated ITB_ASN IPR.
    case coreLib::HW_ITB_TAG:
        c.cpu->itbTag = c.opB;
        break;
    case coreLib::HW_ITB_PTE: {
        c.cpu->itbPteTemp = c.opB;   // TEMP contract: stage raw IPR value
        pteLib::AlphaPte const pte = pteLib::canonicalFromItbPte(c.opB);
        uint8_t const gh = static_cast<uint8_t>((c.opB >> 5) & 0x3ULL);
        c.cpu->itbMgr.insert(pteLib::TlbRealm::Itb, c.cpu->itbTag,
            c.cpu->asn, pte, gh);
#if EMULATR_MEMDIAG
        if (c.cpu->itbTag >= 0x600000ULL && c.cpu->itbTag < 0x608000ULL) {
            std::fprintf(stderr,
                "MEMDIAG-ITBFILL pc=0x%016llx exc=0x%016llx tag=0x%016llx "
                "vpn=0x%llx opB=0x%016llx pfn=0x%llx asn=%llu gh=%u\n",
                static_cast<unsigned long long>(c.cpu->pc),
                static_cast<unsigned long long>(c.cpu->excAddr),
                static_cast<unsigned long long>(c.cpu->itbTag),
                static_cast<unsigned long long>(c.cpu->itbTag >> 13),
                static_cast<unsigned long long>(c.opB),
                static_cast<unsigned long long>(pte.pfn()),
                static_cast<unsigned long long>(c.cpu->asn),
                static_cast<unsigned>(gh));
        }
#endif
        break;
    }
    case coreLib::HW_ITB_IAP:
        c.cpu->itbMgr.invalidateAllProcess();
        break;
    case coreLib::HW_ITB_IA:
        c.cpu->itbMgr.invalidateAll();
        break;
    case coreLib::HW_ITB_IS:
        c.cpu->itbMgr.invalidateSingle(pteLib::TlbRealm::Itb, c.opB, c.cpu->asn);
        break;

        // ---- IBox writes (silent no-op) ----
    case coreLib::HW_IVA_FORM:     // architecturally read-only; permissive
    case coreLib::HW_SIRR:
    case coreLib::HW_ISUM:         // architecturally read-only; permissive
    case coreLib::HW_INT_CLR:
    case coreLib::HW_EXC_SUM:
    case coreLib::HW_IC_FLUSH_ASM:
    case coreLib::HW_IC_FLUSH:
    case coreLib::HW_PCTR_CTL:
    case coreLib::HW_CLR_MAP:
    case coreLib::HW_I_STAT:       // architecturally read-only; permissive
    case coreLib::HW_SLEEP:
        break;

        // ---- DTB fill / invalidate (C2b: software-managed TLB) ----
        // DTB has dual banks 0/1 for dual-issue store-pair.  TAG/ASN are
        // staging; a DTB_PTEn write retires TAGn + ASNn + PTE into a fill.
        // DTB_ASN holds ASN at register bits [63:56] (HRM, Tim 2026-05-20).
    case coreLib::HW_DTB_TAG0:  c.cpu->dtbTag0 = c.opB;                       break;
    case coreLib::HW_DTB_TAG1:  c.cpu->dtbTag1 = c.opB;                       break;
    case coreLib::HW_DTB_ASN0:  c.cpu->dtbAsn0 = (c.opB >> 56) & 0xFFULL;     break;
    case coreLib::HW_DTB_ASN1:  c.cpu->dtbAsn1 = (c.opB >> 56) & 0xFFULL;     break;
    case coreLib::HW_DTB_PTE0: {
        c.cpu->dtbPteTemp = c.opB;   // TEMP contract: stage raw IPR value
        pteLib::AlphaPte const pte = pteLib::canonicalFromDtbPte(c.opB);
        uint8_t const gh = static_cast<uint8_t>((c.opB >> 5) & 0x3ULL);
        c.cpu->dtbMgr.insert(pteLib::TlbRealm::Dtb, c.cpu->dtbTag0,
            c.cpu->dtbAsn0, pte, gh);
#if EMULATR_MEMDIAG
        if (c.cpu->dtbTag0 >= 0x600000ULL && c.cpu->dtbTag0 < 0x608000ULL) {
            std::fprintf(stderr,
                "MEMDIAG-DTBFILL0 pc=0x%016llx exc=0x%016llx tag=0x%016llx "
                "vpn=0x%llx opB=0x%016llx pfn=0x%llx asn=%llu gh=%u\n",
                static_cast<unsigned long long>(c.cpu->pc),
                static_cast<unsigned long long>(c.cpu->excAddr),
                static_cast<unsigned long long>(c.cpu->dtbTag0),
                static_cast<unsigned long long>(c.cpu->dtbTag0 >> 13),
                static_cast<unsigned long long>(c.opB),
                static_cast<unsigned long long>(pte.pfn()),
                static_cast<unsigned long long>(c.cpu->dtbAsn0),
                static_cast<unsigned>(gh));
        }
#endif
        break;
    }
    case coreLib::HW_DTB_PTE1: {
        c.cpu->dtbPteTemp = c.opB;   // shared TEMP, bank-1 tag/asn
        pteLib::AlphaPte const pte = pteLib::canonicalFromDtbPte(c.opB);
        uint8_t const gh = static_cast<uint8_t>((c.opB >> 5) & 0x3ULL);
        c.cpu->dtbMgr.insert(pteLib::TlbRealm::Dtb, c.cpu->dtbTag1,
            c.cpu->dtbAsn1, pte, gh);
        break;
    }
    case coreLib::HW_DTB_IAP:
        c.cpu->dtbMgr.invalidateAllProcess();
        break;
    case coreLib::HW_DTB_IA:
        c.cpu->dtbMgr.invalidateAll();
        break;
    case coreLib::HW_DTB_IS0:
        c.cpu->dtbMgr.invalidateSingle(pteLib::TlbRealm::Dtb, c.opB, c.cpu->dtbAsn0);
        break;
    case coreLib::HW_DTB_IS1:
        c.cpu->dtbMgr.invalidateSingle(pteLib::TlbRealm::Dtb, c.opB, c.cpu->dtbAsn1);
        break;

        // ---- TB PTE_TEMP staging (HRM 5.2.3 / 5.3.3; scbd PROVISIONAL) ----
        // TLB TEMP-register contract: HW_MTPR to a PTE_TEMP stages the raw IPR
        // payload; a subsequent HW_MFPR reads it back.
    case coreLib::HW_ITB_PTE_TEMP_PROVISIONAL: c.cpu->itbPteTemp = c.opB; break;
    case coreLib::HW_DTB_PTE_TEMP_PROVISIONAL: c.cpu->dtbPteTemp = c.opB; break;

        // ---- MBox writes (silent no-op) ----
    case coreLib::HW_DTB_ALTMODE:
    case coreLib::HW_DC_CTL:
    case coreLib::HW_DC_STAT:      // architecturally read-only; permissive
    case coreLib::HW_PCTX:
        break;

        // ---- CBox CSR / IPR writes (HRM section 5.4) ----
        // HW_MTPR HW_C_DATA pushes the low 6 bits of opB into the
        // 36-bit WRITE_MANY chain.  HW_MTPR HW_C_SHFT with bit 0 set
        // triggers a 6-bit shift of ERROR_REG into the visible C_DATA
        // register (read back by a subsequent HW_MFPR HW_C_DATA);
        // bit 0 clear is a no-op.  See coreLib/CBoxState.h.
    case coreLib::HW_C_DATA: {
        // REMOVED 2026-05-28: __debugbreak() guard "errorReg should always be
        // 0 on a clean boot."  Obsolete now that MemDrainer.h intentionally
        // sets cBox.errorReg on BusError.
        uint64_t const chunk = c.opB & 0x3FULL;
        c.cpu->cBox.pushWriteMany(chunk);
        mmuLib::logCboxEvent(c.cpu->cycleCount, c.cpu->pc,
            mmuLib::CboxOp::Write,
            static_cast<uint16_t>(sel),
            chunk, c.cpu->cBox.writeMany);
        break;
    }
    case coreLib::HW_C_SHFT: {
        // REMOVED 2026-05-28: __debugbreak() guard (same rationale).
        uint64_t const trig = c.opB & 0x1ULL;
        c.cpu->cBox.shftCtrl = static_cast<uint8_t>(trig);
        if (trig != 0) {
            c.cpu->cBox.shiftErrorOut();
        }
        mmuLib::logCboxEvent(c.cpu->cycleCount, c.cpu->pc,
            mmuLib::CboxOp::Write,
            static_cast<uint16_t>(sel),
            trig, c.cpu->cBox.errorReg);
        break;
    }

                           // ---- CBox / Misc writes (silent no-op) ----
    case coreLib::HW_CC_CTL:
    case coreLib::HW_VA:            // architecturally read-only; permissive
    case coreLib::HW_VA_FORM:       // architecturally read-only; permissive
        break;

        // PAL_TEMP range handled above by isPalTemp gate; the labels
        // are still listed for switch exhaustiveness.
    case coreLib::HW_PAL_TEMP_0:  case coreLib::HW_PAL_TEMP_1:
    case coreLib::HW_PAL_TEMP_2:  case coreLib::HW_PAL_TEMP_3:
    case coreLib::HW_PAL_TEMP_4:  case coreLib::HW_PAL_TEMP_5:
    case coreLib::HW_PAL_TEMP_6:  case coreLib::HW_PAL_TEMP_7:
    case coreLib::HW_PAL_TEMP_8:  case coreLib::HW_PAL_TEMP_9:
    case coreLib::HW_PAL_TEMP_10: case coreLib::HW_PAL_TEMP_11:
    case coreLib::HW_PAL_TEMP_12: case coreLib::HW_PAL_TEMP_13:
    case coreLib::HW_PAL_TEMP_14: case coreLib::HW_PAL_TEMP_15:
    case coreLib::HW_PAL_TEMP_16: case coreLib::HW_PAL_TEMP_17:
    case coreLib::HW_PAL_TEMP_18: case coreLib::HW_PAL_TEMP_19:
    case coreLib::HW_PAL_TEMP_20: case coreLib::HW_PAL_TEMP_21:
    case coreLib::HW_PAL_TEMP_22: case coreLib::HW_PAL_TEMP_23:
    case coreLib::HW_PAL_TEMP_24: case coreLib::HW_PAL_TEMP_25:
    case coreLib::HW_PAL_TEMP_26: case coreLib::HW_PAL_TEMP_27:
    case coreLib::HW_PAL_TEMP_28: case coreLib::HW_PAL_TEMP_29:
    case coreLib::HW_PAL_TEMP_30: case coreLib::HW_PAL_TEMP_31:
        c.cpu->palTemp[coreLib::palTempIndex(sel)] = c.opB;
        break;

    default:
        // Truly unknown selector outside the V1 HW_IPR enum.
        r.faultCode = coreLib::kFaultUnimplemented;
        return r;
    }

    return r;
}

// ----------------------------------------------------------------------------
// TEMP DIAGNOSTIC (DIVERT-REI register ledger) -- REMOVE AFTER the
// nvram_get fclose(&spl_kernel) corruption is root-caused.
//
// Machine.cpp records the native conserved registers (R2-R7, R20-R23)
// at every interval-timer divert; execHwRei compares them when the
// CPU returns to the interrupted PC.  Any register that differs across
// the full PAL interrupt round trip names the broken save/restore.
// Background: divert[2] interrupted nvram_checksum with the eerom
// FILE* live in a conserved register; after REI the register held
// &spl_kernel (0x1cc4c0) -> fclose(&spl_kernel) -> PC=0 halt at
// cyc 21,431,065,646 (2026-06-03).
//
// Extern-linked so Machine.cpp can fill the pending slots without a
// new header; both TUs link into the Emulatr image.
// ----------------------------------------------------------------------------
namespace palDiag {
uint64_t g_divertPendingPc[2]      = {};   // savedPc of an in-flight divert
uint64_t g_divertPendingCyc[2]     = {};   // cycle the divert fired
uint64_t g_divertPendingReg[2][10] = {};   // natives R2-R7, R20-R23 at divert
bool     g_divertPendingLive[2]    = {};   // slot occupied
// Register-number map for the 10 tracked slots (R2-R7, R20-R23).
constexpr int kDivertRegMap[10] = { 2, 3, 4, 5, 6, 7, 20, 21, 22, 23 };

// ---- TEMP (SDE shadow-swap ledger) -- REMOVE WITH the DIVERT-REI block ----
// Arms on a clock divert whose interrupted PC is in the 0x1ad600-0x1adbff
// wall loop, then logs the 8 shadow regs (R4-R7, R20-R23) + palMode + SDE<1>
// at every swap-eliciting edge (DIVERT, post-enter, ictl zap/restore, post-rei)
// for g_sdeTraceWindows ticks.  Auditing the 4-swap parity per tick pins which
// edge double-swaps or no-ops.  See project_ds20_wall_sde_shadow_choreography.
bool g_sdeTraceArmed   = false;
int  g_sdeTraceWindows = 3;        // number of 0x1adb60-window ticks to capture
void sdeLog(char const* tag, coreLib::CpuState const& cpu) noexcept
{
#if EMULATR_BRINGUP_PROBES
    if (!g_sdeTraceArmed) return;
    std::fprintf(stderr,
        "[SDE %-13s] cyc=%llu pal=%d sde1=%d  r4=%llx r5=%llx r6=%llx r7=%llx "
        "r20=%llx r21=%llx r22=%llx r23=%llx\n",
        tag,
        static_cast<unsigned long long>(cpu.cycleCount),
        cpu.inPalMode() ? 1 : 0,
        coreLib::iCtlSdeHigh(cpu.i_ctl) ? 1 : 0,
        static_cast<unsigned long long>(cpu.intReg[4]),
        static_cast<unsigned long long>(cpu.intReg[5]),
        static_cast<unsigned long long>(cpu.intReg[6]),
        static_cast<unsigned long long>(cpu.intReg[7]),
        static_cast<unsigned long long>(cpu.intReg[20]),
        static_cast<unsigned long long>(cpu.intReg[21]),
        static_cast<unsigned long long>(cpu.intReg[22]),
        static_cast<unsigned long long>(cpu.intReg[23]));
    std::fflush(stderr);
#else
    (void)tag; (void)cpu;
#endif
}
// ---- END TEMP SDE shadow-swap ledger ----
} // namespace palDiag
// ---- END TEMP DIVERT-REI ledger storage ----

// ----------------------------------------------------------------------------
// HW_REI -- return from PALcode.  Reads EXC_ADDR (saved when the trap
// entered PAL), packs divertTarget = excAddr with divert = true, and
// clears PS<PALmode> on the way out so the resumed instruction runs
// outside PAL mode.
//
// The PALmode clear is an explicit exception to the "leaves do not
// mutate CpuState directly" contract -- documented in BoxResult.h's
// drain map for HW_xxx and CALL_PAL leaves.  ExecCtx::cpu is the
// escape hatch; we reach through it because PS<PALmode> is PAL-side
// state, not a regfile slot, and does not flow through the
// BoxResult commit path.
//
// In v1, EXC_ADDR is initialised to zero and only ever written by a
// future trap-delivery path.  Calling HW_REI before any trap fires
// diverts to PC=0 -- valid PAL behaviour (boot path) but not v1-
// relevant.
// ----------------------------------------------------------------------------
AXP_HOT AXP_FLATTEN
auto execHwRei(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult
{
    BoxResult r;
    r.semFlags = g.semFlags;

    // HW_REI / HW_RET (opcode 0x1E).  Per Alpha 21264 EV6 HRM:
    //
    //   bits[15:14]  ic_pred (icache prediction hint: HW_JMP /
    //                HW_JSR / HW_RET / HW_COROUTINE)
    //   bit 13       STALL (icache stall control)
    //   bit 12       STACKED -- 1 = use saved exc_addr as resume PC,
    //                0 = use Rb (intReg[encoded[20:16]]) as resume PC.
    //
    // STACKED form (bit 12 = 1) is used by PAL exception handlers
    // exiting back to the trapped instruction; the trap delivery path
    // saved the interrupted PC into excAddr with bit 0 = palMode of
    // the interrupted context.
    //
    // REGISTER form (bit 12 = 0) is used by PAL subroutine returns
    // and explicit jumps with palMode change.  The SRM bootstrap
    // computes the target into Rb (commonly R0) -- the low bit of the
    // computed value sets the resulting palMode (1 = stay in PAL,
    // 0 = exit PAL).
    //
    // V4 history note: previous implementation used bit 13 as the
    // STACKED/REGISTER selector and looped indefinitely on the SRM
    // bootstrap's HW_REI at PC 0x220 (encoding 0x7be0a000) -- bit 13
    // is set there but bit 12 is clear, and the bootstrap had pre-
    // computed R0 = 0x229 expecting REGISTER form.  Correcting to
    // bit 12 lets the bootstrap divert to PC 0x228 in PAL mode and
    // continue.  The bit-12 reading matches the EV6 HRM; bit 13 is
    // STALL and orthogonal to the resume-PC source.
    bool const stacked = ((g.encoded >> 12) & 0x1u) != 0;
    uint64_t const rawTarget =
        stacked ? c.cpu->excAddr
        : c.cpu->intReg[(g.encoded >> 16) & 0x1Fu];

    bool const resumeInPal = (rawTarget & 0x1ULL) != 0;
    r.divertTarget = rawTarget;   // keep bit 0 (PALmode == PC<0>), applied at WB
    r.divert = true;

    // TEMP DIAG 2026-05-30 (HW_REI target H1/H2 probe) -- REMOVE BEFORE COMMIT.
    // Wide window around the clock-handler return (event ~cyc 189564930-984
    // depending on cold-boot vs snapshot resume).  HW_REI is high-volume, so
    // gate to PAL->native returns only (the interrupt-return signature) and
    // cap.  rawTarget is the PC this HW_REI resumes at: 0x1c699c => HW_REI fine
    // (the ITB-miss excAddr latch was the bug, H1); 0 => HW_REI mis-targeted
    // (H2), and STACKED/REGISTER + Rb show which source was zero.
#if EMULATR_BRINGUP_PROBES
    if (c.cpu->cycleCount >= 189564000ull && c.cpu->cycleCount <= 189565200ull
        && c.cpu->inPalMode() && !resumeInPal) {
        static int s_reiProbe = 0;
        if (s_reiProbe < 16) {
            ++s_reiProbe;
            std::fprintf(stderr,
                "[REI-PROBE] cyc=%llu pc=0x%016llx enc=0x%08x %s Ra=%u Rb=%u "
                "R6=0x%016llx R23=0x%016llx rawTarget=0x%016llx excAddr=0x%016llx\n",
                static_cast<unsigned long long>(c.cpu->cycleCount),
                static_cast<unsigned long long>(c.cpu->pc),
                static_cast<unsigned>(g.encoded),
                stacked ? "STACKED" : "REGISTER",
                static_cast<unsigned>((g.encoded >> 21) & 0x1Fu),
                static_cast<unsigned>((g.encoded >> 16) & 0x1Fu),
                static_cast<unsigned long long>(c.cpu->intReg[6]),
                static_cast<unsigned long long>(c.cpu->intReg[23]),
                static_cast<unsigned long long>(rawTarget),
                static_cast<unsigned long long>(c.cpu->excAddr));
            std::fflush(stderr);
        }
    }
#endif

    // PAL-mode bit comes off the resume target's low bit; this
    // matches V1's behaviour and is the documented PALmode-truth on
    // EV6 (h->inPalMode() == (h->pc & 1)).  Route through setPalMode
    // so the EV6 R4-R7 / R20-R23 shadow swap fires on the transition
    // when I_CTL[SDE] is set.  No-op when palMode is unchanged
    // (HW_REI from PAL to PAL, e.g. nested PAL handler unwinding).
    // TEMP DIAGNOSTIC (HW_REI mode-transition trace, un-gated) -- REMOVE BEFORE COMMIT.
    // Tim's theory: an HW_REI dropping PAL (1->0) may land at a distorted VA
    // (bit-0 / offset mishandling under PALmode==PC<0>), putting the console at
    // a non-kseg VA -> the 0x60222c self-check halt.  Log every actual mode
    // change with its target so a malformed drop target is visible.  Capped.
#if EMULATR_BRINGUP_PROBES
    {
        bool const wasPal = c.cpu->inPalMode();
        if (wasPal != resumeInPal) {
            static int s_reiXlog = 0;
            if (s_reiXlog < 256) {
                ++s_reiXlog;
                std::fprintf(stderr,
                    "[HW_REI XITION #%d] pc=0x%016llx %s Rb=%u rawTarget=0x%016llx "
                    "%s->%s excAddr=0x%016llx cyc=%llu\n",
                    s_reiXlog,
                    static_cast<unsigned long long>(c.cpu->pc),
                    stacked ? "STACKED" : "REGISTER",
                    static_cast<unsigned>((g.encoded >> 16) & 0x1Fu),
                    static_cast<unsigned long long>(rawTarget),
                    wasPal ? "PAL" : "native",
                    resumeInPal ? "PAL" : "native",
                    static_cast<unsigned long long>(c.cpu->excAddr),
                    static_cast<unsigned long long>(c.cpu->cycleCount));
            }
        }
    }
#endif
    coreLib::setPalMode(*c.cpu, resumeInPal);

#if EMULATR_BRINGUP_PROBES
    // TEMP (SDE swap ledger): log the post-REI native view, then close the
    // window when this REI resumes the interrupted wall-loop PC.
    if (palDiag::g_sdeTraceArmed) {
        palDiag::sdeLog("post-rei", *c.cpu);
        uint64_t const rp = rawTarget & ~uint64_t{3};
        if (!resumeInPal && rp >= 0x1ad600ull && rp <= 0x1adbffull) {
            if (palDiag::g_sdeTraceWindows > 0) --palDiag::g_sdeTraceWindows;
            palDiag::g_sdeTraceArmed = false;
            std::fprintf(stderr, "[SDE window-close] resumePc=0x%llx "
                         "windows-left=%d\n",
                         static_cast<unsigned long long>(rp),
                         palDiag::g_sdeTraceWindows);
            std::fflush(stderr);
        }
    }
#endif

    // TEMP DIAGNOSTIC (DIVERT-REI register ledger compare) -- REMOVE AFTER
    // the nvram_get fclose(&spl_kernel) corruption is root-caused.
    // Runs AFTER setPalMode so the SDE shadow swap (PAL->native) has
    // already published the native view the resumed code will see.
    // Match on the resume target == a pending divert's savedPc; the
    // divert only fires outside PAL mode, so savedPc bit 0 is clear.
    if (!resumeInPal) {
        uint64_t const resumePc = rawTarget & ~uint64_t{3};
        for (int s = 0; s < 2; ++s) {
            if (!palDiag::g_divertPendingLive[s] ||
                palDiag::g_divertPendingPc[s] != resumePc) {
                continue;
            }
            palDiag::g_divertPendingLive[s] = false;
            int bad = 0;
            for (int i = 0; i < 10; ++i) {
                int const rn = palDiag::kDivertRegMap[i];
                uint64_t const was = palDiag::g_divertPendingReg[s][i];
                uint64_t const now = c.cpu->intReg[rn];
                if (was != now) {
                    ++bad;
#if EMULATR_BRINGUP_PROBES
                    std::fprintf(stderr,
                        "DIVERT-REI MISMATCH R%02d was=0x%016llx now=0x%016llx "
                        "savedPc=0x%llx divertCyc=%llu reiCyc=%llu\n",
                        rn,
                        static_cast<unsigned long long>(was),
                        static_cast<unsigned long long>(now),
                        static_cast<unsigned long long>(resumePc),
                        static_cast<unsigned long long>(
                            palDiag::g_divertPendingCyc[s]),
                        static_cast<unsigned long long>(c.cpu->cycleCount));
#endif
                }
            }
            if (bad == 0) {
                // First 8 clean round trips loud, then muted -- the
                // mismatches are the signal, the cleans are confidence.
                static int s_clean = 0;
                if (s_clean < 8) {
                    ++s_clean;
#if EMULATR_BRINGUP_PROBES
                    std::fprintf(stderr,
                        "DIVERT-REI CLEAN savedPc=0x%llx divertCyc=%llu "
                        "reiCyc=%llu\n",
                        static_cast<unsigned long long>(resumePc),
                        static_cast<unsigned long long>(
                            palDiag::g_divertPendingCyc[s]),
                        static_cast<unsigned long long>(c.cpu->cycleCount));
#endif
                }
            }
            std::fflush(stderr);
        }
    }
    // ---- END TEMP DIVERT-REI ledger compare ----

    return r;
}

#pragma endregion HW_xxx Stubs (CpuState prerequisite)


#pragma region CALL_PAL bulk-delegating leaves (S_PalEntry divert)

// ----------------------------------------------------------------------------
// Bulk-converted CALL_PAL leaves -- every entry below delegates to
// execCallPalDispatch.  Each row in GrainMasterV4.tsv marked S_PalEntry
// (= divert into PALcode at palBase + vector_offset, per HRM 6.8.1)
// gets a hand-written leaf with the dispatcher delegation, listed in
// handwritten.tsv so the codegen drops its kFaultUnimplemented stub.
//
// Why one big region instead of one #pragma per leaf:
//   - All bodies are identical -- 'return execCallPalDispatch(g, c);'
//   - The mnemonic shows up in the per-row GrainEntry name for traces
//     (DispatchTables.cpp), so trace fidelity is preserved.
//   - Per-leaf specialization (S_PalIntrinsic posture) can lift any one
//     leaf out of this region into a dedicated body without touching
//     the others.
//
// First-firmware-hit context: DS10 SRM at PC 0x1c6200 calls a sequence
// of MTPR_FEN / MTPR_DATFX / MTPR_MCES / MTPR_IPL during early CPU init.
// Adding leaves piecemeal would stop the boot one CALL_PAL at a time;
// the bulk conversion unblocks the whole class at once.
//
// Excluded (kept as real intrinsics, not delegated):
//   HALT, CSERVE, LDQP, STQP, SWPCTX (Tru64), MFPR_VPTB, MTPR_VPTB,
//   MFPR_SCBB, MTPR_SCBB, WTINT, MFPR_WHAMI.
// ----------------------------------------------------------------------------

// ---- MFPR group ----
AXP_HOT AXP_FLATTEN
auto execMfprAsn_vms(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult {
    return execCallPalDispatch(g, c);
}

AXP_HOT AXP_FLATTEN
auto execMfprAsten_vms(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult {
    return execCallPalDispatch(g, c);
}

AXP_HOT AXP_FLATTEN
auto execMfprAstsr_vms(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult {
    return execCallPalDispatch(g, c);
}

AXP_HOT AXP_FLATTEN
auto execMfprEsp_vms(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult {
    return execCallPalDispatch(g, c);
}

AXP_HOT AXP_FLATTEN
auto execMfprIpl_vms(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult {
    return execCallPalDispatch(g, c);
}

AXP_HOT AXP_FLATTEN
auto execMfprMces(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult {
    return execCallPalDispatch(g, c);
}

AXP_HOT AXP_FLATTEN
auto execMfprPcbb_vms(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult {
    return execCallPalDispatch(g, c);
}

AXP_HOT AXP_FLATTEN
auto execMfprPrbr_vms(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult {
    return execCallPalDispatch(g, c);
}

AXP_HOT AXP_FLATTEN
auto execMfprPtbr_vms(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult {
    return execCallPalDispatch(g, c);
}

AXP_HOT AXP_FLATTEN
auto execMfprSisr_vms(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult {
    return execCallPalDispatch(g, c);
}

AXP_HOT AXP_FLATTEN
auto execMfprSsp_vms(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult {
    return execCallPalDispatch(g, c);
}

AXP_HOT AXP_FLATTEN
auto execMfprSysptbr(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult {
    return execCallPalDispatch(g, c);
}

AXP_HOT AXP_FLATTEN
auto execMfprTbchk_vms(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult {
    return execCallPalDispatch(g, c);
}

AXP_HOT AXP_FLATTEN
auto execMfprUsp_vms(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult {
    return execCallPalDispatch(g, c);
}

AXP_HOT AXP_FLATTEN
auto execMfprVirbnd(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult {
    return execCallPalDispatch(g, c);
}


// ---- MTPR group ----
AXP_HOT AXP_FLATTEN
auto execMtprAsten_vms(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult {
    return execCallPalDispatch(g, c);
}

AXP_HOT AXP_FLATTEN
auto execMtprAstsr_vms(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult {
    return execCallPalDispatch(g, c);
}

AXP_HOT AXP_FLATTEN
auto execMtprDatfx(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult {
    return execCallPalDispatch(g, c);
}

AXP_HOT AXP_FLATTEN
auto execMtprEsp_vms(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult {
    return execCallPalDispatch(g, c);
}

AXP_HOT AXP_FLATTEN
auto execMtprIpir(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult {
    return execCallPalDispatch(g, c);
}

AXP_HOT AXP_FLATTEN
auto execMtprIpl_vms(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult {
    return execCallPalDispatch(g, c);
}

AXP_HOT AXP_FLATTEN
auto execMtprMces(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult {
    return execCallPalDispatch(g, c);
}

AXP_HOT AXP_FLATTEN
auto execMtprPerfmon(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult {
    return execCallPalDispatch(g, c);
}

AXP_HOT AXP_FLATTEN
auto execMtprPrbr_vms(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult {
    return execCallPalDispatch(g, c);
}

AXP_HOT AXP_FLATTEN
auto execMtprSirr_vms(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult {
    return execCallPalDispatch(g, c);
}

AXP_HOT AXP_FLATTEN
auto execMtprSsp_vms(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult {
    return execCallPalDispatch(g, c);
}

AXP_HOT AXP_FLATTEN
auto execMtprTbia_vms(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult {
    return execCallPalDispatch(g, c);
}

AXP_HOT AXP_FLATTEN
auto execMtprTbiap_vms(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult {
    return execCallPalDispatch(g, c);
}

AXP_HOT AXP_FLATTEN
auto execMtprTbis_vms(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult {
    return execCallPalDispatch(g, c);
}

AXP_HOT AXP_FLATTEN
auto execMtprTbisd_vms(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult {
    return execCallPalDispatch(g, c);
}

AXP_HOT AXP_FLATTEN
auto execMtprTbisi_vms(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult {
    return execCallPalDispatch(g, c);
}

AXP_HOT AXP_FLATTEN
auto execMtprUsp_vms(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult {
    return execCallPalDispatch(g, c);
}


// ---- CONTROL group ----
AXP_HOT AXP_FLATTEN
auto execBpt(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult {
    return execCallPalDispatch(g, c);
}

AXP_HOT AXP_FLATTEN
auto execBugchk_vms(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult {
    return execCallPalDispatch(g, c);
}

AXP_HOT AXP_FLATTEN
auto execCflush(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult {
    return execCallPalDispatch(g, c);
}

AXP_HOT AXP_FLATTEN
auto execChmk(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult {
    return execCallPalDispatch(g, c);
}

AXP_HOT AXP_FLATTEN
auto execChms_vms(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult {
    return execCallPalDispatch(g, c);
}

AXP_HOT AXP_FLATTEN
auto execChmu_vms(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult {
    return execCallPalDispatch(g, c);
}

AXP_HOT AXP_FLATTEN
auto execClrfen(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult {
    return execCallPalDispatch(g, c);
}

AXP_HOT AXP_FLATTEN
auto execDraina(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult {
    return execCallPalDispatch(g, c);
}

AXP_HOT AXP_FLATTEN
auto execGentrap(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult {
    return execCallPalDispatch(g, c);
}

AXP_HOT AXP_FLATTEN
auto execImb(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult {
    return execCallPalDispatch(g, c);
}

AXP_HOT AXP_FLATTEN
auto execRei(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult {
    return execCallPalDispatch(g, c);
}

AXP_HOT AXP_FLATTEN
auto execRetsys_tru64(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult {
    return execCallPalDispatch(g, c);
}

AXP_HOT AXP_FLATTEN
auto execSwpipl_tru64(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult {
    return execCallPalDispatch(g, c);
}

AXP_HOT AXP_FLATTEN
auto execSwppal(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult {
    return execCallPalDispatch(g, c);
}


// ---- WRRD group ----
AXP_HOT AXP_FLATTEN
auto execRdps_tru64(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult {
    return execCallPalDispatch(g, c);
}

AXP_HOT AXP_FLATTEN
auto execRdusp_tru64(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult {
    return execCallPalDispatch(g, c);
}

AXP_HOT AXP_FLATTEN
auto execRdPs_vms(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult {
    return execCallPalDispatch(g, c);
}

AXP_HOT AXP_FLATTEN
auto execTbi_tru64(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult {
    return execCallPalDispatch(g, c);
}

AXP_HOT AXP_FLATTEN
auto execWrent_tru64(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult {
    return execCallPalDispatch(g, c);
}

AXP_HOT AXP_FLATTEN
auto execWriteUnq(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult {
    return execCallPalDispatch(g, c);
}

AXP_HOT AXP_FLATTEN
auto execWrkgp_tru64(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult {
    return execCallPalDispatch(g, c);
}

AXP_HOT AXP_FLATTEN
auto execWrperfmon_tru64(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult {
    return execCallPalDispatch(g, c);
}

AXP_HOT AXP_FLATTEN
auto execWrusp_tru64(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult {
    return execCallPalDispatch(g, c);
}

AXP_HOT AXP_FLATTEN
auto execWrval_tru64(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult {
    return execCallPalDispatch(g, c);
}

AXP_HOT AXP_FLATTEN
auto execWrvptptr_tru64(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult {
    return execCallPalDispatch(g, c);
}

AXP_HOT AXP_FLATTEN
auto execWrPsSw_vms(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult {
    return execCallPalDispatch(g, c);
}


// ---- QUEUE group ----
AXP_HOT AXP_FLATTEN
auto execInsqhil_vms(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult {
    return execCallPalDispatch(g, c);
}

AXP_HOT AXP_FLATTEN
auto execInsqhilr_vms(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult {
    return execCallPalDispatch(g, c);
}

AXP_HOT AXP_FLATTEN
auto execInsqhiq_vms(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult {
    return execCallPalDispatch(g, c);
}

AXP_HOT AXP_FLATTEN
auto execInsqhiqr_vms(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult {
    return execCallPalDispatch(g, c);
}

AXP_HOT AXP_FLATTEN
auto execInsqtil_vms(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult {
    return execCallPalDispatch(g, c);
}

AXP_HOT AXP_FLATTEN
auto execInsqtilr_vms(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult {
    return execCallPalDispatch(g, c);
}

AXP_HOT AXP_FLATTEN
auto execInsqtiq_vms(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult {
    return execCallPalDispatch(g, c);
}

AXP_HOT AXP_FLATTEN
auto execInsqtiqr_vms(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult {
    return execCallPalDispatch(g, c);
}

AXP_HOT AXP_FLATTEN
auto execInsquel_vms(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult {
    return execCallPalDispatch(g, c);
}

AXP_HOT AXP_FLATTEN
auto execInsquelD_vms(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult {
    return execCallPalDispatch(g, c);
}

AXP_HOT AXP_FLATTEN
auto execInsqueq_vms(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult {
    return execCallPalDispatch(g, c);
}

AXP_HOT AXP_FLATTEN
auto execInsqueqD_vms(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult {
    return execCallPalDispatch(g, c);
}

AXP_HOT AXP_FLATTEN
auto execRemqhil_vms(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult {
    return execCallPalDispatch(g, c);
}

AXP_HOT AXP_FLATTEN
auto execRemqhiq_vms(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult {
    return execCallPalDispatch(g, c);
}

AXP_HOT AXP_FLATTEN
auto execRemqtil_vms(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult {
    return execCallPalDispatch(g, c);
}

AXP_HOT AXP_FLATTEN
auto execRemqtiq_vms(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult {
    return execCallPalDispatch(g, c);
}

// Interlocked (resident-reentrant) queue removes -- delegate to the CALL_PAL
// dispatcher exactly like the non-R removes above and the INSQ*R inserts (the
// firmware PAL runs the self-relative-queue + secondary-interlock algorithm).
// These were stubbed to logUnimplementedStub instead of delegated -- a coverage
// oversight, now corrected.
AXP_HOT AXP_FLATTEN
auto execRemqhilr_vms(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult {
    return execCallPalDispatch(g, c);
}

AXP_HOT AXP_FLATTEN
auto execRemqtilr_vms(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult {
    return execCallPalDispatch(g, c);
}

AXP_HOT AXP_FLATTEN
auto execRemqhiqr_vms(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult {
    return execCallPalDispatch(g, c);
}

AXP_HOT AXP_FLATTEN
auto execRemqtiqr_vms(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult {
    return execCallPalDispatch(g, c);
}

AXP_HOT AXP_FLATTEN
auto execRemquel_vms(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult {
    return execCallPalDispatch(g, c);
}

AXP_HOT AXP_FLATTEN
auto execRemquelD_vms(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult {
    return execCallPalDispatch(g, c);
}

AXP_HOT AXP_FLATTEN
auto execRemqueq_vms(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult {
    return execCallPalDispatch(g, c);
}

AXP_HOT AXP_FLATTEN
auto execRemqueqD_vms(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult {
    return execCallPalDispatch(g, c);
}


// ---- OTHER group ----
AXP_HOT AXP_FLATTEN
auto execAmovrm_vms(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult {
    return execCallPalDispatch(g, c);
}

AXP_HOT AXP_FLATTEN
auto execAmovrr_vms(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult {
    return execCallPalDispatch(g, c);
}

AXP_HOT AXP_FLATTEN
auto execProber_vms(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult {
    return execCallPalDispatch(g, c);
}

AXP_HOT AXP_FLATTEN
auto execProbew_vms(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult {
    return execCallPalDispatch(g, c);
}

AXP_HOT AXP_FLATTEN
auto execReadUnq(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult {
    return execCallPalDispatch(g, c);
}

AXP_HOT AXP_FLATTEN
auto execRscc_vms(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult {
    return execCallPalDispatch(g, c);
}

AXP_HOT AXP_FLATTEN
auto execSwasten_vms(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult {
    return execCallPalDispatch(g, c);
}

AXP_HOT AXP_FLATTEN
auto execWhami_tru64(InstructionGrain const& g, ExecCtx const& c) noexcept -> BoxResult {
    return execCallPalDispatch(g, c);
}

#pragma endregion CALL_PAL bulk-delegating leaves

} // namespace palBox
