// ============================================================================
// coreLib/CpuState.h -- per-CPU architectural and side state for V4
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
// CpuState is the single per-CPU state carrier for V4.  One instance
// per CPU, owned by the pipeline driver, reachable from leaves via
// ExecCtx::cpu.  Holds the integer and floating-point register files,
// the architectural PC, the IPRs, and a small lifecycle flag set
// (halt, etc.).  No globals, no shared mutable state across CPUs --
// all per-CPU state is here.
//
// Replaces the v1-interim IprBank carrier.  The IprBank shape was a
// stepping stone before CpuState landed; once the regfile and PC
// joined the picture, IprBank's separate-type framing bought nothing
// structural and was costing a wrapper-name to mentally translate at
// every reference.  CpuState absorbs the IprBank fields verbatim
// (same names, same types) so the translator and any future PAL leaf
// reaching for an IPR finds it at the expected offset.
//
// Design contract:
//
//   The regfile is bit-pattern uniform: both intReg and fpReg are
//   uint64_t arrays.  FP leaves type-pun to double / float via
//   std::bit_cast or memcpy when they need IEEE-S or IEEE-T
//   interpretation; the storage layer carries no FP-vs-int
//   distinction.  This matches V1/V2/V3's regfile shape and keeps
//   the bypass network and the MEM-stage commit path uniform.
//
//   intReg[31] is the architectural zero register (R31).  The
//   regfile commit at MEM checks regWriteIdx against kNoRegWrite
//   (which is numerically 31) and skips the write -- so intReg[31]
//   is never written and reads-as-zero is preserved by the storage
//   never being non-zero.  fpReg[31] (F31) follows the same pattern.
//
//   pc is the CPU's architectural program counter.  IF reads pc to
//   mint the next slot; WB advances it from {grain.pc, divertTarget,
//   semFlags} at retire.  Distinct from grain.pc, which is the
//   address that an in-flight slot was fetched from -- a per-slot
//   property, not a CPU-wide one.
//
//   IPRs are flat members rather than a nested struct.  Field names
//   match the canonical Alpha names so a leaf reads cpu.ptbr, not
//   cpu.iprs.ptbr.  No accessor methods; direct member access is the
//   contract.  Translator and IPR-touching leaves consult this
//   directly; nothing else routinely touches the IPR fields.
//
//   halted is set by the pipeline driver at WB when it intercepts
//   BoxResult::faultCode == kFaultHalt.  The pipeline driver consults
//   halted at the top of each cycle and stops the run when it sees
//   true.  Distinct from a fault-code path because HALT is a graceful
//   shutdown signal, not a trap to be delivered into PALcode.
//
//   No leaf mutates CpuState directly.  Architectural register writes
//   come through BoxResult::regWriteValue + regWriteIdx, applied at
//   MEM by the regfile commit.  IPR mutation is the exception: HW_MTPR
//   and a handful of CALL_PAL functions read/write IPRs via the
//   CpuState pointer on ExecCtx.  Memory state is in GuestMemory, not
//   here.
//
// ============================================================================

#ifndef CORELIB_CPUSTATE_H
#define CORELIB_CPUSTATE_H

#include <cstdint>

#include "VA_types.h"
#include "axp_attributes_core.h"
#include "coreLib/CBoxState.h"
#include "coreLib/VA_types.h"
#include "deviceLib/Hwrpb.h"
#include "pteLib/SPAMShardManager.h"   // CpuState-resident ITB/DTB managers

namespace coreLib {

struct CpuState
{
    // ------------------------------------------------------------------
    // Architectural register files.
    // ------------------------------------------------------------------
    // intReg holds the 32 integer registers, R0..R31.  R31 is the
    // architectural zero register; the regfile commit at MEM never
    // writes intReg[31] because regWriteIdx == kNoRegWrite (== 31)
    // suppresses the write.
    //
    // fpReg holds the 32 floating-point registers, F0..F31, as raw
    // 64-bit bit patterns.  F31 follows the same zero-register
    // posture as R31.  FP leaves type-pun via std::bit_cast / memcpy
    // when they need IEEE single (32-bit) or T-format (64-bit)
    // interpretation.
    uint64_t intReg[32] = {};
    uint64_t fpReg[32]  = {};

    // FP control register (FPCR): rounding mode + trap-enable/status bits.
    // Read/written by MF_FPCR/MT_FPCR.  The SSE-FP library
    // (coreLib/proposed/alpha_SSE_fp_inl.h) folds host FP exceptions into
    // this same field via applyToFPCR(quint64& fpcr) once FP arithmetic is
    // wired through it.
    uint64_t fpcr       = 0;

    // ------------------------------------------------------------------
    // PAL shadow registers (EV6 / 21264).
    // ------------------------------------------------------------------
    // EV6 implements 8 PAL shadow registers that are exchanged with the
    // architectural registers R4-R7 and R20-R23 when the CPU enters PAL
    // mode AND I_CTL[SDE] (= I_CTL bit 7) is set.  PALcode can use the
    // shadow set as private scratch without disturbing the interrupted
    // user/kernel context.  Source: Alpha 21264/EV6 HRM Section 6.6
    // "PALshadow Registers" and Section 5.4.1 I_CTL[7:6] = SDE[1:0].
    //
    // Storage layout:
    //   intShadow[0..3]  -- shadows for R4, R5, R6, R7
    //   intShadow[4..7]  -- shadows for R20, R21, R22, R23
    //
    // swapPalShadowRegs (in coreLib::pal namespace) performs the swap
    // by exchanging intReg[4..7] with intShadow[0..3] and intReg[20..23]
    // with intShadow[4..7].  Called on every transition of palMode
    // gated by (i_ctl & kSdeBit).
    //
    // Init state: zeros.  Real EV6 power-on PALcode is responsible for
    // any initialisation it cares about; our cold-boot honours that by
    // leaving the shadows at zero until the firmware writes them.
    uint64_t intShadow[8] = {};


    // ------------------------------------------------------------------
    // SMP slot index / WHAMI (Phase 2) -- the ONE source of truth.
    // ------------------------------------------------------------------
    // cpuSlot is the per-CPU SLOT (0..cpu_count-1) the harness assigns this
    // agent (set from AlphaCpuAgent::id() in the agent ctor, T4).  It is the
    // single "which CPU" source: BOTH the trace/diagnostic cpu= tag AND the
    // PALcode WHAMI reads -- CSERVE$WHAMI and the MFPR_WHAMI CALL_PAL in
    // palBoxLib/grains/PalEntries.cpp -- source it (T5).  Single agent => 0.
    //
    // (T5 removed the former mis-typed mCpuId/cpuId()/setCpuId() -- a CpuType
    // MODEL enum, the wrong abstraction for "which CPU", dormant -- so there are
    // no longer two divergent slot sources.  The removal shrank the POD blob, so
    // kCpuStateVersion was bumped 8 -> 9; see systemLib/Snapshot.h.)
    // Default-initialized so the snapshot POD blob stays deterministic.
    uint32_t cpuSlot = 0;
    // ------------------------------------------------------------------
    // Architectural program counter.
    // ------------------------------------------------------------------
    // The CPU's PC.  IF reads this to fetch the next instruction;
    // WB advances it from {grain.pc, divertTarget, semFlags} at
    // retire.  Distinct from grain.pc, which is the address an
    // in-flight slot was fetched from.
    uint64_t pc = 0;

    // ------------------------------------------------------------------
    // PALmode == PC<0>  (EV6 architectural truth).
    // ------------------------------------------------------------------
    // CHANGE 2026-05-21: deprecated the standalone `bool palMode`.  Bit 0
    // of the PC is the PALmode flag (instructions are longword-aligned,
    // so PC<1:0> are otherwise zero and bit 0 is free for this use).
    // Set == executing PALcode; PAL-mode fetch/data accesses bypass
    // translation (PA = VA).  This is the single source of truth -- the
    // legacy bool created state-tearing every place the architectural PC
    // was materialized (link capture, trap save, hw_ret).  Consumers use
    // inPalMode() for the mode test and pcAddr() to recover the aligned
    // fetch/compare address.  See journals/PalmodePC0_Refactor.md.
    // TEP 2026-05-21: added inline macro. 
    [[nodiscard]] AXP_ALWAYS_INLINE bool     inPalMode() const noexcept { return (pc & 1ull) != 0; }
    [[nodiscard]] AXP_ALWAYS_INLINE uint64_t pcAddr()    const noexcept { return pc & ~uint64_t{1}; }

    // ------------------------------------------------------------------
    // Internal Processor Registers (IPRs) -- flat members.
    // ------------------------------------------------------------------
    // Read by the translator and by IPR-touching leaves (HW_MFPR,
    // HW_MTPR, RC, RS, RPCC, AMASK, IMPLVER).  Written by HW_MTPR,
    // by the MEM drainer's mm_stat fault-side update, and by the
    // pipeline driver's RPCC tick.  Field names match Alpha SRM
    // canonical names.

    // Page Table Base Register.  Physical address of the L1 page
    // table.  Read by the future page walker; loaded by PALcode on
    // context switch.
    uint64_t ptbr = 0;

    // Address Space Number, 8 bits on EV6.  TLB-tag for cross-context
    // separation.
    ASNType asn = 0;

    // Virtual Address Control register.  Bit 1 selects physical-mode
    // bypass when clear; configures VA window size.  Read by the
    // translator before any TLB lookup.
    uint64_t va_ctl = 0;

    // Instruction-side Control register (I_CTL).  Bit 1 selects
    // 48-bit VA mode (vs 43-bit).  Read by the kseg detector to know
    // which segment-bit positions to inspect.
    uint64_t i_ctl = 0;

    // Memory-side Control register (M_CTL).  Symmetric pair to
    // i_ctl for the data path.  Reserved for future expansion.
    uint64_t m_ctl = 0;

    // Instruction-Stream Superpage Enable, low 3 bits.  Each bit
    // gates one of three SPE modes (SPE[0], [1], [2]) for the I-
    // path translator.
    uint8_t i_spe = 0;

    // Data-Stream Superpage Enable, low 3 bits.  Same shape as
    // i_spe for the D-path.
    uint8_t m_spe = 0;

    // Current processor mode.  Mirrors PS<CM> after the pipeline's
    // raw-bits-to-enum conversion.  Read by the translator for the
    // kernel-only kseg gate and by the permission check.
    Mode_Privilege mode = Mode_Privilege::Kernel;

    // Free-running cycle counter.  Read by RPCC; ticked by the
    // pipeline driver each cycle.
    uint64_t cycleCount = 0; // pipeline - TRACE only cycle counter
    uint64_t ccOffset = 0; // architectural CC

    // ------------------------------------------------------------------
    // RPCC / HW_CC scale multiplier                            2026-05-29
    // ------------------------------------------------------------------
    // The architectural cycle counter the guest sees (via RPCC and
    // HW_MFPR HW_CC) is multiplied by kCcMultiplier relative to our
    // pipeline-step cycleCount.  This is an INTENTIONAL ASSERTION of
    // the RPCC tick rate vs the pipeline rate -- not a 1:1 simulation.
    //
    // Why:
    //   The DS10 SRM firmware's RSCC PALcode at PAL 0xb740-0xb770
    //   divides RPCC by a constant (observed ~5594) to produce the
    //   System Cycle Counter.  Its delay-loop calibration then sets a
    //   target measured in RSCC ticks.  If RPCC ticks at 1/pipeline-
    //   cycle, the firmware's target translates to ~5594x more pipeline
    //   cycles than is practical to emulate (~44.7B for an 8M-tick
    //   target).  By multiplying RPCC by 5594 here, the firmware's
    //   /5594 divide cancels out and RSCC effectively reports the
    //   pipeline cycle count directly -- 8M-tick targets resolve in
    //   8M pipeline cycles instead of 44.7B.
    //
    // Side effects:
    //   - Interval timer (Cchip timer, gated on cycleCount & mask) is
    //     UNAFFECTED -- it reads cycleCount directly, not the scaled
    //     RPCC value.  Timer fires every 2^18 = 262K pipeline cycles
    //     as designed.
    //   - HW_MTPR HW_CC writes ccOffset = written - cycleCount; the
    //     written value gets rounded down by the multiplier on the
    //     next read.  Firmware seeds RPCC very rarely; the value is
    //     opaque to it so a factor-of-5594 round-down is invisible.
    //   - Trace/log RPCC values look ~5594x larger than wall-clock
    //     cycles.  This is intentional and documents the assertion.
    //
    // Tuning:
    //   5594 is the observed firmware divisor (RSCC = RPCC / 5594 in
    //   the DS10 PAL handler).  Increase to make boot faster at the
    //   cost of coarser RSCC resolution; decrease toward 1 to restore
    //   strict 1:1 simulation (and watch the firmware spin forever).
    // ------------------------------------------------------------------
    // 2026-06-01 (timer spec v2 sec.5.2): set to 1 -- the x5594 pre-scale was
    // a stale troubleshooting artifact, NOT a cancellation of a firmware /5594
    // divide (no such divide exists; confirmed by the sec.0 RPCC probe -- the
    // timer_check loop exited after scaledspan/5594 == 1790 real cyc, proving
    // RSCC = cycleCount x 5594 passed through UNSCALED). With =1, RPCC=RSCC=
    // cycleCount (faithful) and the timer_check window uncollapses. The long
    // rationale comment above is SUPERSEDED/FALSE -- rewrite when the fix lands
    // permanently (spec sec.8). Revert to 5594 only for A/B comparison.
    static constexpr uint64_t kCcMultiplier = 1;

    // CBox CSR / IPR shadow state.  Models the three logical shift
    // chains documented in 21264/EV67 HRM section 5.4 (WRITE_ONCE,
    // WRITE_MANY, ERROR_REG) plus the C_DATA / C_SHFT access window.
    // Full description in coreLib/CBoxState.h.
    //
    // Why a separate struct rather than flat fields: the Cbox has
    // its own multi-register protocol (shift-in for writes, shift-
    // out via C_SHFT trigger for reads) that benefits from grouped
    // storage + helper methods (pushWriteMany, shiftErrorOut) at
    // the call sites in palBoxLib/grains/PalEntries.cpp.  POD layout
    // is preserved so the snapshot byte-blob serialization is
    // unaffected.
    CBoxState cBox{};

    // Interrupt flag IPR.  Per-CPU bit read-modified by RC (set) and
    // RS (clear); each returns the prior value to Ra.  PALcode
    // synchronisation primitive.
    uint64_t intrFlag = 0;

    uint8_t palPersonality = 0;   // 0 = Tru64/OSF, 1 = OpenVMS, 2 = Linux; selects CALL_PAL table

    // ------------------------------------------------------------------
    // Interrupt Enable Register (IPR HW_IER, scbd 0x010A).
    // ------------------------------------------------------------------
    // 64-bit register where each bit position enables a specific
    // hardware/software/AST interrupt source.  Bit layout mirrors
    // HW_ISUM (paired register) per Alpha 21264 HRM Section 5.4:
    //
    //   bits 14..28 : SI -- software interrupt enables, IRQ_SW[14..28]
    //   bits 29..30 : PC -- performance counter enables
    //   bit  31     : CR -- correctable read error enable
    //   bit  32     : SL -- serial line interrupt enable
    //   bits 33..38 : EI -- external interrupt enables, IRQ_H[0..5]
    //
    // Of EI: bit 35 = ei[2] = interval timer (IPL 22).
    //
    // Reset value 0 (all interrupts masked) so cold boot does not
    // accept spurious chipset interrupts before the firmware has
    // initialised its handler infrastructure.  The first time the
    // firmware writes a value with the relevant bit set via HW_MTPR
    // HW_IER is the moment that source is ready to deliver.
    //
    // Read/written via HW_MFPR / HW_MTPR with IPR index 0x010A.
    // Machine's canAcceptInterrupt(irqLevel) consults cpu.ier when
    // arbitrating chipset-driven divert requests.
    uint64_t ier = 0;

    // Interrupt Summary register (HW_ISUM, scbd 0x0D).  Architecturally
    // read-only; reflects the cause of the most recent INTERRUPT-class
    // trap.  Decoded by the OSF/1 PAL INTERRUPT entry vector
    // (ev6_osf_pal.mar START_HW_VECTOR <INTERRUPT>) which dispatches on
    // the bit pattern below.  Bit map per ev6_defs.mar EV6__ISUM__*:
    //
    //   bits 14..28 : SI -- software interrupt requests, IRQ_SW[14..28]
    //   bits 29..30 : PC -- performance counter overflow, PMC[0..1]
    //   bit  31     : CR -- correctable read error (CRD)
    //   bit  32     : SL -- serial line interrupt
    //   bits 33..38 : EI -- external interrupts IRQ_H[0..5]
    //
    // V4 v1 carries this as RW backing storage; the trap delivery path
    // writes the relevant cause bit before redirecting PC to
    // (palBase + 0x100), and HW_MFPR(HW_ISUM) returns the stored value.
    // Real EV6 derives the bits from live pin/IPL state; v1 emulates
    // the decode contract without modelling the underlying chain.
    uint64_t isum = 0;

    // Memory Management Status.  Written by the MEM-stage drainer on
    // memory-fault delivery; PALcode inspects to determine what
    // happened.  Mirrors V1 MM_STAT.
    uint64_t mm_stat = 0;

    // Faulting virtual address (HW_VA IPR, scbd 0x01C2; read-only).  Set by
    // the trap-delivery path on a TB miss / memory fault: I-side faults to
    // the faulting fetch PC, D-side faults to the data EA.  PALcode's
    // miss/fault handler reads it via HW_MFPR HW_VA.
    uint64_t va = 0;

    // Saved exception address.  Written by the trap delivery path
    // when entering PALcode; read by HW_REI to resume the
    // interrupted instruction.
    uint64_t excAddr = 0;

    // Virtual Page Table Base.  PALcode-visible IPR (MFPR_VPTB =
    // CALL_PAL function 0x29; MTPR_VPTB = CALL_PAL function 0x2A).
    // The OS sets this to point at its top-level page table; PALcode
    // page-walkers and TLB miss handlers use it.  V4 currently has no
    // page walker, so this field is read-modify-write storage only --
    // CALL_PAL MFPR_VPTB returns whatever was last written via
    // MTPR_VPTB (zero at boot).
    uint64_t vptb = 0;

    // ----------------------------------------------------------------
    // Translation buffers (ITB / DTB) -- CPU-local, software-managed.
    // ----------------------------------------------------------------
    // EV6 is a software-managed-TLB architecture: a TB miss traps to
    // PALcode (ITB_MISS @ palBase+0x180, DTB_MISS @ palBase+0x280),
    // PALcode walks the page tables with HW_LD and installs the result
    // with HW_MTPR ITB_PTE / DTB_PTE.  There is no hardware page walker.
    //
    // The TBs are per-CPU and accessed single-threaded by this CPU's
    // pipeline, so the managers carry no synchronisation (see
    // pteLib/TlbEpoch.h).  Each is a 16-shard x 8-way SPAM cache (128
    // slots), matching the architectural per-realm TB size (HRM 4.2).
    //
    // Lookup is driven by Ev6Translator; insert / invalidate are driven
    // by the HW_MTPR ITB_* / DTB_* PAL handlers, which reach these via
    // ExecCtx::cpu.
    pteLib::SPAMShardManager<16, 8> itbMgr;
    pteLib::SPAMShardManager<16, 8> dtbMgr;

    // TB IPR staging registers (HRM 5.2.1 / 5.3.x).  ITB_TAG / DTB_TAG
    // are write-only staging registers; a write to ITB_PTE / DTB_PTE
    // retires the staged tag+pte into a TB entry via round-robin
    // replacement.  The DTB carries a dual bank (0/1) for dual-issue
    // store-pair; the ITB is single-ported.
    //
    // The ITB has no dedicated ASN IPR: the I-side ASN comes from PCTX
    // (HRM 5.2.20), which V4 carries as the existing cpu.asn field.  ITB
    // inserts therefore tag with cpu.asn.  The D-side does have explicit
    // DTB_ASN0 / DTB_ASN1 IPRs (HRM 5.3.7); DTB inserts tag with whichever
    // bank PALcode prepared.
    uint64_t itbTag  = 0;   // ITB_TAG    (write-only; HW_MTPR ITB_TAG)
    uint64_t dtbTag0 = 0;   // DTB_TAG0   (write-only; HW_MTPR DTB_TAG0)
    uint64_t dtbTag1 = 0;   // DTB_TAG1   (write-only; HW_MTPR DTB_TAG1)
    ASNType  dtbAsn0 = 0;   // DTB_ASN0   (HW_MTPR DTB_ASN0)
    ASNType  dtbAsn1 = 0;   // DTB_ASN1   (HW_MTPR DTB_ASN1)

    // TB PTE_TEMP staging / read-back registers (HRM 5.2.3 / 5.3.3).
    // Single slot per realm: both DTB_PTE0 and DTB_PTE1 writes flow
    // through dtbPteTemp (last-write-wins), and the install operation
    // consumes the current TEMP contents.  HW_MFPR ITB_PTE_TEMP /
    // DTB_PTE_TEMP read these back.  Stored in raw IPR-encoded format
    // (decoded to canonical AlphaPte at install time).
    uint64_t itbPteTemp = 0;   // ITB_PTE_TEMP
    uint64_t dtbPteTemp = 0;   // DTB_PTE_TEMP

    // PAL base address.  Top of the PAL image in physical memory --
    // PALcode entry points are computed as palBase + offset, where
    // offset comes from the PAL personality vector table (RESET,
    // OPCDEC, ARITH, CALL_PAL, ...).  Read by HW_MFPR HW_PAL_BASE,
    // written by HW_MTPR HW_PAL_BASE; also seeded by FirmwareLoader
    // / SrmLoader from the embedded value at SRM payload offset 0x10.
    uint64_t palBase = 0;

    // System Control Block Base.  Per AARM Section 14.6, the SCB is an
    // OS-managed dispatch table (8K..32K bytes, page-aligned, 512 to 2048
    // entries of 16 bytes each) holding the kernel-mode entry points
    // PALcode jumps to after handling an exception/interrupt at the
    // palBase entry vector.  Software-visible form is the PFN of the SCB;
    // physical-byte form is PFN<<13 for 8 KiB pages.
    //
    // V4 architecturally: the SCB itself is OS-private memory, not owned
    // by the emulator.  Once a guest OS boots and writes SCBB via CALL_PAL
    // MTPR_SCBB, V4 stores the value here so subsequent CALL_PAL MFPR_SCBB
    // returns it.  PALcode (when loaded) walks the SCB itself; V4 does
    // not perform the SCB walk.  The Scb.h header in deviceLib defines
    // the byte-precise layout so leaves that need to inspect SCB entries
    // (e.g., for diagnostics) can overlay it on guest memory.
    //
    // Storage and personality notes:
    //
    //   Real Alpha hardware has no dedicated SCBB IPR.  Each PAL
    //   personality keeps SCBB in its own private location:
    //
    //     OSF/1   -- PT6 (palTemp[6]).  See HW_IPR.h HW_PAL_TEMP_6.
    //     VMS     -- PALcode-private context block in guest memory at
    //                p21+0x170, NOT in a PT slot.  Reference:
    //                axpbox AlphaCPU_vmspal.cpp vmspal_call_{m,f}pr_scbb.
    //
    //   V4 exposes scbb as a first-class named CpuState field, written
    //   and read exclusively by the MFPR_SCBB/MTPR_SCBB intrinsic
    //   short-circuit pair (execMfprScbb_vms / execMtprScbb_vms in
    //   palBoxLib/grains/PalEntries.cpp).  Under VMS personality there
    //   is no PT-slot aliasing (VMS uses PT8 for SSP, PT6 for unrelated
    //   scratch), so cpu.scbb is the sole source of truth and no mirror
    //   to palTemp[] is required.  When (and if) V4 grows OSF/1 PAL
    //   personality leaves, an execMtprScbb_osf variant will need to
    //   mirror cpu.scbb to palTemp[6].
    //
    //   Storage convention: raw round-trip of the value written by
    //   CALL_PAL MTPR_SCBB (whatever the firmware passes in R16).  V4
    //   does not interpret the value because V4 does not walk the SCB.
    //   If V4 grows SCB walking, switch to the AXPBox-faithful
    //   PFN<<13 byte-address form and shift on MFPR readback.
    //
    //   Canonical per the V4 storage rule (see palTemp[] doc).  The
    //   palTemp[] slot the OSF/1 convention would assign (PT6) is not
    //   maintained as a mirror; see TODO(paltemp-redirect).
    uint64_t scbb = 0;

    // PAL temporary registers PT0..PT31.  HW_MFPR / HW_MTPR access them
    // via HW_PAL_TEMP_0..HW_PAL_TEMP_31 (raw scbd 0x40..0x5F + 0x01C0
    // namespace offset = 0x0200..0x021F).
    //
    // SCOPE: palTemp[] holds ONLY transient PALcode scratch -- the
    // spill/fill working storage a PAL flow uses across its window.
    // Persistent, hot-path architectural context (SCBB, PTBR, PCBB,
    // PRBR, the per-mode stack pointers, SYSPTBR, VIRBND) is NOT stored
    // here in V4; it lives in dedicated named CpuState members, which
    // are canonical.  See cpu.scbb et al.
    //
    // The real-PALcode PT-slot convention is recorded here for reference
    // only (it is personality-dependent, NOT V4 storage):
    //   OSF/1: PT0=KSP PT2=PTBR PT4=PCBB PT5=PRBR PT6=SCBB PT7=USP
    //          PT8=SSP PT9=ESP PT10=VIRBND PT11=SYSPTBR
    //   VMS:   SCBB is NOT a PT slot -- VMS PAL stores it in a private
    //          memory context block at p21+0x170 (see scbb member doc).
    //
    // PRECONDITION (intrinsic short-circuit): while CALL_PAL MTPR/MFPR
    // for the named-member registers is handled as an intrinsic, the PT
    // slot the convention above would assign is never written, so the
    // named member and palTemp[] cannot alias.
    // TODO(paltemp-redirect): when real PALcode runs and issues HW_MTPR
    // against a named-member's convention slot, redirect that selector
    // to the named member (or mirror) so the two stores stay coherent.
    //
    // EV6/21264 exposes 24 PT slots; PT24..PT31 are the EV5 extension
    // provisioned for SRM .exe builds carrying EV5-vintage code
    // (identified 2026-05-10, hw_mtpr Rx scbd=0x5F at PC 0x12f88).
    uint64_t palTemp[32] = {};

    // ------------------------------------------------------------------
    // Per-mode shadow stack pointers and HWPCB context fields.
    // ------------------------------------------------------------------
    // Alpha defines four privilege modes (Kernel / Executive / Supervisor /
    // User) each with its own architectural stack pointer.  PS<CM> selects
    // which is "current" and exposed as R30; the other three are shadowed.
    // PALcode CHME / CHMK / CHMS / CHMU and HW_REI swap R30 against the
    // appropriate shadow when crossing mode boundaries.
    //
    // V4 storage rule (see palTemp[] block above): the named members in
    // this section are CANONICAL.  palTemp[] does not hold persistent
    // architectural context; aliasing to PT slots is not maintained.
    //
    // The real-PALcode PT-slot convention is recorded here for reference
    // only (personality-dependent, NOT V4 storage):
    //     OSF/1: ksp=PT0  usp=PT7  ssp=PT8  esp=PT9  pcbb=PT4
    //     VMS:   ksp=PT0  usp=PT7  ssp=PT8  esp=PT9  pcbb=PT4
    //            (stack-pointer slots happen to agree; SCBB differs --
    //            see scbb member doc)
    //
    // V4 C++ leaves (SWPCTX, trap delivery, HWPCB save/restore) read
    // and write these named fields directly.  The helpers in
    // deviceLib/HwpcbContext.h move data between these fields and a
    // deviceLib::hwrpb::Hwpcb overlay on guest memory.
    //
    // PRECONDITION (intrinsic short-circuit): CALL_PAL handlers that
    // would manipulate these named members run as V4 intrinsics, so
    // the convention PT slot is never written and the named member and
    // palTemp[] cannot alias.  See TODO(paltemp-redirect) in palTemp[]
    // doc for the future seam when real PALcode SWPCTX runs.

    // Kernel-mode stack pointer.  Active as R30 when PS<CM> = Kernel.
    uint64_t ksp = 0;

    // Executive-mode stack pointer (VMS only; Tru64 runs in K and U
    // exclusively).  Active as R30 when PS<CM> = Executive.
    uint64_t esp = 0;

    // Supervisor-mode stack pointer (VMS only).  Active as R30 when
    // PS<CM> = Supervisor.
    uint64_t ssp = 0;

    // User-mode stack pointer.  Active as R30 when PS<CM> = User.
    uint64_t usp = 0;

    // Floating-point enable.  Bit 0 = enable; cleared by CALL_PAL
    // CLRFEN, set by MTPR_FEN.  When clear, FP instructions raise
    // FP_DISABLED.  Mirrors the per-process FEN field in
    // HWPCB+56[0:0] (AARM Section III).
    uint64_t fen = 0;

    // AST enable / status composite register.  Per HWPCB layout
    // (HWPCB+48); per-mode AST request and enable bits packed.  Read
    // by CALL_PAL MFPR_ASTEN / MFPR_ASTSR; written by MTPR_ASTEN /
    // MTPR_ASTSR.
    uint64_t asten_sr = 0;

    // Privileged Context Block Base.  Physical address of the current
    // process's HWPCB in guest memory.  Updated by CALL_PAL SWPCTX
    // (function 0x05 VMS / 0x30 OSF) when the OS switches processes.
    // Read by HW_MFPR HW_PCBB / CALL_PAL MFPR_PCBB; written by SWPCTX.
    uint64_t pcbb = 0;

    // ------------------------------------------------------------------
    // Load-locked / store-conditional reservation.
    // ------------------------------------------------------------------
    // Per-CPU reservation tracking for LDL_L / LDQ_L paired with
    // STL_C / STQ_C.  64-byte cache-line granularity per V1
    // ReservationManager.  Single-CPU posture: one reservation per
    // CPU, no shared bookkeeping.  Multi-CPU machinery (cache-line
    // invalidation across CPUs on stores) is deferred until v1
    // sees a multi-CPU consumer.
    //
    // LDL_L / LDQ_L drain at MEM:
    //   reservedCacheLine = pa & ~0x3FULL;
    //   hasReservation    = true;
    //
    // STL_C / STQ_C drain at MEM:
    //   bool valid = hasReservation && reservedCacheLine == (pa & ~0x3FULL);
    //   hasReservation = false;
    //   regWriteValue  = valid ? 1 : 0;
    //   if (valid) actually publish the store, else skip.
    //
    // TODO(deprecated) 2026-05-14: reservation state has moved into
    //   memoryLib::GuestMemory::lockMonitor() (a memoryLib::LockMonitor
    //   instance).  The LockMonitor provides per-CPU cache-line
    //   reservation tracking with the cross-CPU invalidation hook
    //   already wired -- every store calls clearLine(pa) on its own
    //   so STQ_C semantics are correct in SMP-eventual.  The fields
    //   below remain for MemDrainer / BreakpointSink / CpuStateDump
    //   call sites that have not yet been migrated; migration is
    //   mechanical (find these names, route the read/write through
    //   GuestMemory::lockMonitor()).  Once all call sites are
    //   migrated, remove these two fields and the snapshot path
    //   that serialises them.  See journals/MemoryV2_Integration_Notes.md.
    uint64_t reservedCacheLine = 0;
    bool     hasReservation    = false;

    // ------------------------------------------------------------------
    // CPU lifecycle flags.
    // ------------------------------------------------------------------
    // Set by the pipeline driver at WB when it intercepts
    // BoxResult::faultCode == kFaultHalt.  The driver checks halted
    // at the top of each cycle and stops the run when it sees true.
    // Distinct from a fault-code path because HALT is a graceful
    // shutdown signal, not a trap to deliver into PALcode.
    bool halted = false;

    // Unalign trap policy.  When true, the data-path alignment check
    // in mmuLib::Ev6Translator::translateDataAligned returns
    // TranslationResult::Unaligned on EA misalignment and the pipeline
    // delivers a kFaultUnaligned trap to PALcode (architecturally
    // correct EV6 behaviour).  When false (V4 v1 default), the trap
    // is suppressed and the access proceeds at the byte offset --
    // GuestMemory's read*/write* use std::memcpy which is alignment-
    // agnostic on x64, so unaligned access just works at the cost of
    // never exercising the PALcode UNALIGN handler.
    //
    // Default is false to unblock pre->>> firmware bring-up: the
    // OSF/1 PAL UNALIGN handler at PC 0xdb64..0xdbcc has a latent
    // bug (R20 zeroed before STQ to top-of-PA) that blocks forward
    // progress.  Bypass lets V4 continue past the UNALIGN site and
    // surface whatever comes next.  Once that path is clear and the
    // handler bug is correctly diagnosed, this can be flipped back
    // to true (or made dynamically settable via HW_MTPR I_CTL when
    // the I_CTL.IC_EN equivalent is wired).
    bool unalignTrapEnabled = false;

    // Last BoxResult fault code recorded at retire.  Stays kNoFault
    // during normal execution; set to kFaultHalt on graceful HALT,
    // set to the offending kFault* code on every other fault that
    // halts the run.  Lets post-mortem code (Machine::run, CpuStateDump,
    // diagnostics) classify why the CPU stopped without re-deriving
    // it from the BoxResult that caused the stop.
    uint16_t lastFaultCode = 0;
};

} // namespace coreLib

#endif // CORELIB_CPUSTATE_H
