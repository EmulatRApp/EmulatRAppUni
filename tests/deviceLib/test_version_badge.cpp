// ============================================================================
// tests/deviceLib/test_version_badge.cpp -- console badge <-> version.h sync
// ============================================================================
// Project: EmulatR -- Alpha AXP / EV6 Architecture Emulator (V5)
// Copyright (C) 2025, 2026 eNVy Systems, Inc.  All rights reserved.
// Licensed under eNVy Systems Non-Commercial License v1.1
//
// Project Architect: Timothy Peer
// AI Collaboration:  Claude (Anthropic)
//
// Commercial use prohibited without separate license.
// Contact:        peert@envysys.com  |  https://envysys.com
// ============================================================================
//
// Pins the connect-time console badge to the generated emulatr_version.h
// (single source of truth: project() VERSION + EMULATR_VERSION_STAGE in
// CMakeLists.txt, mirrored to the H&M doc project by tools/sync_hm_version.py).
// Grammar per the H&M topic "EmulatR Version Numbering and Release Stages":
// <stage><major>.<variant>-<build>, stage in {X,T,V} (DEC convention), e.g.
// "X1.5-12".
//
// doctest CHECK only (house rule).  ASCII(128) only.
// ============================================================================

#include "doctest.h"

#include <string>

#include "emulatr_version.h"
#include "deviceLib/SRMConsoleDevice.h"

TEST_CASE("version: EMULATR_VERSION_STRING composes stage + three numerics")
{
    // Stage letter must be one of the DEC convention letters from the H&M
    // topic: X experimental, T field test, V released.
    const std::string stage = EMULATR_VERSION_STAGE;
    CHECK(stage.size() == 1);
    CHECK((stage == "X" || stage == "T" || stage == "V"));

    const std::string expect = stage + std::to_string(EMULATR_VERSION_MAJOR) +
                               "." + std::to_string(EMULATR_VERSION_MINOR) +
                               "-" + std::to_string(EMULATR_VERSION_BUILD);
    CHECK(expect == EMULATR_VERSION_STRING);

    const std::string dotted = std::to_string(EMULATR_VERSION_MAJOR) + "." +
                               std::to_string(EMULATR_VERSION_MINOR) + "." +
                               std::to_string(EMULATR_VERSION_BUILD);
    CHECK(dotted == EMULATR_VERSION_DOTTED);
}

TEST_CASE("badge: console banner carries the generated version string")
{
    const std::string banner = SRMConsoleDevice::consoleBanner();

    // The badge line, fully composed -- the H&M-synchronized surface.
    const std::string badge =
        std::string("Alpha Emulator Console ") + EMULATR_VERSION_STRING;
    CHECK(banner.find(badge) != std::string::npos);

    // The retired hardcoded V4.0-0 badge must never resurface.
    CHECK(banner.find("V4.0-0") == std::string::npos);

    // Raw-terminal discipline: explicit CRLF, banner-bracketing blank lines.
    CHECK(banner.find("\r\n") != std::string::npos);
    CHECK(banner.find("ASA EmulatR") != std::string::npos);
}
