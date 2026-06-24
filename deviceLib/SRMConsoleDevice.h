// ============================================================================
// SRMConsoleDevice.h - Get single character (CSERVE 0x01 - GETC)
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

#ifndef SRMCONSOLEDEVICE_H
#define SRMCONSOLEDEVICE_H
// ============================================================================
// SRMConsoleDevice.h
// ============================================================================
// Copyright (c) 2025 EmulatR Project. All rights reserved.
// Licensed under the MIT License. See LICENSE file in the project root.
// ============================================================================
// SRM-Compliant Console Device
//
// Purpose:
//   Provides console I/O services for Alpha SRM firmware
//   Implements full CSERVE specification (GETC/PUTC/PUTS/GETS/POLL)
//
// Features:
//   - Blocking and non-blocking I/O
//   - Line editing support (GETS)
//   - Thread-safe (mutex + wait condition)
//   - TCP transport (PuTTY RAW mode recommended)
//   - Echo control
//   - Backspace/delete handling
//
// SRM Requirements:
//   CSERVE 0x01 - GETC  (get character, blocking or non-blocking)
//   CSERVE 0x02 - PUTC  (put character)
//   CSERVE 0x09 - PUTS  (put string)
//   CSERVE 0x0C - GETS  (get string with line editing)
//   CSERVE Poll - Check input availability
//
// References:
//   - Alpha Architecture Reference Manual (Console Services)
//   - Alpha SRM Console Architecture Specification
//   - Digital UNIX PALcode Specification
// ============================================================================

#include <QObject>
#include <QtNetwork/QTcpServer>
#include <QtNetwork/QTcpSocket>
#include <QQueue>
#include <QMutex>
#include <QWaitCondition>
#include <QThread>           // 2026-05-29: backend lives on its own QThread so
                             // QTcpServer/QTcpSocket signals deliver on a thread
                             // with a running event loop, independent of main.
#include "IConsoleDevice.h"

// ============================================================================
// SRMConsoleDevice
// ============================================================================
class SRMConsoleDevice : public QObject, public IConsoleDevice
{
    Q_OBJECT

public:
    // ------------------------------------------------------------------------
    // Configuration
    // ------------------------------------------------------------------------
    struct Config {
        quint16 port{ 23 };                    // TCP port (23 = telnet, for PuTTY RAW)
        quint32 rxBufferSize{ 4096 };          // RX queue size
        quint32 defaultTimeoutMs{ 30000 };     // Default blocking timeout (30s)
        bool echoEnabled{ true };              // Echo input by default
        bool emitBanner{ true };               // Emit EmulatR connect-time identity banner
        bool autoLaunchPutty{ false };         // Auto-launch PuTTY on start
        QString puttyPath{ "putty.exe" };      // Path to PuTTY executable
        Config() = default;
    };

    // ------------------------------------------------------------------------
    // Construction
    // ------------------------------------------------------------------------
    explicit SRMConsoleDevice(Config& config, QObject* parent = nullptr);
    ~SRMConsoleDevice() override;

    // Delete copy/move
    SRMConsoleDevice(const SRMConsoleDevice&) = delete;
    SRMConsoleDevice& operator=(const SRMConsoleDevice&) = delete;

    // ------------------------------------------------------------------------
    // Lifecycle
    // ------------------------------------------------------------------------
    bool start();
    void stop();
    bool isRunning() const;

    // ------------------------------------------------------------------------
    // IConsoleDevice Interface (CSERVE Implementation)
    // ------------------------------------------------------------------------

    /**
     * @brief Get single character (CSERVE 0x01 - GETC)
     * @param blocking If true, wait for input
     * @param timeoutMs Timeout in milliseconds (0 = use default, UINT32_MAX = infinite)
     * @return Character (0-255) or -1 on timeout/error
     *
     * SRM behavior:
     *   - Blocking mode: Waits for input (required for console prompt)
     *   - Non-blocking mode: Returns immediately with -1 if no data
     *   - Timeout: Returns -1 if no input within timeout
     */
    int getChar(bool blocking = false, quint32 timeoutMs = 0) override;

    /**
     * @brief Put single character (CSERVE 0x02 - PUTC)
     * @param ch Character to output (0-255)
     *
     * SRM behavior:
     *   - Emits exactly one byte to console
     *   - No translation (raw byte)
     *   - Flushes immediately
     */
    void putChar(quint8 ch) override;

    /**
     * @brief Put string (CSERVE 0x09 - PUTS)
     * @param data Pointer to data buffer
     * @param len Number of bytes to write
     * @return Number of bytes actually written
     *
     * SRM behavior:
     *   - Writes N bytes from buffer
     *   - No null-termination required
     *   - Flushes after write
     */
    quint64 putString(const quint8* data, quint64 len) override;

    /**
     * @brief Get string with line editing (CSERVE 0x0C - GETS)
     * @param buffer Output buffer
     * @param maxLen Maximum bytes to read (including null terminator)
     * @param echo If true, echo characters as typed
     * @return Number of bytes read (excluding null terminator)
     *
     * SRM behavior:
     *   - Reads until CR/LF or maxLen-1 characters
     *   - Supports backspace/delete editing
     *   - Null-terminates buffer
     *   - Echo mode: displays characters as typed
     *   - Cooked mode: CR -> CRLF translation
     */
    quint64 getString(quint8* buffer, quint64 maxLen, bool echo = true) override;

    // ------------------------------------------------------------------------
    // Legacy char-based API (IConsoleDevice compatibility)
    // ------------------------------------------------------------------------

    /**
     * @brief Read single character (blocking)
     * @return Character (0-255) or -1 on error
     *
     * Wrapper around getChar(true) for compatibility
     */
    int readChar() override
    {
        return getChar(true, UINT32_MAX);  // Blocking, infinite timeout
    }

    /**
     * @brief Write single character
     * @param ch Character to output
     *
     * Wrapper around putChar() for compatibility
     */
    void writeChar(char ch) override
    {
        putChar(static_cast<quint8>(ch));
    }

    // ------------------------------------------------------------------------
    // Convenience Methods (QString-based output)
    // ------------------------------------------------------------------------

    /**
     * @brief Write QString to console (no newline)
     */
    void putText(const QString& str)
    {
        QByteArray utf8 = str.toUtf8();
        putString(reinterpret_cast<const quint8*>(utf8.constData()), utf8.size());
    }

    /**
     * @brief Write QString to console with CRLF
     */
    void putLine(const QString& str)
    {
        putText(str);
        putChar('\r');
        putChar('\n');
    }

    /**
     * @brief Write blank line (CRLF only)
     */
    void putLine()
    {
        putChar('\r');
        putChar('\n');
    }

    // ------------------------------------------------------------------------
    // Status Query
    // ------------------------------------------------------------------------

    /**
     * @brief Check if input is available (CSERVE Poll)
     * @return true if data ready to read
     */
    bool hasInput() const override;

    /**
     * @brief Check if console is connected
     * @return true if TCP client connected
     */
    bool isConnected() const override;

    /**
     * @brief Reset console state
     */
    void reset() override;

    // ------------------------------------------------------------------------
    // Configuration Access
    // ------------------------------------------------------------------------
    const Config& config() const { return m_config; }
    quint16 port() const { return m_config.port; }

    // 2026-05-30: bound port reported by the QTcpServer.  Differs from
    // port() above when m_config.port == 0 (ephemeral / OS-assigned).
    // Required by the doctest harness that needs to connect a client
    // socket without knowing the port in advance.  Returns 0 if the
    // server is not listening.
    quint16 boundPort() const { return m_server.serverPort(); }

signals:
    /**
     * @brief Emitted when input becomes available
     * Can be used for IRQ generation in future
     */
    void inputAvailable();

    /**
     * @brief Emitted when client connects
     */
    void clientConnected();

    /**
     * @brief Emitted when client disconnects
     */
    void clientDisconnected();

    // ------------------------------------------------------------------------
    // Internal cross-thread TX wake-up signal -- 2026-05-29
    // ------------------------------------------------------------------------
    // putChar / putString are called from the emulator thread, but m_socket
    // lives on the backend QThread.  Emitting txReady() with a Qt::Queued
    // connection wakes the backend thread's event loop, which then runs
    // onDrainTx() under the backend thread's affinity to actually call
    // m_socket->write().  No socket access ever happens off-thread.
    // ------------------------------------------------------------------------
    void txReady();

private slots:
    void onNewConnection();
    void onReadyRead();
    void onDisconnected();

    // 2026-05-29: drains m_txQueue from the backend thread.  Connected via
    // Qt::QueuedConnection from the txReady() signal so that no matter
    // which thread emits, the slot runs where m_socket lives.
    void onDrainTx();

    // 2026-05-30: backend-thread setup/teardown slots.  start() and stop()
    // run on the caller's thread, but m_server.listen() and socket
    // teardown must run on the backend thread so QNativeSocketEngine's
    // thread affinity matches every subsequent m_socket->write().
    // Invoked via QMetaObject::invokeMethod(Qt::BlockingQueuedConnection)
    // so the caller still gets synchronous start/stop semantics.  See
    // start()/stop() rationale in the .cpp.
    bool doStart();
    void doStop();

private:
    // ------------------------------------------------------------------------
    // Internal Helpers
    // ------------------------------------------------------------------------
    void launchPutty();
    bool writeRaw(const quint8* data, qint64 len);
    bool writeRaw(quint8 ch) { return writeRaw(&ch, 1); }

    // Line editing helpers
    void handleBackspace(QByteArray& lineBuffer, bool echo);
    void handleDelete(QByteArray& lineBuffer, bool echo);

    // ------------------------------------------------------------------------
    // State
    // ------------------------------------------------------------------------
    Config m_config;

    QTcpServer m_server;
    QTcpSocket* m_socket{ nullptr };

    mutable QMutex m_mutex;
    QWaitCondition m_rxCondition;  // For blocking reads

    QQueue<quint8> m_rxQueue;

    // ------------------------------------------------------------------------
    // 2026-05-29: cross-thread TX path
    // ------------------------------------------------------------------------
    // m_txQueue accumulates bytes from putChar/putString called on the
    // emulator thread; onDrainTx pulls them off on the backend thread
    // and hands them to m_socket->write() under proper owner-thread
    // affinity.  Shared with m_mutex.
    // m_thread is the QThread the backend lives on after start();
    // ownership chain: Machine -> SRMConsoleDevice -> m_thread.  The
    // thread is started by start() (after listen() + launchPutty
    // succeed on the main thread) and joined by stop() during Machine
    // destruction.
    // ------------------------------------------------------------------------
    QQueue<quint8> m_txQueue;
    QThread m_thread;

    bool m_running{ false };
};

#endif // SRMCONSOLEDEVICE_H
