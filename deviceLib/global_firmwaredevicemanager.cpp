// ============================================================================
// global_firmwaredevicemanager.cpp - ============================================================================
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

#include "global_FirmwareDeviceManager.h"
#include "FirmwareDeviceManager.h"
#include "config/EmulatorSettings.h"
#include "coreLib/LoggingMacros.h"
#include <atomic>

namespace {
    // Thread-safe initialization flag
    std::atomic<bool> g_initialized{false};
}

// ============================================================================
// Meyer's Singleton Accessor
// ============================================================================

FirmwareDeviceManager& global_FirmwareDeviceManager() noexcept
{
    static FirmwareDeviceManager instance;
    return instance;
}

// ============================================================================
// Initialization Status
// ============================================================================

bool global_FirmwareDeviceManager_IsInitialized() noexcept
{
    return g_initialized.load(std::memory_order_acquire);
}

// ============================================================================
// Device Tree Initialization (5 Phases)
// ============================================================================

bool initializeGlobalFirmwareDeviceManager() noexcept
{
    EMULATR_LOG_INFO("Initializing Global FirmwareDeviceManager...");

    // Get singleton instance
    auto& fdm = global_FirmwareDeviceManager();

    // Get configuration
   // auto& config = global_EmulatorSettings();

    // ------------------------------------------------------------------------
    // Phase 0: Firmware Context Initialization
    // ------------------------------------------------------------------------
    /*
     * TODO initializePhase0_FirmwareContext not implemented.
    if (!fdm.initializePhase0_FirmwareContext(config)) {
        EMULATR_LOG_ERROR("Device Tree Phase 0 failed: Firmware Context");
        return false;
    }
    EMULATR_LOG_TRACE("Device Tree Phase 0: Firmware Context - OK");
    */
    // ------------------------------------------------------------------------
    // Phase 1: Platform Root Creation
    // ------------------------------------------------------------------------
    
    if (!fdm.initializePhase1_PlatformRoot()) {
        EMULATR_LOG_ERROR("Device Tree Phase 1 failed: Platform Root");
        return false;
    }
    EMULATR_LOG_INFO("Device Tree Phase 1: Platform Root - OK");

    // ------------------------------------------------------------------------
    // Phase 2: Bus Discovery and Attachment
    // ------------------------------------------------------------------------
    
    if (!fdm.initializePhase2_BusDiscovery()) {
        EMULATR_LOG_ERROR("Device Tree Phase 2 failed: Bus Discovery");
        return false;
    }
    EMULATR_LOG_INFO("Device Tree Phase 2: Bus Discovery - OK");

    // ------------------------------------------------------------------------
    // Phase 3: Device Enumeration and Registration
    // ------------------------------------------------------------------------
    
    /*
     *TODO initializePhase3_DeviceEnumeration not implemented
    if (!fdm.initializePhase3_DeviceEnumeration()) {
        EMULATR_LOG_ERROR("Device Tree Phase 3 failed: Device Enumeration");
        return false;
    }
    EMULATR_LOG_INFO("Device Tree Phase 3: Device Enumeration - OK");
    */
    // ------------------------------------------------------------------------
    // Phase 4: Device Finalization and Console Exposure
    // ------------------------------------------------------------------------
    
    if (!fdm.initializePhase4_Finalization()) {
        EMULATR_LOG_ERROR("Device Tree Phase 4 failed: Finalization");
        return false;
    }
    EMULATR_LOG_INFO("Device Tree Phase 4: Finalization - OK");

    // ------------------------------------------------------------------------
    // Mark as initialized
    // ------------------------------------------------------------------------
    
    g_initialized.store(true, std::memory_order_release);

    EMULATR_LOG_INFO("Global FirmwareDeviceManager initialized successfully");
    EMULATR_LOG_INFO("Device Tree contains {} devices", fdm.getAllDeviceNames().size());

    return true;
}

// ============================================================================
// Shutdown
// ============================================================================

void shutdownGlobalFirmwareDeviceManager() noexcept
{
    EMULATR_LOG_INFO("Shutting down Global FirmwareDeviceManager...");
    
    g_initialized.store(false, std::memory_order_release);
    
    // Note: The FirmwareDeviceManager instance persists as a Meyer's singleton
    // but is marked as uninitialized. Call initializeGlobalFirmwareDeviceManager()
    // again to re-initialize if needed.
    
    EMULATR_LOG_INFO("Global FirmwareDeviceManager shutdown complete");
}
