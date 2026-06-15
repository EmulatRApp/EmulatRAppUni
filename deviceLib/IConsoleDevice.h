// ============================================================================
// IConsoleDevice.h - Get single character (CSERVE 0x01 - GETC)
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

#ifndef ICONSOLEDEVICE_H
#define ICONSOLEDEVICE_H
// ============================================================================
// IConsoleDevice.h
// ============================================================================
// Copyright (c) 2025 EmulatR Project. All rights reserved.
// Licensed under the MIT License. See LICENSE file in the project root.
// ============================================================================
// Console Device Interface
//
// Purpose:
//   Abstract interface for console devices
//   Supports CSERVE requirements (GETC/PUTC/PUTS/GETS/POLL)
//
// Implementations:
//   - SRMConsoleDevice (SRM-compliant, full CSERVE support)
//   - OPAConsoleDevice (legacy, basic I/O)
// ============================================================================

#include <cstdint>

// ============================================================================
// IConsoleDevice
// ============================================================================
class IConsoleDevice
{
public:
    virtual ~IConsoleDevice() = default;

    // ------------------------------------------------------------------------
    // CSERVE Core Operations
    // ------------------------------------------------------------------------

    /**
     * @brief Get single character (CSERVE 0x01 - GETC)
     * @param blocking If true, wait for input
     * @param timeoutMs Timeout in milliseconds (0 = default, UINT32_MAX = infinite)
     * @return Character (0-255) or -1 on timeout/error
     */
    virtual int getChar(bool blocking = false, uint32_t timeoutMs = 0) = 0;

    /**
     * @brief Put single character (CSERVE 0x02 - PUTC)
     * @param ch Character to output (0-255)
     */
    virtual void putChar(uint8_t ch) = 0;

    /**
     * @brief Put string (CSERVE 0x09 - PUTS)
     * @param data Pointer to data buffer
     * @param len Number of bytes to write
     * @return Number of bytes actually written
     */
    virtual uint64_t putString(const uint8_t* data, uint64_t len) = 0;

    /**
     * @brief Get string with line editing (CSERVE 0x0C - GETS)
     * @param buffer Output buffer
     * @param maxLen Maximum bytes to read (including null terminator)
     * @param echo If true, echo characters as typed
     * @return Number of bytes read (excluding null terminator)
     */
    virtual uint64_t getString(uint8_t* buffer, uint64_t maxLen, bool echo = true) = 0;

    // ------------------------------------------------------------------------
    // Legacy char-based API (for compatibility)
    // ------------------------------------------------------------------------

    /**
     * @brief Read single character (blocking)
     * @return Character (0-255) or -1 on error
     *
     * Note: Convenience wrapper around getChar(true)
     */
    virtual int readChar() = 0;

    /**
     * @brief Write single character
     * @param ch Character to output
     *
     * Note: Convenience wrapper around putChar(static_cast<uint8_t>(ch))
     */
    virtual void writeChar(char ch) = 0;

    // ------------------------------------------------------------------------
    // Status Query
    // ------------------------------------------------------------------------

    /**
     * @brief Check if input is available (CSERVE Poll)
     * @return true if data ready to read
     */
    virtual bool hasInput() const = 0;

    /**
     * @brief Check if console is connected
     * @return true if ready for I/O
     */
    virtual bool isConnected() const = 0;

    // ------------------------------------------------------------------------
    // Maintenance
    // ------------------------------------------------------------------------

    /**
     * @brief Reset console state (clear buffers, etc.)
     */
    virtual void reset() = 0;
};

#endif // ICONSOLEDEVICE_H