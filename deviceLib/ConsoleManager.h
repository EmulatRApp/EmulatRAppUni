// ============================================================================
// ConsoleManager.h - Register console device
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

#ifndef CONSOLEMANAGER_H
#define CONSOLEMANAGER_H
// ============================================================================
// ConsoleManager.h
// ============================================================================
// Copyright (c) 2025 EmulatR Project. All rights reserved.
// Licensed under the MIT License. See LICENSE file in the project root.
// ============================================================================
// Console Device Manager
//
// Purpose:
//   Manages console devices for Alpha SRM system
//   Provides CSERVE entry points for PAL handlers
//
// Architecture:
//   - Registers console devices by name (OPA0, OPA1, etc.)
//   - Primary console is OPA0 (SRM requirement)
//   - SRM limited to CPU0 (no SMP console support required)
//
// CSERVE Interface:
//   Maps PAL CSERVE calls to console device operations
//   Called by PalService::executeCSERVE()
// ============================================================================

#include <QObject>
#include <QMap>
#include <QMutex>
#include <QMutexLocker>
#include <QSet>
#include "coreLib/Axp_Attributes_core.h"

#include "IConsoleDevice.h"

#include "coreLib/LoggingMacros.h"

// Forward declarations
class OPAConsoleDevice;
class SRMConsoleDevice;
struct PipelineSlot;

// ============================================================================
// ConsoleManager
// ============================================================================
class ConsoleManager : public QObject
{
    Q_OBJECT

public:
    explicit ConsoleManager(QObject* parent = nullptr);
    ~ConsoleManager();

    // Delete copy/move
    ConsoleManager(const ConsoleManager&) = delete;
    ConsoleManager& operator=(const ConsoleManager&) = delete;

    // ------------------------------------------------------------------------
    // Lifecycle
    // ------------------------------------------------------------------------
    bool initialize() noexcept;

    void shutdown() noexcept;

    // ------------------------------------------------------------------------
    // Device Registration
    // ------------------------------------------------------------------------
    
    /**
     * @brief Register console device
     * @param name Device name (e.g., "OPA0")
     * @param device Device instance (takes ownership)
     * @return true if registered successfully
     */
    bool registerDevice(const QString& name, IConsoleDevice* device);
    
    /**
     * @brief Unregister console device
     * @param name Device name
     * @return true if unregistered successfully
     */
    bool unregisterDevice(const QString& name);
    
    /**
     * @brief Get device by name
     * @param name Device name
     * @return Device pointer, or nullptr if not found
     */
    IConsoleDevice* getDevice(const QString& name);
    
    /**
     * @brief Get primary console (OPA0)
     * @return Primary console device, or nullptr if not registered
     */
    IConsoleDevice* getPrimaryConsole();
    
    /**
     * @brief Check if device exists
     */
    bool hasDevice(const QString& name) const;
    
    /**
     * @brief Get list of registered device names
     */
    QStringList deviceNames() const;
    
    /**
     * @brief Get device count
     */
    qsizetype deviceCount() const;

    // ------------------------------------------------------------------------
    // CSERVE Entry Points (Called by PalService::executeCSERVE)
    // ------------------------------------------------------------------------
    
    /**
     * @brief CSERVE 0x01 - GETC (Get Character)
     * @param opaIndex OPA device index (0 = OPA0)
     * @param blocking If true, wait for input
     * @param timeoutMs Timeout in milliseconds (0 = default, UINT32_MAX = infinite)
     * @return Character (0-255) or -1 on timeout/error
     *
     * Called by: PalService::executeCSERVE case 0x01
     */
    int getCharFromOPA(int opaIndex, bool blocking = true, quint32 timeoutMs = 0);
    
    /**
     * @brief CSERVE 0x02 - PUTC (Put Character)
     * @param opaIndex OPA device index (0 = OPA0)
     * @param ch Character to output (0-255)
     *
     * Called by: PalService::executeCSERVE case 0x02
     */
    bool putCharToOPA(int opaIndex, quint8 ch);
    
    /**
     * @brief CSERVE 0x09 - PUTS (Put String)
     * @param opaIndex OPA device index (0 = OPA0)
     * @param data Pointer to data buffer
     * @param len Number of bytes to write
     * @return Number of bytes actually written
     *
     * Called by: PalService::executeCSERVE case 0x09
     */
    quint64 putStringToOPA(int opaIndex, const quint8* data, quint64 len);
    
    /**
     * @brief CSERVE 0x0C - GETS (Get String with Line Editing)
     * @param opaIndex OPA device index (0 = OPA0)
     * @param buffer Output buffer
     * @param maxLen Maximum bytes to read
     * @param echo If true, echo characters
     * @return Number of bytes read (excluding null terminator)
     *
     * Called by: PalService::executeCSERVE case 0x0C
     */
    quint64 getStringFromOPA(int opaIndex, quint8* buffer, quint64 maxLen, bool echo = true);
    
    /**
     * @brief CSERVE Poll - Check if input available
     * @param opaIndex OPA device index (0 = OPA0)
     * @return true if input ready
     */
    bool hasInputOnOPA(int opaIndex) const;
    
    /**
     * @brief Check if OPA device is connected
     * @param opaIndex OPA device index (0 = OPA0)
     * @return true if device exists and is connected
     */
    bool isAvailable(int opaIndex) const;

    // ------------------------------------------------------------------------
    // Utility (for PAL handlers that need to read guest memory)
    // ------------------------------------------------------------------------
    
    /**
     * @brief Read virtual byte from guest memory
     * Used by PUTS/GETS to access guest buffers
     */
/*    MEM_STATUS readVirtualByteFromPalHandler(quint64 virtualAddr, quint8& outByte) noexcept;*/

    // ------------------------------------------------------------------------
    // Device Access (Legacy - for OPAConsoleDevice compatibility)
    // ------------------------------------------------------------------------
    
    /**
     * @brief Get OPA device by index
     * @param index OPA index (0 = OPA0, 1 = OPA1, etc.)
     * @return OPAConsoleDevice pointer (cast from IConsoleDevice)
     */

	 // ============================================================================
	 // Legacy Device Access
	 // ============================================================================
	IConsoleDevice* getOPA(int index) const;

    // ------------------------------------------------------------------------
    // Maintenance
    // ------------------------------------------------------------------------
    
    /**
     * @brief Reset all console devices
     */
    void resetAll();



    bool openConsole(int opaIndex);
    bool isConsoleOpen(int opaIndex) const;
private:
    // ------------------------------------------------------------------------
    // Internal Helpers
    // ------------------------------------------------------------------------
    IConsoleDevice* getOPADevice(int opaIndex);
  
    bool isAvailable_internal(int opaIndex) const;
    void extracted();

    // ------------------------------------------------------------------------
    // State
    // ------------------------------------------------------------------------
    mutable QMutex m_mutex;
    QMap<QString, IConsoleDevice*> m_devices;
    QSet<int> m_openedConsoles;  // Track which OPAs are "open"
 
};

#endif // CONSOLEMANAGER_H
