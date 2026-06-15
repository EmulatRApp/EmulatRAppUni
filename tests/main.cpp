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
// This file is the test runner entry point for the doctest framework.  The
// DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN macro instructs doctest to emit its
// canonical main() that registers and runs all TEST_CASE blocks from the
// other test files in this target.
//
// Per-test files (test_xxx.cpp) include "doctest.h" without the
// _WITH_MAIN macro and contain TEST_CASE blocks that register
// automatically.
//
// ============================================================================

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
