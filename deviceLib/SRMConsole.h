// ============================================================================
// SRMConsole.h - Create SRM console with configuration
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

#ifndef SRMCONSOLE_H
#define SRMCONSOLE_H

#include "ControllerConfig.h"

#include "SRMEnvStore.h"
#include "SRMConsoleDevice.h"
#include "config/EmulatorSettings.h"


// Forward declarations
struct PipelineSlot;
struct PalResult;

// ============================================================================
// SRMConsole - Alpha SRM Console Implementation
// ============================================================================
// Provides authentic Alpha SRM console functionality including:
// - Interactive command prompt (">>>")
// - Device enumeration and configuration
// - Environment variable management
// - Boot command processing
// - System introspection
//
// Integrates with existing PAL CSERVE infrastructure for I/O
// ============================================================================

class SRMConsole
{
public:
    // ------------------------------------------------------------------------
    // Construction and Configuration
    // ------------------------------------------------------------------------

    /**
     * @brief Create SRM console with configuration
     * @param settings Emulator configuration data
     * @param envStore Environment variable store
     */
    SRMConsole(const emulatr::config::EmulatorSettings& settings, SRMEnvStore& envStore);
    /**
     * @brief Initialize SRM console
     * @param cpuId
     * @param ctx Processor context for PAL calls
     */
    void initialize(uint16_t cpuId) noexcept;
    // ------------------------------------------------------------------------
    // Console Control
    // ------------------------------------------------------------------------

    /**
     * @brief Start SRM console with banner
     */
    void start() noexcept;

    /**
     * @brief Run one iteration of console loop
     * @return true to continue, false to halt
     */
    bool step() noexcept;

    /**
     * @brief Stop console and clean up
     */
    void stop() noexcept;

    /**
     * @brief Check if console is running
     * @return true if console loop is active
     */
    bool isRunning() const noexcept;

    // ------------------------------------------------------------------------
    // Banner and Prompt
    // ------------------------------------------------------------------------

    /**
     * @brief Display SRM boot banner
     */
    void showBanner() noexcept;

    /**
     * @brief Display command prompt
     */
    void showPrompt() noexcept;

    // ------------------------------------------------------------------------
    // Command Processing
    // ------------------------------------------------------------------------

    /**
     * @brief Process a complete command line
     * @param commandLine Command string from user
     */
    void processCommand(const QString& commandLine) noexcept;

    /**
     * @brief Parse command line into tokens
     * @param commandLine Raw command string
     * @return List of command tokens
     */
    static QStringList parseCommand(const QString& commandLine) noexcept;

   
     /**
     * @brief Put string with newline
     * @param str String to output
     */
    void putLine(const QString& str) noexcept;
    void putLine(const std ::string &str) noexcept;
    // Extended Messaging
    static void        putLine(SRMConsoleDevice* console);
    static void putLine(SRMConsoleDevice* console, const QString& str);
    static void putText(SRMConsoleDevice* console, const QString& str);
private:
    // ------------------------------------------------------------------------
    // Console I/O (CSERVE Integration)
    // ------------------------------------------------------------------------

    /**
     * @brief Get character from console (CSERVE 0x01)
     * @return Character code (0-255) or -1 if no data
     */
    int getChar() const noexcept;

    /**
     * @brief Put character to console (CSERVE 0x02)
     * @param ch Character to output
     */
    void putChar(char ch) const noexcept;

    /**
     * @brief Put string to console (CSERVE 0x09)
     * @param str String to output
     */
    void putString(const QString& str) const noexcept;



    // ------------------------------------------------------------------------
    // Line Editor
    // ------------------------------------------------------------------------

    /**
     * @brief Read complete line with editing support
     * @return Complete command line (without newline)
     */
    QString readLine() noexcept;

    /**
     * @brief Handle backspace during line input
     */
    void handleBackspace() noexcept;

    static void handleShowConfig(SRMConsoleDevice* console);
    static void handleShowDevice();
    static void handleBoot(SRMConsoleDevice* console, const QString& args);
    void        handleBootCommand(const QString& deviceName);
    /**
     * @brief Echo character to console
     * @param ch Character to echo
     */
    void echoChar(char ch) const noexcept;

    // ------------------------------------------------------------------------
    // Command Implementations
    // ------------------------------------------------------------------------

    /**
     * @brief HELP command - show available commands
     */
    void cmdHelp(const QStringList& args) noexcept;

    /**
     * @brief SHOW DEVICE - enumerate devices
     */
    void cmdShowDevice(const QStringList& args) noexcept;

    void    cmdReloadConfig(const QStringList& args) noexcept{}
    void    processDeviceChanges(const emulatr::config::EmulatorSettings& newSettings, const QSet<QString>& newDevices, const QSet<QString>& removedDevices, const QSet<QString>& existingDevices) noexcept {}
    void    regenerateDeviceListingWithNewConfig(const emulatr::config::EmulatorSettings& newSettings) noexcept {}
    
    
    void    reportConfigurationChanges(const QSet<QString>& newDevices, const QSet<QString>& removedDevices, const QSet<QString>& existingDevices) noexcept;
    QString getConfigurationPath() const noexcept;
    void    cmdShowDeviceWithOffline(const QStringList& args) noexcept;
    /**
     * @brief SHOW CONFIG - display system configuration
     */
    void cmdShowConfig(const QStringList& args) noexcept;

    /**
     * @brief SHOW * - list all environment variables
     */
    void cmdShowAll(const QStringList& args) noexcept;

    /**
     * @brief SHOW <var> - show specific environment variable
     */
    void cmdShowVar(const QString& varName) noexcept;

    /**
     * @brief SET <var> <value> - set environment variable
     */
    void cmdSet(const QStringList& args) noexcept;

    /**
     * @brief BOOT [device] [flags] - initiate boot sequence
     */
    void cmdBoot(const QStringList& args) noexcept;

    /**
     * @brief HALT - halt system
     */
    void cmdHalt(const QStringList& args) noexcept;

    /**
     * @brief CONTINUE - resume execution
     */
    void cmdContinue(const QStringList& args) noexcept;

    /**
     * @brief RESET - reset system
     */
    void cmdReset(const QStringList& args) noexcept;

    // ------------------------------------------------------------------------
    // Device Enumeration
    // ------------------------------------------------------------------------

    /**
     * @brief Generate device listing from configuration
     */
    void generateDeviceListing() noexcept;

    /**
     * @brief Map device to SRM name
     * @param deviceName Original device name
     * @param deviceConfig Device configuration
     * @return SRM device name (e.g., "dka0", "ewa0")
     */
    QString mapToSRMName(const QString& deviceName, const emulatr::config::DeviceConfig& deviceConfig) noexcept { return QString();  }

    static QString     determineSRMPrefix(const emulatr::config::DeviceConfig& deviceConfig) noexcept;
    QString     getDeviceStatistics() const noexcept;
    static bool isValidSRMName(const QString& srmName) noexcept;
    /**
     * @brief Format device description
     * @param deviceConfig Device configuration
     * @return Human-readable device description
     */
    static QString formatDeviceDescription(const emulatr::config::DeviceConfig& deviceConfig) noexcept;

    /**
     * @brief Format device path string
     * @param deviceConfig Device configuration
     * @return Device path for SRM display
     */
    static QString formatDevicePath(const emulatr::config::DeviceConfig& deviceConfig) noexcept;

    // ------------------------------------------------------------------------
    // System Information
    // ------------------------------------------------------------------------

    /**
     * @brief Format CPU information
     * @return CPU description string
     */
    QString formatCPUInfo() noexcept { return QString(); }

    /**
     * @brief Format memory information
     * @return Memory description string
     */
    QString formatMemoryInfo() const noexcept;

    /**
     * @brief Format system summary
     * @return System configuration summary
     */
    QString formatSystemInfo() const noexcept;

    // ------------------------------------------------------------------------
    // Boot Processing
    // ------------------------------------------------------------------------

    /**
     * @brief Resolve boot device name to configuration
     * @param deviceName SRM device name or alias
     * @return Device configuration, or nullptr if not found
     */
    const emulatr::config::DeviceConfig* resolveBootDevice(const QString& deviceName) const noexcept;

    /**
     * @brief Initiate boot from device
     * @param device Boot device configuration
     * @param flags Boot flags
     */
    void initiateBootSequence(const emulatr::config::DeviceConfig& device, const QString& flags) noexcept;

    // ------------------------------------------------------------------------
    // Utility Methods
    // ------------------------------------------------------------------------

    /**
     * @brief Print error message
     * @param message Error message
     */
    void printError(const QString& message) noexcept;

    /**
     * @brief Check if string matches pattern (case-insensitive)
     * @param str String to check
     * @param pattern Pattern to match
     * @return true if matches
     */
    static bool matchesPattern(const QString& str, const QString& pattern) noexcept;

    int     getNextIndex(const QString& prefix) const noexcept;
    QString mapToSRMName(const QString& deviceName, const emulatr::config::DeviceConfig& config) const noexcept;
    /**
     * @brief Execute CSERVE call
     * @param function CSERVE function code
     * @param a0 Argument 0
     * @param a1 Argument 1
     * @param a2 Argument 2
     * @param a3 Argument 3
     * @return Return value from PAL
     *  Note: SRMConsole's CSERVE is a logical console service, not a literal CALL_PAL ABI.
     */
    quint64 executeCSERVE(quint8 function, quint64 a0 = 0, quint64 a1 = 0, quint64 a2 = 0, quint64 a3 = 0) const noexcept;

    // ------------------------------------------------------------------------
    // Member Data
    // ------------------------------------------------------------------------

    // Configuration and state
    const emulatr::config::EmulatorSettings& m_settings;        // Emulator configuration
    SRMEnvStore& m_envStore;   // Environment variables
    uint16_t m_cpuId{ 0 };     // Processor context

    // Console state
    bool m_running{ false };                      // Console active flag
    QString m_currentLine;                      // Current input line
    bool m_initialized{ false };                  // Initialization complete

    // Device mappings
    QMap<QString, QString> m_deviceAliases;    // SRM name -> original name
    QStringList m_deviceList;                  // Formatted device strings

    // Command dispatch table
    QMap<QString, std::function<void(const QStringList&)>> m_commands;

    // Constants
    static constexpr int MAX_LINE_LENGTH = 256;
    static constexpr char BACKSPACE = '\b';
    static constexpr char DELETE_ = '\x7F';
    static constexpr char CTRL_C = '\x03';
    static constexpr char CTRL_U = '\x15';
    static constexpr char CR = '\r';
    static constexpr char LF = '\n';
};

#endif // SRMCONSOLE_H
