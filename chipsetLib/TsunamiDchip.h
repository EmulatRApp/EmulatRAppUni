// ============================================================================
// TsunamiDchip.h -- Tsunami/Typhoon Dchip (DRAM Controller)
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
//   Emulates the Tsunami/Typhoon Dchip -- the DRAM system controller.
//   Variant-aware: returns correct DREV for Tsunami (21272) vs Typhoon (21274).
//
// MMIO ADDRESS:
//   Base PA: 0x801.B000.0000 (fixed, same for both variants)
//   Size:    256 MB (0x1000.0000)
//
// REGISTER MAP:
//   Offset    Name     Description
//   0x0800    DSC      DRAM System Configuration
//   0x0840    STR      Stripe Register
//   0x0880    DREV     Dchip Revision (variant-dependent)
//   0x08C0    DSC2     Extended DRAM System Configuration
//
// REFERENCES:
//   Tsunami/Typhoon Hardware Reference Manual (EC-RE2CA-TE)
//   Chapter 5: Dchip Registers
//
// ============================================================================
// TODO Register Table -- unwired or partially-wired behavior (Phase B)
// ============================================================================
// Phase B (2026-05-17) added uniform CSR diagnostic instrumentation
// against the Phase A spec scaffolding.  All four Dchip registers
// have storage; none have SDRAM-side behavior wired.  Each carries
// a `// TODO(unwired): ...` line comment at its decode site pointing
// back here.
//
//   DSC    -- storage RW; no DRAM-system-config side effect.  Wire
//             when memory training becomes load-bearing.
//   STR    -- storage RW; no striping behavior.  Wire when multi-
//             memory-bus striping becomes load-bearing.
//   DREV   -- storage RO; written once at reset from variantInfo.
//             Writes ignored per HRM.  No further wiring needed.
//   DSC2   -- storage RW; reserved-future per HRM.  Phase B leaves
//             writes pass-through with no side effect.
//
// ============================================================================
// CHANGE HISTORY
// ============================================================================
//
//   2026-05-17  Phase B uniform-CSR-surface refactor.  Surface: new
//               read(offset, cpuId) / write(offset, value, cpuId)
//               overloads (cpuId unused in Dchip; uniform surface
//               for Phase C/D dispatch parity).  Diagnostics: every
//               recognized case instruments CSR_LOG_R / CSR_LOG_W
//               against the spec name.  Storage unchanged from
//               Phase A.
//
// ============================================================================

#ifndef TSUNAMI_DCHIP_H
#define TSUNAMI_DCHIP_H

#include <QDataStream>
#include <atomic>
#include <cstdio>

#include "TsunamiVariant.h"

#include "Tsunami21272_RegisterMap.h"
#include "Tsunami21272_CsrSpec.h"   // Phase B: spec parity (no field decode yet)
#include "CsrDiag.h"                // Phase B: per-CSR-access diagnostic sink

class TsunamiDchip
{
public:
    // ========================================================================
    // Constants
    // ========================================================================
    static constexpr uint64_t kBasePA   = 0x801B0000000ULL;
    static constexpr uint64_t kSize     = 0x10000000ULL;     // 256 MB
    /*

    // ========================================================================
    // Register offsets
    // ========================================================================
    static constexpr uint64_t kDSC      = 0x0800;
    static constexpr uint64_t kSTR      = 0x0840;
    static constexpr uint64_t kDREV     = 0x0880;
    static constexpr uint64_t kDSC2     = 0x08C0;

    */
    // ========================================================================
    // Construction
    // ========================================================================

    /**
     * @brief Construct Dchip
     * @param variant      Chipset variant (determines DREV value)
     * @param memSizeBytes Total physical memory in bytes
     */
    explicit TsunamiDchip(ChipsetVariant variant = ChipsetVariant::Tsunami,
                          uint64_t memSizeBytes = 0x800000000ULL) noexcept
        : m_variant(variant)
        , m_memSizeBytes(memSizeBytes)
    {
        reset();
    }

    // ========================================================================
    // Reset
    // ========================================================================

    void reset() noexcept
    {
        m_dsc  = 0x01;      // DRAM present, single array
        m_str  = 0;         // no striping
        m_dsc2 = 0;         // default single-array config

        // DREV from variant info
        const auto* info = variantInfo(m_variant);
        m_drev = info ? info->drev : 0x10;

       /* INFO_LOG(QString("TsunamiDchip: reset -- %1, DREV=0x%2, %3 MB RAM")
            .arg(info ? info->chipName : "Unknown")
            .arg(m_drev, 2, 16, QChar('0'))
            .arg(m_memSizeBytes / (1024 * 1024)));
            */
    }

    // ========================================================================
    // Variant access
    // ========================================================================

    ChipsetVariant variant() const noexcept { return m_variant; }

    // ========================================================================
    // MMIO Handlers
    // ========================================================================

    static uint64_t mmioRead(void* ctx, uint64_t offset, uint8_t width) noexcept
    {
        return static_cast<TsunamiDchip*>(ctx)->read(offset);
    }

    // ------------------------------------------------------------------
    // Phase B: read(offset) is now a delegator into the canonical
    // cpuId-aware path.  Dchip ignores cpuId (no per-CPU registers),
    // but the uniform surface lets Phase C/D dispatch without a
    // signature mismatch against TsunamiCchip.
    // ------------------------------------------------------------------
    inline uint64_t read(uint64_t offset) const noexcept
    {
        return read(offset, -1);
    }

    inline uint64_t read(uint64_t offset, int cpuId) const noexcept
    {
        using namespace Tsunami21272;

        constexpr uint64_t kPhaseBNoCycle = 0;

        switch (offset)
        {
        case Dchip::DSC:
            // TODO(unwired): DRAM-system-config side effects.
            CSR_LOG_R("Dchip", "DSC",  m_dsc,  offset, cpuId, kPhaseBNoCycle);
            return m_dsc;
        case Dchip::STR:
            // TODO(unwired): striping behavior across memory buses.
            CSR_LOG_R("Dchip", "STR",  m_str,  offset, cpuId, kPhaseBNoCycle);
            return m_str;
        case Dchip::DREV:
            CSR_LOG_R("Dchip", "DREV", m_drev, offset, cpuId, kPhaseBNoCycle);
            return m_drev;
        case Dchip::DSC2:
            // TODO(unwired): reserved-future per HRM.
            CSR_LOG_R("Dchip", "DSC2", m_dsc2, offset, cpuId, kPhaseBNoCycle);
            return m_dsc2;
        default: {
            // Forensic: throttled stderr for unhandled Dchip reads.  See
            // TsunamiCchip.h's matching block for posture / rationale.
            CSR_LOG_R("Dchip", "UNKNOWN", 0, offset, cpuId, kPhaseBNoCycle);
#if EMULATR_BRINGUP_PROBES
            static std::atomic<uint64_t> s_cnt{ 0 };
            uint64_t const n = s_cnt.fetch_add(1, std::memory_order_relaxed);
            if (n < 32) {
                std::fprintf(stderr,
                             "TsunamiDchip: UNKNOWN READ offset=0x%08llx "
                             "(event %llu)\n",
                             static_cast<unsigned long long>(offset),
                             static_cast<unsigned long long>(n));
                std::fflush(stderr);
            } else if ((n & 0xFFFFu) == 0) {
                std::fprintf(stderr,
                             "TsunamiDchip: %llu unknown reads "
                             "(loud-stderr muted past 32)\n",
                             static_cast<unsigned long long>(n + 1));
                std::fflush(stderr);
            }
#endif
            return 0;
        }
        }
    }

    static void mmioWrite(void* ctx, uint64_t offset, uint64_t value, uint8_t width) noexcept
    {
        static_cast<TsunamiDchip*>(ctx)->write(offset, value);
    }

    void write(uint64_t offset, uint64_t value) noexcept
    {
        write(offset, value, -1);
    }

    void write(uint64_t offset, uint64_t value, int cpuId) noexcept
    {
        using namespace Tsunami21272;

        constexpr uint64_t kPhaseBNoCycle = 0;

        switch (offset)
        {
        case Dchip::DSC:
            CSR_LOG_W("Dchip", "DSC",  value, offset, cpuId, kPhaseBNoCycle);
            m_dsc = value;
            break;
        case Dchip::STR:
            CSR_LOG_W("Dchip", "STR",  value, offset, cpuId, kPhaseBNoCycle);
            m_str = value;
            break;
        case Dchip::DSC2:
            CSR_LOG_W("Dchip", "DSC2", value, offset, cpuId, kPhaseBNoCycle);
            m_dsc2 = value;
            break;
        case Dchip::DREV:
            // RO per HRM -- writes logged but discarded.
            CSR_LOG_W("Dchip", "DREV(ignored)", value, offset, cpuId, kPhaseBNoCycle);
            break;
        default: {
            // Forensic: throttled stderr for unhandled Dchip writes.
            CSR_LOG_W("Dchip", "UNKNOWN", value, offset, cpuId, kPhaseBNoCycle);
#if EMULATR_BRINGUP_PROBES
            static std::atomic<uint64_t> s_cnt{ 0 };
            uint64_t const n = s_cnt.fetch_add(1, std::memory_order_relaxed);
            if (n < 32) {
                std::fprintf(stderr,
                             "TsunamiDchip: UNKNOWN WRITE offset=0x%08llx "
                             "value=0x%016llx (event %llu)\n",
                             static_cast<unsigned long long>(offset),
                             static_cast<unsigned long long>(value),
                             static_cast<unsigned long long>(n));
                std::fflush(stderr);
            } else if ((n & 0xFFFFu) == 0) {
                std::fprintf(stderr,
                             "TsunamiDchip: %llu unknown writes "
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
    // Captures the four storage fields written after construction.
    // m_drev is included for completeness even though it is normally
    // RO after reset -- restoring the captured value matches what the
    // running emulator would observe via MFPR_DREV.

    void serialize(QDataStream& ds) const noexcept
    {
        ds << m_dsc << m_str << m_drev << m_dsc2;
    }

    void deserialize(QDataStream& ds) noexcept
    {
        ds >> m_dsc >> m_str >> m_drev >> m_dsc2;
    }

private:
    ChipsetVariant m_variant;
    uint64_t m_memSizeBytes;
    uint64_t m_dsc;
    uint64_t m_str;
    uint64_t m_drev;
    uint64_t m_dsc2;
};

#endif // TSUNAMI_DCHIP_H
