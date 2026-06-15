// ============================================================================
// StdoutConsoleBackend.h -- Minimum-viable IConsoleDevice for V4 SRM bring-up
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
//   Cheapest possible IConsoleDevice implementation: putChar / putString
//   write to stdout, getChar / getString report no input. Sufficient to
//   reach the SRM ">>>" prompt in v1 -- the firmware only needs a sink
//   for its banner output.
//
//   Phase-2 backends (TCP transport to PuTTY, line-buffered stdin reader,
//   SRMConsoleDevice with line editing) plug into the same interface
//   without changing the UART or chipset wiring.
//
// THREAD SAFETY:
//   None. Single-threaded boot-time use only.
// ============================================================================

#ifndef STDOUT_CONSOLE_BACKEND_H
#define STDOUT_CONSOLE_BACKEND_H

#include <cstdint>
#include "IConsoleDevice.h"

class StdoutConsoleBackend : public IConsoleDevice
{
public:
    StdoutConsoleBackend() noexcept = default;
    ~StdoutConsoleBackend() override = default;

    // ------------------------------------------------------------------------
    // CSERVE Core Operations
    // ------------------------------------------------------------------------
    int      getChar(bool blocking = false, uint32_t timeoutMs = 0) override;
    void     putChar(uint8_t ch) override;
    uint64_t putString(const uint8_t* data, uint64_t len) override;
    uint64_t getString(uint8_t* buffer, uint64_t maxLen, bool echo = true) override;

    // ------------------------------------------------------------------------
    // Legacy char-based API
    // ------------------------------------------------------------------------
    int  readChar() override;
    void writeChar(char ch) override;

    // ------------------------------------------------------------------------
    // Status
    // ------------------------------------------------------------------------
    bool hasInput()    const override { return false; }
    bool isConnected() const override { return true;  }

    // ------------------------------------------------------------------------
    // Maintenance
    // ------------------------------------------------------------------------
    void reset() override {}
};

#endif // STDOUT_CONSOLE_BACKEND_H
