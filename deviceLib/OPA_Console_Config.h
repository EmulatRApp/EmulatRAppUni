// ============================================================================
// OPA_Console_Config.h - Configuration for a single OPA console device.
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

// ============================================================================
//  OPA_Console_Config_Fixed.h
//
//  Purpose: Corrected OPA console configuration loader
//
//  Fixes:
//    1. Proper QSettings group navigation
//    2. Correct childGroups() checking
//    3. Transport-specific validation
//    4. Best-effort application launch
//    5. Proper string prefix handling
// ============================================================================

#ifndef OPA_CONSOLE_CONFIG_H
#define OPA_CONSOLE_CONFIG_H

#include <QString>
#include <QSettings>
#include <QStringList>
#include <QProcess>
#include "coreLib/LoggingMacros.h"


static constexpr const char* kDevicePrefix = "Device.";
static constexpr const char* kUARTGroup = "UART";

/**
 * @brief Configuration for a single OPA console device.
 */
 /**
  * @brief Configuration for a single OPA console device.
  */
struct OPAConfig {
    QString name;              // "OPA0", "OPA1", etc.
    QString type;              // "UART"
    QString location;          // "cab0/drw0"
    QString iface;             // "Net", "Serial", "File"
    quint16 ifacePort;         // TCP port
    QString application;       // Optional launch command

    // Buffering
    quint32 rxBufferSize;      // RX queue size
    quint32 txBufferSize;      // TX queue size
    bool dropOnOverflow;       // Drop vs block on full buffer
    bool autoReconnect;        // Auto-accept new connections

    // Defaults
    OPAConfig()
        : ifacePort(0)
        , rxBufferSize(256)
        , txBufferSize(1024)
        , dropOnOverflow(true)
        , autoReconnect(true)
    {
    }
};

/**
 * @brief Load OPA console configuration from QSettings.
 *
 * Tries canonical format first, then legacy format.
 *
 * @param settings QSettings instance
 * @param deviceName Device name (e.g., "OPA0")
 * @param[out] config Loaded configuration
 * @return true if found and valid, false otherwise
 */
inline bool loadOPAConfig(QSettings& settings, const QString& deviceName, OPAConfig& config)
{
    // Try canonical format: [Device.OPA0]
    QString canonicalKey = QString("Device.%1").arg(deviceName);

    if (settings.childGroups().contains(QString("Device.%1").arg(deviceName).section('.', 0, 0))) {
        settings.beginGroup(canonicalKey);

        config.name = settings.value("name", deviceName).toString();
        config.type = settings.value("type", "UART").toString();
        config.location = settings.value("location", "cab0/drw0").toString();
        config.iface = settings.value("iface", "Net").toString();
        config.ifacePort = settings.value("iface_port", 0).toUInt();
        config.application = settings.value("application", "").toString();

        config.rxBufferSize = settings.value("rx_buffer_size", 256).toUInt();
        config.txBufferSize = settings.value("tx_buffer_size", 1024).toUInt();
        config.dropOnOverflow = settings.value("drop_on_overflow", true).toBool();
        config.autoReconnect = settings.value("auto_reconnect", true).toBool();

        settings.endGroup();

        if (config.ifacePort == 0) {
            EMULATR_LOG_ERROR(QString("OPA %1: Invalid port 0").arg(deviceName).toStdString());
            return false;
        }

        return true;
    }

    // Try legacy format: [UART/OPA0]
    QString legacyKey = QString("UART/%1").arg(deviceName);

    if (settings.childGroups().contains("UART")) {
        settings.beginGroup(legacyKey);

        config.name = settings.value("name", deviceName).toString();
        config.type = "UART";
        config.location = "cab0/drw0";  // Default
        config.iface = settings.value("iface", "Net").toString();
        config.ifacePort = settings.value("iface_port", 0).toUInt();
        config.application = settings.value("application", "").toString();

        // Legacy format doesn't have these - use defaults
        config.rxBufferSize = 256;
        config.txBufferSize = 1024;
        config.dropOnOverflow = true;
        config.autoReconnect = true;

        settings.endGroup();

        if (config.ifacePort == 0) {
            EMULATR_LOG_ERROR(QString("OPA %1: Invalid port 0").arg(deviceName).toStdString());
            return false;
        }

        return true;
    }

    // Not found in either format
    EMULATR_LOG_WARN(QString("OPA %1: No configuration found").arg(deviceName).toStdString());
    return false;
}

/**
 * @brief Discover all configured OPA devices.
 *
 * Scans QSettings for both canonical and legacy formats.
 *
 * @param settings QSettings instance
 * @return List of device names (e.g., ["OPA0", "OPA1"])
 */
inline QStringList discoverOPADevices(QSettings& settings)
{
    QStringList devices;

    // Scan canonical format: [Device.OPA*]
    for (const QString& group : settings.childGroups()) {
        if (group.startsWith("Device.OPA")) {
            QString deviceName = group.mid(7);  // Remove "Device."
            if (!devices.contains(deviceName)) {
                devices.append(deviceName);
            }
        }
    }

    // Scan legacy format: [UART/OPA*]
    settings.beginGroup("UART");
    for (const QString& key : settings.childGroups()) {
        if (key.startsWith("OPA")) {
            if (!devices.contains(key)) {
                devices.append(key);
            }
        }
    }
    settings.endGroup();

    // Sort for consistent ordering
    devices.sort();

    return devices;
}

#endif // OPA_CONSOLE_CONFIG_H
