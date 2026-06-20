// ============================================================================
// tests/main.cpp -- doctest test runner entry point
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
// This file is the test runner entry point for the doctest framework.  We
// supply our OWN main() (DOCTEST_CONFIG_IMPLEMENT, not _WITH_MAIN) so we can
// pin one piece of process-wide test environment before any TEST_CASE runs,
// then hand off to doctest's canonical Context(argc, argv).run().
//
// Per-test files (test_xxx.cpp) include "doctest.h" without the IMPLEMENT
// macro and contain TEST_CASE blocks that register automatically.
//
// EMULATR_CONSOLE_PORT=0 (ephemeral): tests that construct a full Machine
// bring up the SRM console TCP backend.  Pinning the console to an OS-assigned
// ephemeral port means neither the fixed production port 10023 (possibly held
// by a running emulator) nor a coexisting second Machine in the same test case
// (e.g. test_snapshot_roundtrip builds `source` + `restored` simultaneously)
// can ever collide.  makeCom1Cfg honors 0 as ephemeral.  Set only if the
// developer has not already pinned a port explicitly.
//
// ============================================================================

#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"

#include <cstdlib>

int main(int argc, char** argv)
{
    if (std::getenv("EMULATR_CONSOLE_PORT") == nullptr) {
#if defined(_WIN32)
        _putenv_s("EMULATR_CONSOLE_PORT", "0");
#else
        setenv("EMULATR_CONSOLE_PORT", "0", /*overwrite*/ 1);
#endif
    }
    return doctest::Context(argc, argv).run();
}
