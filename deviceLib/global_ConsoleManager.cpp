// ============================================================================
// global_ConsoleManager.cpp - ============================================================================
// ============================================================================
// Project: ASA-EMulatR - Alpha AXP Architecture Emulator
// Copyright (C) 2025 eNVy Systems, Inc. All rights reserved.
// Licensed under eNVy Systems Non-Commercial License v1.1
//
// Project Architect: Timothy Peer
// AI Code Generation: Claude (Anthropic) / ChatGPT (OpenAI)
//
// Commercial use prohibited without separate license.
// Contact: peert@envysys.com | https://envysys.com
// Documentation: https://timothypeer.github.io/ASA-EMulatR-Project/
// ============================================================================

#include "global_ConsoleManager.h"
#include "ConsoleManager.h"
#include "StdoutConsoleBackend.h"

ConsoleManager& global_ConsoleManager() noexcept
{
    // Lazily construct the manager and register the minimum-viable OPA0
    // console backend (stdout) on first access, so CSERVE terminal I/O
    // (palBoxLib/grains/PalEntries.cpp execCserve) lands on a live device
    // instead of getOPADevice("OPA0") returning null.  StdoutConsoleBackend
    // is the C1 sink; a richer IConsoleDevice (SRMConsoleDevice / TCP
    // transport to PuTTY) can replace it later via registerDevice without
    // touching any CSERVE call site.  Pointer (not value) so the manager is
    // never destroyed mid-run; process-lifetime singleton, intentionally
    // not freed.
    static ConsoleManager* instance = [] {
        auto* m = new ConsoleManager();
        m->registerDevice("OPA0", new StdoutConsoleBackend());
        return m;
    }();
    return *instance;
}
