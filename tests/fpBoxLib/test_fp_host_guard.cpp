// ============================================================================
// tests/fpBoxLib/test_fp_host_guard.cpp -- host-FP self-test, doctest policy path
// ============================================================================
// Project: EmulatR -- Alpha AXP / EV6 Architecture Emulator (V4)
// Copyright (C) 2025, 2026 eNVy Systems, Inc.  All rights reserved.
// Licensed under eNVy Systems Non-Commercial License v1.1
//
// Project Architect: Timothy Peer
// AI Collaboration:  Claude (Anthropic)
// ============================================================================
//
// Exercises the SAME detector the emulator startup uses (fpBox::runHostFpSelfTest),
// but applies the DOCTEST policy -- CHECK the structured result -- instead of the
// abort policy (enforceHostFpSafeOrAbort), so a failure reports rather than
// killing the test binary. doctest CHECK only (exceptions disabled in V4).
// ============================================================================

#include "doctest.h"
#include "fpBoxLib/fp_host_guard.h"

TEST_CASE("host FP self-test agrees with the SoftFloat oracle (no-abort path)")
{
    fpBox::FpGuardResult const r = fpBox::runHostFpSelfTest();

    CHECK(r.ok);
    if (!r.ok) {
        MESSAGE("host-FP divergence: op=" << (r.op ? r.op : "?")
                << " host bits != oracle bits (x87 / FMA / flush-to-zero?)");
    }
}
