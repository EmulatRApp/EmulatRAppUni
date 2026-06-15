// ============================================================================
// ConsoleManager.cpp - Get OPA device by index
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

#include "ConsoleManager.h"
#include <spdlog/spdlog.h>
#include <QMutexLocker>
#define COMPONENT_NAME "ConsoleManager"

// ============================================================================
// Construction
// ============================================================================

ConsoleManager::ConsoleManager(QObject* parent)
    : QObject(parent)
{
}

ConsoleManager::~ConsoleManager()
{
    QMutexLocker lock(&m_mutex);
    extracted();
    m_devices.clear();
}

void ConsoleManager::extracted()
{
    for (auto* device : m_devices) {
        device->reset();
        delete device;
    }
}

// ============================================================================
// Lifecycle
// ============================================================================

bool ConsoleManager::initialize() noexcept
{
    EMULATR_LOG_DEBUG("ConsoleManager: Initializing");
    EMULATR_LOG_INFO("ConsoleManager: Initialized successfully");
    return true;
}

void ConsoleManager::shutdown() noexcept
{
    EMULATR_LOG_DEBUG("ConsoleManager: Shutting down");
    resetAll();
    EMULATR_LOG_INFO("ConsoleManager: Shutdown complete");
}

// ============================================================================
// Device Registration
// ============================================================================

bool ConsoleManager::registerDevice(const QString& name, IConsoleDevice* device)
{
    QMutexLocker lock(&m_mutex);

    if (m_devices.contains(name)) {
        SPDLOG_ERROR("Console {}: Already registered", name.toStdString());
        return false;
    }

    m_devices.insert(name, device);

    EMULATR_LOG_INFO("Console {}: Registered", name.toStdString());

    return true;
}

bool ConsoleManager::unregisterDevice(const QString& name)
{
    QMutexLocker lock(&m_mutex);

    if (!m_devices.contains(name)) {
        EMULATR_LOG_WARN("Console {}: Not registered", name.toStdString());
        return false;
    }

    auto* device = m_devices.take(name);
    delete device;

    EMULATR_LOG_INFO("Console {}: Unregistered", name.toStdString());

    return true;
}

IConsoleDevice* ConsoleManager::getDevice(const QString& name)
{
    QMutexLocker lock(&m_mutex);
    return m_devices.value(name, nullptr);
}

IConsoleDevice* ConsoleManager::getPrimaryConsole()
{
    return getDevice("OPA0");
}

bool ConsoleManager::hasDevice(const QString& name) const
{
    QMutexLocker lock(&m_mutex);
    return m_devices.contains(name);
}

QStringList ConsoleManager::deviceNames() const
{
    QMutexLocker lock(&m_mutex);
    return m_devices.keys();
}

qsizetype ConsoleManager::deviceCount() const
{
    QMutexLocker lock(&m_mutex);
    return m_devices.size();
}

// ============================================================================
// CSERVE Entry Points (Called by PalService::executeCSERVE)
// ============================================================================

/**
 * @brief Get OPA device by index
 * Internal helper - not thread-safe (caller must lock)
 */
IConsoleDevice* ConsoleManager::getOPADevice(int opaIndex)
{
    QString deviceName = QString("OPA%1").arg(opaIndex);
    return m_devices.value(deviceName, nullptr);
}

// ----------------------------------------------------------------------------
// CSERVE 0x01 - GETC
// ----------------------------------------------------------------------------

int ConsoleManager::getCharFromOPA(int opaIndex, bool blocking, quint32 timeoutMs)
{
    QMutexLocker lock(&m_mutex);

    IConsoleDevice* device = getOPADevice(opaIndex);
    if (!device) {
        EMULATR_LOG_WARN("CSERVE GETC: OPA{} not found", opaIndex);
        return -1;
    }

    lock.unlock();  // Release lock during potentially blocking I/O

    int ch = device->getChar(blocking, timeoutMs);

    if (ch >= 0) {
        EMULATR_CONSOLE_TRACE("CSERVE GETC <- 0x{:02x} ('{}')",
            ch,
            (ch >= 0x20 && ch < 0x7F) ? static_cast<char>(ch) : '.');
    }

    return ch;
}

// ----------------------------------------------------------------------------
// CSERVE 0x02 - PUTC
// ----------------------------------------------------------------------------

bool ConsoleManager::putCharToOPA(int opaIndex, quint8 ch)
{
    QMutexLocker lock(&m_mutex);

    IConsoleDevice* device = getOPADevice(opaIndex);
    if (!device) {
        EMULATR_LOG_WARN("CSERVE PUTC: OPA{} not found", opaIndex);
        return false;
    }

    lock.unlock();  // Release lock during I/O

    device->putChar(ch);

    EMULATR_CONSOLE_TRACE("CSERVE PUTC <- 0x{:02x} ('{}')",
        ch,
        (ch >= 0x20 && ch < 0x7F) ? static_cast<char>(ch) : '.');
    return true;
}

// ----------------------------------------------------------------------------
// CSERVE 0x09 - PUTS
// ----------------------------------------------------------------------------

quint64 ConsoleManager::putStringToOPA(int opaIndex, const quint8* data, quint64 len)
{
    if (!data || len == 0) {
        return 0;
    }

    QMutexLocker lock(&m_mutex);

    IConsoleDevice* device = getOPADevice(opaIndex);
    if (!device) {
        EMULATR_LOG_WARN("CSERVE PUTS: OPA{} not found", opaIndex);
        return 0;
    }

    lock.unlock();  // Release lock during I/O

    quint64 written = device->putString(data, len);

    EMULATR_CONSOLE_TRACE("CSERVE PUTS <- 0x{:02x} ('{}')",
        written,
        (written >= 0x20 && written < 0x7F) ? static_cast<char>(written) : '.');

    return written;
}

// ----------------------------------------------------------------------------
// CSERVE 0x0C - GETS
// ----------------------------------------------------------------------------

quint64 ConsoleManager::getStringFromOPA(int opaIndex, quint8* buffer, quint64 maxLen, bool echo)
{
    if (!buffer || maxLen < 2) {
        return 0;
    }

    IConsoleDevice* device = nullptr;
    {
        QMutexLocker lock(&m_mutex);
        device = getOPADevice(opaIndex);
    }   // lock released here, deterministically, by destructor

    if (!device) {
        EMULATR_LOG_WARN("CSERVE GETS: OPA{} not found", opaIndex);
        return 0;
    }

    quint64 bytesRead = device->getString(buffer, maxLen, echo);
    EMULATR_LOG_TRACE("CSERVE GETS: OPA{} -> {} bytes", opaIndex, bytesRead);
    return bytesRead;
}

// ----------------------------------------------------------------------------
// CSERVE Poll
// ----------------------------------------------------------------------------

bool ConsoleManager::hasInputOnOPA(int opaIndex) const
{
    QMutexLocker lock(&m_mutex);

    IConsoleDevice* device = const_cast<ConsoleManager*>(this)->getOPADevice(opaIndex);
    if (!device) {
        return false;
    }

    return device->hasInput();
}

// ----------------------------------------------------------------------------
// Connection Status
// ----------------------------------------------------------------------------

bool ConsoleManager::isAvailable_internal(int opaIndex) const
{
    // IMPORTANT: NO LOCK - caller must hold m_mutex

    // IMPORTANT: Assumes caller holds m_mutex
    QString deviceName = QString("OPA%1").arg(opaIndex);
    auto* device = m_devices.value(deviceName, nullptr);
    return device && device->isConnected();
}

bool ConsoleManager::isAvailable(int opaIndex) const
{
    QMutexLocker lock(&m_mutex);

    QString deviceName = QString("OPA%1").arg(opaIndex);
    auto* device = m_devices.value(deviceName, nullptr);

    if (!device) {
        return false;
    }

    return device->isConnected();
}

IConsoleDevice* ConsoleManager::getOPA(int index) const
{
    QMutexLocker lock(&m_mutex);

    QString deviceName = QString("OPA%1").arg(index);
    return m_devices.value(deviceName, nullptr);
}

// ============================================================================
// Maintenance
// ============================================================================

void ConsoleManager::resetAll()
{
    QMutexLocker lock(&m_mutex);

    for (auto* device : m_devices) {
        device->reset();
    }

    EMULATR_LOG_INFO("All consoles reset");
}
bool ConsoleManager::openConsole(int opaIndex)
{
    QMutexLocker lock(&m_mutex);

    // Use internal version (no double-lock)
    if (!isAvailable_internal(opaIndex)) {  //  No lock here!
        return false;
    }

    m_openedConsoles.insert(opaIndex);
    EMULATR_LOG_INFO("Console OPA{} opened", opaIndex);
    return true;
}

bool ConsoleManager::isConsoleOpen(int opaIndex) const
{
    QMutexLocker lock(&m_mutex);
    return m_openedConsoles.contains(opaIndex);
}