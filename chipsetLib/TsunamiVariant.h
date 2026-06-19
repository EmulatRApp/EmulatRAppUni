// ============================================================================
// TsunamiVariant.h -- Tsunami/Typhoon/Titan Chipset Variant Selection
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
//   Defines the chipset variant enum and provides model-to-chipset
//   mapping for Alpha AXP platforms.
//
//   CHIPSET TAXONOMY (corrected 2026-06-16 -- see Processor Support/
//   REFERENCE_INDEX.md sec 3.1 and journals/20260616_titan_21274_interface.md):
//     - 21272 = Tsunami.  "Typhoon" is the higher-bandwidth (dual-Dchip)
//       *variant of the 21272*, NOT a separate part.  Used: DS10/DS20/ES40.
//     - 21274 = Titan.  A SEPARATE chipset: dual discrete PA-chips, each with
//       a G-port (PCI) and an A-port (AGP); richer error register set.  Shares
//       the top-level PA map with the 21272 (Cchip 0x801_A000_0000,
//       Pchip0 0x801_8000_0000, Pchip1 0x803_8000_0000, hose stride h<<33).
//       Used: DS15/DS25/ES45.
//
//   PRIOR BUG (now corrected): the Typhoon enum was labelled "21274" and
//   DS25/ES45 were mapped to it -- those are really Titan (21274).  The Titan
//   path is implemented in Titan21274_CsrSpec.h / TitanPchip.h / TitanChipset.h,
//   adjacent to the Tsunami model.
//
//   Tsunami, Typhoon and Titan share the CSR address map and MMIO routing.
//   They differ in: chip revision ID (DREV), max memory, extended register
//   fields, AGP (Titan only), and the Pchip port structure (Titan = dual
//   G/A port).  The variant is determined by the machine model, latched at
//   construction, immutable for the chipset's lifetime.
//
// MODEL-TO-CHIPSET MAPPING:
//   DS10   -> Tsunami (21272)
//   DS20   -> Tsunami (21272)
//   ES40   -> Tsunami (21272)   [Typhoon-capable high-bw 21272 in HW]
//   DS15   -> Titan   (21274)
//   DS25   -> Titan   (21274)
//   ES45   -> Titan   (21274)
//   GS80   -> Wildfire (not supported -- future)
//   GS160  -> Wildfire (not supported -- future)
//   GS320  -> Wildfire (not supported -- future)
//
// ============================================================================

#ifndef TSUNAMI_VARIANT_H
#define TSUNAMI_VARIANT_H
#include <cctype>
#include <cstdint>
#include <string>
#include <algorithm>


// ============================================================================
// ChipsetVariant -- Tsunami-family chipset identification
// ============================================================================

enum class ChipsetVariant : uint8_t
{
    Tsunami = 0,    // 21272 -- DS10, DS20, ES40
    Typhoon = 1,    // 21272 high-bandwidth (dual-Dchip) variant -- ES40-class
    Titan   = 2,    // 21274 -- DS15, DS25, ES45 (dual G/A-port PA-chips + AGP)
    Unknown = 0xFF
};

// NOTE: adding Titan means any `switch (ChipsetVariant)` in the *Tsunami*
// CSR spec (Tsunami21272_CsrSpec.h) that lacks a Titan arm will take its
// `default`.  The Titan chipset path uses Titan21274_CsrSpec.h instead, so the
// Tsunami spec only needs a Titan arm if a Titan instance is ever built through
// the Tsunami classes (it is not).  Verify at compile time.

// ============================================================================
// Variant properties
// ============================================================================

struct ChipsetVariantInfo
{
    ChipsetVariant variant;
    const char*    chipName;        // "Tsunami" / "Typhoon" / "Titan"
    const char*    chipId;          // "21272" or "21274"
    uint64_t      drev;            // DREV register value
    uint8_t         crev;            // Cchip revision (MISC[39:32])
    int            maxCpus;         // max CPUs supported
    uint64_t        maxMemBytes;     // max physical memory
    int            maxPciHoses;     // max PCI hose count
};

// ============================================================================
// Variant lookup tables
// ============================================================================
inline constexpr ChipsetVariantInfo kTsunamiInfo = {
    ChipsetVariant::Tsunami,
    "Tsunami", "21272",
    0x10,           // DREV
    1,              // CREV (HRM: Tsunami = 1)
    4,
    0x100000000ULL,  // 4 GB max DRAM (Tsunami: ASIZ 0x7 = 1GB x 4 arrays)
    2
};

inline constexpr ChipsetVariantInfo kTyphoonInfo = {
    ChipsetVariant::Typhoon,
    "Typhoon", "21272",   // CORRECTED: Typhoon is a 21272 variant, not 21274
    0x20,           // DREV
    8,              // CREV (HRM: Typhoon = 8)
    4,
    0x800000000ULL,  // 32 GB max DRAM (Typhoon: ASIZ 0xA = 8GB x 4 arrays)
    2
};

// Titan (21274) -- DS15/DS25/ES45.  drev/crev are PLACEHOLDERS pending the
// 21274 Engineering Spec Rev 0.12 DREV/MISC.REV values (TODO-verify); they
// only feed the Dchip DREV and Cchip MISC<REV> reset values, which firmware
// reads loosely.  Max 32 GB (ES45); maxPciHoses = 4 (pachip0/1 x G/A ports).
inline constexpr ChipsetVariantInfo kTitanInfo = {
    ChipsetVariant::Titan,
    "Titan", "21274",
    0x22,           // DREV   (TODO-verify against 21274 Eng Spec)
    0x0A,           // CREV   (TODO-verify)
    4,
    0x800000000ULL,  // 32 GB max DRAM (ES45)
    4               // 4 hoses: pachip0.G(0) pachip1.G(1) pachip0.A(2) pachip1.A(3)
};

// ============================================================================
// Model-to-variant mapping
// ============================================================================

/**
 * @brief Determine chipset variant from machine model string
 * @param model  Machine model from INI (e.g., "ES40", "ES45")
 * @return       ChipsetVariant (Tsunami, Titan, or Unknown)
 *
 * The mapping is fixed by the Alpha platform architecture:
 *   DS10, DS20, ES40       -> Tsunami (21272)
 *   DS15, DS25, ES45       -> Titan   (21274)
 *   GS80, GS160, GS320    -> Unknown (Wildfire -- not supported)
 *
 * 2026-06-16: DS25/ES45 corrected from Typhoon to Titan (21274); DS15 added.
 */
inline ChipsetVariant variantFromModel(const std::string& model) noexcept
{
    std::string m = model;
    m.erase(0, m.find_first_not_of(" \t"));
    m.erase(m.find_last_not_of(" \t") + 1);
    std::ranges::transform(m, m.begin(), ::toupper);

    if (m == "DS10" || m == "DS20" || m == "ES40")
        return ChipsetVariant::Tsunami;
    if (m == "DS15" || m == "DS25" || m == "ES45")
        return ChipsetVariant::Titan;
    return ChipsetVariant::Unknown;
}

/**
 * @brief Get variant info struct for a given variant
 * @param v  ChipsetVariant
 * @return   Pointer to static info struct, or nullptr for Unknown
 */
inline const ChipsetVariantInfo* variantInfo(ChipsetVariant v) noexcept
{
    switch (v) {
    case ChipsetVariant::Tsunami: return &kTsunamiInfo;
    case ChipsetVariant::Typhoon: return &kTyphoonInfo;
    case ChipsetVariant::Titan:   return &kTitanInfo;
    default:                      return nullptr;
    }
}

#endif // TSUNAMI_VARIANT_H
