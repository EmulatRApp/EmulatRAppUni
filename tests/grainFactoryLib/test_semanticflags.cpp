// ============================================================================
// tests/grainFactoryLib/test_semanticflags.cpp
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
// Smoke tests for codegen output.  Validates that genGrains.py produces
// compilable C++ with the expected semantics.  Exercises:
//
//   - GrainSem bitwise operators (|, &, ^, ~, |=, &=)
//   - GrainSem predicates (any, has)
//   - Per-category masks (kFormatMask, kOperandMask, ...)
//   - Bit-position ABI for spot-checked flags
//   - DispatchKind enum coverage and dispatchKindName() helper
//
// This is the first real test target file past the doctest entry
// stub at tests/main.cpp.
//
// ============================================================================

#include "doctest.h"

#include "grainFactoryLib/generated/SemanticFlagsEnum.h"
#include "grainFactoryLib/generated/DispatchKinds.h"

#include <cstdint>
#include <ostream>      // doctest forward-declares std::basic_ostream;
                        // we use std::string_view in CHECK below, which
                        // routes through MSVC's operator<<(ostream&,
                        // string_view) and needs ostream complete.
#include <string_view>

using namespace grainFactory;


TEST_CASE("GrainSem -- bitwise operators")
{
    GrainSem a = GrainSem::S_Load;
    GrainSem b = GrainSem::S_Cacheable;
    GrainSem combined = a | b;

    CHECK(any(combined));
    CHECK(has(combined, GrainSem::S_Load));
    CHECK(has(combined, GrainSem::S_Cacheable));
    CHECK_FALSE(has(combined, GrainSem::S_Store));

    GrainSem onlyLoad = combined & GrainSem::S_Load;
    CHECK(onlyLoad == GrainSem::S_Load);

    GrainSem cleared = combined & ~GrainSem::S_Load;
    CHECK(cleared == GrainSem::S_Cacheable);

    GrainSem toggled = combined ^ GrainSem::S_Load;
    CHECK_FALSE(has(toggled, GrainSem::S_Load));
    CHECK(has(toggled, GrainSem::S_Cacheable));
}


TEST_CASE("GrainSem -- compound assignment")
{
    GrainSem a = GrainSem::None;
    CHECK_FALSE(any(a));

    a |= GrainSem::S_Load;
    a |= GrainSem::S_Cacheable;
    CHECK(has(a, GrainSem::S_Load));
    CHECK(has(a, GrainSem::S_Cacheable));

    a &= ~GrainSem::S_Load;
    CHECK_FALSE(has(a, GrainSem::S_Load));
    CHECK(has(a, GrainSem::S_Cacheable));
}


TEST_CASE("GrainSem -- category masks contain expected members")
{
    // kFormatMask must contain every format flag
    CHECK(has(kFormatMask, GrainSem::S_PalFormat));
    CHECK(has(kFormatMask, GrainSem::S_BraFormat));
    CHECK(has(kFormatMask, GrainSem::S_MemFormat));
    CHECK(has(kFormatMask, GrainSem::S_OpFormat));
    CHECK(has(kFormatMask, GrainSem::S_FpFormat));
    CHECK(has(kFormatMask, GrainSem::S_HwFormat));
    CHECK(has(kFormatMask, GrainSem::S_JmpFormat));
    CHECK(has(kFormatMask, GrainSem::S_MiscFormat));

    // and contain nothing from other categories
    CHECK_FALSE(has(kFormatMask, GrainSem::S_ReadsRa));
    CHECK_FALSE(has(kFormatMask, GrainSem::S_Load));
    CHECK_FALSE(has(kFormatMask, GrainSem::S_ChangesPC));
}


TEST_CASE("GrainSem -- categories do not bleed")
{
    GrainSem fmt = GrainSem::S_OpFormat | GrainSem::S_FpFormat;

    // Two format flags fall entirely within the format mask
    CHECK((fmt & kFormatMask) == fmt);

    // and entirely outside other category masks
    CHECK((fmt & kOperandMask)   == GrainSem::None);
    CHECK((fmt & kRegfileMask)   == GrainSem::None);
    CHECK((fmt & kMemoryMask)    == GrainSem::None);
    CHECK((fmt & kControlMask)   == GrainSem::None);
    CHECK((fmt & kPrivilegeMask) == GrainSem::None);
}


TEST_CASE("GrainSem -- bit positions are ABI")
{
    // Bit numbers are the cross-module ABI.  Snapshot files, trace
    // logs, and any host/target marshalling depend on them.  Spot-
    // check enough of the table to catch any future renumbering.
    CHECK(static_cast<uint64_t>(GrainSem::S_PalFormat)  == (1ULL <<  0));
    CHECK(static_cast<uint64_t>(GrainSem::S_MiscFormat) == (1ULL <<  7));
    CHECK(static_cast<uint64_t>(GrainSem::S_ReadsRa)    == (1ULL <<  8));
    CHECK(static_cast<uint64_t>(GrainSem::S_WritesRc)   == (1ULL << 10));
    CHECK(static_cast<uint64_t>(GrainSem::S_HasLit)     == (1ULL << 11));
    CHECK(static_cast<uint64_t>(GrainSem::S_ReadsInt)   == (1ULL << 12));
    CHECK(static_cast<uint64_t>(GrainSem::S_Load)       == (1ULL << 16));
    CHECK(static_cast<uint64_t>(GrainSem::S_Locked)     == (1ULL << 18));
    CHECK(static_cast<uint64_t>(GrainSem::S_WritesRa)   == (1ULL << 22));
    CHECK(static_cast<uint64_t>(GrainSem::S_ChangesPC)  == (1ULL << 24));
    CHECK(static_cast<uint64_t>(GrainSem::S_Privileged) == (1ULL << 32));
    CHECK(static_cast<uint64_t>(GrainSem::S_PalTru64)   == (1ULL << 36));
    CHECK(static_cast<uint64_t>(GrainSem::S_PalVms)     == (1ULL << 37));
    CHECK(static_cast<uint64_t>(GrainSem::S_PalIntrinsic) == (1ULL << 38));
    CHECK(static_cast<uint64_t>(GrainSem::S_FpTrap)     == (1ULL << 40));
    CHECK(static_cast<uint64_t>(GrainSem::S_IprRead)    == (1ULL << 48));
    CHECK(static_cast<uint64_t>(GrainSem::S_Mb)         == (1ULL << 50));
    CHECK(static_cast<uint64_t>(GrainSem::S_NoTrace)    == (1ULL << 56));
}


TEST_CASE("DispatchKind -- name helper round-trip")
{
    CHECK(std::string_view(dispatchKindName(DispatchKind::Direct))     == "Direct");
    CHECK(std::string_view(dispatchKindName(DispatchKind::IntArith))   == "IntArith");
    CHECK(std::string_view(dispatchKindName(DispatchKind::IntLogical)) == "IntLogical");
    CHECK(std::string_view(dispatchKindName(DispatchKind::Pal))        == "Pal");
    CHECK(std::string_view(dispatchKindName(DispatchKind::HwRei))      == "HwRei");
    CHECK(std::string_view(dispatchKindName(DispatchKind::JmpClass))   == "JmpClass");
    CHECK(std::string_view(dispatchKindName(DispatchKind::FpTiExt))    == "FpTiExt");
    CHECK(std::string_view(dispatchKindName(DispatchKind::Reserved))   == "Reserved");
}


TEST_CASE("DispatchKind -- canonical count")
{
    // Walk values 0..31; expect exactly 19 named, the rest <invalid>.
    // Catches drift between the codegen's CANONICAL_DISPATCH_KINDS
    // list and any consumer that switches on the enum by value.
    // 2026-06-20: bumped 18 -> 19; the generated DispatchKind enum +
    // dispatchKindName now carry 19 named kinds (Direct..Reserved), all
    // handled in the switch.  This tripwire correctly flagged the codegen
    // growth; the count is updated to match the regenerated enum.
    int validCount = 0;
    for (uint8_t i = 0; i < 32; ++i) {
        char const* n = dispatchKindName(static_cast<DispatchKind>(i));
        if (std::string_view(n) != "<invalid>") {
            ++validCount;
        }
    }
    CHECK(validCount == 19);
}
