// ============================================================================
// deviceLib/Tsunami/PciConfigSpace.h -- reusable PCI config-space mechanics
// ============================================================================
// Project: EmulatR -- Alpha AXP / EV6 Architecture Emulator (V4)
// Copyright (C) 2025, 2026 eNVy Systems, Inc.  All rights reserved.
// Licensed under eNVy Systems Non-Commercial License v1.1
//
// Project Architect: Timothy Peer
// AI Collaboration:  Claude (Anthropic)
// Contact:        peert@envysys.com  |  https://envysys.com
// ============================================================================
//
// FILE: deviceLib/Tsunami/PciConfigSpace.h  (NEW, Phase 2A)
// FUNCTION: PciConfigSpace (whole file)
// CHANGE: Identity-INDEPENDENT 256-byte PCI config-space store shared by the
//   Tsunami IDE controllers (Cypress + ALi M5229).  Each controller seeds the
//   reset bytes + a per-byte writable mask; this class does width-correct reads
//   and mask-driven writes.  Byte granularity (writable 0xFF / RO 0x00) exactly
//   reproduces the old Cy82C693Ide continue-guard; finer per-bit masks can be
//   added if a consumer ever needs them.
// ============================================================================

#ifndef DEVICELIB_TSUNAMI_PCICONFIGSPACE_H
#define DEVICELIB_TSUNAMI_PCICONFIGSPACE_H

#include <array>
#include <cstddef>
#include <cstdint>

class PciConfigSpace
{
public:
    PciConfigSpace() noexcept { m_bytes.fill(0); m_wmask.fill(0); }

    // ---- seeding (controller ctor) ----------------------------------------
    void fillZero() noexcept        { m_bytes.fill(0); m_wmask.fill(0); }
    void setAllWritable() noexcept  { m_wmask.fill(0xFFu); }
    void seedByte(uint8_t off, uint8_t val) noexcept { m_bytes[off] = val; }
    void setWritable(uint8_t off) noexcept { m_wmask[off] = 0xFFu; }
    void setReadOnly(uint8_t off) noexcept { m_wmask[off] = 0x00u; }
    void setReadOnlyRange(uint8_t lo, uint8_t hi) noexcept {
        for (int o = lo; o <= hi; ++o) m_wmask[static_cast<size_t>(o)] = 0x00u;
    }

    // ---- config cycles -----------------------------------------------------
    [[nodiscard]] uint32_t read(uint8_t reg, uint8_t width) const noexcept {
        uint32_t v = 0;
        for (int i = 0; i < width && (reg + i) < 256; ++i)
            v |= static_cast<uint32_t>(m_bytes[static_cast<size_t>(reg + i)]) << (8 * i);
        return v;
    }
    void write(uint8_t reg, uint32_t value, uint8_t width) noexcept {
        for (int i = 0; i < width && (reg + i) < 256; ++i) {
            size_t  const off = static_cast<size_t>(reg + i);
            uint8_t const m   = m_wmask[off];
            uint8_t const in  = static_cast<uint8_t>((value >> (8 * i)) & 0xFFu);
            m_bytes[off] = static_cast<uint8_t>((m_bytes[off] & ~m) | (in & m));
        }
    }
    [[nodiscard]] uint8_t byte(uint8_t off) const noexcept { return m_bytes[off]; }

private:
    std::array<uint8_t, 256> m_bytes{};   // config-space image
    std::array<uint8_t, 256> m_wmask{};   // per-byte writable mask (0xFF RW / 0x00 RO)
};

#endif // DEVICELIB_TSUNAMI_PCICONFIGSPACE_H
