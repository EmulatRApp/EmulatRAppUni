// ============================================================================
// tests/deviceLib/test_srm_console_device.cpp -- OPA0 console end-to-end
// ============================================================================
// Project: EmulatR -- Alpha AXP / EV6 Architecture Emulator (V4)
// Copyright (C) 2025, 2026 eNVy Systems, Inc.  All rights reserved.
// Licensed under eNVy Systems Non-Commercial License v1.1
//
// Project Architect: Timothy Peer
// AI Collaboration:  Claude (Anthropic)
//
// Commercial use prohibited without separate license.
// Contact:        peert@envysys.com  |  https://envysys.com
// Documentation:  https://timothypeer.github.io/ASA-EMulatR-Project/
// ============================================================================
//
// First Qt-using doctest in the V4 suite.  Exercises OPA0 (the primary
// SRM serial console) end-to-end across the SRMConsoleDevice TCP backend:
//
//   SRM byte
//     -> Uart16550::writeTHR (production path, not exercised here directly)
//     -> SRMConsoleDevice::putChar             <-- entry point under test
//     -> m_txQueue (cross-thread)
//     -> txReady() signal (Qt::QueuedConnection)
//     -> SRMConsoleDevice::onDrainTx (runs on backend QThread)
//     -> m_socket->write()
//     -> [TCP loopback]
//     -> client QTcpSocket::readyRead
//     -> client.read()
//
// Tests run with port=0 (ephemeral, OS-assigned) and autoLaunchPutty=false
// so neither real-run port 10023 nor any installed PuTTY are touched.
//
// Per house rule, doctest CHECK only -- never REQUIRE (V4 builds disable
// exceptions; REQUIRE expands to static_assert that fails compile).
// Reference: memory [[v4-doctest-no-require]].
//
// ============================================================================

#include "doctest.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QHostAddress>
#include <QtNetwork/QTcpSocket>

#include "deviceLib/SRMConsoleDevice.h"

namespace {

// ---------------------------------------------------------------------------
// Single QCoreApplication shared across every TEST_CASE in this file.
// ---------------------------------------------------------------------------
// Qt forbids constructing more than one QCoreApplication per process, so
// each TEST_CASE that needs Qt's event loop pumps this single instance
// rather than instantiating a fresh one.  Lazy-init via function-local
// static is safe under doctest because TEST_CASEs run sequentially on
// one thread.
QCoreApplication& testApp()
{
    static int    argc = 1;
    static char   prog[] = "Emulatr_tests";
    static char*  argv[] = { prog, nullptr };
    static QCoreApplication app(argc, argv);
    return app;
}

// ---------------------------------------------------------------------------
// Pump the Qt event loop for up to `budgetMs`, returning early once
// `predicate()` becomes true.  Used by tests that need to wait for an
// asynchronous Qt signal to be dispatched (clientConnected,
// onDrainTx -> m_socket->write -> client readyRead, ...).
// ---------------------------------------------------------------------------
template <typename Predicate>
bool pumpUntil(Predicate pred, int budgetMs)
{
    QElapsedTimer t;
    t.start();
    while (!pred()) {
        if (t.elapsed() >= budgetMs) {
            return false;
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }
    return true;
}

// ---------------------------------------------------------------------------
// Connect a fresh client socket to `device.boundPort()`, drain the
// device-side onNewConnection slot, and confirm both sides see the
// connection.  Returns the connected socket (owned by caller).
// ---------------------------------------------------------------------------
QTcpSocket* connectClient(SRMConsoleDevice& device)
{
    auto* client = new QTcpSocket();
    client->connectToHost(QHostAddress::LocalHost, device.boundPort());
    client->waitForConnected(500);

    // Let onNewConnection fire on the backend thread.
    pumpUntil([&] { return device.isConnected(); }, 500);
    return client;
}

}  // namespace

// ============================================================================
// TEST 1 -- start / stop is clean.
// ============================================================================
TEST_CASE("SRMConsoleDevice: start + stop lifecycle (ephemeral port)")
{
    (void)testApp();

    SRMConsoleDevice::Config cfg;
    cfg.port            = 0;       // ephemeral; OS picks a free port
    cfg.autoLaunchPutty = false;

    SRMConsoleDevice device(cfg);

    CHECK(device.start());
    CHECK(device.isRunning());
    CHECK(device.boundPort() != 0);  // OS actually assigned one

    device.stop();
    CHECK_FALSE(device.isRunning());
}

// ============================================================================
// TEST 2 -- TX path: device.putChar(byte) -> client socket reads byte.
// ============================================================================
TEST_CASE("SRMConsoleDevice: putChar('A') reaches connected client socket")
{
    (void)testApp();

    SRMConsoleDevice::Config cfg;
    cfg.port            = 0;
    cfg.autoLaunchPutty = false;
    cfg.emitBanner      = false;  // suppress connect-time banner so the test
                                  // reads its own TX bytes, not the brand

    SRMConsoleDevice device(cfg);
    CHECK(device.start());

    QTcpSocket* client = connectClient(device);
    CHECK(client->state() == QAbstractSocket::ConnectedState);
    CHECK(device.isConnected());

    device.putChar(static_cast<quint8>('A'));

    // Pump until the byte traverses txQueue -> onDrainTx -> socket
    // -> loopback -> client readyRead.  500 ms is generous for loopback.
    bool const arrived = pumpUntil(
        [&] { return client->bytesAvailable() >= 1; }, 500);
    CHECK(arrived);
    if (arrived) {
        QByteArray got = client->read(1);
        CHECK(got.size() == 1);
        CHECK(got.at(0) == 'A');
    }

    client->disconnectFromHost();
    delete client;
    device.stop();
}

// ============================================================================
// TEST 3 -- RX path: client socket writes byte -> device.getChar() returns it.
// ============================================================================
TEST_CASE("SRMConsoleDevice: client write -> getChar() returns byte")
{
    (void)testApp();

    SRMConsoleDevice::Config cfg;
    cfg.port            = 0;
    cfg.autoLaunchPutty = false;

    SRMConsoleDevice device(cfg);
    CHECK(device.start());

    QTcpSocket* client = connectClient(device);
    CHECK(device.isConnected());

    qint64 const written = client->write("X", 1);
    CHECK(written == 1);
    client->flush();

    // Pump until device's onReadyRead has fed the RX queue.
    pumpUntil([&] { return device.hasInput(); }, 500);
    CHECK(device.hasInput());

    int const ch = device.getChar(false, 0);   // non-blocking
    CHECK(ch == 'X');

    client->disconnectFromHost();
    delete client;
    device.stop();
}

// ============================================================================
// TEST 4 -- round-trip a multi-byte payload both directions.
// ============================================================================
TEST_CASE("SRMConsoleDevice: round-trip 'HELLO\\r\\n' device->client and back")
{
    (void)testApp();

    SRMConsoleDevice::Config cfg;
    cfg.port            = 0;
    cfg.autoLaunchPutty = false;
    cfg.echoEnabled     = false;  // disable echo so RX bytes don't bounce back
    cfg.emitBanner      = false;  // suppress connect-time banner (TX assertions)

    SRMConsoleDevice device(cfg);
    CHECK(device.start());

    QTcpSocket* client = connectClient(device);
    CHECK(device.isConnected());

    // ---- Device -> client direction (PUTS) ----------------------------
    constexpr char const* kPayload = "HELLO\r\n";
    constexpr quint64     kLen     = 7;

    quint64 const wrote = device.putString(
        reinterpret_cast<const quint8*>(kPayload), kLen);
    CHECK(wrote == kLen);

    bool const arrived = pumpUntil(
        [&] { return static_cast<quint64>(client->bytesAvailable()) >= kLen; },
        500);
    CHECK(arrived);
    if (arrived) {
        QByteArray got = client->read(static_cast<qint64>(kLen));
        CHECK(got.size() == static_cast<int>(kLen));
        CHECK(got == QByteArray(kPayload, static_cast<int>(kLen)));
    }

    // ---- Client -> device direction (each byte via getChar) -----------
    qint64 const wrote2 = client->write(kPayload, static_cast<qint64>(kLen));
    CHECK(wrote2 == static_cast<qint64>(kLen));
    client->flush();

    pumpUntil([&] { return device.hasInput(); }, 500);

    QByteArray devRx;
    for (quint64 i = 0; i < kLen; ++i) {
        // Pump a little between reads so any in-flight onReadyRead fires.
        pumpUntil([&] { return device.hasInput(); }, 100);
        int const ch = device.getChar(false, 0);
        if (ch < 0) break;
        devRx.append(static_cast<char>(ch));
    }
    CHECK(devRx.size() == static_cast<int>(kLen));
    CHECK(devRx == QByteArray(kPayload, static_cast<int>(kLen)));

    client->disconnectFromHost();
    delete client;
    device.stop();
}
