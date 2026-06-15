// ============================================================================
// SRMConsoleDevice.cpp - ============================================================================
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

#include "SRMConsoleDevice.h"
#include "coreLib/LoggingMacros.h"
#include <spdlog/spdlog.h>
#include <QProcess>
#include <QtNetwork/QHostAddress>
#include <algorithm>

#define COMPONENT_NAME "SRMConsole"

// ============================================================================
// ASCII Control Characters
// ============================================================================
namespace ASCII {
    constexpr uint8_t NUL = 0x00;
    constexpr uint8_t BS = 0x08;  // Backspace
    constexpr uint8_t LF = 0x0A;  // Line feed
    constexpr uint8_t CR = 0x0D;  // Carriage return
    constexpr uint8_t DEL = 0x7F;  // Delete
    constexpr uint8_t ESC = 0x1B;  // Escape
}

// ============================================================================
// Connect-time program-identity banner (2026-06-05).
// ============================================================================
// Emitted into the TX queue when a console client (plink/PuTTY) attaches,
// so the EmulatR brand prints on the SRM console terminal itself -- above
// the genuine DEC firmware banner in the common "open PuTTY, watch boot"
// flow (queue empty at connect).  This is EmulatR's voice, NOT emulated
// firmware output.  ASCII(128) only; explicit CRLF for the raw terminal.
// Mirrors the V1 SRMConsole::showBanner text.
// ============================================================================
namespace {
    constexpr char kConsoleBanner[] =
        "\r\n"
        "ASA EmulatR -- Alpha AXP (EV6 / 21264) Emulator\r\n"
        "Alpha Emulator Console V4.0-0\r\n"
        "(c) 2026 Timothy Peer / eNVy Systems, Inc.\r\n"
        "\r\n";
}

// ============================================================================
// Construction
// ============================================================================

SRMConsoleDevice::SRMConsoleDevice(Config& config, QObject* parent)
    : QObject(parent)
    , m_config(config)
{
    // 2026-05-30: reparent m_server to this so a later moveToThread on
    // this also moves m_server (QObject's moveToThread cascades to
    // children only).  Without this, m_server stayed on the caller's
    // thread, m_server.listen() bound the QNativeSocketEngine's thread
    // affinity to the caller, then m_socket->write() called from the
    // backend QThread tripped the "Cannot create children for a parent
    // that is in a different thread" warning.  doctest test 2 + 4 fired
    // it on every TX before this fix; test 3 (RX-only) did not.
    m_server.setParent(this);

    // Wire up server signals
    connect(&m_server, &QTcpServer::newConnection,
        this, &SRMConsoleDevice::onNewConnection);

    // 2026-05-29 -- cross-thread TX wakeup.  putChar / putString run on
    // the emulator thread; they enqueue bytes under m_mutex and emit
    // txReady().  This connection is Qt::QueuedConnection so the slot
    // ALWAYS runs on the receiver's thread (this == backend, lives on
    // m_thread after start()).  Without this, putChar would be calling
    // m_socket->write() across owner threads, which is undefined.
    connect(this, &SRMConsoleDevice::txReady,
            this, &SRMConsoleDevice::onDrainTx,
            Qt::QueuedConnection);
}

SRMConsoleDevice::~SRMConsoleDevice()
{
    stop();
}

// ============================================================================
// Lifecycle
// ============================================================================

// ----------------------------------------------------------------------------
// start() -- 2026-05-30 thread-affinity-correct revision
// ----------------------------------------------------------------------------
// Sequence (replaces the old "listen-then-moveToThread" order that triggered
// "Cannot create children for a parent that is in a different thread"):
//
//   1.  Lock briefly to flip m_running -> true.
//   2.  moveToThread(&m_thread); m_thread.start() so the backend event
//       loop is up and running.
//   3.  QMetaObject::invokeMethod(this, "doStart", Qt::BlockingQueuedConnection)
//       so m_server.listen() runs on the backend thread.  The internal
//       QNativeSocketEngine binds its thread affinity to the backend
//       thread, and every later m_socket->write() finds matching
//       affinity -- no more Qt warning, no more silently dropped TX.
//   4.  PuTTY auto-launch is QProcess::startDetached on the caller's
//       thread (it has its own threading model; no socket touching).
//
// The BlockingQueuedConnection preserves the synchronous start()
// contract -- callers learn immediately whether listen() succeeded.
// ----------------------------------------------------------------------------
bool SRMConsoleDevice::start()
{
    {
        QMutexLocker lock(&m_mutex);
        if (m_running) {
            SPDLOG_WARN("SRM Console already running");
            return true;
        }
        m_running = true;
    }

    moveToThread(&m_thread);
    m_thread.start();

    bool ok = false;
    QMetaObject::invokeMethod(this, "doStart",
                              Qt::BlockingQueuedConnection,
                              Q_RETURN_ARG(bool, ok));

    if (!ok) {
        // Roll back: stop the backend thread and clear m_running.
        m_thread.quit();
        m_thread.wait();
        QMutexLocker lock(&m_mutex);
        m_running = false;
        return false;
    }

    if (m_config.autoLaunchPutty) {
        launchPutty();
    }

    return true;
}

// ----------------------------------------------------------------------------
// doStart() -- private slot, runs on the backend QThread (invoked via
// BlockingQueuedConnection from start()).  Performs m_server.listen()
// on the correct thread so QNativeSocketEngine's affinity matches every
// downstream m_socket child created in onNewConnection.
//
// Log line uses m_server.serverPort() instead of m_config.port so the
// real OS-assigned port is visible when callers pass port=0
// (ephemeral) -- the old log misled by printing "Listening on TCP port 0".
// ----------------------------------------------------------------------------
bool SRMConsoleDevice::doStart()
{
    if (!m_server.listen(QHostAddress::Any, m_config.port)) {
        SPDLOG_ERROR("SRM Console: Failed to listen on port {}: {}",
                     m_config.port,
                     m_server.errorString().toStdString());
        return false;
    }
    SPDLOG_INFO("SRM Console: Listening on TCP port {} (requested {})",
                m_server.serverPort(),
                m_config.port);
    return true;
}

// ----------------------------------------------------------------------------
// stop() -- 2026-05-30 thread-affinity-correct revision
// ----------------------------------------------------------------------------
// Mirror of start().  Socket / server teardown must happen on the
// backend thread (where they live after start's moveToThread); doing
// it from the caller's thread is the same UB as the old listen()
// placement.  Schedule via BlockingQueuedConnection to keep stop()
// synchronous; then quit + join the backend thread.  Tolerates being
// called twice (the m_running guard makes the second call a no-op).
// ----------------------------------------------------------------------------
void SRMConsoleDevice::stop()
{
    {
        QMutexLocker lock(&m_mutex);
        if (!m_running) {
            return;
        }
        m_running = false;
    }

    if (m_thread.isRunning()) {
        QMetaObject::invokeMethod(this, "doStop",
                                  Qt::BlockingQueuedConnection);
        m_thread.quit();
        m_thread.wait();
    }
}

// ----------------------------------------------------------------------------
// doStop() -- private slot, runs on backend QThread.  Safe to touch
// m_socket and m_server here because they all live on this thread
// after the start() refactor.  Mutex protects only the per-queue state
// (m_rxQueue / m_txQueue / m_rxCondition) which is shared with the
// emulator-side putChar / getChar callers.
// ----------------------------------------------------------------------------
void SRMConsoleDevice::doStop()
{
    // Re-entrancy-safe teardown (FIX 2026-05-30): null m_socket FIRST and
    // operate on a local handle.  disconnectFromHost() can emit `disconnected`
    // synchronously; onDisconnected runs on this same backend thread, nulls
    // m_socket and deleteLater()s it -- the prior code then dereferenced the
    // now-null m_socket on the next line (deleteLater()) and faulted with an
    // access violation on every halt-driven stop.  Detaching this object's
    // slots from the socket up front makes the re-entrant onDisconnected a
    // no-op (it sees m_socket == nullptr and returns).
    if (QTcpSocket* const sock = m_socket) {
        m_socket = nullptr;
        sock->disconnect(this);          // drop readyRead / disconnected slots
        sock->disconnectFromHost();
        sock->deleteLater();
    }
    m_server.close();

    {
        QMutexLocker lock(&m_mutex);
        m_rxQueue.clear();
        m_txQueue.clear();
        m_rxCondition.wakeAll();
    }
    SPDLOG_INFO("SRM Console: Stopped");
}

bool SRMConsoleDevice::isRunning() const
{
    QMutexLocker lock(&m_mutex);
    return m_running;
}

// ============================================================================
// CSERVE 0x01 - GETC (Get Character)
// ============================================================================

int SRMConsoleDevice::getChar(bool blocking, quint32 timeoutMs)
{
    QMutexLocker lock(&m_mutex);

    // Non-blocking mode - return immediately
    if (!blocking) {
        if (m_rxQueue.isEmpty()) {
            return -1;
        }
        return static_cast<int>(m_rxQueue.dequeue());
    }

    // Blocking mode - wait for data
    quint32 timeout = timeoutMs;
    if (timeout == 0) {
        timeout = m_config.defaultTimeoutMs;
    }

    while (m_rxQueue.isEmpty()) {
        if (timeout == UINT32_MAX) {
            // Infinite wait
            m_rxCondition.wait(&m_mutex);
        }
        else {
            // Timed wait
            if (!m_rxCondition.wait(&m_mutex, timeout)) {
                SPDLOG_TRACE("SRM Console: GETC timeout");
                return -1;  // Timeout
            }
        }

        // Check if we were woken up for shutdown
        if (!m_running) {
            return -1;
        }
    }

    int ch = static_cast<int>(m_rxQueue.dequeue());

    SPDLOG_TRACE("SRM Console: GETC -> 0x{:02x} ('{}')", ch, static_cast<char>(ch));

    return ch;
}

// ============================================================================
// CSERVE 0x02 - PUTC (Put Character)
// ============================================================================

void SRMConsoleDevice::putChar(uint8_t ch)
{
    // 2026-05-29: cross-thread safe.  Enqueue under lock, then signal
    // the backend thread via txReady() (Qt::QueuedConnection -> onDrainTx
    // runs where m_socket lives).  Direct m_socket->write() from this
    // thread is NOT safe because the socket's owner thread is m_thread.
    //
    // If the client has not connected yet, the bytes accumulate in
    // m_txQueue.  onDrainTx leaves them there until isConnected; the
    // RX buffer-overflow pattern used elsewhere caps growth.  In
    // practice the SRM banner is a few hundred bytes -- the queue
    // never grows large enough to matter.
    {
        QMutexLocker lock(&m_mutex);
        m_txQueue.enqueue(ch);
    }
    emit txReady();

    SPDLOG_TRACE("SRM Console: PUTC <- 0x{:02x} ('{}')", ch, static_cast<char>(ch));
}

// ============================================================================
// CSERVE 0x09 - PUTS (Put String)
// ============================================================================

quint64 SRMConsoleDevice::putString(const uint8_t* data, quint64 len)
{
    if (!data || len == 0) {
        return 0;
    }

    // 2026-05-29: cross-thread safe.  Same enqueue-and-signal pattern as
    // putChar -- onDrainTx runs on the backend thread, batches everything
    // currently queued into a single QTcpSocket::write() call.  Returns
    // len regardless of TCP connection state -- the bytes are committed
    // to our TX queue; whether and when the wire ships them is the
    // backend thread's problem.
    {
        QMutexLocker lock(&m_mutex);
        for (quint64 i = 0; i < len; ++i) {
            m_txQueue.enqueue(static_cast<quint8>(data[i]));
        }
    }
    emit txReady();

    SPDLOG_TRACE("SRM Console: PUTS <- {} bytes", len);

    return len;
}

// ============================================================================
// CSERVE 0x0C - GETS (Get String with Line Editing)
// ============================================================================

quint64 SRMConsoleDevice::getString(uint8_t* buffer, quint64 maxLen, bool echo)
{
    if (!buffer || maxLen < 2) {
        return 0;  // Need at least space for 1 char + null
    }

    QByteArray lineBuffer;
    lineBuffer.reserve(static_cast<int>(maxLen));

    while (true) {
        // Get next character (blocking)
        int ch = getChar(true, UINT32_MAX);  // Infinite wait

        if (ch < 0) {
            break;  // Timeout or error
        }

        uint8_t c = static_cast<uint8_t>(ch);

        // Handle special characters
        if (c == ASCII::CR || c == ASCII::LF) {
            // End of line
            if (echo) {
                putChar(ASCII::CR);
                putChar(ASCII::LF);
            }
            break;
        }
        else if (c == ASCII::BS || c == ASCII::DEL) {
            // Backspace/Delete
            handleBackspace(lineBuffer, echo);
        }
        else if (c >= 0x20 && c < 0x7F) {
            // Printable character
            if (lineBuffer.size() < static_cast<int>(maxLen - 1)) {
                lineBuffer.append(static_cast<char>(c));
                if (echo) {
                    putChar(c);
                }
            }
            // else: buffer full, ignore
        }
        // else: ignore control characters
    }

    // Copy to output buffer
    quint64 len = static_cast<quint64>(lineBuffer.size());
    len = std::min(len, maxLen - 1);

    if (len > 0) {
        memcpy(buffer, lineBuffer.constData(), len);
    }

    // Null-terminate
    buffer[len] = 0;

    SPDLOG_TRACE("SRM Console: GETS -> {} bytes", len);

    return len;
}

// ============================================================================
// CSERVE Poll - Check Input Availability
// ============================================================================

bool SRMConsoleDevice::hasInput() const
{
    QMutexLocker lock(&m_mutex);
    return !m_rxQueue.isEmpty();
}

// ============================================================================
// Connection Status
// ============================================================================

bool SRMConsoleDevice::isConnected() const
{
    QMutexLocker lock(&m_mutex);
    return m_socket && m_socket->state() == QAbstractSocket::ConnectedState;
}

// ============================================================================
// Reset
// ============================================================================

void SRMConsoleDevice::reset()
{
    QMutexLocker lock(&m_mutex);

    m_rxQueue.clear();
    m_rxCondition.wakeAll();

    SPDLOG_DEBUG("SRM Console: Reset");
}

// ============================================================================
// Qt Slots - Network Events
// ============================================================================

void SRMConsoleDevice::onNewConnection()
{
    QMutexLocker lock(&m_mutex);

    if (m_socket) {
        // Already have a client - reject
        QTcpSocket* newSocket = m_server.nextPendingConnection();
        newSocket->disconnectFromHost();
        newSocket->deleteLater();

        SPDLOG_WARN("SRM Console: Rejected connection (already connected)");
        return;
    }

    // Accept new connection
    m_socket = m_server.nextPendingConnection();

    if (!m_socket) {
        SPDLOG_ERROR("SRM Console: Failed to accept connection");
        return;
    }

    // Wire up socket signals
    connect(m_socket, &QTcpSocket::readyRead,
        this, &SRMConsoleDevice::onReadyRead);
    connect(m_socket, &QTcpSocket::disconnected,
        this, &SRMConsoleDevice::onDisconnected);

    // 2026-05-30: bumped from SPDLOG_TRACE to SPDLOG_INFO so this signal
    // is visible in release / relwithdebinfo builds (where TRACE is
    // dropped).  This is the only log line that tells us PuTTY has
    // actually attached to our TCP listener -- distinguishing
    // "auto-launch succeeded + connected" from "auto-launch failed
    // silently / window hidden" was previously impossible without an
    // attached debugger.  "Client disconnected" is already INFO; this
    // restores symmetry.
    SPDLOG_INFO("SRM Console: Client connected from {}:{}",
        m_socket->peerAddress().toString().toStdString(),
        m_socket->peerPort());

    // Clear stale data
    m_rxQueue.clear();

    // 2026-06-05: emit the EmulatR program-identity banner the instant a
    // client attaches.  We already hold m_mutex here, so enqueue directly
    // (putChar would re-lock and deadlock).  The existing txReady emit
    // below flushes it.  In the common cold-boot flow the queue is empty
    // at connect, so the brand prints above the DEC firmware banner.
    if (m_config.emitBanner) {
        for (char const* p = kConsoleBanner; *p != '\0'; ++p) {
            m_txQueue.enqueue(static_cast<quint8>(*p));
        }
    }

    emit clientConnected();

    // 2026-05-29: any TX bytes that piled up while there was no client
    // (typically the SRM banner emitted before the user's PuTTY window
    // attached) are flushed now via the queued-connection wakeup.
    if (!m_txQueue.isEmpty()) {
        emit txReady();
    }
}

void SRMConsoleDevice::onReadyRead()
{
    QMutexLocker lock(&m_mutex);

    if (!m_socket) {
        return;
    }

    // Read all available data
    QByteArray data = m_socket->readAll();

    for (char c : data) {
        // Check buffer overflow
        if (m_rxQueue.size() >= static_cast<int>(m_config.rxBufferSize)) {
            // Drop oldest character (FIFO)
            m_rxQueue.dequeue();
            SPDLOG_WARN("SRM Console: Rx buffer overflow (dropped oldest)");
        }

        m_rxQueue.enqueue(static_cast<uint8_t>(c));
    }

    // Wake any blocked readers
    m_rxCondition.wakeAll();

    emit inputAvailable();

    SPDLOG_TRACE("SRM Console: Received {} bytes (queue: {})",
        data.size(),
        m_rxQueue.size());
}

void SRMConsoleDevice::onDisconnected()
{
    QMutexLocker lock(&m_mutex);

    SPDLOG_INFO("SRM Console: Client disconnected");

    if (m_socket) {
        m_socket->deleteLater();
        m_socket = nullptr;
    }

    // Clear buffers
    m_rxQueue.clear();

    // Wake any blocked readers (they'll get -1)
    m_rxCondition.wakeAll();

    emit clientDisconnected();
}

// ============================================================================
// onDrainTx -- backend-thread batch TX flush      (2026-05-29)
// ============================================================================
// Connected via Qt::QueuedConnection from txReady() in the constructor, so
// regardless of which thread emitted, this slot runs on m_thread (the
// thread that owns m_socket).  Steps:
//
//   1. Acquire m_mutex briefly.  If not connected, leave bytes in queue
//      and return -- a future onNewConnection-triggered txReady will
//      flush them.  Otherwise drain the entire queue into a local
//      QByteArray under the lock.
//   2. Release the lock.  Call m_socket->write() + flush() on the local
//      batch.  Holding the lock across the blocking I/O would serialize
//      every emulator-thread putChar against the network, which is the
//      bug we are fixing in the first place.
//
// Multiple txReady emissions may coalesce into a single onDrainTx call
// (Qt's event compression); that is exactly the behavior we want -- one
// batched write per scheduling slice instead of one per byte.
// ============================================================================
void SRMConsoleDevice::onDrainTx()
{
    QByteArray batch;
    QTcpSocket* sock = nullptr;
    {
        QMutexLocker lock(&m_mutex);

        if (!m_socket
            || m_socket->state() != QAbstractSocket::ConnectedState)
        {
            // No client -- bytes remain in m_txQueue for a later drain.
            // No TX-discard policy here; the queue is bounded only by
            // SRM banner length in practice.
            return;
        }

        if (m_txQueue.isEmpty()) {
            return;
        }

        batch.reserve(m_txQueue.size());
        while (!m_txQueue.isEmpty()) {
            batch.append(static_cast<char>(m_txQueue.dequeue()));
        }

        // Capture the socket under the lock so the write below cannot deref
        // a freshly-nulled m_socket (FIX 2026-05-30, same teardown race as
        // doStop()).  deleteLater() defers destruction to this thread's event
        // loop, which is not running while we are inside this slot, so the
        // captured handle stays valid for the duration of the write.
        sock = m_socket;
    }

    // Outside lock: blocking I/O against the kernel TCP buffer.  Other
    // threads can putChar()/putString() during this without serializing
    // against the wire.
    if (!sock) {
        // Defensive: the guard above already guarantees a non-null,
        // connected socket at capture time, so this cannot fire today --
        // but it makes the dereference site explicitly safe against future
        // refactors of the guard logic.
        return;
    }
    qint64 const written = sock->write(batch);
    sock->flush();

    if (written != batch.size()) {
        SPDLOG_WARN("SRM Console: TX short write: {} of {} bytes",
                    written, batch.size());
    }
}

// ============================================================================
// Internal Helpers
// ============================================================================

void SRMConsoleDevice::launchPutty()
{
    if (m_config.puttyPath.isEmpty()) {
        return;
    }

    // Build PuTTY command:
    // putty.exe -raw -P <port> localhost
    QStringList args;
    args << "-raw";
    args << "-P" << QString::number(m_config.port);
    // 2026-06-02: flag and value MUST be separate argv tokens (Qt quotes
    // each list element verbatim); a trailing space made PuTTY reject
    // "-sessionlog " as an unknown option.
    args << "-sessionlog" << "d:/emulatr/traces/app_output_&Y&M&D&T.log";
    args << "localhost";

    SPDLOG_INFO("SRM Console: Launching PuTTY: {} {}",
        m_config.puttyPath.toStdString(),
        args.join(" ").toStdString());

    if (QProcess::startDetached(m_config.puttyPath, args)) {
        SPDLOG_DEBUG("SRM Console: PuTTY launched successfully");
    }
    else {
        SPDLOG_WARN("SRM Console: Failed to launch PuTTY (non-fatal)");
    }
}

bool SRMConsoleDevice::writeRaw(const uint8_t* data, qint64 len)
{
    if (!m_socket || m_socket->state() != QAbstractSocket::ConnectedState) {
        return false;
    }

    qint64 written = m_socket->write(reinterpret_cast<const char*>(data), len);
    m_socket->flush();

    return written == len;
}

void SRMConsoleDevice::handleBackspace(QByteArray& lineBuffer, bool echo)
{
    if (lineBuffer.isEmpty()) {
        return;  // Nothing to delete
    }

    // Remove last character from buffer
    lineBuffer.chop(1);

    if (echo) {
        // VT100 backspace sequence: BS + SPACE + BS
        // (moves cursor back, erases character, moves cursor back again)
        putChar(ASCII::BS);
        putChar(' ');
        putChar(ASCII::BS);
    }
}

void SRMConsoleDevice::handleDelete(QByteArray& lineBuffer, bool echo)
{
    // Same as backspace for line editing
    handleBackspace(lineBuffer, echo);
}