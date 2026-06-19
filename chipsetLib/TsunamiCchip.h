// ============================================================================
// TsunamiCchip.h -- Tsunami/Typhoon Cchip (System Configuration Controller)
// ============================================================================
// Project: ASA-EMulatR - Alpha AXP Architecture Emulator
// Copyright (C) 2025, 2026 eNVy Systems, Inc. All rights reserved.
// Licensed under eNVy Systems Non-Commercial License v1.1
//
// Project Architect: Timothy Peer
// AI Code Generation: Claude (Anthropic)
// ============================================================================
//
// PURPOSE:
//   Emulates the Tsunami/Typhoon Cchip -- the central system configuration
//   controller for DS10, DS20, ES40 (Tsunami) and DS25, ES45 (Typhoon).
//
//   Supports 1-4 emulated Alpha CPUs with concurrent access.
//
// SYNCHRONIZATION MODEL:
//   Three categories of register state:
//
//   1. READ-ONLY AFTER INIT -- CSC, MPD, MTR  (AAR0-3 are RW; see below)
//      Written once during construction/reset. Only read thereafter.
//      No synchronization needed.
//
//   2. PER-CPU REGISTERS -- DIM[0-3], IIC[0-3]
//      CPU N writes its own DIM[N]. Interrupt delivery logic reads
//      DIM[N] from a potentially different thread. Implemented as
//      std::atomic<uint64_t> with relaxed ordering.
//
//   3. SHARED MUTABLE STATE -- DRIR
//      Multiple writers (device interrupt assert via fetch_or),
//      multiple readers (CPU interrupt poll), and clear-on-ack
//      (fetch_and). Implemented as std::atomic<uint64_t>.
//
//   DIR[N] is NOT stored. It is computed on read as:
//      DIR[N] = DRIR & DIM[N]
//   This is combinational logic on real hardware and eliminates
//   a synchronization target in the emulator.
//
// VARIANT DIFFERENCES:
//   Tsunami (21272):  DREV = 0x10, max 64 GB RAM
//   Typhoon (21274):  DREV = 0x20, max 256 GB RAM, wider AAR fields
//   The variant is latched at construction from the machine model.
//   Register offsets and access model are identical.
//
// MMIO ADDRESS:
//   Base PA: 0x801.A000.0000 (fixed, same for both variants)
//   Size:    256 MB (0x1000.0000)
//
// REGISTER MAP:
//   Offset    Name     R/W    Description
//   0x0000    CSC      RO     System Configuration (CPU present mask)
//   0x0040    MTR      RO     Memory Timing Register
//   0x0080    MISC     RW     Miscellaneous (interval timer, NXM)
//   0x00C0    MPD      RO     Memory Presence Detect
//   0x0100    AAR0     RW     Array Address Register 0
//   0x0140    AAR1     RW     Array Address Register 1
//   0x0180    AAR2     RW     Array Address Register 2
//   0x01C0    AAR3     RO     Array Address Register 3
//   0x0200    DIM0     RW     Device Interrupt Mask (CPU 0)
//   0x0240    DIM1     RW     Device Interrupt Mask (CPU 1)
//   0x0280    DIR0     RO     Device Interrupt Request (CPU 0) [computed]
//   0x02C0    DIR1     RO     Device Interrupt Request (CPU 1) [computed]
//   0x0300    DRIR     RO*    Device Raw Interrupt Request
//   0x0340    PRBEN    RW     Probe Enable
//   0x0380    IIC0     RW     Inter-processor Interrupt Control (CPU 0)
//   0x03C0    IIC1     RW     Inter-processor Interrupt Control (CPU 1)
//   0x0400    MPR0     RO     Memory Port Status 0
//   0x0440    MPR1     RO     Memory Port Status 1
//   0x0480    MPR2     RO     Memory Port Status 2
//   0x04C0    MPR3     RO     Memory Port Status 3
//   0x0500    DIM2     RW     Device Interrupt Mask (CPU 2)
//   0x0540    DIM3     RW     Device Interrupt Mask (CPU 3)
//   0x0580    DIR2     RO     Device Interrupt Request (CPU 2) [computed]
//   0x05C0    DIR3     RO     Device Interrupt Request (CPU 3) [computed]
//   0x0600    IIC2     RW     Inter-processor Interrupt Control (CPU 2)
//   0x0640    IIC3     RW     Inter-processor Interrupt Control (CPU 3)
//
//   * DRIR is written by devices via assertInterrupt()/deassertInterrupt(),
//     not by CPU MMIO writes. CPU writes to DRIR offset are ignored.
//
// REFERENCES:
//   Tsunami/Typhoon Hardware Reference Manual (EC-RE2CA-TE)
//   Chapter 4: Cchip Registers
//
// ============================================================================
// TODO Register Table -- unwired or partially-wired behavior (Phase B)
// ============================================================================
// Phase B refactor (2026-05-17) landed the uniform CSR surface against
// `Tsunami21272_CsrSpec.h` and instrumented every recognized access
// with `CSR_LOG_R` / `CSR_LOG_W`.  The following registers have storage
// + diagnostics but their HRM-defined side effects are NOT yet wired.
// Each carries a `// TODO(unwired): ...` line comment at its decode
// site pointing back at this table.  Grep `TODO(unwired)` across
// chipsetLib/ for the full backlog.
//
//   MTR    -- storage RO; no SDRAM timing model.  Wire when memory
//             training becomes load-bearing.
//   MPD    -- storage RO; SPD pins fixed at 0xFF.  Wire when DIMM
//             discovery becomes guest-visible.
//   AAR0-3 -- RW per HRM Tables 10-14/10-15.  reset() computes the
//             power-on defaults (the topology SROM would program) from
//             cpuCount/memSize; firmware may reprogram array sizes during
//             memory init and those writes stick.  ASIZ encoding is
//             byte-correct (Ticket 2 doctests pin it).
//   MPR0-3 -- storage RO (Phase B leaves them zero-init; WO per HRM).
//             Wire when SDRAM mode-register programming becomes
//             load-bearing.
//   PRBEN  -- storage RW; no probe-enable effect.  Wire when SMP
//             cache-coherence modelling lands.
//   DIM0-3 -- storage RW per CPU; DIR readback already combinational.
//             No CPU-side b_irq<0/1> assert path -- Phase C wires it.
//   DRIR   -- storage RW shared (assertInterrupt/deassertInterrupt
//             from device side already work).  CPU read returns
//             current shadow.  Phase C wires b_irq edge delivery.
//   IIC0-3 -- storage RW; no ignore-count decrement.  Phase C MVP
//             fires the interval timer unconditionally; the fetch_sub
//             ignore-count decrement remains TODO for Phase C+.
//   TTR    -- NEW in Phase B: storage RW only.  TIGbus timing has no
//             guest-visible effect today.  Wire when flash ROM
//             access goes live (HRM 10.2.2.14, Table 10-23).
//   TDR    -- NEW in Phase B: storage RW only.  Same posture as TTR.
//
//   MISC   -- now fully spec-driven on the W1C/W1S/WO/RO axis (CAS
//             loop, single atomic uint64_t).  ITINTR deassert hook
//             WIRED in Phase C: a W1C clear of MISC<ITINTR<4+n>>
//             also clears m_pendingIrq2[n].  Remaining side effects
//             (deassert b_irq<3> on IPINTR clear, b_irq<3> assert on
//             IPREQ write, DEVSUP one-poll suppression) stay
//             TODO(unwired).  ABT/ABW arbitration auto-promote and
//             ACL arbitration clear are WIRED (2026-05-30, HRM 12.2)
//             for Phase C+ / Phase D follow-up.
//
// When wiring lands, the relevant table entry above is removed in
// the same edit, the per-site line comment is removed, and the
// CHANGE HISTORY block below records the removal.
//
// ============================================================================
// CHANGE HISTORY
// ============================================================================
//
//   2026-05-17  Phase B uniform-CSR-surface refactor.  Storage:
//               m_misc switched to std::atomic<uint64_t> for CAS-loop
//               W1C/W1S; m_ttr / m_tdr added (HRM 10.2.2.14/15).
//               Surface: new read(offset, cpuId) / write(offset,
//               value, cpuId) overloads (Phase A resolution 5);
//               existing cpu-less overloads now delegate.  Behavior:
//               MISC writes route through miscWriteW1C() per
//               CchipMiscSpec; MISC reads inject CPUID per spec when
//               cpuId >= 0.  Diagnostics: every recognized case
//               instruments CSR_LOG_R / CSR_LOG_W against the spec
//               name.  Snapshot: format unchanged (atomic load/store
//               on the wire is byte-identical; TTR/TDR deliberately
//               NOT serialized this pass -- adds in the next
//               format-version bump alongside the sparse-format
//               snapshot upgrade).
//
//   2026-05-17  Phase C interval-timer storage + delivery hooks.
//               Storage: m_pendingIrq2 -- per-CPU b_irq<2> latch as
//               std::array<std::atomic<bool>, kMaxCPUs>, zero-init
//               at reset().  Surface: new public methods
//               pendingIrq2(cpuId), clearPendingIrq2(cpuId), and
//               fireIntervalTimer().  Behavior: fireIntervalTimer()
//               CAS-sets MISC<ITINTR<4+n>> per enabled CPU and
//               asserts m_pendingIrq2[n] (Phase C MVP fires
//               unconditionally; IIC ignore-count fetch_sub is the
//               Phase C+ follow-up).  miscWriteW1C now wires the
//               Phase B TODO(unwired) ITINTR deassert hook -- when a
//               write clears MISC<ITINTR<4+n>> the matching
//               m_pendingIrq2[n] is also cleared, mirroring the
//               in-Cchip b_irq<2> latch's HRM-defined W1C deassert.
//               Snapshot: pendingIrq2 deliberately NOT serialized
//               (transient hardware-edge state -- a snapshot taken
//               mid-divert and resumed should rely on the PAL
//               handler's W1C to leave a quiescent state, not on
//               replaying the pending edge).
//
// ============================================================================

#ifndef TSUNAMI_CCHIP_H
#define TSUNAMI_CCHIP_H
#include <array>
#include <atomic>
#include <cassert>
#include <cstdio>
#include "TsunamiVariant.h"
#include <algorithm>

#include <QDataStream>
#include <ios>

#include "Tsunami21272_RegisterMap.h"
#include "Tsunami21272_CsrSpec.h"   // Phase B: spec-driven decode of MISC fields
#include "CsrDiag.h"                // Phase B: per-CSR-access diagnostic sink

class TsunamiCchip
{
public:

    // ========================================================================
    // Constants
    // ========================================================================
    static constexpr uint64_t kBasePA = 0x801A0000000ULL;
    static constexpr uint64_t kSize = 0x10000000ULL;    // 256 MB
    static constexpr int     kMaxCPUs = 4;
   /*

    // ========================================================================
    // Register offsets
    // ========================================================================
    static constexpr uint64_t kCSC       = 0x0000;
    static constexpr uint64_t kMTR       = 0x0040;
    static constexpr uint64_t kMISC      = 0x0080;
    static constexpr uint64_t kMPD       = 0x00C0;
    static constexpr uint64_t kAAR0      = 0x0100;
    static constexpr uint64_t kAAR1      = 0x0140;
    static constexpr uint64_t kAAR2      = 0x0180;
    static constexpr uint64_t kAAR3      = 0x01C0;
    static constexpr uint64_t kDIM0      = 0x0200;
    static constexpr uint64_t kDIM1      = 0x0240;
    static constexpr uint64_t kDIR0      = 0x0280;
    static constexpr uint64_t kDIR1      = 0x02C0;
    static constexpr uint64_t kDRIR      = 0x0300;
    static constexpr uint64_t kPRBEN     = 0x0340;
    static constexpr uint64_t kIIC0      = 0x0380;
    static constexpr uint64_t kIIC1      = 0x03C0;
    static constexpr uint64_t kMPR0      = 0x0400;
    static constexpr uint64_t kMPR1      = 0x0440;
    static constexpr uint64_t kMPR2      = 0x0480;
    static constexpr uint64_t kMPR3      = 0x04C0;
    static constexpr uint64_t kDIM2      = 0x0500;
    static constexpr uint64_t kDIM3      = 0x0540;
    static constexpr uint64_t kDIR2      = 0x0580;
    static constexpr uint64_t kDIR3      = 0x05C0;
    static constexpr uint64_t kIIC2      = 0x0600;
    static constexpr uint64_t kIIC3      = 0x0640;

*/
    // ========================================================================
    // Construction
    // ========================================================================

    /**
     * @brief Construct Cchip from variant and system parameters
     * @param variant      Chipset variant (Tsunami or Typhoon)
     * @param cpuCount     Number of CPUs present (1-4)
     * @param memSizeBytes Total physical memory in bytes
     */
    explicit TsunamiCchip(ChipsetVariant variant = ChipsetVariant::Tsunami,
                          int cpuCount = 4,
        uint64_t memSizeBytes = 0x800000000ULL) noexcept
        : m_variant(variant)
        , m_cpuCount(cpuCount)
        , m_memSizeBytes(memSizeBytes)
    {
        //static_assert(cpuCount >= 1 && cpuCount <= kMaxCPUs); 
        reset();
    }
    // --- Bus Arbiter Gatekeeper Logic ---
    // The Chipset calls this to decide if a PA routes to GuestMemory.
    bool isDramAddress(uint64_t pa) const noexcept {
        // Tsunami silicon constraint: DRAM is mapped via AARs 0-3.  The
        // AAR.base field holds the *encoded* register word (see computeAAR
        // / the CSR path); decode it to the linear [base, base+size) window
        // before comparing.  Do NOT reinterpret the encoded word as a linear
        // address, and do NOT rely on AAR.mask -- firmware AAR writes update
        // only the encoded word, leaving mask stale.
        for (const auto& aar : m_aar) {
            if (aar.enabled) {
                const AarWindow w = decodeAAR(aar.base);
                if (w.size != 0 && pa >= w.base && pa < (w.base + w.size)) {
                    return true;
                }
            }
        }
        return false;
    }

    // ========================================================================
    // Reset -- power-on defaults
    // ========================================================================

    // # ============================================================================
    // # TsunamiCchip.h -- reset() rewrite for multi - array memory splitting
    // # ============================================================================
    // #
    // # Tsunami: max 1GB per array, 4 arrays = 4GB max
    // # Typhoon : max 8GB per array, 4 arrays = 32GB max
    // #
    // # Examples :
    // #   4GB on Tsunami : 4 x 1GB arrays(AAR0 - AAR3 each 1GB)
    // #   8GB on Typhoon : 4 x 2GB arrays(AAR0 - AAR3 each 2GB)
    // #   16GB on Typhoon : 4 x 4GB arrays
    // #   32GB on Typhoon : 4 x 8GB arrays
    // #
    // # ============================================================================
    void reset() noexcept
    {
        // ------------------------------------------------------------------
        // AAR0-3: memory array address registers
        // ------------------------------------------------------------------
        const bool isTyphoon = (m_variant == ChipsetVariant::Typhoon);
        const uint64_t maxPerArray = isTyphoon
            ? (8ULL * 1024 * 1024 * 1024)
            : (1ULL * 1024 * 1024 * 1024);

        uint64_t remaining = m_memSizeBytes;
        uint64_t base = 0;

        for (int i = 0; i < 4; ++i) {
            if (remaining == 0) {
                m_aar[i] = { 0, 0, false };
            }
            else {
                const uint64_t thisArray = std::min(remaining, maxPerArray);

                // Correctly initializing all struct fields
                m_aar[i].base = computeAAR(base, thisArray, isTyphoon);
                m_aar[i].mask = thisArray - 1;
                m_aar[i].enabled = true;

                base += thisArray;
                remaining -= thisArray;
            }
        }

        // ------------------------------------------------------------------
        // CSC: System Configuration Register
        // ------------------------------------------------------------------
        m_csc = 0;
        for (int i = 0; i < m_cpuCount && i < kMaxCPUs; ++i)
            m_csc |= (1ULL << i);
        m_csc |= isTyphoon ? 0x03ULL : 0x01ULL;
        m_csc |= (2ULL << 52) | (1ULL << 48) | (1ULL << 44) |
            (1ULL << 40) | (1ULL << 36) | (1ULL << 31) |
            (2ULL << 26) | (3ULL << 20) | (3ULL << 18) | (3ULL << 16);

        // ------------------------------------------------------------------
        // MISC: Miscellaneous Control
        // ------------------------------------------------------------------
        const auto* info = variantInfo(m_variant);
        m_misc.store(static_cast<uint64_t>(info ? info->crev : 1) << 32,
            std::memory_order_relaxed);

        // ------------------------------------------------------------------
        // Registers (TTR, TDR, MTR, MPD)
        // ------------------------------------------------------------------
        m_ttr = 0;
        m_tdr = 0;
        m_mtr = 0;
        m_mpd = 0xFFULL;

        // ------------------------------------------------------------------
        // Interrupt and Probe state
        // ------------------------------------------------------------------
        m_drir.store(0, std::memory_order_relaxed);
        for (int i = 0; i < kMaxCPUs; ++i) {
            m_dim[i].store(0, std::memory_order_relaxed);
            m_iic[i].store(0, std::memory_order_relaxed);
            m_pendingIrq2[i].store(false, std::memory_order_relaxed);
            m_pendingIrq3[i].store(false, std::memory_order_relaxed);
            m_mpr[i] = 0;
        }
        m_prben = 0xFFFFFFFFFFFFFFFFULL;
    }

    // ========================================================================
    // Variant access
    // ========================================================================

    ChipsetVariant variant() const noexcept { return m_variant; }
    int cpuCount() const noexcept { return m_cpuCount; }

    // ========================================================================
    // Device interrupt assertion (called by device models, any thread)
    // ========================================================================

    /**
     * @brief Assert a device interrupt line
     * @param bit  Interrupt bit position in DRIR (0-63)
     *
     * Thread-safe. Uses atomic fetch_or.
     */
    void assertInterrupt(int bit) noexcept
    {
       // static_assert(bit >= 0 && bit < 64);
        m_drir.fetch_or(1ULL << bit, std::memory_order_release);
    }

    /**
     * @brief Deassert a device interrupt line
     * @param bit  Interrupt bit position in DRIR (0-63)
     *
     * Thread-safe. Uses atomic fetch_and.
     */
    void deassertInterrupt(int bit) noexcept
    {
       // static_assert(bit >= 0 && bit < 64);
        m_drir.fetch_and(~(1ULL << bit), std::memory_order_release);
    }

    /**
     * @brief Read pending interrupt mask for a specific CPU
     * @param cpuId  CPU index (0-3)
     * @return       DRIR & DIM[cpuId] -- combinational interrupt request
     *
     * Thread-safe. Computed on read (no stored DIR register).
     */
    inline uint64_t readDIR(int cpuId) const noexcept
    {
       // static_assert(cpuId >= 0 && cpuId < kMaxCPUs);
        return m_drir.load(std::memory_order_relaxed)
             & m_dim[cpuId].load(std::memory_order_relaxed);
    }

    // ========================================================================
    // IPI support
    // ========================================================================

    inline uint64_t readIIC(int cpuId) const noexcept
    {
      //  static_assert(cpuId >= 0 && cpuId < kMaxCPUs);
        return m_iic[cpuId].load(std::memory_order_relaxed);
    }

    inline void sendIPI(int targetCpu, uint64_t value) noexcept
    {
      //  static_assert(targetCpu >= 0 && targetCpu < kMaxCPUs);
        m_iic[targetCpu].store(value, std::memory_order_release);
    }

    // ========================================================================
    // Phase C: Interval-timer fire / latch query / latch clear.
    // ========================================================================
    //
    // These three methods are the Phase C interface between the Cchip's
    // in-chip b_irq<2> latch and the rest of the system:
    //
    //   - `fireIntervalTimer()` is called from Machine::run when the
    //     cycle-driven predicate in CchipIntervalTimer.h returns true.
    //     It sets MISC<ITINTR<4+n>> for each enabled CPU via the same
    //     CAS path that miscWriteW1C() uses (so the W1C invariants are
    //     preserved) and asserts m_pendingIrq2[n].  Phase C MVP fires
    //     unconditionally; the HRM IIC*n* ignore-count decrement is
    //     marked TODO(unwired) for Phase C+ follow-up.
    //
    //   - `pendingIrq2(cpuId)` is the per-retire poll in Machine::run.
    //     Returns true while the latch is asserted; relaxed-load atomic
    //     since the polling cadence (every retire) dwarfs any plausible
    //     reordering window.
    //
    //   - `clearPendingIrq2(cpuId)` is called both from Machine::run at
    //     divert time (edge acknowledgment so the next pump iteration
    //     does not re-divert) and from miscWriteW1C() when firmware
    //     W1C's the matching MISC<ITINTR<4+n>> bit.  Idempotent.
    //
    // Memory-order discipline:
    //   - fireIntervalTimer sets the latch with `release` so the
    //     subsequent Machine::run `pendingIrq2()` load acquires a
    //     consistent view of MISC<ITINTR>.
    //   - clearPendingIrq2 uses `relaxed` -- the clear is paired with
    //     the divert side-effects (excAddr/pc/palMode/isum writes) that
    //     happen on the same thread, so no cross-thread synchronization
    //     is needed today.  When SMP arrives, this will need re-review.
    // ========================================================================

    /**
     * @brief Query the per-CPU b_irq<2> latch (interval-timer pending).
     * @param cpuId  CPU index in [0, kMaxCPUs).
     * @return       True if the timer-pending latch is asserted for this CPU.
     *
     * Thread-safe.  Called from Machine::run per retire; cheap relaxed
     * load.
     */
    inline bool pendingIrq2(int cpuId) const noexcept
    {
        return m_pendingIrq2[cpuId].load(std::memory_order_relaxed);
    }

    /**
     * @brief Clear the per-CPU b_irq<2> latch.
     * @param cpuId  CPU index in [0, kMaxCPUs).
     *
     * Two call sites:
     *   1. Machine::run at divert-fire time, immediately after staging
     *      excAddr/pc/palMode/isum for the synthetic INTERRUPT vector.
     *      Acks the edge so the next pump iteration does not re-divert
     *      while the PAL handler is in flight.
     *   2. miscWriteW1C() when firmware W1C's MISC<ITINTR<4+cpuId>>.
     *      Defensive idempotent clear -- the divert above already
     *      cleared the latch, but mirroring MISC storage state keeps
     *      the model consistent with HRM "ITINTR drives b_irq<2>"
     *      semantics.
     */
    inline void clearPendingIrq2(int cpuId) noexcept
    {
        m_pendingIrq2[cpuId].store(false, std::memory_order_relaxed);
    }

    /**
     * @brief Query the per-CPU b_irq<3> latch (interprocessor-interrupt pending).
     * @param cpuId  CPU index in [0, kMaxCPUs).
     * Direct analog of pendingIrq2(); polled per retire by Machine::run.
     */
    inline bool pendingIrq3(int cpuId) const noexcept
    {
        return m_pendingIrq3[cpuId].load(std::memory_order_relaxed);
    }

    /**
     * @brief Clear the per-CPU b_irq<3> latch.
     * @param cpuId  CPU index in [0, kMaxCPUs).
     *
     * Call sites mirror clearPendingIrq2(): Machine::run at IPI divert-fire,
     * and miscWriteW1C() when firmware W1C's MISC<IPINTR<8+cpuId>>.
     */
    inline void clearPendingIrq3(int cpuId) noexcept
    {
        m_pendingIrq3[cpuId].store(false, std::memory_order_relaxed);
    }


    // ========================================================================
    // b_irq<0> -- Error class (Phase B-NXMA, 2026-05-28)
    // ========================================================================
    //
    // b_irq<3:0> -> internal IPL mapping.  NOTE: this is a 21264 + PALcode
    // convention, NOT a 21272 Cchip fact -- the chipset HRM has no IPL
    // content (the earlier "HRM 6.3.1" citation here was wrong; 6.3.1 is the
    // device/error b_irq<1:0> delivery section).  Source is the 21264 HRM
    // interrupt section + the firmware PAL.  2026-06-18 the IPI delivery is
    // now CONFIRMED from the PC264 OSF PAL source (apisrm ref
    // ev6_osf_pc264_pal.mar IPL table): IRQ<3>=interprocessor maps to EI[3] /
    // IER bit 36 (IRQ_IP=8, EV6__IER__EIEN__S=33 -> bit 36) and sits at the
    // SAME IPL as the clock (IPL 5).  Machine::run delivers it via
    // canAcceptInterrupt(21), which selects IER bit 36 (the descriptive "IPL"
    // numbers below are a separate V4 priority label, NOT the canAcceptInterrupt
    // irqLevel scale -- in that scale err=24, dev=23, clk=22, IP=21):
    //
    //   b_irq<3>  IPI                IPL 20  (descriptive only; gate = 21)
    //   b_irq<1>  Device / PCI / ISA IPL 21
    //   b_irq<2>  Interval timer     IPL 22
    //   b_irq<0>  System error class IPL 23   <-- this method
    //
    // The error class covers DRIR<63:58> -- NXM at bit 63, plus reserved
    // future error sources at 62:58.  When (DRIR & DIM[cpuId]) has any of
    // those bits set, b_irq<0> is asserted to that CPU and the divert into
    // the MCHECK/PAL hardware-error entry vector fires (gated by current
    // IPL < 23 -- see Machine::run b_irq<0> arbitration block, which calls
    // canAcceptInterrupt(23)).
    //
    // NO clearPendingIrq0() method by design.  b_irq<0> is level-pinned by
    // the source latches:
    //   - NXM at bit 63 is cleared by firmware W1C of MISC<NXM>; that W1C
    //     path mirrors the clear into m_drir, deasserting DRIR<63> and
    //     therefore deasserting the per-CPU view computed by this query.
    //   - Other 62:58 sources, when added, will follow the same level-clear
    //     pattern via their own W1C MMIO writes.
    // If the OS or PALcode drops IPL below 23 without clearing the source,
    // b_irq<0> stays asserted and re-diverts immediately -- exactly the
    // sticky-pin semantics HRM specifies for catastrophic errors.

    /**
     * @brief Query b_irq<0> pending (error class: DRIR<63:58> & DIM[cpuId]<63:58>).
     * @param cpuId  CPU index in [0, kMaxCPUs).
     * @return       True if any error-class bit is set in DIR[cpuId]<63:58>.
     *
     * Thread-safe.  Called from Machine::run per retire; cheap relaxed
     * loads on m_drir + m_dim[cpuId].
     *
     * Bit assignment per HRM 10.2.x:
     *   DRIR<63> = NXM (latched mirror of MISC<NXM>)
     *   DRIR<62:58> reserved / future error sources
     */
    inline bool pendingIrq0(int cpuId) const noexcept
    {
        // Error-class mask: bits 63:58.  NXM=63 is the only source wired
        // today (Phase B-NXMA).  Other error sources land at 62:58 later.
        constexpr uint64_t kErrorClassMask = 0xFC00'0000'0000'0000ULL;
        uint64_t const drir = m_drir.load(std::memory_order_relaxed);
        uint64_t const dim  = m_dim[cpuId & (kMaxCPUs - 1)]
                                  .load(std::memory_order_relaxed);
        return ((drir & dim) & kErrorClassMask) != 0;
    }

    /**
     * @brief Query b_irq<1> pending (device class: DRIR<55:0> & DIM).
     * @param cpuId  CPU index in [0, kMaxCPUs).
     * @return       True if any device-class bit is set in DIR[cpuId]<55:0>.
     *
     * Increment 2 (2026-06-04, serial-console interrupt design Seam 2).
     * Level-computed like pendingIrq0 -- no latch, no clear method.  The
     * level is held by the SOURCE (Cypress 8259 output mirrored into
     * DRIR<55> by TsunamiChipset::evalDeviceIrqs) and falls when the PIC
     * acknowledges / the guest ISR services the device.  Per HRM 6.3.1
     * the device class is b_irq<1>; mask split mirrors AXPBox
     * System.cpp:1783 (0x00ffffffffffffff device / 0xfc... error).
     * Bits 57:56 belong to neither class on this board -- excluded.
     */
    inline bool pendingIrq1(int cpuId) const noexcept
    {
        constexpr uint64_t kDeviceClassMask = 0x00FF'FFFF'FFFF'FFFFULL;
        uint64_t const drir = m_drir.load(std::memory_order_relaxed);
        uint64_t const dim  = m_dim[cpuId & (kMaxCPUs - 1)]
                                  .load(std::memory_order_relaxed);
        return ((drir & dim) & kDeviceClassMask) != 0;
    }

    /**
     * @brief Query whether MISC<NXM> is currently latched.
     *
     * Used by the bus arbiter (TsunamiChipset::reportNxm) to apply HRM
     * sticky semantics: a second NXM event while MISC<NXM> is set is
     * silently absorbed (no extra latch transition, no extra IRQ pulse).
     * Firmware W1C of MISC<NXM> clears the latch and re-arms the path.
     */
    inline bool miscNxmLatched() const noexcept
    {
        using Tsunami21272::Spec::Cchip::MISC::NXM;
        using Tsunami21272::Spec::mask;
        return (m_misc.load(std::memory_order_acquire) & mask(NXM)) != 0;
    }


    /**
     * @brief Fire the interval timer for all enabled CPUs.
     *
     * For each CPU `n` in [0, m_cpuCount):
     *   - CAS-set MISC<ITINTR<4+n>> in m_misc using the same path
     *     miscWriteW1C() exercises (preserves W1C/W1S/RO invariants).
     *   - Set m_pendingIrq2[n] (the in-chip b_irq<2> latch).
     *
     * Called from Machine::run when `intervalTimerShouldFire(cycleCount)`
     * (chipsetLib/CchipIntervalTimer.h) returns true.  Hot path is one
     * call per ~2^20 cycles at the ES45 profile, so the per-fire CAS
     * cost is amortized over ~1e6 retires.
     *
     * Phase C MVP fires unconditionally for every enabled CPU.  HRM
     * 6.3.2 IIC ignore-count semantics (fetch_sub per CPU; OF bit set
     * on underflow) are TODO(unwired) for Phase C+.
     */
    inline void fireIntervalTimer() noexcept
    {
        using Tsunami21272::Spec::Cchip::MISC::ITINTR;
        using Tsunami21272::Spec::mask;

        // ITINTR field is 4 bits at lsb=4 (one bit per CPU 0..3).
        // For each enabled CPU, OR-set the corresponding bit into m_misc
        // using a tight CAS loop.  Concurrent miscWriteW1C() writers are
        // tolerated because both paths use compare_exchange_weak.
        for (int n = 0; n < m_cpuCount && n < kMaxCPUs; ++n) {
            // TODO(unwired): HRM 6.3.2 IIC ignore-count.  Phase C+ should
            //                read m_iic[n], fetch_sub on the ICNT field
            //                (bits 23:0), and skip this CPU's assert if
            //                the prior count was non-zero.  Set IIC.OF
            //                (bit 24) on underflow.
            uint64_t const bitMask = uint64_t{1} << (ITINTR.lsb + n);
            uint64_t old = m_misc.load(std::memory_order_acquire);
            while (!m_misc.compare_exchange_weak(
                       old, old | bitMask,
                       std::memory_order_acq_rel,
                       std::memory_order_acquire)) {
                // CAS lost; retry with refreshed `old`.
            }
            m_pendingIrq2[n].store(true, std::memory_order_release);
        }
    }

    // ------------------------------------------------------------------------
    // latchNxm -- hardware-set MISC<NXM> and lock MISC<NXS> (source).
    // ------------------------------------------------------------------------
    // Called by the bus arbiter (TsunamiChipset::reportNxm) when a physical
    // access decodes to no claimant.  NXM is a W1C bit: hardware sets it here,
    // firmware later writes 1 to clear it.  NXS is locked at the 0->1 edge of
    // NXM and is UNPREDICTABLE while NXM == 0 (HRM 10.2.x); we only deposit it
    // on the edge so a second NXM before a clear preserves the first source.
    // CAS loop matches fireIntervalTimer so concurrent miscWriteW1C() is safe.
    inline void latchNxm(unsigned srcCode) noexcept
    {
        using Tsunami21272::Spec::Cchip::MISC::NXM;
        using Tsunami21272::Spec::Cchip::MISC::NXS;
        using Tsunami21272::Spec::mask;
        using Tsunami21272::Spec::deposit;

        uint64_t old = m_misc.load(std::memory_order_acquire);
        for (;;) {
            uint64_t next = old | mask(NXM);
            if ((old & mask(NXM)) == 0) {
                next = deposit(next, NXS, srcCode & 0x7u);
            }
            if (m_misc.compare_exchange_weak(old, next,
                    std::memory_order_acq_rel, std::memory_order_acquire))
                break;
        }
        // Phase B-NXMA (2026-05-28): mirror MISC<NXM> into DRIR<63> so the
        // unified b_irq<0> arbitration in Machine::run can observe the NXM
        // event via pendingIrq0(cpuId) without needing a separate query
        // path.  DRIR<63> de-assertion is paired in miscWriteW1C() below
        // when firmware W1C's MISC<NXM>.  Release ordering pairs with the
        // relaxed load in pendingIrq0().
        m_drir.fetch_or(uint64_t{1} << 63, std::memory_order_release);
    }

    // ========================================================================
    // MMIO Read Handler
    // ========================================================================

    static uint64_t mmioRead(void* ctx, uint64_t offset, uint8_t width) noexcept
    {
        return static_cast<TsunamiCchip*>(ctx)->read(offset);
    }

    // ------------------------------------------------------------------
    // Phase B: read(offset) is now a delegator into the canonical
    // cpuId-aware path.  Existing callers (trace/diagnostic, the
    // static mmioRead() handler that has no CPU context today) get
    // cpuId = -1, which read(offset, -1) treats as "no CPU context"
    // and returns CPUID = 0 in the MISC slot per HRM convention.
    //
    // Phase C threads the originating CPU through TsunamiChipset's
    // dispatcher and calls read(offset, cpuId) directly.
    // ------------------------------------------------------------------
    inline uint64_t read(uint64_t offset) const noexcept
    {
        return read(offset, -1);
    }

    // ------------------------------------------------------------------
    // Phase B: canonical Cchip read path.  Every recognized case
    // narrates itself through CSR_LOG_R (compile-time gated by
    // EMULATR_CHIPSET_DIAG, runtime-mutable via g_csrDiagMuted).  The
    // cycleCount argument is 0 in Phase B -- Phase C wires the real
    // counter when Machine threads it through the dispatcher.
    // ------------------------------------------------------------------
    inline uint64_t read(uint64_t offset, int cpuId) const noexcept
    {
        using namespace Tsunami21272;

        // Phase B convention: cycleCount is 0 here; Phase C wires it.
        constexpr uint64_t kPhaseBNoCycle = 0;

        switch (offset)
        {
        // ------------------------------------------------------------------
        // RO-after-init storage (HRM tables 10-9 .. 10-15).  Phase B keeps
        // them whole-register reads; Phase B+ may pull individual fields
        // through CchipSpec::CSC::FieldSpec entries as needed.
        // ------------------------------------------------------------------
        case Cchip::CSC:
            CSR_LOG_R("Cchip", "CSC",  m_csc,  offset, cpuId, kPhaseBNoCycle);
            return m_csc;

        case Cchip::MTR:
            // TODO(unwired): SDRAM timing model.
            CSR_LOG_R("Cchip", "MTR",  m_mtr,  offset, cpuId, kPhaseBNoCycle);
            return m_mtr;

        case Cchip::MPD:
            // TODO(unwired): SPD pin discovery -- value pinned at 0xFF.
            CSR_LOG_R("Cchip", "MPD",  m_mpd,  offset, cpuId, kPhaseBNoCycle);
            return m_mpd;

        case Cchip::AAR0:
            CSR_LOG_R("Cchip", "AAR0", this->m_aar[0].base, offset, cpuId, kPhaseBNoCycle);
            return this->m_aar[0].base;
        case Cchip::AAR1:
            CSR_LOG_R("Cchip", "AAR1", m_aar[1].base, offset, cpuId, kPhaseBNoCycle);
            return m_aar[1].base;
        case Cchip::AAR2:
            CSR_LOG_R("Cchip", "AAR2", m_aar[2].base, offset, cpuId, kPhaseBNoCycle);
            return m_aar[2].base;
        case Cchip::AAR3:
            CSR_LOG_R("Cchip", "AAR3", m_aar[3].base, offset, cpuId, kPhaseBNoCycle);
            return m_aar[3].base;

        case Cchip::MPR0:
            // TODO(unwired): SDRAM mode-register programming.
            CSR_LOG_R("Cchip", "MPR0", m_mpr[0], offset, cpuId, kPhaseBNoCycle);
            return m_mpr[0];
        case Cchip::MPR1:
            // TODO(unwired): SDRAM mode-register programming.
            CSR_LOG_R("Cchip", "MPR1", m_mpr[1], offset, cpuId, kPhaseBNoCycle);
            return m_mpr[1];
        case Cchip::MPR2:
            // TODO(unwired): SDRAM mode-register programming.
            CSR_LOG_R("Cchip", "MPR2", m_mpr[2], offset, cpuId, kPhaseBNoCycle);
            return m_mpr[2];
        case Cchip::MPR3:
            // TODO(unwired): SDRAM mode-register programming.
            CSR_LOG_R("Cchip", "MPR3", m_mpr[3], offset, cpuId, kPhaseBNoCycle);
            return m_mpr[3];

        case Cchip::PRBEN:
            // TODO(unwired): probe-enable effect.
            CSR_LOG_R("Cchip", "PRBEN", m_prben, offset, cpuId, kPhaseBNoCycle);
            return m_prben;

        // ------------------------------------------------------------------
        // MISC -- Phase B spec-driven path.  CPUID injection on read per
        // HRM 10.2.2.3: bits [1:0] return the reader's CPU id.  When the
        // caller has no CPU context (cpuId < 0), bits [1:0] are zero.
        // ------------------------------------------------------------------
        case Cchip::MISC: {
            // `mask()` is a namespace-level helper in `Tsunami21272::Spec`,
            // not a member of the per-register namespace -- qualify with
            // `Spec::mask(...)`.
            uint64_t const raw    = m_misc.load(std::memory_order_acquire);
            uint64_t const cpuMsk = Spec::mask(Spec::Cchip::MISC::CPUID);
            uint64_t const cpuLsb = Spec::Cchip::MISC::CPUID.lsb;
            uint64_t const cpuField =
                (cpuId >= 0)
                    ? ((static_cast<uint64_t>(cpuId) << cpuLsb) & cpuMsk)
                    : uint64_t{0};
            uint64_t const value = (raw & ~cpuMsk) | cpuField;
            CSR_LOG_R("Cchip", "MISC", value, offset, cpuId, kPhaseBNoCycle);
            return value;
        }

        // ------------------------------------------------------------------
        // Interrupt mask / request -- atomic loads, combinational DIR.
        // TODO(unwired): b_irq<0/1> assert path on (DRIR & DIM) edge.
        // ------------------------------------------------------------------
        case Cchip::DRIR: {
            uint64_t const v = m_drir.load(std::memory_order_relaxed);
            CSR_LOG_R("Cchip", "DRIR", v, offset, cpuId, kPhaseBNoCycle);
            return v;
        }
        case Cchip::DIM0: {
            uint64_t const v = m_dim[0].load(std::memory_order_relaxed);
            CSR_LOG_R("Cchip", "DIM0", v, offset, cpuId, kPhaseBNoCycle);
            return v;
        }
        case Cchip::DIM1: {
            uint64_t const v = m_dim[1].load(std::memory_order_relaxed);
            CSR_LOG_R("Cchip", "DIM1", v, offset, cpuId, kPhaseBNoCycle);
            return v;
        }
        case Cchip::DIM2: {
            uint64_t const v = m_dim[2].load(std::memory_order_relaxed);
            CSR_LOG_R("Cchip", "DIM2", v, offset, cpuId, kPhaseBNoCycle);
            return v;
        }
        case Cchip::DIM3: {
            uint64_t const v = m_dim[3].load(std::memory_order_relaxed);
            CSR_LOG_R("Cchip", "DIM3", v, offset, cpuId, kPhaseBNoCycle);
            return v;
        }
        case Cchip::DIR0: {
            uint64_t const v = readDIR(0);
            CSR_LOG_R("Cchip", "DIR0", v, offset, cpuId, kPhaseBNoCycle);
            return v;
        }
        case Cchip::DIR1: {
            uint64_t const v = readDIR(1);
            CSR_LOG_R("Cchip", "DIR1", v, offset, cpuId, kPhaseBNoCycle);
            return v;
        }
        case Cchip::DIR2: {
            uint64_t const v = readDIR(2);
            CSR_LOG_R("Cchip", "DIR2", v, offset, cpuId, kPhaseBNoCycle);
            return v;
        }
        case Cchip::DIR3: {
            uint64_t const v = readDIR(3);
            CSR_LOG_R("Cchip", "DIR3", v, offset, cpuId, kPhaseBNoCycle);
            return v;
        }

        // ------------------------------------------------------------------
        // IPI registers -- atomic loads.
        // TODO(unwired): ignore-count decrement on interval-timer fire.
        // ------------------------------------------------------------------
        case Cchip::IIC0: {
            uint64_t const v = m_iic[0].load(std::memory_order_relaxed);
            CSR_LOG_R("Cchip", "IIC0", v, offset, cpuId, kPhaseBNoCycle);
            return v;
        }
        case Cchip::IIC1: {
            uint64_t const v = m_iic[1].load(std::memory_order_relaxed);
            CSR_LOG_R("Cchip", "IIC1", v, offset, cpuId, kPhaseBNoCycle);
            return v;
        }
        case Cchip::IIC2: {
            uint64_t const v = m_iic[2].load(std::memory_order_relaxed);
            CSR_LOG_R("Cchip", "IIC2", v, offset, cpuId, kPhaseBNoCycle);
            return v;
        }
        case Cchip::IIC3: {
            uint64_t const v = m_iic[3].load(std::memory_order_relaxed);
            CSR_LOG_R("Cchip", "IIC3", v, offset, cpuId, kPhaseBNoCycle);
            return v;
        }

        // ------------------------------------------------------------------
        // TIG (Phase B NEW): TTR / TDR storage-only.
        // TODO(unwired): TIGbus timing effect / data path.
        // ------------------------------------------------------------------
        case Cchip::TTR:
            CSR_LOG_R("Cchip", "TTR", m_ttr, offset, cpuId, kPhaseBNoCycle);
            return m_ttr;
        case Cchip::TDR:
            CSR_LOG_R("Cchip", "TDR", m_tdr, offset, cpuId, kPhaseBNoCycle);
            return m_tdr;

        default: {
            // Forensic: log unhandled Cchip read offsets so a missing register
            // model is visible during firmware bring-up.  Throttled stderr --
            // first 32 events loud, then a summary every 64K -- matching the
            // CboxEventLog / UnalignedEventLog policy elsewhere in V4.  See
            // memory note `project_idle_wait_interrupt_hypothesis.md` for the
            // 2026-05-13 investigation context that made this visibility
            // diagnostic suddenly load-bearing.  Phase B also routes the
            // event through the CSR_LOG_R sink so the chipset diagnostic
            // stream sees UNKNOWN accesses uniformly.
            CSR_LOG_R("Cchip", "UNKNOWN", 0, offset, cpuId, kPhaseBNoCycle);
#if EMULATR_BRINGUP_PROBES
            static std::atomic<uint64_t> s_cnt{ 0 };
            uint64_t const n = s_cnt.fetch_add(1, std::memory_order_relaxed);
            if (n < 32) {
                std::fprintf(stderr,
                             "TsunamiCchip: UNKNOWN READ offset=0x%08llx "
                             "(event %llu)\n",
                             static_cast<unsigned long long>(offset),
                             static_cast<unsigned long long>(n));
                std::fflush(stderr);
            } else if ((n & 0xFFFFu) == 0) {
                std::fprintf(stderr,
                             "TsunamiCchip: %llu unknown reads "
                             "(loud-stderr muted past 32)\n",
                             static_cast<unsigned long long>(n + 1));
                std::fflush(stderr);
            }
#endif
            return 0;
        }
        }
    }

    // ========================================================================
    // MMIO Write Handler
    // ========================================================================

    static void mmioWrite(void* ctx, uint64_t offset, uint64_t value, uint8_t width) noexcept
    {
        static_cast<TsunamiCchip*>(ctx)->write(offset, value);
    }

    // ------------------------------------------------------------------
    // Phase B: write(offset, value) is now a delegator into the
    // canonical cpuId-aware path.  See read() above for the rationale.
    // ------------------------------------------------------------------
    void write(uint64_t offset, uint64_t value) noexcept
    {
        write(offset, value, -1);
    }

    // ------------------------------------------------------------------
    // Phase B: canonical Cchip write path.  MISC routes through
    // miscWriteW1C() for spec-driven W1C/W1S/WO/RO behavior.  Every
    // other case is whole-register store (or RO/ignored) with a
    // CSR_LOG_W call against the spec name.
    // ------------------------------------------------------------------
    void write(uint64_t offset, uint64_t value, int cpuId) noexcept
    {
        using namespace Tsunami21272;

        constexpr uint64_t kPhaseBNoCycle = 0;

        switch (offset)
        {
        // ------------------------------------------------------------------
        // MISC -- Phase B spec-driven W1C/W1S/WO/RO via miscWriteW1C().
        // ------------------------------------------------------------------
        case Cchip::MISC:
            CSR_LOG_W("Cchip", "MISC", value, offset, cpuId, kPhaseBNoCycle);
            miscWriteW1C(value, cpuId);
            break;

        // ------------------------------------------------------------------
        // PRBEN -- whole-register RW; probe-enable effect not wired.
        // TODO(unwired): probe-enable applies to cache-coherence traffic.
        // ------------------------------------------------------------------
        case Cchip::PRBEN:
            CSR_LOG_W("Cchip", "PRBEN", value, offset, cpuId, kPhaseBNoCycle);
            m_prben = value;
            break;

        // ------------------------------------------------------------------
        // Interrupt-mask writes -- atomic store, no IRQ edge propagation.
        // TODO(unwired): b_irq<0/1> assert/deassert on (DRIR & DIM) edge.
        // ------------------------------------------------------------------
        case Cchip::DIM0:
            CSR_LOG_W("Cchip", "DIM0", value, offset, cpuId, kPhaseBNoCycle);
            m_dim[0].store(value, std::memory_order_relaxed);
            break;
        case Cchip::DIM1:
            CSR_LOG_W("Cchip", "DIM1", value, offset, cpuId, kPhaseBNoCycle);
            m_dim[1].store(value, std::memory_order_relaxed);
            break;
        case Cchip::DIM2:
            CSR_LOG_W("Cchip", "DIM2", value, offset, cpuId, kPhaseBNoCycle);
            m_dim[2].store(value, std::memory_order_relaxed);
            break;
        case Cchip::DIM3:
            CSR_LOG_W("Cchip", "DIM3", value, offset, cpuId, kPhaseBNoCycle);
            m_dim[3].store(value, std::memory_order_relaxed);
            break;

        // ------------------------------------------------------------------
        // IPI writes -- store via sendIPI helper; no b_irq<3> assert.
        // TODO(unwired): IIC ignore-count decrement on timer fire; IPI
        // delivery edge to target CPU.
        // ------------------------------------------------------------------
        case Cchip::IIC0:
            CSR_LOG_W("Cchip", "IIC0", value, offset, cpuId, kPhaseBNoCycle);
            sendIPI(0, value);
            break;
        case Cchip::IIC1:
            CSR_LOG_W("Cchip", "IIC1", value, offset, cpuId, kPhaseBNoCycle);
            sendIPI(1, value);
            break;
        case Cchip::IIC2:
            CSR_LOG_W("Cchip", "IIC2", value, offset, cpuId, kPhaseBNoCycle);
            sendIPI(2, value);
            break;
        case Cchip::IIC3:
            CSR_LOG_W("Cchip", "IIC3", value, offset, cpuId, kPhaseBNoCycle);
            sendIPI(3, value);
            break;

        // ------------------------------------------------------------------
        // TIG (Phase B NEW): TTR / TDR storage-only.
        // TODO(unwired): TIGbus timing programming / data path.
        // ------------------------------------------------------------------
        case Cchip::TTR:
            CSR_LOG_W("Cchip", "TTR", value, offset, cpuId, kPhaseBNoCycle);
            m_ttr = value;
            break;
        case Cchip::TDR:
            CSR_LOG_W("Cchip", "TDR", value, offset, cpuId, kPhaseBNoCycle);
            m_tdr = value;
            break;

        // ------------------------------------------------------------------
        // RO-from-software registers -- writes ignored per HRM (CSC, DIR).
        // The ignored writes are still logged through CSR_LOG_W so the
        // diagnostic stream shows the attempt.  CSC writes during PAL
        // diagnostics are normal.
        // ------------------------------------------------------------------
        case Cchip::CSC:
            CSR_LOG_W("Cchip", "CSC(ignored)", value, offset, cpuId, kPhaseBNoCycle);
            break;

        // ------------------------------------------------------------------
        // AAR0-3 -- RW per HRM Tables 10-14 (Tsunami) / 10-15 (Typhoon).
        // Firmware PROGRAMS the memory array sizes during memory init, so
        // writes must stick; reset() seeds the power-on defaults SROM would
        // have programmed.  Stored raw (house style for PRBEN/TTR; reserved-
        // bit RAZ masking is a future refinement).
        // ------------------------------------------------------------------
        case Cchip::AAR0:
            CSR_LOG_W("Cchip", "AAR0", value, offset, cpuId, kPhaseBNoCycle);
            m_aar[0].base = value;
            m_aar[0].enabled = (value != 0); // Implicit enable if base is non-zero
            // Note: Mask usually persists from reset or is set via a separate CSR 
            // offset defined in the HRM. If your model doesn't have a 
            // separate AAM (Address Array Mask) register, update it here.
            break;
        case Cchip::AAR1:
            CSR_LOG_W("Cchip", "AAR1", value, offset, cpuId, kPhaseBNoCycle);
            m_aar[1].base = value;
            m_aar[1].enabled = (value != 0);
            break;

        case Cchip::AAR2:
            CSR_LOG_W("Cchip", "AAR2", value, offset, cpuId, kPhaseBNoCycle);
            m_aar[2].base = value;
            m_aar[2].enabled = (value != 0);
            break;

        case Cchip::AAR3:
            CSR_LOG_W("Cchip", "AAR3", value, offset, cpuId, kPhaseBNoCycle);
            m_aar[3].base = value;
            m_aar[3].enabled = (value != 0);
            break;
        case Cchip::DIR0:
            CSR_LOG_W("Cchip", "DIR0(ignored)", value, offset, cpuId, kPhaseBNoCycle);
            break;
        case Cchip::DIR1:
            CSR_LOG_W("Cchip", "DIR1(ignored)", value, offset, cpuId, kPhaseBNoCycle);
            break;
        case Cchip::DIR2:
            CSR_LOG_W("Cchip", "DIR2(ignored)", value, offset, cpuId, kPhaseBNoCycle);
            break;
        case Cchip::DIR3:
            CSR_LOG_W("Cchip", "DIR3(ignored)", value, offset, cpuId, kPhaseBNoCycle);
            break;
        case Cchip::DRIR:
            CSR_LOG_W("Cchip", "DRIR(ignored)", value, offset, cpuId, kPhaseBNoCycle);
            break;
        case Cchip::MPR0:
            CSR_LOG_W("Cchip", "MPR0(ignored)", value, offset, cpuId, kPhaseBNoCycle);
            break;
        case Cchip::MPR1:
            CSR_LOG_W("Cchip", "MPR1(ignored)", value, offset, cpuId, kPhaseBNoCycle);
            break;
        case Cchip::MPR2:
            CSR_LOG_W("Cchip", "MPR2(ignored)", value, offset, cpuId, kPhaseBNoCycle);
            break;
        case Cchip::MPR3:
            CSR_LOG_W("Cchip", "MPR3(ignored)", value, offset, cpuId, kPhaseBNoCycle);
            break;
        case Cchip::MPD:
            CSR_LOG_W("Cchip", "MPD(ignored)", value, offset, cpuId, kPhaseBNoCycle);
            break;
        case Cchip::MTR:
            CSR_LOG_W("Cchip", "MTR(ignored)", value, offset, cpuId, kPhaseBNoCycle);
            break;

        default: {
            // Forensic: log unhandled Cchip write offsets, same throttled
            // posture as the read-side block above.  Writes include the
            // value field so the slug of "write to ghost register" attempts
            // is interpretable without cross-referencing other logs.  Phase
            // B also routes the event through the CSR_LOG_W sink.
            CSR_LOG_W("Cchip", "UNKNOWN", value, offset, cpuId, kPhaseBNoCycle);
#if EMULATR_BRINGUP_PROBES
            static std::atomic<uint64_t> s_cnt{ 0 };
            uint64_t const n = s_cnt.fetch_add(1, std::memory_order_relaxed);
            if (n < 32) {
                std::fprintf(stderr,
                             "TsunamiCchip: UNKNOWN WRITE offset=0x%08llx "
                             "value=0x%016llx (event %llu)\n",
                             static_cast<unsigned long long>(offset),
                             static_cast<unsigned long long>(value),
                             static_cast<unsigned long long>(n));
                std::fflush(stderr);
            } else if ((n & 0xFFFFu) == 0) {
                std::fprintf(stderr,
                             "TsunamiCchip: %llu unknown writes "
                             "(loud-stderr muted past 32)\n",
                             static_cast<unsigned long long>(n + 1));
                std::fflush(stderr);
            }
#endif
            break;
        }
        }
    }

    // ========================================================================
    // Snapshot serialize / deserialize
    // ========================================================================
    // Captures every storage field touched after construction.  Variant
    // and cpuCount are NOT written here -- they come from the chipset
    // wrapper's header and are cross-checked before deserialize() runs.
    // Atomic fields are read with relaxed ordering (single-thread save
    // path) and restored with relaxed ordering on the load path.

    void serialize(QDataStream& ds) const noexcept
    {
        // Phase B: m_misc is now std::atomic<uint64_t> -- on-the-wire
        // bytes unchanged (one uint64_t), just an atomic load.
        // kChipsetVersion 2 (2026-06-05): TTR/TDR added to the wire --
        // the format-version bump promised at Phase B happened (the
        // interrupt-chain device serialization; see Snapshot.h).
        ds << m_csc << m_mtr
           << m_misc.load(std::memory_order_relaxed)
           << m_mpd << m_prben
           << m_ttr << m_tdr;
        for (int i = 0; i < 4; ++i) {
            // Write the three members
            ds << m_aar[i].base;
            ds << m_aar[i].mask;

            // Cast to quint8 and write to the stream
            quint8 enabledByte = m_aar[i].enabled ? 1 : 0;
            ds << enabledByte;
        }
        for (int i = 0; i < kMaxCPUs; ++i) ds << m_mpr[i];

        ds << m_drir.load(std::memory_order_relaxed);
        for (int i = 0; i < kMaxCPUs; ++i)
            ds << m_dim[i].load(std::memory_order_relaxed);
        for (int i = 0; i < kMaxCPUs; ++i)
            ds << m_iic[i].load(std::memory_order_relaxed);
    }

    void deserialize(QDataStream& ds) noexcept
    {
        // Phase B: m_misc is now std::atomic<uint64_t>; read into a
        // scratch uint64_t and store atomically.
        // kChipsetVersion 2 (2026-06-05): TTR/TDR now on the wire.
        uint64_t miscBuf = 0;
        ds >> m_csc >> m_mtr >> miscBuf >> m_mpd >> m_prben
           >> m_ttr >> m_tdr;
        m_misc.store(miscBuf, std::memory_order_relaxed);
        for (int i = 0; i < 4; ++i) {
            // Read the three members that make up your AAR struct
            ds >> m_aar[i].base;
            ds >> m_aar[i].mask;

            // bools can be tricky with QDataStream; casting to quint8 is safer
            quint8 enabledByte;
            ds >> enabledByte;
            m_aar[i].enabled = (enabledByte != 0);
        }
        for (int i = 0; i < kMaxCPUs; ++i) ds >> m_mpr[i];

        uint64_t v = 0;
        ds >> v;
        m_drir.store(v, std::memory_order_relaxed);
        for (int i = 0; i < kMaxCPUs; ++i) {
            ds >> v;
            m_dim[i].store(v, std::memory_order_relaxed);
        }
        for (int i = 0; i < kMaxCPUs; ++i) {
            ds >> v;
            m_iic[i].store(v, std::memory_order_relaxed);
        }
    }

private:
    // ========================================================================
    // Phase B: MISC write helper -- spec-driven W1C / W1S / WO / RO.
    // ========================================================================
    //
    // Composed semantics from Tsunami21272::Spec::Cchip::MISC:
    //
    //   W1C_MASK  -- NXM | IPINTR<11:8> | ITINTR<7:4>.  Writing 1 to
    //                a bit in this set clears the matching stored bit.
    //   W1S_MASK  -- ABT<23:20> | ABW<19:16>.  Writing 1 to a bit in
    //                this set sets the matching stored bit.  ABW also
    //                has a lock-on-first-set semantic (HRM 10.2.2.3
    //                paragraph 4) -- TODO(unwired): Phase C wires the
    //                lock; Phase B treats ABW as plain W1S.
    //   WO_MASK   -- DEVSUP<43:40> | ACL<24> | IPREQ<15:12>.  These
    //                are pure-action fields; their write value drives
    //                a side effect (TIG poll suppression, ABT/ABW
    //                clear, IPI request) without being stored.  Phase
    //                B does NOT execute the side effects (storage-pure
    //                pass); each carries a TODO(unwired) hook below.
    //   RO_MASK   -- REV | NXS | CPUID.  Writes to these bits are
    //                discarded; the stored value is preserved.
    //
    // The CAS loop preserves bits not in any of W1C/W1S/WO/RO, applies
    // the W1C clear and W1S set as a single transformation, then
    // compare-exchanges the new value.  Contention is rare today
    // (single emulated CPU), and the loop is forward-compatible with
    // SMP.
    // ========================================================================
    void miscWriteW1C(uint64_t writeVal, int /*cpuId*/) noexcept
    {
        using namespace Tsunami21272::Spec::Cchip;
        using Tsunami21272::Spec::mask;

        uint64_t old = m_misc.load(std::memory_order_acquire);
        for (;;) {
            // Clear: bits the writer set inside W1C clear their match
            // in the stored value.
            uint64_t const clearBits = writeVal & MISC::W1C_MASK;
            // Set: bits the writer set inside W1S set their match.
            uint64_t const setBits   = writeVal & MISC::W1S_MASK;
            // RO bits stay at their old value; WO bits do not persist
            // (their side effects are dispatched below; storage stays
            // at the old value for those bit positions); everything
            // else is unchanged from old.
            // RO bits are disjoint from W1C/W1S/WO per HRM 10-12; the
            // `(old & ~clearBits)` term preserves them by construction.
            // MBZ/reserved bits are likewise outside any mask and stay
            // at their old value.
            uint64_t stagedW1CW1S = (old & ~clearBits) | setBits;

            // ----------------------------------------------------------
            // 2026-05-30: HRM 12.2 Cchip Firmware Initialization Sequence
            // -- ABT[n] -> ABW[n] auto-promotion on uncontended win.
            // ----------------------------------------------------------
            // Per HRM 12.2 step 4: after a CPU W1S's MISC<ABT[n]> and
            // issues a memory barrier, it reads MISC<ABW>.  If the bit
            // corresponding to its CPU number is set, that CPU won the
            // arbitration and proceeds with init; otherwise it waits
            // forever for an IPI from the winning CPU.  In V4-shallow
            // (single CPU 0), there is no contender, so CPU 0 always
            // wins; the Cchip itself auto-promotes ABT[0] -> ABW[0]
            // in the same atomic CAS cycle.  Without this, the SRM
            // observed (2026-05-30 VS2022 session) was spinning at
            // PC 0x7B4B0 .. 0x7B720 waiting for an IPI from a winning
            // CPU that doesn't exist.
            //
            // Multi-CPU semantics (Typhoon, not modeled in V4-shallow):
            // if multiple ABT bits are set concurrently, all of them
            // would win here -- HRM-strict arbitration would gate on
            // a hardware decision; we approximate first-come-first-
            // served by treating "no prior ABT bits set" as the win
            // gate.  Revisit when V4 adds SMP.
            uint64_t const abtMask = mask(MISC::ABT);
            uint64_t const abwMask = mask(MISC::ABW);
            uint64_t const oldAbt     = old & abtMask;
            uint64_t const stagedAbt  = stagedW1CW1S & abtMask;
            uint64_t const newlyTried = stagedAbt & ~oldAbt;
            if (newlyTried != 0 && oldAbt == 0) {
                // No prior contender; this set of tries wins.  Promote
                // each ABT[n] bit to its matching ABW[n] bit.  ABT is
                // at bits 20-23, ABW at 16-19 -- right-shift by 4 lands
                // each ABT[n] on its corresponding ABW[n] position.
                stagedW1CW1S |= (newlyTried >> 4) & abwMask;
            }

            // ----------------------------------------------------------
            // 2026-05-30: HRM 12.2 -- ACL clear.
            // ----------------------------------------------------------
            // Writing 1 to MISC<ACL> (bit 24, WO per spec) clears all
            // stored ABT and ABW bits.  ACL itself doesn't persist
            // (WO bits are masked out by the WO_MASK term below),
            // but its arbitration-reset side effect on ABT/ABW does.
            // This is how firmware releases the arbitration lock once
            // its init sequence is complete.
            if ((writeVal & mask(MISC::ACL)) != 0) {
                stagedW1CW1S &= ~(abtMask | abwMask);
            }

            // ----------------------------------------------------------
            // 2026-06-18: HRM 10.2.2.3 -- IPREQ -> IPINTR set.
            // ----------------------------------------------------------
            // Writing 1 to MISC<IPREQ<12+n>> (WO) "sets the corresponding
            // bit in the IPINTR" -- UNCONDITIONALLY (HRM verbatim).  IPREQ
            // (15:12) maps to IPINTR (11:8): the SAME >>4 shift distance as
            // the ABT->ABW promote above, but deliberately WITHOUT its
            // `oldAbt == 0` arbitration gate -- an IPI set must always
            // re-assert, even while a prior IPINTR bit is still pending.
            // IPREQ is WO (in WO_MASK) so it does not persist; only this
            // IPINTR side effect does.  The b_irq<3> latch assert is hooked
            // after the CAS, below (mirrors the ITINTR/m_pendingIrq2 split).
            uint64_t const ipreqSet = writeVal & mask(MISC::IPREQ);
            if (ipreqSet != 0) {
                stagedW1CW1S |= (ipreqSet >> 4) & mask(MISC::IPINTR);
            }

            uint64_t const newVal =
                (stagedW1CW1S & ~MISC::WO_MASK) | (old & MISC::WO_MASK);
            if (m_misc.compare_exchange_weak(old, newVal,
                                             std::memory_order_acq_rel,
                                             std::memory_order_acquire)) {
                break;
            }
            // CAS lost; retry with refreshed `old`.
        }

        // -----------------------------------------------------------------
        // Side-effect hooks.
        // -----------------------------------------------------------------
        //
        // Phase C wires the ITINTR -> b_irq<2> deassert side of the
        // interval-timer story: when firmware W1C's a MISC<ITINTR<4+n>>
        // bit, the matching m_pendingIrq2[n] latch is also cleared so
        // the divert poll in Machine::run sees a quiescent state.  This
        // mirrors the HRM "ITINTR drives b_irq<2>" semantic (HRM
        // 10.2.2.3 ITINTR description + HRM 6.3.2 interval-timer block).
        //
        // The clear is defensive/idempotent because Machine::run already
        // cleared m_pendingIrq2 at divert time; this path keeps the
        // model consistent with HRM storage semantics when firmware
        // takes a non-divert code path that touches MISC.
        // -----------------------------------------------------------------
        uint64_t const itintrClears = writeVal & mask(MISC::ITINTR);
        if (itintrClears != 0) {
            // ITINTR is a 4-bit field at lsb=4.  Bit (4+n) corresponds
            // to CPU n; iterate and clear the per-CPU latch for any bit
            // the writer set.
            for (int n = 0; n < kMaxCPUs; ++n) {
                uint64_t const cpuBit = uint64_t{1} << (MISC::ITINTR.lsb + n);
                if ((itintrClears & cpuBit) != 0) {
                    clearPendingIrq2(n);
                }
            }
        }

        // -----------------------------------------------------------------
        // Phase B-NXMA (2026-05-28): NXM W1C -> DRIR<63> de-assert.
        // -----------------------------------------------------------------
        // When firmware W1C's MISC<NXM>, the CAS above has already cleared
        // the bit in m_misc.  Mirror that clear into m_drir bit 63 so the
        // b_irq<0> arbitration in Machine::run sees the source de-asserted
        // on the next poll.  Pairs with the fetch_or in latchNxm() above;
        // release ordering pairs with the relaxed loads in pendingIrq0().
        //
        // Defensive idempotent: if firmware W1Cs an already-clear NXM bit,
        // this is a no-op fetch_and(~0xFFFFFFFFFFFFFFFF).  Harmless.
        if ((writeVal & mask(MISC::NXM)) != 0) {
            m_drir.fetch_and(~(uint64_t{1} << 63),
                             std::memory_order_release);
        }

        // The remaining side-effect hooks stay TODO(unwired) for Phase
        // C+ / Phase D.  `mask()` is a Tsunami21272::Spec namespace-level
        // helper, not a per-register member -- qualify as `Spec::mask(...)`
        // when wiring these in.
        //
        // 2026-05-30: MISC::ACL clear + ABT/ABW arbitration auto-promotion
        // are now WIRED inside the CAS loop above (HRM 12.2 Cchip Firmware
        // Initialization Sequence).  2026-06-18: the IPI side effects are now
        // WIRED below.  Side-effect dispatch for the remaining
        // device-suppression bit stays TODO.
        //
        // -----------------------------------------------------------------
        // 2026-06-18: IPI delivery side effects (mirror the ITINTR/b_irq<2>
        // split above).  The IPREQ->IPINTR storage set already happened in
        // the CAS loop; here we drive the per-CPU b_irq<3> latch.
        // -----------------------------------------------------------------
        // IPREQ write -> assert b_irq<3> for each targeted CPU.  SET loop is
        // bounded by m_cpuCount (exactly like fireIntervalTimer) so a stray
        // write to a non-configured target bit cannot leave an unpolled
        // latch stuck -- Machine::run polls only configured CPUs.
        uint64_t const ipreqFire = writeVal & mask(MISC::IPREQ);
        if (ipreqFire != 0) {
            for (int n = 0; n < m_cpuCount && n < kMaxCPUs; ++n) {
                uint64_t const reqBit = uint64_t{1} << (MISC::IPREQ.lsb + n);
                if ((ipreqFire & reqBit) != 0) {
                    m_pendingIrq3[n].store(true, std::memory_order_release);
                }
            }
        }
        // IPINTR W1C -> deassert b_irq<3>.  The CAS already cleared the
        // stored IPINTR bit (it is in W1C_MASK); this drops the matching
        // latch.  kMaxCPUs bound is fine here -- clearing a non-configured
        // CPU's latch is harmless (mirrors the ITINTR clear loop above).
        uint64_t const ipintrClears = writeVal & mask(MISC::IPINTR);
        if (ipintrClears != 0) {
            for (int n = 0; n < kMaxCPUs; ++n) {
                uint64_t const cpuBit = uint64_t{1} << (MISC::IPINTR.lsb + n);
                if ((ipintrClears & cpuBit) != 0) {
                    clearPendingIrq3(n);
                }
            }
        }

        // TODO(unwired): if (writeVal & Spec::mask(MISC::DEVSUP)) bits
        //                set, suppress device IRQ (irq<1>) until next
        //                TIG poll completion for matching cpus.
    }

    // ========================================================================
    // AAR computation
    // ========================================================================

    //# ============================================================================
    //# 
    //    # HRM Table 10 - 14 AAR layout(Tsunami) :
    //#   [34:24]  ADDR    -- Base physical address bits[34:24]
    //#   [23:17]  Reserved (0)
    //#   [16]     DBG     -- Debug port enable
    //#   [15:12]  ASIZ    -- Array size encoding:
    //        #                      0000 = disabled    0100 = 128MB
    //        #                      0001 = 16MB        0101 = 256MB
    //        #                      0010 = 32MB        0110 = 512M
    //        #                      0011 = 64MB        0111 = 1GB
    //        #   [11:9]   Reserved (0)
    //        #   [8]      SA      -- Split array
    //        #   [7:4]    Reserved (0)
    //        #   [3:2]    ROWS    -- SDRAM row bits (0=11, 1=12, 2=13)
    //        #   [1:0]    BNKS    -- SDRAM bank bits (0=1, 1=2)
    //        #
    //        # HRM Table 10 - 15 AAR layout(Typhoon) -- same but :
    //        #   ASIZ adds : 1000 = 2GB, 1001 = 4GB, 1010 = 8GB
    //        #   ADDR extends to bits[34:24](full 35 - bit PA)
    //        #
    //        # ============================================================================

    static uint64_t computeAAR(uint64_t baseAddr, uint64_t sizeBytes,
        bool isTyphoon = false) noexcept
    {
        if (sizeBytes == 0) return 0;  // disabled

        // ADDR[34:24] -- base address in 16MB units
        const uint64_t addr = (baseAddr >> 24) & 0x7FFULL;  // bits[34:24]

        // ASIZ[15:12] -- encoded array size
        uint8_t asiz = 0;
        if (sizeBytes >= 16ULL * 1024 * 1024 && sizeBytes < 32ULL * 1024 * 1024) asiz = 0x1;
        else if (sizeBytes >= 32ULL * 1024 * 1024 && sizeBytes < 64ULL * 1024 * 1024) asiz = 0x2;
        else if (sizeBytes >= 64ULL * 1024 * 1024 && sizeBytes < 128ULL * 1024 * 1024) asiz = 0x3;
        else if (sizeBytes >= 128ULL * 1024 * 1024 && sizeBytes < 256ULL * 1024 * 1024) asiz = 0x4;
        else if (sizeBytes >= 256ULL * 1024 * 1024 && sizeBytes < 512ULL * 1024 * 1024) asiz = 0x5;
        else if (sizeBytes >= 512ULL * 1024 * 1024 && sizeBytes < 1ULL * 1024 * 1024 * 1024) asiz = 0x6;
        else if (sizeBytes >= 1ULL * 1024 * 1024 * 1024 && sizeBytes < 2ULL * 1024 * 1024 * 1024) asiz = 0x7;
        else if (isTyphoon && sizeBytes >= 2ULL * 1024 * 1024 * 1024 && sizeBytes < 4ULL * 1024 * 1024 * 1024) asiz = 0x8;
        else if (isTyphoon && sizeBytes >= 4ULL * 1024 * 1024 * 1024 && sizeBytes < 8ULL * 1024 * 1024 * 1024) asiz = 0x9;
        else if (isTyphoon && sizeBytes >= 8ULL * 1024 * 1024 * 1024) asiz = 0xA;
        else asiz = 0x7;  // clamp to 1GB for Tsunami

        // ROWS[3:2] = 2 (13 rows, standard for modern SDRAM)
        // BNKS[1:0] = 1 (2 banks, standard)
        const uint8_t rows = 2;
        const uint8_t bnks = 1;

        return (addr << 24)
            | (static_cast<uint64_t>(asiz) << 12)
            | (static_cast<uint64_t>(rows) << 2)
            | static_cast<uint64_t>(bnks);
    }

    // ------------------------------------------------------------------------
    // decodeAAR -- inverse of computeAAR.  Recovers the linear DRAM window
    // [base, base + size) from an encoded AAR register word.  The encoded
    // word (as stored in AAR.base and returned over the CSR path) is the
    // single source of truth; address routing decodes it here rather than
    // reinterpreting the encoded bit-field as a linear address.
    //
    //   ADDR[34:24] -> base = ADDR << 24            (16 MB-aligned)
    //   ASIZ[15:12] -> size = 16 MB << (asiz - 1)   (asiz 0 == disabled)
    // ------------------------------------------------------------------------
    struct AarWindow { uint64_t base; uint64_t size; };
    static AarWindow decodeAAR(uint64_t reg) noexcept
    {
        const uint64_t base = ((reg >> 24) & 0x7FFULL) << 24;
        const uint8_t  asiz = static_cast<uint8_t>((reg >> 12) & 0xFULL);
        const uint64_t size = (asiz == 0)
            ? 0
            : ((16ULL * 1024 * 1024) << (asiz - 1));
        return { base, size };
    }

    // ========================================================================
    // Configuration (immutable after construction)
    // ========================================================================
    ChipsetVariant  m_variant;
    int             m_cpuCount;
    uint64_t         m_memSizeBytes;

    // ========================================================================
    // Read-only after init
    // ========================================================================
    uint64_t m_csc;
    uint64_t m_mtr;
    uint64_t m_mpd;
    uint64_t m_prben;
    uint64_t m_mpr[kMaxCPUs];

    // ========================================================================
    // Phase B: TIGbus timing / data registers -- storage only.
    // HRM 10.2.2.14 (TTR) and 10.2.2.15 (TDR).  No TIGbus side effects
    // wired today; see file-header TODO Register Table.
    // ========================================================================
    uint64_t m_ttr = 0;   // TODO(unwired): TIGbus timing
    uint64_t m_tdr = 0;   // TODO(unwired): TIGbus data

    // ========================================================================
    // Shared mutable (atomic)
    // ========================================================================
    // m_misc moved here from "Read-only after init" group in Phase B.
    // HRM 10.2.2.3 defines MISC as a mixed-semantic register (W1C, W1S,
    // WO, RO, MBZ, DYN) -- the CAS-loop W1C path in miscWriteW1C()
    // requires single-word atomicity across writers.  Single-CPU
    // emulator today contends rarely; structure is forward-compatible
    // with SMP.
    std::atomic<uint64_t> m_misc{ 0 };
    std::atomic<uint64_t> m_drir{ 0 };


    // AAR configuration: These represent the physical base and mask
    // for DRAM arrays, configured by SRM during boot.
    struct AAR {
        uint64_t base; // Physical base address (PA bits [34:24])
        uint64_t mask; // Size mask (ASIZ encoding)
        bool     enabled;
    };

    std::array<AAR, 4> m_aar;

    // ========================================================================
    // Phase C: per-CPU b_irq<2> latch (interval-timer pending edge).
    // ========================================================================
    //
    // One std::atomic<bool> per CPU.  Set by fireIntervalTimer() when
    // the corresponding MISC<ITINTR<4+n>> bit is asserted; cleared
    // either by clearPendingIrq2(n) at divert time in Machine::run, or
    // by miscWriteW1C() when firmware W1C's MISC<ITINTR<4+n>>.  Edge-
    // acknowledged semantic: the divert acks the edge so subsequent
    // pump iterations do not re-divert; firmware's W1C is a defensive
    // idempotent clear so the latch tracks MISC<ITINTR> storage.
    //
    // Atomic-bool rather than packing into m_misc bits because:
    //   - the latch is queried per retire in the hot run loop;
    //     a dedicated atomic load is cheaper than masking MISC.
    //   - clearPendingIrq2() avoids touching m_misc, sidestepping the
    //     CAS contention path through miscWriteW1C().
    //   - matches the "in-Cchip b_irq<2> latch" abstraction in the
    //     HRM (Section 6.3, the timer pin enters Cchip and latches
    //     independent of MISC storage; MISC is the SOFTWARE-visible
    //     mirror).
    //
    // Snapshot: NOT serialized.  Transient hardware-edge state -- a
    // snapshot taken mid-divert and resumed should rely on the PAL
    // handler's W1C to leave a quiescent state, not on replaying the
    // pending edge.  Reset clears all entries to false.
    // ========================================================================
    std::array<std::atomic<bool>, kMaxCPUs> m_pendingIrq2{};

    // ========================================================================
    // Per-CPU b_irq<3> latch (interprocessor-interrupt pending edge).
    // ========================================================================
    // Direct analog of m_pendingIrq2 for the IPI path: set when firmware
    // writes MISC<IPREQ<12+n>> (which also sets MISC<IPINTR<8+n>>), cleared
    // by clearPendingIrq3(n) at divert time in Machine::run or by
    // miscWriteW1C() when firmware W1C's MISC<IPINTR<8+n>>.  NOT serialized
    // (transient edge state); reset clears all entries to false.  Mirrors the
    // already-wired ITINTR/b_irq<2> timer path.  Wired 2026-06-18.
    // ========================================================================
    std::array<std::atomic<bool>, kMaxCPUs> m_pendingIrq3{};

    // ========================================================================
    // Per-CPU registers (atomic)
    // ========================================================================
    std::atomic<uint64_t> m_dim[kMaxCPUs]{ {0}, {0}, {0}, {0} };
    std::atomic<uint64_t> m_iic[kMaxCPUs]{ {0}, {0}, {0}, {0} };
};

#endif // TSUNAMI_CCHIP_H
