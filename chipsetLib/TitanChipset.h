// ============================================================================
// TitanChipset.h -- DECchip 21274 "Titan" chipset (adjacent to TsunamiChipset)
// ============================================================================
// Project: ASA-EMulatR -- Alpha AXP / EV6 Architecture Emulator (V4)
// Copyright (C) 2025, 2026 eNVy Systems, Inc. All rights reserved.
// Licensed under eNVy Systems Non-Commercial License v1.1
//
// Project Architect: Timothy Peer
// AI Code Generation: Claude (Anthropic)
// ============================================================================
//
// STATUS: SCAFFOLD (2026-06-16).  Compiles against the same sub-chip classes
// as TsunamiChipset, but is NOT yet a drop-in boot target.  See the
// "FAITHFUL vs STUBBED" map in journals/20260616_titan_21274_interface.md and
// the per-section TODOs below.  Build/iterate handoff is to the MSVC/Qt
// project (this file was authored without a local compile).
//
// DESIGN -- why this is small even though Titan is "a new chipset":
//   Titan shares the top-level PA map and the Cchip/Dchip/TIG register
//   offsets with the 21272 (see Titan21274_CsrSpec.h).  The 21272 model in
//   this project already carries the 4-CPU DIM/DIR Cchip.  Therefore Titan
//   REUSES TsunamiCchip / TsunamiDchip / TsunamiTig unchanged, and the only
//   genuinely-new silicon is the dual G/A-port PA-chip + AGP, modeled in
//   TitanPchip.h.  The device/platform layer (ISA bridge, UARTs, IDE, RTC,
//   IIC, PIC, flash) is PC264-derived and identical; the intended final form
//   composes those exactly as TsunamiChipset::wireDevices() does.
//
//   Two honest divergences vs Tsunami the integrator must finish:
//     1. PCI MEM/IO/config routing per hose (4 hoses on Titan vs Tsunami's
//        Pchip0/1 halves) -- TODO(titan-pci-route).
//     2. ISystemBus (read/write/fetch arbiter) + full device wiring -- this
//        scaffold exposes mmioRead/mmioWrite CSR routing only; the arbiter
//        should mirror TsunamiChipset 1:1.  TODO(titan-isystembus).
// ============================================================================

#ifndef TITAN_CHIPSET_H
#define TITAN_CHIPSET_H

#include <cstdint>
#include <string>
#include "TsunamiVariant.h"
#include "Titan21274_CsrSpec.h"
#include "TitanPchip.h"
#include "TsunamiCchip.h"   // reused unchanged: 4-CPU DIM/DIR, MISC, NXM, IPI
#include "TsunamiDchip.h"   // reused unchanged
#include "TsunamiTig.h"     // reused unchanged: smir/halt/ipcr (the halt-button path)

// NOTE: when promoted past scaffold, derive from memoryLib::ISystemBus and
// add the GuestMemory + device members exactly as TsunamiChipset does.
class TitanChipset {
public:
    explicit TitanChipset(const std::string& model,
                          int cpuCount = 4,
                          uint64_t memSizeBytes = 0x800000000ULL) noexcept
        : m_variant(ChipsetVariant::Titan)
        , m_model(model)
        , m_cpuCount(cpuCount)
        , m_cchip(ChipsetVariant::Titan, cpuCount, memSizeBytes)
        , m_dchip(ChipsetVariant::Titan, memSizeBytes)
        , m_pchip(ChipsetVariant::Titan, /*pachip1Present=*/cpuCount > 1)
        , m_tig()
    {
        // Reflect pachip1 presence into Cchip CSC<14> so titan_init_pachips()
        // discovers the right number of hoses. [K-S]
        // TODO(titan-csc): expose a Cchip CSC setter; TsunamiCchip currently
        // computes CSC internally.  For now this is advisory.
        reset();
    }

    void reset() noexcept {
        m_cchip.reset();
        m_dchip.reset();
        m_pchip.reset();
        m_tig.reset();
    }

    ChipsetVariant     variant() const noexcept { return m_variant; }
    const std::string& model()   const noexcept { return m_model; }
    int                cpuCount()const noexcept { return m_cpuCount; }

    TsunamiCchip& cchip() noexcept { return m_cchip; }
    TsunamiDchip& dchip() noexcept { return m_dchip; }
    TitanPchip&   pchip() noexcept { return m_pchip; }
    TsunamiTig&   tig()   noexcept { return m_tig; }

    // ------------------------------------------------------------------
    // MMIO CSR routing (full PA in).  Handles the four chipset CSR blocks:
    // Cchip, Dchip, and the two PA-chips' CSR ports.  PCI MEM/IO/config and
    // the TIG-bus flash window are TODO(titan-pci-route)/delegated.
    // ------------------------------------------------------------------
    uint64_t mmioRead(uint64_t pa, uint8_t width, int cpuId = 0) noexcept {
        using namespace Titan21274;
        // Cchip CSR block (0x801_A000_0000 .. +0x10000000 region; registers
        // are 64B-aligned within).  Reuse the 21272 Cchip read(off, cpuId).
        if (inBlock(pa, Base::kCchip))
            return m_cchip.read(pa - Base::kCchip, cpuId);
        if (inBlock(pa, Base::kDchip & ~0xFFFULL))   // Dchip block (mask the +0x800)
            return m_dchip.read(pa - (Base::kDchip & ~0xFFFULL));
        // PA-chip CSR ports.
        uint64_t v;
        if (m_pchip.csrRead(pa, v)) return v;
        // TODO(titan-pci-route): per-hose PCI MEM/IO/IACK/config + TIG flash.
        // TODO(titan-isystembus): delegate to the reused Tsunami routing.
        (void)width;
        return 0xFFFFFFFFFFFFFFFFULL; // unclaimed float
    }

    void mmioWrite(uint64_t pa, uint64_t value, uint8_t width, int cpuId = 0) noexcept {
        using namespace Titan21274;
        if (inBlock(pa, Base::kCchip)) { m_cchip.write(pa - Base::kCchip, value, cpuId); return; }
        if (inBlock(pa, Base::kDchip & ~0xFFFULL)) { m_dchip.write(pa - (Base::kDchip & ~0xFFFULL), value); return; }
        if (m_pchip.csrWrite(pa, value)) return;
        // TODO(titan-pci-route) / TODO(titan-isystembus)
        (void)width;
    }

    // Cross-chip interrupt promotion -- Titan has 4 hoses (pachip0/1 x G/A);
    // map each hose's INTx onto DRIR bits.  Convention parallels the Tsunami
    // pciIntxToDrirBit (docs/hrm_deviations.md): hose h, line l -> 32+4h+l.
    // 4 hoses * 4 INTx = DRIR[47:32]; DRIR<63> reserved for error/NXM.
    static constexpr int pciIntxToDrirBit(int hose, int intxLine) noexcept {
        return 32 + (hose * 4) + (intxLine & 0x3);
    }
    void raisePciInterrupt(int hose, int intxLine) noexcept {
        m_cchip.assertInterrupt(pciIntxToDrirBit(hose, intxLine));
    }
    void lowerPciInterrupt(int hose, int intxLine) noexcept {
        m_cchip.deassertInterrupt(pciIntxToDrirBit(hose, intxLine));
    }

private:
    // A chipset CSR block is 256 MB wide in the PA map (matches the 21272
    // kCchip_CSR_End - kCchip_CSR span); registers live in the low part.
    static constexpr uint64_t kCsrBlockSpan = 0x10000000ULL; // 256 MB
    static constexpr bool inBlock(uint64_t pa, uint64_t base) noexcept {
        return pa >= base && pa < base + kCsrBlockSpan;
    }

    ChipsetVariant m_variant;
    std::string    m_model;
    int            m_cpuCount;

    TsunamiCchip   m_cchip;   // reused (4-CPU, offset-compatible)
    TsunamiDchip   m_dchip;   // reused
    TitanPchip     m_pchip;   // NEW: dual G/A-port PA-chips + AGP
    TsunamiTig     m_tig;     // reused (halt-button / smir / ipcr path)

    // TODO(titan-isystembus): GuestMemory m_guestMemory; device members
    // (Cy82C693IsaBridge, Uart16550 x2, Cy82C693Ide, ToyRtc, IicPcf8584,
    // Pic8259Pair, FlashRom) + wireDevices() + read/write/fetch arbiter,
    // mirrored 1:1 from TsunamiChipset.  Reuse, do not re-invent.
};

#endif // TITAN_CHIPSET_H
