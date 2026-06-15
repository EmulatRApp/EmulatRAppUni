// ============================================================================
// global_firmwaredevicemanager.h - Get global FirmwareDeviceManager instance with thread-safe initialization.
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

#ifndef GLOBAL_FIRMWAREDEVICEMANAGER_H
#define GLOBAL_FIRMWAREDEVICEMANAGER_H

// Forward declarations
class FirmwareDeviceManager;

/**
 * @brief Get global FirmwareDeviceManager instance with thread-safe initialization.
 *
 * First call initializes the singleton using Meyer's singleton pattern.
 * Subsequent calls return the same instance with minimal overhead.
 *
 * The device tree remains uninitialized until explicit initialization is called.
 * Use initializeGlobalFirmwareDeviceManager() during emulator startup.
 *
 * @return Reference to global FirmwareDeviceManager instance
 */
FirmwareDeviceManager& global_FirmwareDeviceManager() noexcept;

/**
 * @brief Check if global FirmwareDeviceManager is initialized.
 *
 * Useful for startup code that wants to verify dependencies without
 * triggering initialization.
 *
 * @return true if device tree is initialized, false otherwise
 */
bool global_FirmwareDeviceManager_IsInitialized() noexcept;

/**
 * @brief Initialize the global FirmwareDeviceManager with configuration.
 *
 * Should be called during emulator Phase 7.5 (Device Tree initialization).
 * Runs all 5 phases of device tree initialization:
 * - Phase 0: Firmware Context
 * - Phase 1: Platform Root
 * - Phase 2: Bus Discovery
 * - Phase 3: Device Enumeration
 * - Phase 4: Finalization
 *
 * @return true if initialization successful
 */
bool initializeGlobalFirmwareDeviceManager() noexcept;

/**
 * @brief Force re-initialization of global FirmwareDeviceManager.
 *
 * WARNING: Only use during testing or shutdown. Not thread-safe with
 * concurrent access to FirmwareDeviceManager.
 */
void shutdownGlobalFirmwareDeviceManager() noexcept;

#endif // GLOBAL_FIRMWAREDEVICEMANAGER_H
