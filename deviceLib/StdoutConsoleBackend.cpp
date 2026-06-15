// ============================================================================
// StdoutConsoleBackend.cpp -- IConsoleDevice impl over stdout
// ============================================================================
// Project: ASA-EMulatR - Alpha AXP Architecture Emulator
// Copyright (C) 2025, 2026 eNVy Systems, Inc. All rights reserved.
// Licensed under eNVy Systems Non-Commercial License v1.1
// ============================================================================

#include "StdoutConsoleBackend.h"

#include <cstdio>

int StdoutConsoleBackend::getChar(bool /*blocking*/, uint32_t /*timeoutMs*/)
{
    // No input source in v1 minimum. Phase-2 will plug stdin or TCP here.
    return -1;
}

void StdoutConsoleBackend::putChar(uint8_t ch)
{
    std::fputc(static_cast<int>(ch), stdout);
    std::fflush(stdout);
}

uint64_t StdoutConsoleBackend::putString(const uint8_t* data, uint64_t len)
{
    if (data == nullptr || len == 0) return 0;
    const uint64_t written =
        static_cast<uint64_t>(std::fwrite(data, 1, static_cast<size_t>(len), stdout));
    std::fflush(stdout);
    return written;
}

uint64_t StdoutConsoleBackend::getString(uint8_t* /*buffer*/,
                                         uint64_t /*maxLen*/,
                                         bool     /*echo*/)
{
    return 0;
}

int StdoutConsoleBackend::readChar()
{
    return -1;
}

void StdoutConsoleBackend::writeChar(char ch)
{
    putChar(static_cast<uint8_t>(ch));
}
