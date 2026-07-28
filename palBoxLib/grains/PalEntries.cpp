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
#include "coreLib/PcTrace.h"
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
// 2026-07-08: ToyRtc.h include removed with the CSERVE 0x66 get_time case (its
// only user).  Time is read via the internal get_timestamp bsr, not a CSERVE.

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
            case 0x46: return "IIC_WRITE";
            case 0x65: return "MP_WORK_REQUEST";
            case 0x66: return "GET_PAL_BASE (masked)";
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
        case 0x46: {   // CSERVE$IIC_WRITE -- ev6_vms_pc264_pal.mar sys__iic_write (:5208)
            // JRN-VMB-006.  The VMS PAL packs a PCF8584 I2C write into R17:
            //   [7:0]  slave address   [15:8]  word address
            //   [23:16] data byte      [31:24] data-present flag (!=0 -> data)
            // sys__iic_write enable_superpage's then drives the controller at the
            // pc264 IIC base 0xFFF80000 in Pchip0 PCI-mem space (registerPciMemRange
            // in TsunamiChipset::wireDevices).  Superpage == rung-1 direct VA->PA, so
            // the resolved PA is kBasePA(0x800.0000.0000)+0xFFF80000; S0 (data) at +0,
            // S1 (control/status) at +1.  We replay START + slave (+word/+data) + STOP
            // against the emulated IicPcf8584 via c.memory (same physical-bus idiom as
            // the CSERVE primitives 0x10-0x13 above) and sample S1 for the ACK (LRB,
            // 0x08).  V5's IIC is an EMPTY bus (no slaves) so the address phase NAKs ->
            // R0 = -1, matching real HW with no device at slave 0x4E.
            uint64_t const arg    = c.cpu->intReg[17];
            uint8_t  const slave  = static_cast<uint8_t>( arg        & 0xFFu);
            uint8_t  const word   = static_cast<uint8_t>((arg >> 8)  & 0xFFu);
            uint8_t  const data   = static_cast<uint8_t>((arg >> 16) & 0xFFu);
            bool     const hasDat = ((arg >> 24) & 0xFFu) != 0u;
            int64_t        r0     = -1;   // empty-bus NAK default (no slave ACK)
            uint8_t        s1     = 0x08u;
            if (c.memory != nullptr) {
                constexpr coreLib::PAType kIicS0 = 0x800FFF80000ULL; // pc264 IIC S0 (data)
                constexpr coreLib::PAType kIicS1 = 0x800FFF80001ULL; // pc264 IIC S1 (ctl/stat)
                (void)c.memory->write1(kIicS0, slave);   // load slave into S0
                (void)c.memory->write1(kIicS1, 0xC5u);   // control: generate START
                (void)c.memory->read1(kIicS1, s1);       // sample S1 status
                if ((s1 & 0x08u) == 0u) {                // LRB=0 -> slave ACK'd
                    (void)c.memory->write1(kIicS0, word);
                    if (hasDat) (void)c.memory->write1(kIicS0, data);
                    r0 = 0;
                }
                (void)c.memory->write1(kIicS1, 0xC3u);   // control: generate STOP
            }
            r.regWriteIdx   = 0;     // R0
            r.regWriteIsFp  = false;
            r.regWriteValue = static_cast<uint64_t>(r0);
#if EMULATR_BRINGUP_PROBES
            {
                static bool const s_iicDiag =
                    (std::getenv("EMULATR_IIC_DIAG") != nullptr);
                if (s_iicDiag) {
                    static unsigned long s_iicN = 0;
                    if (s_iicN < 128) { ++s_iicN;
                        std::fprintf(stderr,
                            "IIC-DIAG[cserve46] cyc=%llu pc=0x%016llx arg=0x%016llx "
                            "slave=0x%02x word=0x%02x data=0x%02x s1=0x%02x r0=%lld\n",
                            static_cast<unsigned long long>(c.cpu->cycleCount),
                            static_cast<unsigned long long>(g.pc),
                            static_cast<unsigned long long>(arg),
                            static_cast<unsigned>(slave),
                            static_cast<unsigned>(word),
                            static_cast<unsigned>(data),
                            static_cast<unsigned>(s1),
                            static_cast<long long>(r0));
                        std::fflush(stderr);
                    }
                }
            }
#endif
            return r;
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
            // there are no secondaries to start -- BUT the PRIMARY OS boot
            // transfer routes through this SAME START -> exit_console path.
            // sys__exit_console (ev6_vms_pc264_pal.mar:4245) restores the boot
            // context (PTBR/KSP/PS from the per-CPU HWRPB slot's HWPCB, restart
            // PC = slot.halt_pc == 0x20000000), flushes ITB+DTB, and restarts
            // fetch at halt_pc.  Stubbing this to a no-op stranded the entire
            // handoff (halt at 0x20000000; PTBR never installed; ITB never
            // flushed).  JRN-VMB-004 / memory emulatr5-cserve-start-boot-handoff.
            //
            // OPTION B (this block, env-gated EMULATR_CSERVE_START_RESTART, for
            // testing): replicate sys__exit_console in C++.  OPTION A (faithful
            // default, TODO): divert to the guest PAL exit_console once its
            // runtime PC is located.
            // Mode selector (2026-07-22, JRN-VMB-004): A=guest (faithful divert to
            // the guest exit_console), B=cpp (the C++ replica below), off=no-op.
            // Back-compat: EMULATR_CSERVE_START_RESTART => cpp.
            // DEFAULT = guest (2026-07-26, JRN-SCSI-010 P1): the "off until A is
            // verified" guard was retired -- A was verified 2026-07-24 (JRN-VMB-017/
            // -020: unambiguous locator, APB executes to NOIOVEC), and the off
            // default caused the 07-25 L0 outage (bare launches silently no-op'd
            // the console->APB handoff).  EMULATR_CSERVE_START_MODE = guest | cpp
            // | off (any other non-empty value = off, explicit opt-out).
            enum { kStartOff = 0, kStartGuest = 1, kStartCpp = 2 };
            static int const s_startMode = []() noexcept -> int {
                char const* m = std::getenv("EMULATR_CSERVE_START_MODE");
                if (m != nullptr && *m != '\0') {
                    if (m[0] == 'g') return kStartGuest;
                    if (m[0] == 'c') return kStartCpp;
                    return kStartOff;
                }
                return (std::getenv("EMULATR_CSERVE_START_RESTART") != nullptr)
                     ? kStartCpp : kStartGuest;
            }();

            // OPTION A (faithful, production target): divert to the guest PAL's
            // sys__exit_console so the REAL PAL runs pal__restore_state (full CNS
            // context) + ITB_IA/DTB_IA + hw_ret, exactly as hardware does -- this
            // sets the RWE bits + PAL temps the DTBM_DOUBLE handler needs (which
            // the C++ replica B does NOT, so B's boot0 fetches but the miss-walk
            // bails to identity).
            //
            // LOCATOR (corrected 2026-07-24, JRN-VMB-017): the previous
            // ITB_IA/DTB_IA adjacent-pair scan was AMBIGUOUS.  The BUILT PAL
            // image carries EIGHT such pairs, ALL preceded by NOP padding
            // (never by the `bsr` -- the assembler pads between exit_console's
            // bsr and its flushes), and the FIRST pair in ascending order
            // (DS20 v7_3: 0xa6d0, target-4 = 0xa6cc) is a MTPR_TBIA/IMB-family
            // flush stub whose tail `hw_ret_stall(p23)` bounced the handoff
            // straight back to the console -- THE halt-0-at-0x20000000 wall.
            // Even skipping that false positive, sys__enter_console's pair
            // (0x132c0) precedes sys__exit_console's (0x134a0), so the pair
            // signature is unfixably ambiguous.  The unambiguous anchor is
            // pal__restore_state ITSELF:
            //   STAGE 1 -- pal__restore_state entry: its first instruction is
            //     `hw_ldq/p r1, PT__IMPURE(p_temp)` (ev6_vms_pal.mar:6228) =
            //     op 0x1B, Ra=r1, Rb=r21(p_temp), disp 0x88 (enc 0x6c351088
            //     on DS20 v7_3).  LOWEST match in the window = the ACTIVE PAL
            //     personality copy (the image holds a second copy higher up,
            //     e.g. 0x1d324 -- ignored).
            //   STAGE 2 -- sys__exit_console entry: the `bsr p7,
            //     pal__restore_state` (ev6_vms_pc264_pal.mar:4245-4247) = op
            //     0x34, Ra=r7, whose branch target == the STAGE-1 address.
            //     LOWEST match = the active copy (DS20 v7_3: 0x13480, enc
            //     0xd0ffebc7 -> 0xe3a0).
            // Padding-immune, personality-copy-aware, and self-verifying: the
            // located entry IS a bsr-to-restore_state by construction.  Both
            // addresses + the entry encoding are emitted for the boot canary.
            if (s_startMode == kStartGuest && c.memory != nullptr) {
                static uint64_t s_exitConsolePc = 0;
                static bool     s_scanned       = false;
                if (!s_scanned) {
                    s_scanned = true;
                    auto rd32 = [&](uint64_t a) noexcept -> uint32_t {
                        uint64_t q = 0;
                        (void)c.memory->read8(
                            static_cast<coreLib::PAType>(a & ~uint64_t{7}), q);
                        return static_cast<uint32_t>(
                            (a & 4ULL) ? (q >> 32) : (q & 0xFFFFFFFFULL));
                    };
                    uint64_t const base = c.cpu->palBase;
                    uint64_t restoreStatePc = 0;
                    // STAGE 1 -- pal__restore_state: hw_ldq/p r1,0x88(r21).
                    for (uint64_t p = base; p + 4 <= base + 0x20000ULL; p += 4) {
                        uint32_t const w = rd32(p);
                        if ((w >> 26) == 0x1Bu &&          // HW_LD
                            ((w >> 21) & 31u) == 1u &&     // Ra = r1
                            ((w >> 16) & 31u) == 21u &&    // Rb = r21 (p_temp)
                            (w & 0xFFFu) == 0x88u) {       // disp = PT__IMPURE
                            restoreStatePc = p;
                            break;
                        }
                    }
                    // STAGE 2 -- sys__exit_console: the `bsr p7` targeting it.
                    if (restoreStatePc != 0) {
                        for (uint64_t p = base; p + 4 <= base + 0x20000ULL; p += 4) {
                            uint32_t const w = rd32(p);
                            if ((w >> 26) != 0x34u ||      // BSR
                                ((w >> 21) & 31u) != 7u)   // Ra = r7 (p7)
                                continue;
                            int64_t disp = static_cast<int64_t>(w & 0x1FFFFFu);
                            if (disp & 0x100000) disp -= 0x200000;
                            int64_t const tgt =
                                static_cast<int64_t>(p) + 4 + 4 * disp;
                            if (tgt == static_cast<int64_t>(restoreStatePc)) {
                                s_exitConsolePc = p;
                                break;
                            }
                        }
                    }
                    std::fprintf(stderr,
                        "CSERVE-START-A: palBase=0x%llx restore_state=%s0x%llx "
                        "exit_console=%s0x%llx enc=0x%08x cyc=%llu\n",
                        static_cast<unsigned long long>(c.cpu->palBase),
                        restoreStatePc ? "" : "NOT-FOUND ",
                        static_cast<unsigned long long>(restoreStatePc),
                        s_exitConsolePc ? "" : "NOT-FOUND ",
                        static_cast<unsigned long long>(s_exitConsolePc),
                        s_exitConsolePc ? rd32(s_exitConsolePc) : 0u,
                        static_cast<unsigned long long>(c.cpu->cycleCount));
                    std::fflush(stderr);
                }
                if (s_exitConsolePc != 0) {
                    // MIRROR AXPBox vmspal_call_cserve EXACTLY: its very first line
                    // is `p23 = state.pc` (save the CALL_PAL return PC into the PAL
                    // linkage reg R23) BEFORE `set_pc(cfw_start)`.  sys__exit_console
                    // terminates in `hw_ret_stall (p23)` (ev6_vms_pc264_pal.mar:769),
                    // so p23 IS the resume target unless pal__restore_state overwrites
                    // it.  Option A previously set excAddr/divertTarget but NOT p23,
                    // so exit_console's hw_ret landed on stale R23 -> reset.  p23 =
                    // instruction following the CALL_PAL (== g.pc + 4; matches the
                    // linkage convention and the PAL comment "p23 pc of instruction
                    // following call_pal instruction").
                    // CORRECTED 2026-07-24 (console-garbage root cause): write the
                    // linkage into the PAL'S VIEW of R23 ONLY -- real CALL_PAL
                    // hardware PRESERVES the native R23 (it is caller state; the
                    // console callback ABI passes the PUTS length there, and the
                    // old both-banks write clobbered it -> cb_puts fwrite with a
                    // return-PC-sized length -> binary garbage on COM1).  With
                    // SDE<1> set, the divert's palModeEnter swaps the shadow bank
                    // in, so seeding intShadow[7] (== post-swap R23; swapPalShadowRegs
                    // maps R20+i <-> intShadow[4+i]) reaches the handler.  With SDE
                    // clear there is no swap: seed intReg[23] directly.
                    uint64_t const retPc = g.pc + 4u;
                    if (coreLib::iCtlSdeHigh(c.cpu->i_ctl) && !c.cpu->inPalMode())
                        c.cpu->intShadow[7] = retPc;
                    else
                        c.cpu->intReg[23]   = retPc;
                    uint64_t const tgt = s_exitConsolePc | 1ULL;   // PALmode (PC<0>)
                    c.cpu->excAddr = tgt;
                    r.divertTarget = tgt;
                    r.divert       = true;
                    // EMULATR_PCTRACE: arm the forward retire-trace at the
                    // exit_console target so we can watch where the console->VMB
                    // handoff sends control (and whether PTBR is the boot table
                    // 0x1ff82) -- see coreLib/PcTrace.h, JRN-VMB-016.  Inert
                    // unless EMULATR_PCTRACE is set.
                    coreLib::pctraceArm(s_exitConsolePc, c.cpu->ptbr,
                        c.cpu->vptb, c.cpu->intReg[22],
                        c.cpu->inPalMode() ? 1 : 0, c.cpu->cycleCount);
                    std::fprintf(stderr,
                        "CSERVE-START-A2: mirror-axpbox p23(r23)<-0x%llx (g.pc+4) "
                        "divert->cfw_start/exit_console=0x%llx cyc=%llu\n",
                        static_cast<unsigned long long>(retPc),
                        static_cast<unsigned long long>(s_exitConsolePc),
                        static_cast<unsigned long long>(c.cpu->cycleCount));
                    std::fflush(stderr);
                    return r;
                }
                // TRIPWIRE (JRN-SCSI-010 P4): a silent no-op here strands the
                // console->APB handoff at halt_pc with halt code 0 -- say so.
                std::fprintf(stderr,
                    "CSERVE-START: LOCATOR FAILED -- exit_console not found; "
                    "handoff will strand at halt_pc (halt code 0).  cyc=%llu\n",
                    static_cast<unsigned long long>(c.cpu->cycleCount));
                std::fflush(stderr);
                return r;   // scan failed: safe no-op
            }

            // ================================================================
            // OPTION B (B+) -- C++ REPLICA of the OS-restart -- COMMENTED OUT.
            // CONFIRMED DEAD END (2026-07-23, JRN-VMB-016 3.9-3.12): it
            // hand-installs OS state (PTBR/VPTB, clears p_misc) + jumps to
            // 0x20000000, so boot0 runs 4 instrs BUT the seeded PT__VPTB is then
            // ZEROED by the guest enter_console -> DTBM_DOUBLE_3 VPTB self-test
            // reads 0 -> crash1 halt 0xA.  Not a shadow-bank issue (the divert
            // PAL-swap fix did NOT change this outcome -- re-tested with
            // EMULATR_DIVERT_PALSWAP=1, still 0xA).  Fighting the guest PAL by
            // C++-replicating its effects is the wrong shape; the faithful path
            // is OPTION A (guest exit_console), now correct with the shadow swap.
            // Kept #if 0 for reference (full detail lives in JRN-VMB-016).
#if 0  // --- OPTION B (kStartCpp) : DEAD END, retained for reference only ---
            if (s_startMode == kStartCpp && c.memory != nullptr) {
                using deviceLib::hwrpb::Hwpcb;
                using deviceLib::hwrpb::loadCpuFromHwpcb;
                // Primary per-CPU slot in the single SRM-built HWRPB (PA 0x2000).
                constexpr uint64_t kHwrpbBase = 0x2000ULL;
                uint64_t slotOff = 0;
                (void)c.memory->read8(
                    static_cast<coreLib::PAType>(kHwrpbBase + 160), slotOff); // cpu_slot_offset
                uint64_t const slotPa = kHwrpbBase + slotOff;                 // primary (index 0)
                uint64_t haltPc = 0;
                (void)c.memory->read8(
                    static_cast<coreLib::PAType>(slotPa + 0xF0), haltPc);      // PerCpuSlot.halt_pc
                // Read the HWPCB (slot start) and install it exactly as SWPCTX does.
                Hwpcb ctx{};
                (void)c.memory->read8(static_cast<coreLib::PAType>(slotPa + 0x00), ctx.ksp);
                (void)c.memory->read8(static_cast<coreLib::PAType>(slotPa + 0x08), ctx.esp);
                (void)c.memory->read8(static_cast<coreLib::PAType>(slotPa + 0x10), ctx.ssp);
                (void)c.memory->read8(static_cast<coreLib::PAType>(slotPa + 0x18), ctx.usp);
                (void)c.memory->read8(static_cast<coreLib::PAType>(slotPa + 0x20), ctx.ptbr);
                (void)c.memory->read8(static_cast<coreLib::PAType>(slotPa + 0x28), ctx.asn);
                (void)c.memory->read8(static_cast<coreLib::PAType>(slotPa + 0x30), ctx.asten_sr);
                (void)c.memory->read8(static_cast<coreLib::PAType>(slotPa + 0x38), ctx.fen);
                loadCpuFromHwpcb(*c.cpu, ctx);   // installs cpu.ptbr (<63> stripped), ksp, asn, fen
                c.cpu->pcbb = slotPa;            // PCBB now the boot slot
                // VPTB companion to PTBR: the single-miss VPTE fetch reads VPTB
                // from VA_CTL/I_CTL (not cpu.ptbr).  boot.c's VPTB write does not
                // propagate here, so va_ctl<63:43>=0 strands the self-map and the
                // walk resolves the WRONG PFN (observed: PFN 0x10000/PA 0x20000000
                // -> HALT).  Install hwrpb.vptb_va and merge into VA_CTL/I_CTL
                // exactly as the MTPR_VPTB faithful fix (2026-07-19) does.
                uint64_t vptbVa = 0;
                (void)c.memory->read8(
                    static_cast<coreLib::PAType>(kHwrpbBase + 120), vptbVa); // hwrpb.vptb_va
                c.cpu->vptb  = vptbVa;
                c.cpu->va_ctl = (c.cpu->va_ctl & ~coreLib::kVaCtlVptbMask)
                              | (vptbVa & coreLib::kVaCtlVptbMask);
                c.cpu->i_ctl  = (c.cpu->i_ctl  & ~coreLib::kICtlVptbLowMask)
                              | (vptbVa & coreLib::kICtlVptbLowMask);
                c.cpu->itbMgr.invalidateAll();   // sys__exit_console: EV6__ITB_IA
                c.cpu->dtbMgr.invalidateAll();   // sys__exit_console: EV6__DTB_IA
                // sys__exit_console tail: "Turn off 1-to-1 mapping" --
                //   lda p4,1(r31); sll p4,#P_MISC__PHYS__S,p4; bic p_misc,p4,p_misc
                // (ev6_vms_pc264_pal.mar:4274).  p_misc is PAL shadow R22, with
                // <63> = the PHYS / 1-1-mapping bit (P_MISC__PHYS__S eq 63,
                // ev6_alpha_defs.mar: p_misc=22).  The console + VMB run in
                // physical mode (R22<63>=1); while it is set, EVERY EV6 TB-miss
                // handler (DTBM_SINGLE 0x300, DTBM_DOUBLE_3 0x100, ITB_MISS)
                // branches `blt p_misc, ...1to1` to the identity path and returns
                // pfn = VA>>13 instead of walking the OS page table.  THAT is why
                // VA 0x20000000 resolved to pfn 0x10000 (identity) not 0x2DE.
                // Clearing <63> here restores virtual mode so the guest miss-walk
                // takes the real 3-level flow.  We are in PAL mode, so intReg[22]
                // is the shadow copy the miss handler reads; the value persists
                // across the leave/enter shadow swap around the divert below.
                // p_misc lives in the PAL-shadow bank across traps; at this
                // CSERVE CALL_PAL the live intReg[22] is transient cserve scratch,
                // so clear PHYS<63> in BOTH R22 copies (active + shadow bank) to
                // guarantee the value the OS-trap miss handler reads has 1-1 off.
                // intShadow[6] == R22's other bank (swapPalShadowRegs: R20+i <->
                // intShadow[4+i], so R22 -> intShadow[6]).
                uint64_t const r22Before  = c.cpu->intReg[22];
                uint64_t const r22ShBefore = c.cpu->intShadow[6];
                c.cpu->intReg[22]   &= ~(uint64_t{1} << 63);   // bic p_misc, #1<<63
                c.cpu->intShadow[6] &= ~(uint64_t{1} << 63);   // other-bank R22
                // ---- B+ : seed the PAL-temp MEMORY copies the guest miss-walk
                //           reads (PT__VPTB @ p_temp+0x0, PT__PTBR @ p_temp+0x8).
                // We installed the IPRs (cpu.vptb / cpu.ptbr) above, but the EV6
                // DTBM_DOUBLE_3 handler (ev6_vms_pal.mar:960) does NOT read the
                // IPRs -- it reads PAL-temp memory:
                //   hw_ldq/p r25, PT__PTBR(p_temp)   ; phys page-table addr
                //   .if ne check_ebox_iprs           ; DS20 build has this ON
                //     hw_ldq/p r25, PT__VPTB(p_temp)  ; vptb
                //     srl p4,#33,r26 ; sll r26,#33 ; xor r25,r26 ; bne crash1(0xA)
                // With p_misc<63> now cleared we DON'T take the 1to1 branch, so the
                // self-test runs and crashed halt-code 0xA because PT__VPTB(p_temp)
                // did not match VPTB.  Reset_init writes PT__VPTB = 2<<32 and
                // PT__PTBR = PFN<<13 (ev6_vms_pc264_pal.mar:4935/4944; swpctx
                // ev6_vms_callpal.mar:428).  Match those formats here.
                // p_temp = PAL-bank r21 (ev6_alpha_defs.mar:38) -> intShadow[5].
                uint64_t const pTempBP = c.cpu->intShadow[5] & ~uint64_t{7};
                bool pTempSane = (pTempBP >= 0x1000ull && pTempBP < 0x8000ull);
                if (pTempSane) {
                    uint64_t const ptVptb = vptbVa;                 // PT__VPTB = vptb
                    uint64_t const ptPtbr = c.cpu->ptbr << 13;      // PT__PTBR = PFN<<13
                    c.memory->write8(static_cast<coreLib::PAType>(pTempBP + 0x0), ptVptb);
                    c.memory->write8(static_cast<coreLib::PAType>(pTempBP + 0x8), ptPtbr);
                    std::fprintf(stderr,
                        "CSERVE-START-BPLUS: p_temp=0x%llx  PT__VPTB<-0x%llx  "
                        "PT__PTBR<-0x%llx (PFN 0x%llx<<13)\n",
                        (unsigned long long)pTempBP,
                        (unsigned long long)ptVptb,
                        (unsigned long long)ptPtbr,
                        (unsigned long long)c.cpu->ptbr);
                } else {
                    std::fprintf(stderr,
                        "CSERVE-START-BPLUS: SKIP -- p_temp candidate 0x%llx "
                        "(intShadow[5]) not in [0x1000,0x8000); PT__VPTB/PTBR "
                        "left as-is\n", (unsigned long long)pTempBP);
                }
                std::fflush(stderr);
                // ---- DISASM DUMP (2026-07-22 #1): raw instruction words at the
                // PT__VPTB clobber site pc=0x1333c and its caller ra=0x62f6c.
                // PA-WATCH proved the guest zeroes PT__VPTB from pc=0x1333c
                // (ev6_vms_pc264_pal.mar:4173, console-entry/1-1 mode switch),
                // called from 0x62f6c.  The PAL is identity-mapped in low phys
                // memory, so read8(pa) gives the instruction words; decode offline
                // to identify 0x62f6c (crash-to-console tail vs mis-routed OS-entry).
                {
                    static bool s_dumpedDisasm = false;
                    if (!s_dumpedDisasm && c.memory != nullptr) {
                        s_dumpedDisasm = true;
                        // boot0 lives at VA 0x20000000 -> PA leafPa (0x5bc000 per
                        // the PTWALK).  Dump its first ~16 instructions to see what
                        // the 4 instrs 0x20000000..0x2000000c do and what 0x20000010
                        // (the deterministic exit PC) faults on.
                        struct { const char* name; uint64_t base; } sites[] = {
                            { "clobber@0x1333c", 0x13320ull },
                            { "caller@0x62f6c",  0x62f50ull },
                            { "boot0@va20000000(pa5bc000)", 0x5bc000ull },
                        };
                        for (auto const& s : sites) {
                            for (uint64_t off = 0; off < 0x50; off += 4) {
                                uint64_t const a = s.base + off;
                                uint64_t w = 0;
                                (void)c.memory->read8(static_cast<coreLib::PAType>(a & ~7ull), w);
                                uint32_t const insn =
                                    static_cast<uint32_t>((a & 4) ? (w >> 32) : (w & 0xFFFFFFFFull));
                                std::fprintf(stderr,
                                    "CSERVE-DISASM %s pa=0x%08llx insn=0x%08x\n",
                                    s.name, (unsigned long long)a, insn);
                            }
                        }
                        std::fflush(stderr);
                    }
                }
                std::fprintf(stderr,
                    "CSERVE-START-RESTART: haltPc=0x%016llx ptbr(PFN)=0x%016llx "
                    "vptb=0x%016llx va_ctl=0x%016llx i_ctl=0x%016llx iCtlVptb=0x%016llx "
                    "slotPa=0x%016llx p_misc(R22)=0x%016llx->0x%016llx "
                    "R22sh=0x%016llx->0x%016llx cyc=%llu "
                    "-- restarting OS at halt_pc\n",
                    static_cast<unsigned long long>(haltPc),
                    static_cast<unsigned long long>(c.cpu->ptbr),
                    static_cast<unsigned long long>(c.cpu->vptb),
                    static_cast<unsigned long long>(c.cpu->va_ctl),
                    static_cast<unsigned long long>(c.cpu->i_ctl),
                    static_cast<unsigned long long>(coreLib::iCtlVptb(c.cpu->i_ctl)),
                    static_cast<unsigned long long>(slotPa),
                    static_cast<unsigned long long>(r22Before),
                    static_cast<unsigned long long>(c.cpu->intReg[22]),
                    static_cast<unsigned long long>(r22ShBefore),
                    static_cast<unsigned long long>(c.cpu->intShadow[6]),
                    static_cast<unsigned long long>(c.cpu->cycleCount));
                std::fflush(stderr);
                // One-shot physical 3-level page-table walk for VA 0x20000000
                // (PTBR=l1pt; l1[VA>>33]->l2[VA>>23]->l3[VA>>13]; 8KB pages;
                // PTE.PFN = bits[63:32]).  Shows what VMB physically built --
                // expect leaf PFN = base_pfn (0x2DE / PA 0x5bc000).
                {
                    uint64_t const ptbrPfn = c.cpu->ptbr;
                    auto walk = [&](char const* tag, uint64_t va) {
                        uint64_t const l1Pa = (ptbrPfn << 13);
                        uint64_t l1pte = 0, l2pte = 0, l3pte = 0;
                        (void)c.memory->read8(static_cast<coreLib::PAType>(
                            l1Pa + (((va >> 33) & 0x3FFULL) * 8)), l1pte);
                        uint64_t const l2Pa = ((l1pte >> 32) << 13);
                        (void)c.memory->read8(static_cast<coreLib::PAType>(
                            l2Pa + (((va >> 23) & 0x3FFULL) * 8)), l2pte);
                        uint64_t const l3Pa = ((l2pte >> 32) << 13);
                        (void)c.memory->read8(static_cast<coreLib::PAType>(
                            l3Pa + (((va >> 13) & 0x3FFULL) * 8)), l3pte);
                        std::fprintf(stderr,
                            "PTWALK %s va=0x%llx l1[%llu]pte=0x%llx l2Pa=0x%llx "
                            "l2[%llu]pte=0x%llx l3Pa=0x%llx l3[%llu]pte=0x%llx "
                            "leafPfn=0x%llx leafPa=0x%llx\n",
                            tag, static_cast<unsigned long long>(va),
                            static_cast<unsigned long long>((va >> 33) & 0x3FF),
                            static_cast<unsigned long long>(l1pte),
                            static_cast<unsigned long long>(l2Pa),
                            static_cast<unsigned long long>((va >> 23) & 0x3FF),
                            static_cast<unsigned long long>(l2pte),
                            static_cast<unsigned long long>(l3Pa),
                            static_cast<unsigned long long>((va >> 13) & 0x3FF),
                            static_cast<unsigned long long>(l3pte),
                            static_cast<unsigned long long>(l3pte >> 32),
                            static_cast<unsigned long long>((l3pte >> 32) << 13));
                        std::fflush(stderr);
                    };
                    walk("TARGET", 0x20000000ULL);      // the OS entry (boot0 code)
                    walk("VPTE  ", 0x200080000ULL);     // the self-map VPTE addr for 0x20000000
                    // boot0's FIRST data access is ldq r4,0x50(r0) with r0=1<<28
                    // => VA 0x10000050 (page 0x10000000).  The deterministic exit
                    // PC 0x20000010 faults here.  Is boot0's data page even mapped?
                    walk("BOOT0DATA", 0x10000000ULL);   // boot0 data VA (ldq target page)
                    walk("BOOT0VPTE", 0x200040000ULL);  // self-map VPTE addr for 0x10000000
                }
                // A+ RECON (2026-07-22): the faithful handoff is to prime the CNS
                // save frame with the OS restart context, then divert to the real
                // sys__exit_console -> pal__restore_state (which reads this frame).
                // restore_state reads the frame at impure_base = mem[p_temp+PT__IMPURE];
                // p_temp = R21 (ev6_alpha_defs.mar:38), PT__IMPURE = 0x88
                // (ev6_pal_temps.mar:47).  CNS field offsets (ev6_pal_impure.mar):
                // FLAG 0x0, HALT 0x8, R0 0x10.., PTBR 0x238, KSP 0x250, VPTB 0x268,
                // P_MISC 0x308, VA_CTL 0x328, EXC_ADDR 0x330 (= restart PC in p23),
                // I_CTL 0x360, M_CTL 0x390.  Dump the live frame to confirm the
                // base is valid and whether it holds console or OS context BEFORE
                // any write lands here.
                {
                    // At this CALL_PAL handler the PAL context (p_temp, p_misc)
                    // lives in the SHADOW bank, not the active intReg[] (confirmed:
                    // p_misc was in intShadow[6], intReg[22] was cserve scratch).
                    // p_temp = R21 -> shadow copy intShadow[5]; try both to be sure.
                    uint64_t const pTempRaw    = c.cpu->intShadow[5];  // p_temp (PAL bank)
                    uint64_t const pTempActive = c.cpu->intReg[21];
                    // p_temp must be 8-byte aligned; hw_ldq/p masks low 3 bits.
                    // 0xf01 (odd) -> 0xf00.  Read the region at the ALIGNED base.
                    uint64_t const pTemp       = pTempRaw & ~uint64_t{7};
                    auto rd = [&](uint64_t pa) noexcept -> uint64_t {
                        uint64_t v = 0;
                        (void)c.memory->read8(
                            static_cast<coreLib::PAType>(pa & ~uint64_t{7}), v);
                        return v;
                    };
                    uint64_t const impure = rd(pTemp + 0x88ULL);       // PT__IMPURE
                    // Hexdump the PALtemp region head to see whether the impure
                    // pointer + WHAMI are linked (the ROOT question for the restart).
                    std::fprintf(stderr,
                        "A+RECON PALTEMP@0x%llx:", static_cast<unsigned long long>(pTemp));
                    for (uint64_t o = 0; o <= 0xA0ULL; o += 0x8ULL) {
                        std::fprintf(stderr, " [%02llx]=%016llx",
                            static_cast<unsigned long long>(o),
                            static_cast<unsigned long long>(rd(pTemp + o)));
                    }
                    std::fprintf(stderr, "\n");
                    std::fprintf(stderr,
                        "A+RECON pTempSh(R21sh)=0x%llx pTempAligned=0x%llx "
                        "pTempActive(R21)=0x%llx "
                        "impureBase=0x%llx (slotPa=0x%llx) "
                        "CNS[FLAG=0x%llx HALT=0x%llx EXC_ADDR=0x%llx PTBR=0x%llx "
                        "KSP=0x%llx VPTB=0x%llx P_MISC=0x%llx VA_CTL=0x%llx "
                        "I_CTL=0x%llx M_CTL=0x%llx]\n",
                        static_cast<unsigned long long>(pTempRaw),
                        static_cast<unsigned long long>(pTemp),
                        static_cast<unsigned long long>(pTempActive),
                        static_cast<unsigned long long>(impure),
                        static_cast<unsigned long long>(slotPa),
                        static_cast<unsigned long long>(rd(impure + 0x0ULL)),
                        static_cast<unsigned long long>(rd(impure + 0x8ULL)),
                        static_cast<unsigned long long>(rd(impure + 0x330ULL)),
                        static_cast<unsigned long long>(rd(impure + 0x238ULL)),
                        static_cast<unsigned long long>(rd(impure + 0x250ULL)),
                        static_cast<unsigned long long>(rd(impure + 0x268ULL)),
                        static_cast<unsigned long long>(rd(impure + 0x308ULL)),
                        static_cast<unsigned long long>(rd(impure + 0x328ULL)),
                        static_cast<unsigned long long>(rd(impure + 0x360ULL)),
                        static_cast<unsigned long long>(rd(impure + 0x390ULL)));
                    std::fflush(stderr);
                }
                c.cpu->excAddr = haltPc;         // native restart PC (bit 0 = 0)
                r.divertTarget = haltPc;
                r.divert       = true;
                return r;
            }
#endif // --- end OPTION B (kStartCpp) DEAD END ---
            // Option B (cpp) is compiled out; if selected it now falls through to
            // this faithful no-op (same as kStartOff).  Use kStartGuest (Option A).
            // TRIPWIRE (JRN-SCSI-010 P4): with the mode off/cpp this START is a
            // no-op, which on the PRIMARY-boot path strands the console->APB
            // handoff ("halted CPU 0 / halt code = 0 / PC = 20000000", the 07-25
            // L0 outage).  Loud once per distinct mode so a disabled handoff can
            // never again masquerade as a guest-side failure.
            {
                static bool s_warned = false;
                if (!s_warned) {
                    s_warned = true;
                    std::fprintf(stderr,
                        "CSERVE-START: MODE %s -- START (0x42) is a no-op; a boot "
                        "handoff will strand at halt_pc (halt code 0).  Set "
                        "EMULATR_CSERVE_START_MODE=guest (default) to enable.  "
                        "cyc=%llu\n",
                        s_startMode == kStartCpp ? "cpp (compiled out)" : "OFF",
                        static_cast<unsigned long long>(c.cpu->cycleCount));
                    std::fflush(stderr);
                }
            }
            return r;                // default: SMP-secondary start -- no-op on UP
        }

        // case 0x43 CSERVE$CALLBACK -- REMOVED as an explicit no-op
        // (2026-07-24, JRN-VMB-017 P2 follow-on).  The no-op was the next
        // 0x65-class faithfulness violation: after VMB handed off, the OS
        // called the console callback dispatcher (caller pc=0x101ab054, the
        // dispatch routine boot.c maps into the OS space), which issued
        // CSERVE 0x43 in a tight retry loop (~86 cyc period, identical
        // registers) because nothing was done.  0x43 now falls through to
        // the default: routing block below -- with EMULATR_CSERVE_ROUTE=1
        // it diverts to the guest sys__cserve (p23 + DIVERT_PALSWAP
        // invariant), so the real cfw_callback runs: set callback flag,
        // HALT into the console (hlt$c_callback=33); the console LOOP path
        // (apisrm kernel.c:2683 cbip) services the request and returns to
        // the OS via exit_console(START) -- the faithful Option A path.

        case 0x66: {   // CSERVE 0x66 -- return masked PAL_BASE
            // 2026-07-11: REINSTATED with CORRECTED semantics; machine-code
            // confirmed from the running image's OWN PAL (decompressed_es40_v7_3,
            // load base 0x8000).  HISTORY: a 2026-07-08 change REMOVED an earlier
            // "get_time" case here because it wrote R0 with a BCD TOY value that
            // shifted the SCB base (base = R0 + 0x28000 -> 0x1038000, HW_REI to
            // PC 0 halt; journals/20260708_es40_scb_base_mismatch_root.md), and
            // ev6_vms_pc264_pal.mar (which stops at code 0x65) made 0x66 look
            // undefined.  That .mar was the WRONG variant: the compiled es40_v7_3
            // PAL DOES define 0x66.  Ground-truth disassembly --
            //   sys__cserve: cmpeq r16,#0x66 @guest 0x133f4 -> handler @0x139e8:
            //     HW_MFPR R0, 0x1010    ; read IPR
            //     SRL     R0, #0x15, R0 ; >> 21
            //     SLL     R0, #0x15, R0 ; << 21   (clear low 21 bits = 2MB-align)
            //     HW_RET
            // Per 21264ev67 HRM Figure 6-4 the HW_MFPR field is INDEX[15:8] +
            // SCBD_MASK[7:0], so index16 0x1010 -> IPR index 0x10 = PAL_BASE
            // (calibrated against the trace's EXC_ADDR read at PAL 0x8300 =
            // index 0x06).  So 0x66 returns the 2MB-aligned PAL base -- a
            // base-relative-address primitive, NOT a time/TOY value.
            //
            // BUG THIS FIXES: the ES40 powerup memtest helper at guest 0x8c2d0
            // computes `R0 = arg - cserve(0x66)`.  With 0x66 no-op'd, the stale
            // walk pointer (0xc03ea0a1) survived into the SUBQ, yielding the wild
            // VA 0xffffffff7f827f5f, dereferenced at guest 0x1b7dd4 -> ACV.
            // Returning the real palBase makes `arg - palBase_aligned` a valid
            // address and clears the ACV.  DETERMINISTIC (palBase is fixed once
            // seeded) -- same source as HW_MFPR HW_PAL_BASE (HW_IPR.h) /
            // CpuState.h palBase.  Does NOT reprise the 2026-07-08 regression:
            // the single 0x66 contract is palBase (not a TOY), so the SCB path's
            // base = R0 + 0x28000 = palBase_aligned + 0x28000 is also correct.
            // Ref: journals/20260711_es40_memtest_acv_cserve_0x66_CONFIRMED_machinecode.md
            r.regWriteIdx   = 0;     // R0
            r.regWriteIsFp  = false;
            r.regWriteValue = (c.cpu->palBase >> 21) << 21;   // PAL_BASE, 2MB-aligned
            return r;
        }

        default: {
            // FAITHFULNESS AUDIT (2026-07-23).  A CSERVE func reaching here has
            // NO C++ implementation and NO guest-PAL routing yet, so we currently
            // no-op it (return, R0 untouched).  That no-op is a DIVERGENCE from
            // real execution, NOT a faithful "tolerated no-op": the guest
            // sys__cserve (ev6_vms_pc264_pal.mar:3852) would dispatch this func to
            // a cfw_* handler with real side-effects.  The prior claim that these
            // are silicon-tolerated no-ops was DISPROVEN by 0x65 MP_WORK_REQUEST --
            // the console loops on it and re-inits because the guest
            // cfw_mp_work_request never saves R18 -> CNS__WORK_REQUEST.  Per the
            // CSERVE=run-the-guest-PAL architecture (JRN-VMB-016 PART 3), each
            // such func must be CLOSED by routing to the guest PAL.  This block's
            // job (goal = a referenceable ORACLE, not boot-for-boot's-sake) is to
            // CAPTURE THE MISSING CONTRACT so it can be documented + closed: the
            // func + its INPUTS (R16=func, R17/R18 args, R0), the caller PC, and
            // the call pattern (one-shot vs the tight polling loops), so it
            // cross-references to the guest cfw_* handler in the apisrm source.
            // Codes still landing here: 0x0A, 0x0B-0x0D, 0x32-0x37, 0x45
            // JUMP_TO_ARC, 0x65 MP_WORK_REQUEST.  (0x08/0x09/0x3F/0x40-0x44 have
            // explicit cases.)  EMULATR_CSERVE_AUDIT=1 enables the capture (one
            // record per distinct func code, then periodic, so the polling loops
            // don't flood the log).
            static bool const s_cserveAudit =
                (std::getenv("EMULATR_CSERVE_AUDIT") != nullptr);
            if (s_cserveAudit) {
                static uint64_t s_seen[256] = {};
                uint64_t const fc = funcCode & 0xFFu;
                uint64_t const n  = ++s_seen[fc];
                if (n <= 3u || (n & 0xFFFu) == 0u) {   // first 3 + every 4096th
                    std::fprintf(stderr,
                        "CSERVE-CONTRACT-MISSING: func=0x%llx %s call#%llu  "
                        "R16=0x%016llx R17=0x%016llx R18=0x%016llx R0=0x%016llx  "
                        "callerPc=0x%llx excAddr=0x%llx palMode=%d mode=%u cyc=%llu\n",
                        static_cast<unsigned long long>(funcCode),
                        cserveFuncName(funcCode),
                        static_cast<unsigned long long>(n),
                        static_cast<unsigned long long>(c.cpu->intReg[16]),
                        static_cast<unsigned long long>(c.cpu->intReg[17]),
                        static_cast<unsigned long long>(c.cpu->intReg[18]),
                        static_cast<unsigned long long>(c.cpu->intReg[0]),
                        static_cast<unsigned long long>(g.pc),
                        static_cast<unsigned long long>(c.cpu->excAddr),
                        static_cast<int>(c.cpu->inPalMode()),
                        static_cast<unsigned>(c.cpu->mode),
                        static_cast<unsigned long long>(c.cpu->cycleCount));
                    std::fflush(stderr);
                }
            }
            // CLOSE (discipline step 3): route to the guest sys__cserve dispatcher
            // so the REAL firmware handles this func -- 0x65 -> cfw_mp_work_request
            // saves R18->CNS__WORK_REQUEST (R18=MP$RESTART=1 = the OS-restart post
            // from kernel.c:1352); genuinely-unknown codes hit the guest's own
            // trailing hw_ret(p23) no-op, same as here.  Mirror-AXPBox invariant:
            // p23(R23)=g.pc+4 in BOTH banks + divert, R16 intact, NO C++ effect
            // replication -- the guest PAL does the work.  Gated EMULATR_CSERVE_ROUTE
            // for A/B vs the capture-only no-op above.  sys__cserve located by
            // signature scan = a run of >=8 consecutive `cmpeq r16,#lit,r0 ; bne
            // r0,disp` pairs (the compiled full dispatch table; reference_platform=0
            // + pc264_system=1 both confirmed, so that table IS compiled).  NOTE
            // the boot/MP path is NOT ISP-gated -- ISP only skips real-HW device/
            // timer/debug probes -- so this is faithful in both platform modes.
            // DEFAULT = ON (2026-07-26, JRN-SCSI-010 P1): routing to the guest
            // sys__cserve is the faithful behavior and has been the script-
            // supplied default since 2026-07-23; the engine default now matches
            // so bare launches boot like scripted ones.  Disable explicitly with
            // EMULATR_CSERVE_ROUTE=0 (or empty) for the A/B-vs-no-op comparison.
            static bool const s_cserveRoute = []() noexcept {
                char const* v = std::getenv("EMULATR_CSERVE_ROUTE");
                return (v == nullptr) || !(v[0] == '\0' || v[0] == '0');
            }();
            if (s_cserveRoute && c.memory != nullptr) {
                static uint64_t s_sysCservePc   = 0;
                static bool     s_cserveScanned = false;
                if (!s_cserveScanned) {
                    s_cserveScanned = true;
                    auto rd32 = [&](uint64_t a) noexcept -> uint32_t {
                        uint64_t q = 0;
                        (void)c.memory->read8(
                            static_cast<coreLib::PAType>(a & ~uint64_t{7}), q);
                        return static_cast<uint32_t>(
                            (a & 4ULL) ? (q >> 32) : (q & 0xFFFFFFFFULL));
                    };
                    // cmpeq r16,#lit,r0 : (insn & 0xFFE01FFF) == 0x420015A0
                    //   (op 0x10, Ra=16, lit bit, func 0x2D CMPEQ, Rc=0; mask lit[20:13])
                    // bne   r0,disp     : opcode 0x39, Ra=0
                    auto isCmpeqR16R0 = [](uint32_t i) noexcept {
                        return (i & 0xFFE01FFFu) == 0x420015A0u; };
                    auto isBneR0 = [](uint32_t i) noexcept {
                        return (i >> 26) == 0x3Du && ((i >> 21) & 0x1Fu) == 0u; };  // BNE=0x3D (0x39 is BEQ)
                    // Require the run to CONTAIN cmpeq r16,#0x65,r0 (the
                    // cserve$mp_work_request case) so we can only match the REAL
                    // CSERVE dispatch, never some other r16-dispatch table.  The
                    // cmpeq literal is bits[20:13].
                    uint64_t const base = c.cpu->palBase;
                    uint64_t const hi   = base + 0x20000ULL;
                    for (uint64_t p = base; p + 8 <= hi; p += 4) {
                        uint64_t q = p; int run = 0; bool has65 = false;
                        while (q + 8 <= hi
                               && isCmpeqR16R0(rd32(q)) && isBneR0(rd32(q + 4))) {
                            if (((rd32(q) >> 13) & 0xFFu) == 0x65u) has65 = true;
                            ++run; q += 8;
                        }
                        if (run >= 8 && has65) { s_sysCservePc = p; break; }
                    }
                    std::fprintf(stderr,
                        "CSERVE-ROUTE: sys__cserve=%s0x%llx palBase=0x%llx\n",
                        s_sysCservePc ? "" : "NOT-FOUND ",
                        static_cast<unsigned long long>(s_sysCservePc),
                        static_cast<unsigned long long>(base));
                    // Dump the found table's first 16 words + cmpeq literals so we
                    // can confirm it's the cserve dispatch (literals = cserve func
                    // codes incl 0x65) and not a look-alike.
                    if (s_sysCservePc != 0) {
                        for (uint64_t o = 0; o < 0x40; o += 4) {
                            uint32_t const w = rd32(s_sysCservePc + o);
                            if (isCmpeqR16R0(w)) {
                                std::fprintf(stderr,
                                    "CSERVE-ROUTE-DISASM pa=0x%llx insn=0x%08x  cmpeq r16,#0x%02x,r0\n",
                                    static_cast<unsigned long long>(s_sysCservePc + o),
                                    w, (w >> 13) & 0xFFu);
                            } else {
                                std::fprintf(stderr,
                                    "CSERVE-ROUTE-DISASM pa=0x%llx insn=0x%08x\n",
                                    static_cast<unsigned long long>(s_sysCservePc + o), w);
                            }
                        }
                    }
                    std::fflush(stderr);
                }
                if (s_sysCservePc != 0) {
                    // p23 = CALL_PAL return PC -- written to the PAL'S VIEW of R23
                    // ONLY (corrected 2026-07-24).  The old both-banks write also
                    // clobbered the NATIVE R23, which is caller state: the console
                    // callback ABI packs the PUTS string LENGTH in R23 (cb_puts
                    // reads cns$gpr[2*23]), so the clobber made the console fwrite
                    // a return-PC-sized length -> binary garbage on COM1.  With
                    // SDE<1> set the divert's palModeEnter swaps the shadow bank in
                    // (intShadow[7] == post-swap R23); with SDE clear, no swap.
                    uint64_t const retPc = g.pc + 4u;
                    if (coreLib::iCtlSdeHigh(c.cpu->i_ctl) && !c.cpu->inPalMode())
                        c.cpu->intShadow[7] = retPc;
                    else
                        c.cpu->intReg[23]   = retPc;
                    uint64_t const tgt = s_sysCservePc | 1ULL;   // PALmode (PC<0>=1)
                    c.cpu->excAddr = tgt;
                    r.divertTarget = tgt;
                    r.divert       = true;
                    std::fprintf(stderr,
                        "CSERVE-ROUTE: func=0x%llx %s -> guest sys__cserve=0x%llx "
                        "R16=0x%llx R17=0x%llx R18=0x%llx p23<-0x%llx cyc=%llu\n",
                        static_cast<unsigned long long>(funcCode),
                        cserveFuncName(funcCode),
                        static_cast<unsigned long long>(s_sysCservePc),
                        static_cast<unsigned long long>(c.cpu->intReg[16]),
                        static_cast<unsigned long long>(c.cpu->intReg[17]),
                        static_cast<unsigned long long>(c.cpu->intReg[18]),
                        static_cast<unsigned long long>(retPc),
                        static_cast<unsigned long long>(c.cpu->cycleCount));
                    // ONE-SHOT primary-detection probe (2026-07-23): the 0x65 loop
                    // is all R17=0 (primary posting MP$RESTART to ITSELF); the guest
                    // cfw_mp_work_request's "are we restarting the primary?" check
                    // (cmpeq WHAMI,pal$primary ; cmpeq r17,pal$primary) must be
                    // FAILING for it not to skip.  Dump the values it reads:
                    //   PT__WHAMI = mem[p_temp+0x98]; pal$primary = mem[get_base+0x200]
                    //   (get_base = 0 OR palBase-pal$pal_base -> dump both candidates).
                    static bool s_primDumped = false;
                    if (!s_primDumped && c.memory != nullptr) {
                        s_primDumped = true;
                        uint64_t const pt = c.cpu->intShadow[5] & ~uint64_t{7};
                        uint64_t whami = 0, pp0 = 0, ppPB = 0;
                        (void)c.memory->read8(static_cast<coreLib::PAType>(pt + 0x98), whami);
                        (void)c.memory->read8(static_cast<coreLib::PAType>(0x200), pp0);
                        (void)c.memory->read8(static_cast<coreLib::PAType>(c.cpu->palBase + 0x200), ppPB);
                        std::fprintf(stderr,
                            "CSERVE-ROUTE-PRIM: p_temp=0x%llx PT__WHAMI=0x%llx "
                            "pal$primary[@0x200]=0x%llx pal$primary[@palBase+0x200]=0x%llx "
                            "cpuSlot=%u  (want WHAMI==pal$primary==0 so r17=0 skips)\n",
                            static_cast<unsigned long long>(pt),
                            static_cast<unsigned long long>(whami),
                            static_cast<unsigned long long>(pp0),
                            static_cast<unsigned long long>(ppPB),
                            static_cast<unsigned>(c.cpu->cpuSlot));
                        // MODE STATE at the routed handler entry.  The real fault is
                        // fault=14 (DtbMissDouble) at the DTB-miss handler (pc~0x8321)
                        // cascading to PC=0 -- i.e. the handler's loads are being
                        // VIRTUALLY translated + miss-walked during POWERUP, when 1-1
                        // physical mode (p_misc<63>) should identity-map.  Dump the mode:
                        // p_misc = R22 (both banks: active intReg[22] + PAL-shadow
                        // intShadow[6]); <63> set = 1-1 physical.  vptb/va_ctl show if
                        // the self-map is even set yet (VPTB=0 during powerup => a walk
                        // dereferences low addrs -> the cascade).
                        std::fprintf(stderr,
                            "CSERVE-ROUTE-MODE: p_misc(R22)=0x%016llx R22shadow=0x%016llx "
                            "phys1to1=%d  vptb=0x%llx va_ctl=0x%llx i_ctl=0x%llx palMode=%d\n",
                            static_cast<unsigned long long>(c.cpu->intReg[22]),
                            static_cast<unsigned long long>(c.cpu->intShadow[6]),
                            (int)(((c.cpu->intReg[22] | c.cpu->intShadow[6]) >> 63) & 1u),
                            static_cast<unsigned long long>(c.cpu->vptb),
                            static_cast<unsigned long long>(c.cpu->va_ctl),
                            static_cast<unsigned long long>(c.cpu->i_ctl),
                            (int)c.cpu->inPalMode());
                    }
                    std::fflush(stderr);
                    return r;
                }
                // scan failed: fall through to the safe no-op below.
            }

            return r;                // R0 untouched, no fault (no-op when routing
                                     // is OFF or the scan failed)
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

    // FAITHFUL FIX (2026-07-19, JRN-VMB): the real VMS PAL MTPR_VPTB
    // (EV6_VMS_CALLPAL.MAR :1524) does NOT merely stash VPTB -- it merges VPTB
    // into VA_CTL[VPTB] (bits 63:30) and I_CTL[VPTB] (bits 47:30, sign-extended)
    // via EV6_MTPR, which is what actually feeds the self-map PTE address.
    // EmulatR's D-side VA_FORM (this file :1511) reads cpu.va_ctl and the I-side
    // IVA_FORM (:1484) reads cpu.i_ctl -- NEITHER reads cpu.vptb -- so a VPTB
    // written only to cpu.vptb is STRANDED: computeVaForm(va_ctl=0) locates the
    // PTE at VPTB=0, the VPTE HW_LD double-misses, and the DTB-miss handler
    // spins forever (JRN-VMB DTBM-DBL storm, DS20 cold boot cyc ~182M).
    // Propagate VPTB into both control registers' VPTB fields, preserving each
    // register's control bits (VA_48 / VA_FORM_32, etc.), exactly as the PAL's
    // `bis p7,r16 ; EV6_MTPR VA_CTL` / `... I_CTL` sequence does.
    uint64_t const newVptb = c.cpu->intReg[16];
    c.cpu->va_ctl = (c.cpu->va_ctl & ~coreLib::kVaCtlVptbMask)
                  | (newVptb & coreLib::kVaCtlVptbMask);
    c.cpu->i_ctl  = (c.cpu->i_ctl & ~coreLib::kICtlVptbLowMask)
                  | (newVptb & coreLib::kICtlVptbLowMask);

    // COMPLETING FIX (2026-07-26, JRN-SCSI-026): the 07-19 fix above added the
    // two IPR merges but stopped ONE STORE SHORT of the .mar routine.  The real
    // MTPR_VPTB (EV6_VMS_CALLPAL.MAR :1524-1534) has THREE side effects:
    //     hw_stq/p r16, PT__VPTB(p_temp)   ; <-- the memory copy, was MISSING
    //     bis p7,r16 -> EV6_MTPR VA_CTL
    //     merge -> hw_mtpr <EV6__I_CTL ! ^x20>
    // PT__VPTB = ^x0 off p_temp (EV6_PAL_TEMPS.MAR :33).  The guest's OWN
    // miss handlers read that CELL, not the IPRs: DTBM_DOUBLE_3's rev-1.60
    // self-check (EV6_VMS_PAL.MAR ~1115) compares the IPR-formatted PTE VA
    // <63:33> against PT__VPTB and halts 0x0A on mismatch.  Updating only the
    // IPRs therefore DESYNCS the two the instant the OS arms its own VPTB
    // (OpenVMS: 0xFFFFFEFC_00000000), which is the halt-10 wall at PC 0x2a000.
    // Console-era boots never tripped it because the console never calls this
    // leaf -- hence JRN-VMB-010's "dead code" verdict, correct in ITS scope.
    // Value is RAW R16 (the .mar stores r16 unshifted; its "<29:0> cleared"
    // note is the CALLER's convention).  ONE quadword -- within the leaf's
    // single-memory-effect budget, using the deferred memEffect fields so the
    // store retires through the normal Mbox path.
    // p_temp = PAL-bank r21 (ev6_alpha_defs.mar:38) -> intShadow[5].
    // The .mar uses hw_stq/p -- a PHYSICAL store.  The memEffect MUST carry
    // S_PhysAddr (as execStqp :1466 does) or the Mbox translates p_temp as a
    // VIRTUAL address: VA 0x7000 is unmapped, the store DTB-misses, the VPTE
    // walk double-misses, and DTBM_DOUBLE_3 halts 0x0A *at this instruction*.
    // (Observed exactly that on the first cut of this fix: the wall moved from
    // PC 0x2a000 to PC 0x29dc4, the MTPR_VPTB call site itself.)
    uint64_t const pTempBase = c.cpu->intShadow[5] & ~uint64_t{7};
    if (pTempBase >= 0x1000ull && pTempBase < 0x8000ull) {
        r.semFlags   = r.semFlags
                     | grainFactory::GrainSem::S_PhysAddr
                     | grainFactory::GrainSem::S_Store;
        r.memAddr    = static_cast<coreLib::PAType>(pTempBase + 0x0); // PT__VPTB
        r.memData    = newVptb;
        r.memSize    = 8;
        r.memIsStore = true;
    } else {
        // HARD STOP, never a silent skip: a skipped store recreates exactly
        // this desync under a different precondition (architect's A3 ruling).
        // Fault here so the halt carries the encoding + p_temp in the lookback
        // ring rather than corrupting the guest's page-table view silently.
        std::fprintf(stderr,
            "MTPR_VPTB: FATAL -- p_temp candidate 0x%llx (intShadow[5]) outside "
            "[0x1000,0x8000); refusing to desync PT__VPTB from the IPRs "
            "(JRN-SCSI-026).  pc=0x%llx R16=0x%llx\n",
            static_cast<unsigned long long>(pTempBase),
            static_cast<unsigned long long>(g.pc),
            static_cast<unsigned long long>(newVptb));
        std::fflush(stderr);
        r.faultCode = coreLib::kFaultUnimplemented;
    }
#if EMULATR_BRINGUP_PROBES
    // 2026-07-08: VPTB-write probe (env EMULATR_VPTB_DIAG).  ES40 SCB null-vector
    // hunt (task #29): MTPR_VPTB deposits R16 into cpu.vptb, but VA_FORM reads
    // cpu.va_ctl -- so if the console programs a REAL base here while va_ctl stays
    // ~0x2, the written VPTB is being stranded (confirms the propagate-to-va_ctl
    // fix, and lets us diff ES40 vs a clean DS20).  Env-gated, capped at 64,
    // zero-cost when unset.
    static bool const s_vptbDiag = (std::getenv("EMULATR_VPTB_DIAG") != nullptr);
    if (s_vptbDiag) {
        static unsigned long s_vptbN = 0;
        if (s_vptbN < 64) { ++s_vptbN;
            uint64_t const vaForm = coreLib::computeVaForm(
                c.cpu->va_ctl, c.cpu->va,
                coreLib::vaCtlIsVaForm32(c.cpu->va_ctl),
                coreLib::vaCtlIsVa48(c.cpu->va_ctl));
            std::fprintf(stderr,
                "VPTB-DIAG[mtpr] cyc=%llu pc=0x%016llx R16(VPTB)=0x%016llx "
                "va_ctl=0x%016llx i_ctl=0x%016llx va=0x%016llx VA_FORM=0x%016llx\n",
                static_cast<unsigned long long>(c.cpu->cycleCount),
                static_cast<unsigned long long>(g.pc),
                static_cast<unsigned long long>(c.cpu->intReg[16]),
                static_cast<unsigned long long>(c.cpu->va_ctl),
                static_cast<unsigned long long>(c.cpu->i_ctl),
                static_cast<unsigned long long>(c.cpu->va),
                static_cast<unsigned long long>(vaForm));
            std::fflush(stderr);
        }
    }
#endif
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

#if EMULATR_BRINGUP_PROBES
    // PT__PTBR DESYNC SENSOR (2026-07-26, JRN-SCSI-026 A4 row 1; env
    // EMULATR_PTBR_DIAG).  Sibling of the VPTB sensor that cracked the halt-10
    // arc.  The .mar SWPCTX (EV6_VMS_CALLPAL.MAR:430) stores PT__PTBR(p_temp)
    // = PFN<<13, and the guest's OWN TB-miss handlers read THAT CELL as the
    // page-table walk root (EV6_VMS_PAL.MAR:1107/1166/1505/1778).  This leaf
    // maintains cpu.ptbr -- which has NO functional consumer in EmulatR (only
    // diag printfs) -- so if the cell and the leaf disagree the guest walks a
    // stale page table exactly as it walked a stale VPTB.  Logs both sides so
    // "is this live on the current path" is answered by the boot we already
    // have to run, at zero extra runs.
    {
        static bool const s_ptbrDiag = (std::getenv("EMULATR_PTBR_DIAG") != nullptr);
        if (s_ptbrDiag) {
            static unsigned long s_n = 0;
            if (s_n < 256) { ++s_n;
                uint64_t const pTempBase = c.cpu->intShadow[5] & ~uint64_t{7};
                uint64_t cellPtbr = 0;
                bool const pTempOk = (pTempBase >= 0x1000ull && pTempBase < 0x8000ull);
                if (pTempOk && c.memory != nullptr) {
                    (void)c.memory->read8(
                        static_cast<coreLib::PAType>(pTempBase + 0x8), cellPtbr);
                }
                std::fprintf(stderr,
                    "PTBR-DIAG[swpctx] cyc=%llu pc=0x%llx newPcbb=0x%llx "
                    "cpu.ptbr=0x%llx (<<13=0x%llx) PT__PTBR=0x%llx p_temp=0x%llx%s\n",
                    static_cast<unsigned long long>(c.cpu->cycleCount),
                    static_cast<unsigned long long>(g.pc),
                    static_cast<unsigned long long>(c.cpu->intReg[16]),
                    static_cast<unsigned long long>(c.cpu->ptbr),
                    static_cast<unsigned long long>(c.cpu->ptbr << 13),
                    static_cast<unsigned long long>(cellPtbr),
                    static_cast<unsigned long long>(pTempBase),
                    pTempOk ? "" : "  [p_temp UNUSABLE]");
                std::fflush(stderr);
            }
        }
    }
#endif

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
#if EMULATR_BRINGUP_PROBES
    // JRN-VMB-006 PTBR-DIAG (env EMULATR_PTBR_DIAG).  SWPCTX is the ONLY writer
    // of the cpu->ptbr abstraction (read by MFPR_PTBR).  Log each swap so we can
    // see whether the console/OS ever installs a real (nonzero) PTBR from a
    // valid HWPCB before the DTB-miss loop, and at what cycle.  Low volume.
    {
        static bool const s_diag = (std::getenv("EMULATR_PTBR_DIAG") != nullptr);
        if (s_diag) {
            static unsigned long s_n = 0;
            if (s_n < 64) { ++s_n;
                std::fprintf(stderr,
                    "PTBR-DIAG[swpctx] cyc=%llu pc=0x%016llx oldPcbb=0x%016llx "
                    "newPcbb=0x%016llx newPtbr=0x%016llx\n",
                    static_cast<unsigned long long>(c.cpu->cycleCount),
                    static_cast<unsigned long long>(g.pc),
                    static_cast<unsigned long long>(oldPcbb),
                    static_cast<unsigned long long>(newPcbb),
                    static_cast<unsigned long long>(newCtx.ptbr));
                std::fflush(stderr);
            }
        }
    }
#endif
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
    // PACKED FORMAT (JRN-ISA-001 F-1): CC<63:32> = OFFSET, CC<31:0> =
    // COUNTER (EV6 HRM 5.1.1); fields are NOT pre-summed -- software
    // sums them (AARM 4.11.9 idiom; apisrm SWPCTX depends on the
    // layout).  Must mirror execRpcc exactly; both sites change
    // together.  kCcMultiplier scales the COUNTER FIELD ONLY.
    case coreLib::HW_CC:
        value = ((c.cpu->ccOffset & 0xFFFFFFFFULL) << 32)
              | ((c.cpu->cycleCount * coreLib::CpuState::kCcMultiplier)
                 & 0xFFFFFFFFULL);
        break;
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
            coreLib::iCtlIsVaForm32(c.cpu->i_ctl),
            coreLib::iCtlIsVa48(c.cpu->i_ctl));
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
        value = coreLib::computeVaForm(c.cpu->va_ctl,
            c.cpu->va,
            coreLib::vaCtlIsVaForm32(c.cpu->va_ctl),
            coreLib::vaCtlIsVa48(c.cpu->va_ctl));
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

        // ---- Unassigned IPR index 0x2d -- FAULT (do NOT silent-zero) ----
        // 0x2d is an unassigned EV6 IPR index (table ends at C_SHFT=0x2c).  The
        // 21264 HRM is SILENT on HW_MFPR/HW_MTPR to an unassigned index (its
        // "writes ignored" rule is for reserved BIT-FIELDS within a register,
        // not an unassigned INDEX).  The decisive evidence is the guest PAL: the
        // DS10/DS20 SRM issues HW_MTPR R31->0x2d (encoded 0x77e72d40) once in its
        // register-init sweep and RELIES on it faulting -- treating it as a no-op
        // freezes DS10 in a DtbMiss loop at 0x13d38; restoring kFaultUnimplemented
        // lets DS10 advance to the console region (verified 2026-07-06,
        // journals/20260706_0x2d_rollback_experiment.md).  So fault, do not
        // silent-zero.  (NOT the serial line: SL_XMIT/SL_RCV are I_CTL[13]/[14].)
    case coreLib::HW_RESERVED_2D:
        r.faultCode = coreLib::kFaultUnimplemented;
        return r;

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
        unsigned const ptIdx = coreLib::palTempIndex(sel);
        c.cpu->palTemp[ptIdx] = c.opB;
#if EMULATR_BRINGUP_PROBES
        // JRN-VMB-006 PTBR-DIAG (env EMULATR_PTBR_DIAG).  The VMS PAL keeps the
        // page-table walk base in PHYSICAL scratch at PT__PTBR(p_temp), where
        // p_temp is a PAL_TEMP holding PAL__IMPURE_BASE (ev6_vms_pc264_pal.mar
        // :3400 load / :4944 store).  Log page-aligned, nonzero PAL_TEMP writes:
        // those are the candidate scratch/base pointers (p_temp is page-aligned),
        // so we can see whether/when the PAL establishes its impure-scratch base
        // before the DTB-miss loop.  Value-filtered + capped so the per-PAL-entry
        // GPR-save writes do not flood.  Zero cost unless EMULATR_PTBR_DIAG set.
        {
            static bool const s_diag = (std::getenv("EMULATR_PTBR_DIAG") != nullptr);
            if (s_diag && c.opB != 0 && (c.opB & 0x1FFFull) == 0) {
                static unsigned long s_n = 0;
                if (s_n < 128) { ++s_n;
                    std::fprintf(stderr,
                        "PTBR-DIAG[paltemp] cyc=%llu pc=0x%016llx PT[%u]=0x%016llx\n",
                        static_cast<unsigned long long>(c.cpu->cycleCount),
                        static_cast<unsigned long long>(g.pc),
                        ptIdx,
                        static_cast<unsigned long long>(c.opB));
                    std::fflush(stderr);
                }
            }
        }
#endif
        return r;
    }

#if EMULATR_BRINGUP_PROBES
    // JRN-VMB-008 R1: dedicated VA_CTL / I_CTL write probe (env EMULATR_VACTL_DIAG).
    // Its OWN cap so it is NOT starved by the DTB_TAG flood that fills the shared
    // MMU-ctl probe below (which hit its 256 cap before any late VA_CTL write, so
    // va_ctl=0x2 looked "never written").  Logs, in order, every write to the two
    // VA_48 bits and to VPTB -- resolving the observed VA_CTL[VA_48]=1 /
    // I_CTL[VA_48]=0 split (HRM: "should usually be equal") and whether any VPTB
    // base is ever established (I_CTL[VA_48] also selects DTBM_DOUBLE_3 vs _4).
    if (sel == coreLib::HW_VA_CTL || sel == coreLib::HW_I_CTL) {
        static bool const s_vaDiag = (std::getenv("EMULATR_VACTL_DIAG") != nullptr);
        // 2026-07-26 (JRN-SCSI-025): the fixed 128 cap filled during console
        // init (~cyc 1.18e9), a billion cycles before the halt-10 wall, so the
        // late OS-era writes -- the ones that decide whether I_CTL's VPTB is
        // ever re-installed after sys__enter_console strips it -- were never
        // visible.  Cap is now EMULATR_VACTL_DIAG_N (default 128).
        static unsigned long const s_vaCap = [] {
            char const* const e = std::getenv("EMULATR_VACTL_DIAG_N");
            return e ? std::strtoul(e, nullptr, 0) : 128ul;
        }();
        if (s_vaDiag) {
            static unsigned long s_vaN = 0;
            if (s_vaN < s_vaCap) { ++s_vaN;
                bool const isVaCtl = (sel == coreLib::HW_VA_CTL);
                bool const va48    = isVaCtl ? coreLib::vaCtlIsVa48(c.opB)
                                             : coreLib::iCtlIsVa48(c.opB);
                bool const form32  = isVaCtl ? coreLib::vaCtlIsVaForm32(c.opB)
                                             : coreLib::iCtlIsVaForm32(c.opB);
                uint64_t const vptb = isVaCtl
                                        ? (c.opB & 0xFFFFFFFFC0000000ULL) // VA_CTL[63:30]
                                        : coreLib::iCtlVptb(c.opB);        // I_CTL[47:30] sext
                std::fprintf(stderr,
                    "VACTL-DIAG cyc=%llu pc=0x%016llx reg=%s value=0x%016llx "
                    "va48=%d form32=%d vptb=0x%016llx\n",
                    static_cast<unsigned long long>(c.cpu->cycleCount),
                    static_cast<unsigned long long>(g.pc),
                    isVaCtl ? "VA_CTL" : "I_CTL",
                    static_cast<unsigned long long>(c.opB),
                    static_cast<int>(va48),
                    static_cast<int>(form32),
                    static_cast<unsigned long long>(vptb));
                std::fflush(stderr);
            }
        }
    }
#endif

#if EMULATR_MEMDIAG || defined(EMULATR_BRINGUP_PROBES)
    // 2026-07-09: guard widened to EMULATR_BRINGUP_PROBES so this capped,
    // low-volume MMU-ctl probe compiles in the CLI diag build WITHOUT enabling
    // the EMULATR_MEMDIAG per-retire flooder.  Decides ES40 memtest ACV
    // branch B1 vs B2: empty for the 0x7f826000 region => no VPTB/DTB setup =>
    // superpage(kseg) probe intended => R16 is a malformed kseg VA (upstream).
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
#if EMULATR_IRQDIAG
        // TEMP IRQDIAG (2026-07-07): watch the SRM's HW_IER writes so we can see
        // whether ei[2] (bit 35 = interval timer, IPL 22) is ever enabled.
        std::fprintf(stderr,
            "IRQDIAG-IER   pc=0x%016llx opB=0x%016llx ier=0x%016llx ei2=%d cyc=%llu\n",
            static_cast<unsigned long long>(c.cpu->pc),
            static_cast<unsigned long long>(c.opB),
            static_cast<unsigned long long>(c.cpu->ier),
            int((c.cpu->ier >> 35) & 1u),
            static_cast<unsigned long long>(c.cpu->cycleCount));
#endif
        break;
    case coreLib::HW_IER_CM:
        c.cpu->ier = coreLib::ierCmIerPortion(c.opB);
        c.cpu->mode = coreLib::ierCmExtractMode(c.opB);
#if EMULATR_IRQDIAG
        // TEMP IRQDIAG (2026-07-07): same watch on the combined IER+CM form,
        // which is the write the OpenVMS PAL MTPR_IPL path typically uses.
        std::fprintf(stderr,
            "IRQDIAG-IERCM pc=0x%016llx opB=0x%016llx ier=0x%016llx ei2=%d cyc=%llu\n",
            static_cast<unsigned long long>(c.cpu->pc),
            static_cast<unsigned long long>(c.opB),
            static_cast<unsigned long long>(c.cpu->ier),
            int((c.cpu->ier >> 35) & 1u),
            static_cast<unsigned long long>(c.cpu->cycleCount));
#endif
        break;
    case coreLib::HW_MM_STAT:  c.cpu->mm_stat = c.opB;                                          break;
    case coreLib::HW_VA_CTL: {
        c.cpu->va_ctl = c.opB;
#if EMULATR_BRINGUP_PROBES
        // VA_CTL-write probe (env EMULATR_VPTB_DIAG): the HW_MTPR path that
        // actually fires (the CALL_PAL MTPR_VPTB intrinsic never does on DS20).
        // Shows the raw value the guest programs, the VPTB slice it lands in,
        // and the resulting D-side VA_FORM (self-map PTE address).
        static bool const s_vaCtlDiag = (std::getenv("EMULATR_VPTB_DIAG") != nullptr);
        if (s_vaCtlDiag) {
            static unsigned long s_n = 0;
            if (s_n < 128) { ++s_n;
                uint64_t const vaForm = coreLib::computeVaForm(
                    c.cpu->va_ctl, c.cpu->va,
                    coreLib::vaCtlIsVaForm32(c.cpu->va_ctl),
                    coreLib::vaCtlIsVa48(c.cpu->va_ctl));
                std::fprintf(stderr,
                    "VPTB-DIAG[vactl] cyc=%llu pc=0x%016llx VA_CTL<-0x%016llx "
                    "VPTB=0x%016llx va=0x%016llx VA_FORM=0x%016llx\n",
                    static_cast<unsigned long long>(c.cpu->cycleCount),
                    static_cast<unsigned long long>(g.pc),
                    static_cast<unsigned long long>(c.opB),
                    static_cast<unsigned long long>(c.cpu->va_ctl & coreLib::kVaCtlVptbMask),
                    static_cast<unsigned long long>(c.cpu->va),
                    static_cast<unsigned long long>(vaForm));
                std::fflush(stderr);
            }
        }
#endif
        break;
    }
        /* PACKED FORMAT (JRN-ISA-001 F-1).  EV6 HRM 5.1.1: "A HW_MTPR
         * instruction to the CC writes the upper half of the register
         * and leaves the lower half unchanged."  cpu.ccOffset stores
         * the 32-bit OFFSET field (CC<63:32>); the COUNTER field is
         * derived from cycleCount and is NOT writable here.  This is
         * exactly what apisrm SWPCTX relies on when it writes the new
         * process offset via hw_mtpr pN, EV6__CC with the offset
         * pre-shifted into <63:32> (ev6_vms_callpal.mar:407-411).
         */
    case coreLib::HW_CC: {
        c.cpu->ccOffset = (c.opB >> 32) & 0xFFFFFFFFULL;
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

        // Unassigned IPR index 0x2d (SRM register-init sweep, HW_MTPR R31->0x2d,
        // encoded 0x77e72d40).  V4 raises kFaultUnimplemented here.  IMPORTANT --
        // this is a KEPT-AND-LABELED SCAFFOLD, not a proven-correct choice: 0x2d
        // is a PATH-SELECTOR.  Faulting vs no-op'ing it routes each platform to a
        // DIFFERENT, independent downstream defect (DS10 -> a device-model poll at
        // 0x13d38; ES40 -> a virtual-MMIO DtbMiss at 0x1b7dd4; DS20 -> a benign
        // 300M settling delay).  Delivery itself is FAITHFUL (correct OPCDEC
        // vector 0x8400, clean saved excAddr, no shadow-bank artifact) -- the old
        // "ES40 mis-delivers this fault" claim is WITHDRAWN.  Silicon most likely
        // IGNORES the write (no-op); the fault is kept only because it currently
        // routes DS10/DS20 to `>>>`.  Final disposition is an open architect
        // decision (going no-op REQUIRES fixing DS10's device bit first).  NOT the
        // serial line (SL_XMIT/SL_RCV are I_CTL[13]/[14]).  Full analysis:
        // journals/20260706_0x2d_path_selector_and_three_bug_decomposition.md.
    case coreLib::HW_RESERVED_2D: {
        // PHASE SCAFFOLD (DS10 device-model work): EMULATR_2D_NOOP=1 flips 0x2d
        // to the faithful no-op path so DS10 reaches its real 0x13d38 I2C poll.
        // Default = the labeled fault scaffold.  Remove when 0x2d disposition is
        // finalized (journals/20260706_0x2d_path_selector_and_three_bug_*.md).
        static int const noop2d =
            (std::getenv("EMULATR_2D_NOOP") != nullptr) ? 1 : 0;
        if (noop2d) break;
        r.faultCode = coreLib::kFaultUnimplemented;
        return r;
    }

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
// EDIT 4 (spec 20260713): exact divert->REI pairing.  A LIFO STACK of pending
// diverts, each with a FULL 32-int-reg snapshot + a monotonic id + the raw
// i_ctl at divert (for the SDE<1> state).  The matching HW_REI pops the NEWEST
// unmatched entry whose resume PC equals the divert's savedPc -- interrupts nest
// LIFO, so newest-first is the correct pairing.  This replaces the old 2-slot
// FIFO keyed on resumePc, whose cross-pairing at a repeatedly-interrupted spin
// PC made DS20 report 19,449 false mismatches.  Bounds (8) duplicated at the
// Machine.cpp fill site (namespace-scope constexpr has internal linkage).
constexpr int kDivertStackDepth = 8;
uint64_t g_divertStackPc[kDivertStackDepth]      = {};   // savedPc (word-aligned)
uint64_t g_divertStackCyc[kDivertStackDepth]     = {};   // cycle the divert fired
uint64_t g_divertStackReg[kDivertStackDepth][32] = {};   // full R0-R31 at divert
uint64_t g_divertStackIctl[kDivertStackDepth]    = {};   // i_ctl at divert (SDE)
uint64_t g_divertStackId[kDivertStackDepth]      = {};   // monotonic divert id
int      g_divertStackTop                        = 0;    // # live entries (LIFO)
uint64_t g_divertIdCounter                       = 0;    // last id handed out

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

    // DIVERT-REI EXACT compare (spec 20260713 EDIT 4).  Runs AFTER setPalMode so
    // the SDE PAL->native swap has published the native view the resumed code
    // sees.  Pops the NEWEST unmatched pending divert whose savedPc == this
    // resume PC (LIFO -- interrupts nest newest-first), diffs ALL 32 registers,
    // and records SDE at divert vs at REI so an unpaired shadow swap is visible.
    // Gated at runtime on EMULATR_RSCC_DIAG -> inert (and no stack churn) unless
    // armed.  Replaces the old 2-slot FIFO whose resumePc cross-pairing produced
    // 19,449 false mismatches on DS20 (which boots clean).
    {
        static bool const s_rsccDiag =
            std::getenv("EMULATR_RSCC_DIAG") != nullptr;
        if (s_rsccDiag && !resumeInPal) {
            uint64_t const resumePc = rawTarget & ~uint64_t{3};
            int match = -1;
            for (int s = palDiag::g_divertStackTop - 1; s >= 0; --s) {
                if (palDiag::g_divertStackPc[s] == resumePc) { match = s; break; }
            }
            if (match >= 0) {
                bool const sdeDiv =
                    coreLib::iCtlSdeHigh(palDiag::g_divertStackIctl[match]);
                bool const sdeRei = coreLib::iCtlSdeHigh(c.cpu->i_ctl);
                int bad = 0;
                for (int rn = 0; rn < 32; ++rn) {
                    uint64_t const was = palDiag::g_divertStackReg[match][rn];
                    uint64_t const now = c.cpu->intReg[rn];
                    if (was != now) {
                        ++bad;
                        std::fprintf(stderr,
                            "DIVERT-REI-EXACT id=%llu R%02d was=0x%016llx "
                            "now=0x%016llx savedPc=0x%llx divCyc=%llu reiCyc=%llu "
                            "sdeDiv=%d sdeRei=%d\n",
                            static_cast<unsigned long long>(
                                palDiag::g_divertStackId[match]),
                            rn,
                            static_cast<unsigned long long>(was),
                            static_cast<unsigned long long>(now),
                            static_cast<unsigned long long>(resumePc),
                            static_cast<unsigned long long>(
                                palDiag::g_divertStackCyc[match]),
                            static_cast<unsigned long long>(c.cpu->cycleCount),
                            sdeDiv ? 1 : 0, sdeRei ? 1 : 0);
                    }
                }
                if (bad == 0) {
                    static int s_clean = 0;
                    if (s_clean < 8) {
                        ++s_clean;
                        std::fprintf(stderr,
                            "DIVERT-REI-EXACT id=%llu CLEAN savedPc=0x%llx "
                            "divCyc=%llu reiCyc=%llu sdeDiv=%d sdeRei=%d\n",
                            static_cast<unsigned long long>(
                                palDiag::g_divertStackId[match]),
                            static_cast<unsigned long long>(resumePc),
                            static_cast<unsigned long long>(
                                palDiag::g_divertStackCyc[match]),
                            static_cast<unsigned long long>(c.cpu->cycleCount),
                            sdeDiv ? 1 : 0, sdeRei ? 1 : 0);
                    }
                }
                // Remove the matched entry; compact entries above it down.
                for (int k = match; k < palDiag::g_divertStackTop - 1; ++k) {
                    palDiag::g_divertStackPc[k]   = palDiag::g_divertStackPc[k + 1];
                    palDiag::g_divertStackCyc[k]  = palDiag::g_divertStackCyc[k + 1];
                    palDiag::g_divertStackId[k]   = palDiag::g_divertStackId[k + 1];
                    palDiag::g_divertStackIctl[k] = palDiag::g_divertStackIctl[k + 1];
                    for (int i = 0; i < 32; ++i)
                        palDiag::g_divertStackReg[k][i] =
                            palDiag::g_divertStackReg[k + 1][i];
                }
                --palDiag::g_divertStackTop;
                std::fflush(stderr);
            }
        }
    }
    // ---- END DIVERT-REI EXACT compare ----

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
