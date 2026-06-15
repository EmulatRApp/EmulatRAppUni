// ============================================================================
// tests/chipsetLib/test_ticket01_5_variant_binding.cpp
//   Ticket 1.5 -- first-class Platform/variant binding
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
// Validates that variant is a first-class, consistent concept across the
// chipset and all three sub-chips, using the existing variant- and
// model-string constructors (no separate Platform enum -- the model-string
// path already provides platform identity, Surface 15).  Also pins the
// corrected ChipsetVariantInfo memory caps.
//
// Per V4 doctest convention: CHECK only, never REQUIRE.  Enum comparisons
// cast to int so doctest never stringifies an enum class.
// ============================================================================

#include "doctest.h"

#include "chipsetLib/TsunamiChipset.h"
#include "chipsetLib/TsunamiVariant.h"
#include "chipsetLib/Tsunami21272_RegisterMap.h"

#include <cstdint>
#include <string>

using namespace Tsunami21272;

static int iv(ChipsetVariant v) { return static_cast<int>(v); }

TEST_CASE("variantFromModel maps platform models to the right variant")
{
    CHECK(iv(variantFromModel("DS10")) == iv(ChipsetVariant::Tsunami));
    CHECK(iv(variantFromModel("DS20")) == iv(ChipsetVariant::Tsunami));
    CHECK(iv(variantFromModel("ES40")) == iv(ChipsetVariant::Tsunami));
    CHECK(iv(variantFromModel("DS25")) == iv(ChipsetVariant::Typhoon));
    CHECK(iv(variantFromModel("ES45")) == iv(ChipsetVariant::Typhoon));
}

TEST_CASE("ChipsetVariantInfo caps match the documented AAR limits")
{
    CHECK(variantInfo(ChipsetVariant::Tsunami)->maxMemBytes == 0x100000000ULL);  // 4 GB
    CHECK(variantInfo(ChipsetVariant::Typhoon)->maxMemBytes == 0x800000000ULL);  // 32 GB
}

TEST_CASE("All three sub-chips agree on variant (ES45 / Typhoon, 4 CPUs)")
{
    TsunamiChipset cs(ChipsetVariant::Typhoon, 4, 8ULL << 30);
    CHECK(iv(cs.variant())         == iv(ChipsetVariant::Typhoon));
    CHECK(iv(cs.cchip().variant()) == iv(ChipsetVariant::Typhoon));
    CHECK(iv(cs.dchip().variant()) == iv(ChipsetVariant::Typhoon));
    CHECK(iv(cs.pchip().variant()) == iv(ChipsetVariant::Typhoon));
}

TEST_CASE("All three sub-chips agree on variant (ES40 / Tsunami)")
{
    TsunamiChipset cs(ChipsetVariant::Tsunami, 1, 1ULL << 30);
    CHECK(iv(cs.cchip().variant()) == iv(ChipsetVariant::Tsunami));
    CHECK(iv(cs.dchip().variant()) == iv(ChipsetVariant::Tsunami));
    CHECK(iv(cs.pchip().variant()) == iv(ChipsetVariant::Tsunami));
}

TEST_CASE("Model-string ctor derives variant and reports the model")
{
    TsunamiChipset es45("ES45");
    CHECK(iv(es45.variant()) == iv(ChipsetVariant::Typhoon));
    CHECK(es45.model() == "ES45");

    TsunamiChipset es40("ES40");
    CHECK(iv(es40.variant()) == iv(ChipsetVariant::Tsunami));
    CHECK(es40.model() == "ES40");
}

TEST_CASE("Unknown model normalizes to Tsunami; consistency guard holds")
{
    TsunamiChipset cs("NONESUCH");
    CHECK(iv(cs.variant())         == iv(ChipsetVariant::Tsunami));
    CHECK(iv(cs.pchip().variant()) == iv(ChipsetVariant::Tsunami));
}

TEST_CASE("DREV via MMIO reflects the platform-derived variant")
{
    TsunamiChipset es40(ChipsetVariant::Tsunami, 2, 4ULL << 30);
    CHECK(es40.mmioRead(Base::kDchip_CSR + Dchip::DREV, 8) == 0x10);

    TsunamiChipset es45(ChipsetVariant::Typhoon, 4, 8ULL << 30);
    CHECK(es45.mmioRead(Base::kDchip_CSR + Dchip::DREV, 8) == 0x20);
}
