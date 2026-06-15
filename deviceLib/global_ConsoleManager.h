// ============================================================================
// global_ConsoleManager.h - Get global ConsoleManager singleton instance.
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

#ifndef GLOBAL_CONSOLEMANAGER_H
#define GLOBAL_CONSOLEMANAGER_H

// Forward declaration
class ConsoleManager;

/**
 * @brief Get global ConsoleManager singleton instance.
 * 
 * Provides access to the system-wide console device registry.
 * Thread-safe Meyer's singleton pattern.
 * 
 * @return Reference to global ConsoleManager instance
 */
ConsoleManager& global_ConsoleManager() noexcept;

#endif // GLOBAL_CONSOLEMANAGER_H
