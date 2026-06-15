// ============================================================================
// TsunamiVariant.h -- Tsunami/Typhoon Chipset Variant Selection
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
//   Tsunami (21272) and Typhoon (21274) share the same CSR address
//   map, MMIO routing, and consumption model. They differ in:
//     - Chip revision ID (DREV register)
//     - Maximum memory capacity
//     - Extended register fields (Typhoon supports wider AAR, etc.)
//     - Interrupt line count
//
//   The variant is determined exclusively by the machine model.
//   It is latched at construction and immutable for the lifetime
//   of the chipset instance.
//
// MODEL-TO-CHIPSET MAPPING:
//   DS10   -> Tsunami (21272)
//   DS20   -> Tsunami (21272)
//   DS25   -> Typhoon (21274)
//   ES40   -> Tsunami (21272)
//   ES45   -> Typhoon (21274)
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
// ChipsetVariant -- Tsunami family chipset identification
// ============================================================================

enum class ChipsetVariant : uint8_t
{
    Tsunami = 0,    // 21272 -- DS10, DS20, ES40
    Typhoon = 1,    // 21274 -- ES45, DS25
    Unknown = 0xFF
};

// ============================================================================
// Variant properties
// ============================================================================

struct ChipsetVariantInfo
{
    ChipsetVariant variant;
    const char*    chipName;        // "Tsunami" or "Typhoon"
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
    "Typhoon", "21274",
    0x20,           // DREV
    8,              // CREV (HRM: Typhoon = 8)
    4,
    0x800000000ULL,  // 32 GB max DRAM (Typhoon: ASIZ 0xA = 8GB x 4 arrays)
    2
};

// ============================================================================
// Model-to-variant mapping
// ============================================================================

/**
 * @brief Determine chipset variant from machine model string
 * @param model  Machine model from INI (e.g., "ES40", "ES45")
 * @return       ChipsetVariant (Tsunami, Typhoon, or Unknown)
 *
 * The mapping is fixed by the Alpha platform architecture:
 *   DS10, DS20, ES40       -> Tsunami (21272)
 *   DS25, ES45             -> Typhoon (21274)
 *   GS80, GS160, GS320    -> Unknown (Wildfire -- not supported)
 */
inline ChipsetVariant variantFromModel(const std::string& model) noexcept
{
    std::string m = model;
    m.erase(0, m.find_first_not_of(" \t"));
    m.erase(m.find_last_not_of(" \t") + 1);
    std::ranges::transform(m, m.begin(), ::toupper);

    if (m == "DS10" || m == "DS20" || m == "ES40")
        return ChipsetVariant::Tsunami;
    if (m == "DS25" || m == "ES45")
        return ChipsetVariant::Typhoon;
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
    default:                      return nullptr;
    }
}

#endif // TSUNAMI_VARIANT_H
