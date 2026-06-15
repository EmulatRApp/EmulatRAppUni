// ============================================================================
// SRMEnvStore.h - Create environment store with specified config path
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

#ifndef SRMENVSTORE_H
#define SRMENVSTORE_H

#include <QMap>
#include <QString>
#include <QStringList>
#include <QDateTime>

// ============================================================================
// SRMEnvStore - SRM Environment Variable Management
// ============================================================================
// Implements "toy persistence" for SRM environment variables using JSON
// storage. Not architecturally accurate but provides the necessary SRM
// console functionality for development and testing.
// ============================================================================

class SRMEnvStore
{
public:
    // ------------------------------------------------------------------------
    // Construction
    // ------------------------------------------------------------------------

    /**
     * @brief Create environment store with specified config path
     * @param configPath Directory for JSON storage (e.g., EmulatorConfig::configPath)
     */
    explicit SRMEnvStore(const QString& configPath = ".");

    ~SRMEnvStore();

    // ------------------------------------------------------------------------
    // Variable Access
    // ------------------------------------------------------------------------

    /**
     * @brief Get environment variable value
     * @param name Variable name (case-insensitive)
     * @return Variable value, or empty string if not found
     */
    QString get(const QString& name) const noexcept;

    /**
     * @brief Set environment variable
     * @param name Variable name (will be normalized to lowercase)
     * @param value Variable value
     */
    void set(const QString& name, const QString& value) noexcept;

    /**
     * @brief Check if variable exists
     * @param name Variable name (case-insensitive)
     * @return true if variable exists
     */
    bool exists(const QString& name) const noexcept;

    /**
     * @brief Get all variable names
     * @return List of all variable names
     */
    QStringList getAllNames() const noexcept;

    /**
     * @brief Remove variable
     * @param name Variable name (case-insensitive)
     */
    void remove(const QString& name) noexcept;

    /**
     * @brief Clear all variables (keeps defaults)
     */
    void clear() noexcept;

    /**
     * @brief Get variable count
     * @return Number of variables
     */
    int count() const noexcept;

    // ------------------------------------------------------------------------
    // Time Management (Toy Clock)
    // ------------------------------------------------------------------------

    /**
     * @brief Set time offset from host time
     * @param offsetSeconds Seconds to add to host time
     */
    void setTimeOffset(qint64 offsetSeconds) noexcept;

    /**
     * @brief Get current time offset
     * @return Time offset in seconds
     */
    qint64 getTimeOffset() const noexcept;

    /**
     * @brief Get adjusted time (host time + offset)
     * @return Adjusted date/time
     */
    QDateTime getAdjustedTime() const noexcept;

    // ------------------------------------------------------------------------
    // Persistence
    // ------------------------------------------------------------------------

    /**
     * @brief Save variables to JSON file
     */
    void save() noexcept;

    /**
     * @brief Load variables from JSON file
     */
    void load() noexcept;

    /**
     * @brief Get storage file path
     * @return Full path to JSON storage file
     */
    QString getStoragePath() const noexcept;

private:
    // ------------------------------------------------------------------------
    // Implementation
    // ------------------------------------------------------------------------

    /**
     * @brief Initialize default SRM environment variables
     */
    void initializeDefaults() noexcept;

    // ------------------------------------------------------------------------
    // Member Data
    // ------------------------------------------------------------------------

    QString m_configPath;                   // Configuration directory
    QMap<QString, QString> m_variables;     // Environment variables (lowercase keys)

    // Time management
    qint64 m_timeOffsetSeconds{ 0 };         // Offset from host time
    QDateTime m_lastSetTimestamp;          // When offset was last set
};

#endif // SRMENVSTORE_H