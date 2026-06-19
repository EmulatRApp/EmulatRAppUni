// ============================================================================
// TitanPchip.h -- DECchip 21274 "Titan" PA-chip (dual G/A port + AGP)
// ============================================================================
// Project: ASA-EMulatR -- Alpha AXP / EV6 Architecture Emulator (V4)
// Copyright (C) 2025, 2026 eNVy Systems, Inc. All rights reserved.
// Licensed under eNVy Systems Non-Commercial License v1.1
//
// Project Architect: Timothy Peer
// AI Code Generation: Claude (Anthropic)
// ============================================================================
//
// SCOPE / STATUS  (read before trusting -- this is the new-silicon delta)
//   This models the Titan PA-chip CSR REGISTER FILE: the two ports per
//   PA-chip (G = PCI, A = AGP), each with 4 DMA windows (WSBA/WSM/TBA),
//   a port-control register (GPCTL/APCTL), and the Titan error set
//   (SERR/GPERR/APERR/AGPERR).  This is the part of Titan that genuinely
//   differs from the 21272 single-port Pchip.
//
//   It does NOT (yet) re-implement PCI dense/sparse MEM, IO, or config-cycle
//   routing or the on-board device registry -- that machinery is identical
//   to the 21272 and lives in TsunamiPchip.h.  The intended composition is:
//   TitanChipset owns the Titan CSR ports (this file) for hose 0..3 and
//   DELEGATES PCI MEM/IO/config to the existing Tsunami Pchip routing per
//   hose.  Marked TODO(titan-pci-route) below.  Verified pieces are cited
//   [K-C]/[K-S]; unverified are marked TODO-verify.
//
// PROVENANCE: Titan21274_CsrSpec.h (kernel core_titan.h/.c, ES45 Appendix D).
// ============================================================================

#ifndef TITAN_PCHIP_H
#define TITAN_PCHIP_H

#include <cstdint>
#include <cstring>
#include "TsunamiVariant.h"        // ChipsetVariant
#include "Titan21274_CsrSpec.h"

// ----------------------------------------------------------------------------
// One PA-chip PORT (G or A).  64 quadword-aligned CSR slots (0x40 stride).
// We back the port with a flat 0x1000-byte slot array indexed by offset>>6
// for any slot we do not special-case, and give named accessors to the
// architecturally meaningful registers.
// ----------------------------------------------------------------------------
class TitanPachipPort {
public:
    enum class Kind { G, A };

    explicit TitanPachipPort(Kind kind) noexcept : m_kind(kind) { reset(); }

    Kind kind() const noexcept { return m_kind; }
    bool isAgp() const noexcept { return m_kind == Kind::A; }

    // Reset to the post-SROM window programming the kernel expects to find.
    // [K-S] titan_init_one_pachip_port.  W0 SG 8MB@8MB, W1 direct 1GB@2GB,
    // W2 SG 1GB@3GB, W3 disabled.  Monster Window enabled in PCTL.
    void reset() noexcept {
        std::memset(m_slot, 0, sizeof(m_slot));
        using namespace Titan21274;
        setReg(Port::WSBA0, Window::kSgIsaBase  | Port::kWsbaEna | Port::kWsbaSg);
        setReg(Port::WSM0,  (Window::kSgIsaSize - 1) & 0xFFF00000ULL);
        setReg(Port::WSBA1, Window::kDirectMapBase | Port::kWsbaEna);
        setReg(Port::WSM1,  (Window::kDirectMapSize - 1) & 0xFFF00000ULL);
        setReg(Port::WSBA2, Window::kSgPciBase | Port::kWsbaEna | Port::kWsbaSg);
        setReg(Port::WSM2,  (Window::kSgPciSize - 1) & 0xFFF00000ULL);
        setReg(Port::WSBA3, 0);
        // Monster Window (DAC pci64) enabled by the kernel for both ports.
        uint64_t pctl = Port::kPctlMwin | Port::kPctlArbena;
        // Advertise AGP present on the A-port so titan_query_agp() succeeds.
        if (isAgp()) pctl |= Port::kApctlAgpPresent;   // TODO-verify reset value
        setReg(Port::PCTL, pctl);
    }

    // Quadword CSR read/write at a PORT-relative offset (0..0xFFF).
    uint64_t read(uint64_t portOff) const noexcept { return reg(portOff); }

    void write(uint64_t portOff, uint64_t val) noexcept {
        using namespace Titan21274;
        // TLB-invalidate registers are write-triggers (value latched, action
        // is a no-op in a non-caching SG model). [K-S] titan_pci_tbi.
        // Error *SET registers are W1S onto the matching error reg; *EN are
        // plain RW masks.  For the scaffold we store-through; refine W1C/W1S
        // semantics when the PAL error path is exercised. TODO-verify.
        setReg(portOff, val);
    }

    // Named conveniences (used by TitanChipset DMA translation if/when wired).
    uint64_t wsba(int w) const noexcept { return reg(Titan21274::Port::WSBA0 + uint64_t(w & 3) * 0x40); }
    uint64_t wsm (int w) const noexcept { return reg(Titan21274::Port::WSM0  + uint64_t(w & 3) * 0x40); }
    uint64_t tba (int w) const noexcept { return reg(Titan21274::Port::TBA0  + uint64_t(w & 3) * 0x40); }
    uint64_t pctl()      const noexcept { return reg(Titan21274::Port::PCTL); }

private:
    uint64_t reg(uint64_t off) const noexcept {
        uint64_t i = off >> 6;
        return (i < kSlots) ? m_slot[i] : 0xFFFFFFFFFFFFFFFFULL;
    }
    void setReg(uint64_t off, uint64_t v) noexcept {
        uint64_t i = off >> 6;
        if (i < kSlots) m_slot[i] = v;
    }

    static constexpr uint64_t kSlots = 0x1000 / 0x40; // 64 quadword slots
    Kind     m_kind;
    uint64_t m_slot[kSlots] = {};
};

// ----------------------------------------------------------------------------
// One PA-chip = G-port (PCI) at +0x000 and A-port (AGP) at +0x1000.
// ----------------------------------------------------------------------------
class TitanPachip {
public:
    TitanPachip() noexcept : m_g(TitanPachipPort::Kind::G), m_a(TitanPachipPort::Kind::A) {}

    void reset() noexcept { m_g.reset(); m_a.reset(); }

    TitanPachipPort&       gPort()       noexcept { return m_g; }
    const TitanPachipPort& gPort() const noexcept { return m_g; }
    TitanPachipPort&       aPort()       noexcept { return m_a; }
    const TitanPachipPort& aPort() const noexcept { return m_a; }

    // PA-chip-relative CSR access (offset 0..0x1FFF): <0x1000 = G, else A.
    uint64_t read(uint64_t pachipOff) const noexcept {
        return (pachipOff < Titan21274::Port::kPortStride)
            ? m_g.read(pachipOff)
            : m_a.read(pachipOff - Titan21274::Port::kPortStride);
    }
    void write(uint64_t pachipOff, uint64_t val) noexcept {
        if (pachipOff < Titan21274::Port::kPortStride)
            m_g.write(pachipOff, val);
        else
            m_a.write(pachipOff - Titan21274::Port::kPortStride, val);
    }

private:
    TitanPachipPort m_g;
    TitanPachipPort m_a;
};

// ----------------------------------------------------------------------------
// TitanPchip -- the two PA-chips (pachip0 always present; pachip1 optional,
// presence advertised via Cchip CSC<14>).  This is the CSR surface only;
// PCI MEM/IO/config routing is delegated by TitanChipset (see SCOPE note).
// ----------------------------------------------------------------------------
class TitanPchip {
public:
    explicit TitanPchip(ChipsetVariant variant, bool pachip1Present = true) noexcept
        : m_variant(variant), m_pachip1Present(pachip1Present) { reset(); }

    void reset() noexcept { m_pachip[0].reset(); m_pachip[1].reset(); }
    ChipsetVariant variant() const noexcept { return m_variant; }
    bool pachip1Present() const noexcept { return m_pachip1Present; }

    TitanPachip&       pachip(int n)       noexcept { return m_pachip[n & 1]; }
    const TitanPachip& pachip(int n) const noexcept { return m_pachip[n & 1]; }

    // CSR read/write addressed by ABSOLUTE physical address.  Decodes which
    // PA-chip (Base::kPachip0 @ 0x801_8000_0000, Base::kPachip1 @
    // 0x803_8000_0000) then which port/register.  Returns false if PA is not
    // within either PA-chip CSR block (caller falls through to PCI routing).
    bool csrRead(uint64_t pa, uint64_t& out) const noexcept {
        int n; uint64_t off;
        if (!decode(pa, n, off)) return false;
        if (n == 1 && !m_pachip1Present) { out = 0xFFFFFFFFFFFFFFFFULL; return true; }
        out = m_pachip[n].read(off);
        return true;
    }
    bool csrWrite(uint64_t pa, uint64_t val) noexcept {
        int n; uint64_t off;
        if (!decode(pa, n, off)) return false;
        if (n == 1 && !m_pachip1Present) return true; // absorbed
        m_pachip[n].write(off, val);
        return true;
    }

private:
    // Each PA-chip CSR block spans the two ports = 0x2000 bytes from its base.
    static constexpr uint64_t kPachipCsrSpan = 2 * Titan21274::Port::kPortStride; // 0x2000
    bool decode(uint64_t pa, int& nOut, uint64_t& offOut) const noexcept {
        using namespace Titan21274;
        if (pa >= Base::kPachip0 && pa < Base::kPachip0 + kPachipCsrSpan) {
            nOut = 0; offOut = pa - Base::kPachip0; return true;
        }
        if (pa >= Base::kPachip1 && pa < Base::kPachip1 + kPachipCsrSpan) {
            nOut = 1; offOut = pa - Base::kPachip1; return true;
        }
        return false;
    }

    ChipsetVariant m_variant;
    bool           m_pachip1Present;
    TitanPachip    m_pachip[2];

    // TODO(titan-pci-route): PCI dense/sparse MEM, IO, and Type-0/1 config
    // cycles per hose are identical to the 21272 -- delegate to a
    // TsunamiPchip-style registry from TitanChipset, or embed one here.
};

#endif // TITAN_PCHIP_H
