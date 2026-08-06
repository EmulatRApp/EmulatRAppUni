// ============================================================================
// src/EmulatorProcess.cpp -- subprocess wrapper: start, clean shutdown, escalation
// ============================================================================
// Project: EmulatR -- EmulatrLaunch (SPEC-LAUNCH-001 Rev D.2)
// Copyright (C) 2026 eNVy Systems, Inc.  All rights reserved.
// Licensed under eNVy Systems Non-Commercial License v1.1
//
// Project Architect: Timothy Peer
// AI Collaboration:  Claude (Anthropic)
//
// Version: 1.0.0-alpha   Date: 2026-07-29
// Spec:    Section 8 L2-L5, Section 11 E2/E6
// ============================================================================

#include "EmulatorProcess.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QTimer>

#include "Version.h"

namespace launch {

namespace {

constexpr int kDefaultShutdownTimeoutMs = 10000;   // L3

QString nowStamp()
{
    return QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss"));
}

QString nowIso()
{
    return QDateTime::currentDateTime().toString(Qt::ISODate);
}

}  // namespace

EmulatorProcess::EmulatorProcess(QObject* parent) : QObject(parent)
{
    m_process = new QProcess(this);
    m_process->setProcessChannelMode(QProcess::MergedChannels);

    connect(m_process, &QProcess::readyRead, this, &EmulatorProcess::onReadyRead);
    connect(m_process, &QProcess::finished, this, &EmulatorProcess::onFinished);
    connect(m_process, &QProcess::errorOccurred, this, &EmulatorProcess::onErrorOccurred);

    m_timer = new QTimer(this);
    m_timer->setSingleShot(true);
    connect(m_timer, &QTimer::timeout, this, &EmulatorProcess::onShutdownTimeout);
}

EmulatorProcess::~EmulatorProcess()
{
    // Do NOT kill here.  If the launcher is closing while a system runs, the
    // window asks the user first; reaching this destructor with a live child
    // means the user chose to leave it running.
    if (m_log) { m_log->close(); delete m_log; m_log = nullptr; }
}

int EmulatorProcess::shutdownTimeoutMs() const
{
    QSettings s = openSettings();
    return s.value(QLatin1String(keys::kShutdownTimeoutMs),
                   kDefaultShutdownTimeoutMs).toInt();
}

void EmulatorProcess::setShutdownTimeoutMs(int ms)
{
    QSettings s = openSettings();
    s.setValue(QLatin1String(keys::kShutdownTimeoutMs), qMax(1000, ms));
}

void EmulatorProcess::setState(State s)
{
    if (m_state == s) return;
    m_state = s;
    emit stateChanged(s);
}

// ---------------------------------------------------------------------------
// the stop sentinel (G2a channel)
// ---------------------------------------------------------------------------
QString EmulatorProcess::sentinelPath() const
{
    // Honor EMULATR_STOP_FILE if the composed environment sets it -- the core
    // resolves the sentinel the same way, so the two must agree or Stop would
    // write a file nothing is watching.
    QString const fromEnv = m_request.environment.value(QStringLiteral("EMULATR_STOP_FILE"));
    if (!fromEnv.isEmpty()) {
        QFileInfo const fi(fromEnv);
        return fi.isAbsolute() ? fi.absoluteFilePath()
                               : QDir(m_request.runDir).filePath(fromEnv);
    }
    return QDir(m_request.runDir).filePath(QStringLiteral("EMULATR_STOP"));
}

// ---------------------------------------------------------------------------
// mirror log (L2, V5)
// ---------------------------------------------------------------------------
bool EmulatorProcess::openMirrorLog(QString* error)
{
    QDir const logs(QDir(m_request.runDir).filePath(QStringLiteral("logs")));
    if (!logs.exists() && !QDir().mkpath(logs.absolutePath())) {
        if (error) {
            *error = QStringLiteral("cannot create %1")
                         .arg(QDir::toNativeSeparators(logs.absolutePath()));
        }
        return false;
    }

    m_logPath = logs.filePath(QStringLiteral("run_launch_%1.log").arg(nowStamp()));
    m_log = new QFile(m_logPath);
    if (!m_log->open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (error) {
            *error = QStringLiteral("cannot write %1: %2")
                         .arg(QDir::toNativeSeparators(m_logPath), m_log->errorString());
        }
        delete m_log;
        m_log = nullptr;
        return false;
    }
    return true;
}

void EmulatorProcess::appendToLog(QString const& text)
{
    if (!m_log) return;
    m_log->write(text.toUtf8());
    m_log->flush();          // a crash must not cost the tester the tail
}

void EmulatorProcess::writeLogHead()
{
    // V5: "Every launch appends the effective EMULATR_* environment to the head
    // of the mirror log, so a tester's log always answers 'what diagnostics
    // were on for this run'."
    QStringList h;
    h << QStringLiteral("================================================================");
    h << QStringLiteral("  %1 %2 -- launch record")
             .arg(QLatin1String(kAppName), QLatin1String(kAppVersion));
    h << QStringLiteral("  %1").arg(QLatin1String(kSpecRevision));
    h << QStringLiteral("================================================================");
    h << QStringLiteral("  started        : %1").arg(nowIso());
    h << QStringLiteral("  system         : %1").arg(m_request.systemName);
    h << QStringLiteral("  platform       : %1").arg(m_request.platform);
    h << QStringLiteral("  system id      : %1").arg(m_request.systemId);
    h << QStringLiteral("  executable     : %1").arg(m_request.exePath);
    h << QStringLiteral("  working dir    : %1")
             .arg(QDir::toNativeSeparators(m_request.runDir));
    h << QStringLiteral("  command line   : %1").arg(m_commandLine);
    if (m_request.consolePort > 0) {
        h << QStringLiteral("  console        : 127.0.0.1:%1").arg(m_request.consolePort);
    }
    h << QStringLiteral("  stop sentinel  : %1")
             .arg(QDir::toNativeSeparators(sentinelPath()));

    h << QString();
    if (m_request.envForLog.isEmpty()) {
        h << QStringLiteral("  EMULATR_* environment: none enabled for this run.");
    } else {
        h << QStringLiteral("  EMULATR_* environment enabled for this run:");
        for (QString const& e : m_request.envForLog)
            h << QStringLiteral("      %1").arg(e);
    }
    if (!m_request.strippedForLog.isEmpty()) {
        h << QString();
        h << QStringLiteral("  Quarantined variables stripped from the inherited");
        h << QStringLiteral("  environment (denied tier -- they were set in the");
        h << QStringLiteral("  launcher's own environment and have been removed):");
        for (QString const& e : m_request.strippedForLog)
            h << QStringLiteral("      %1").arg(e);
    }
    h << QStringLiteral("================================================================");
    h << QString();

    appendToLog(h.join(QStringLiteral("\r\n")) + QStringLiteral("\r\n"));
}

// ---------------------------------------------------------------------------
// start (L2)
// ---------------------------------------------------------------------------
bool EmulatorProcess::start(StartRequest const& request, QString* error)
{
    if (isRunning()) {
        if (error) *error = QStringLiteral("A system is already running.");
        return false;
    }

    m_request = request;
    m_forced  = false;
    m_pending.clear();

    if (!QFileInfo(m_request.exePath).isFile()) {
        if (error) {
            *error = QStringLiteral("Emulatr.exe was not found at %1")
                         .arg(QDir::toNativeSeparators(m_request.exePath));
        }
        return false;
    }
    if (!QFileInfo(m_request.runDir).isDir()) {
        if (error) {
            *error = QStringLiteral("Run directory is missing: %1")
                         .arg(QDir::toNativeSeparators(m_request.runDir));
        }
        return false;
    }

    // Clear a sentinel left behind by an earlier run.  The core pre-clears it
    // too, but doing it here as well closes the window between our Start and
    // the core reaching that code -- a stale sentinel inside that window would
    // stop the run the instant it began.
    QFile::remove(sentinelPath());

    // Arguments.  Deliberately minimal: everything else comes from Emulatr.ini
    // in the working directory, which is the whole point of the run-dir
    // contract.  --firmware is passed because the core's precedence is
    // CLI > ini, so the launcher's W2 selection cannot be silently overridden
    // by a stale [ROM] firmwareImage.
    QStringList args;
    if (!m_request.firmwareRelPath.isEmpty())
        args << QStringLiteral("--firmware") << m_request.firmwareRelPath;

    m_commandLine = QStringLiteral("\"%1\" %2")
                        .arg(m_request.exePath, args.join(QLatin1Char(' ')));

    if (!openMirrorLog(error)) return false;
    writeLogHead();

    m_process->setProgram(m_request.exePath);
    m_process->setArguments(args);
    m_process->setWorkingDirectory(m_request.runDir);      // R1, always
    m_process->setProcessEnvironment(m_request.environment);

    m_uptime.start();
    m_process->start();

    if (!m_process->waitForStarted(5000)) {
        QString const why = m_process->errorString();
        appendToLog(QStringLiteral("\r\n[launcher] failed to start: %1\r\n").arg(why));
        if (m_log) { m_log->close(); delete m_log; m_log = nullptr; }
        if (error) *error = QStringLiteral("Could not start Emulatr.exe: %1").arg(why);
        return false;
    }

    setState(State::Running);
    return true;
}

// ---------------------------------------------------------------------------
// clean shutdown (L3)
// ---------------------------------------------------------------------------
void EmulatorProcess::requestCleanShutdown()
{
    if (m_state != State::Running) return;

    QString const path = sentinelPath();
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        // The sentinel is the only sanctioned channel; if it cannot be written
        // the honest thing is to say so and leave the emulator running, not to
        // silently escalate to a kill.
        appendToLog(QStringLiteral("\r\n[launcher] CANNOT WRITE STOP SENTINEL %1: %2\r\n")
                        .arg(QDir::toNativeSeparators(path), f.errorString()));
        emit outputLine(QStringLiteral(
            "[launcher] Could not write the stop sentinel (%1). The emulator is "
            "still running.").arg(f.errorString()));
        return;
    }
    f.write("stop\r\n");
    f.close();

    appendToLog(QStringLiteral("\r\n[launcher] clean shutdown requested at %1 "
                               "(sentinel %2)\r\n")
                    .arg(nowIso(), QDir::toNativeSeparators(path)));

    setState(State::Stopping);
    m_timer->start(shutdownTimeoutMs());
}

void EmulatorProcess::onShutdownTimeout()
{
    if (m_state != State::Stopping) return;
    appendToLog(QStringLiteral("[launcher] emulator has not exited %1 ms after the "
                               "shutdown request\r\n").arg(shutdownTimeoutMs()));
    setState(State::Escalating);
    emit escalationAvailable();          // the WINDOW decides; we never kill
}

void EmulatorProcess::forceStop()
{
    if (!isRunning()) return;
    m_forced = true;
    appendToLog(QStringLiteral("\r\n[launcher] FORCED KILL at %1 -- flash state that "
                               "had not yet been persisted is lost\r\n").arg(nowIso()));
    m_timer->stop();
    m_process->kill();
}

// ---------------------------------------------------------------------------
// output and exit
// ---------------------------------------------------------------------------
void EmulatorProcess::onReadyRead()
{
    QByteArray const chunk = m_process->readAll();
    if (chunk.isEmpty()) return;

    appendToLog(QString::fromLocal8Bit(chunk));

    m_pending += QString::fromLocal8Bit(chunk);
    m_pending.replace(QLatin1String("\r\n"), QLatin1String("\n"));
    int nl;
    while ((nl = m_pending.indexOf(QLatin1Char('\n'))) >= 0) {
        QString const line = m_pending.left(nl);
        m_pending.remove(0, nl + 1);
        if (!line.isEmpty()) emit outputLine(line);
    }
}

void EmulatorProcess::onErrorOccurred(QProcess::ProcessError error)
{
    if (error == QProcess::FailedToStart) {
        appendToLog(QStringLiteral("\r\n[launcher] process failed to start: %1\r\n")
                        .arg(m_process->errorString()));
    }
}

void EmulatorProcess::onFinished(int exitCode, QProcess::ExitStatus status)
{
    m_timer->stop();

    // Drain whatever arrived between the last read and exit.
    onReadyRead();
    if (!m_pending.isEmpty()) { emit outputLine(m_pending); m_pending.clear(); }

    qint64 const ms = m_uptime.isValid() ? m_uptime.elapsed() : 0;

    QStringList tail;
    tail << QString();
    tail << QStringLiteral("================================================================");
    tail << QStringLiteral("  exited      : %1").arg(nowIso());
    tail << QStringLiteral("  exit code   : %1").arg(exitCode);
    tail << QStringLiteral("  disposition : %1")
                .arg(m_forced ? QStringLiteral("FORCED KILL -- unpersisted flash "
                                               "state may have been lost")
                              : QStringLiteral("clean (emulator exited by its own "
                                               "accord)"));
    tail << QStringLiteral("  duration    : %1 s").arg(ms / 1000.0, 0, 'f', 1);
    tail << QStringLiteral("================================================================");
    appendToLog(tail.join(QStringLiteral("\r\n")) + QStringLiteral("\r\n"));

    if (m_log) { m_log->close(); delete m_log; m_log = nullptr; }

    // Leave no sentinel behind: the next run would stop instantly.  (The core
    // pre-clears it as well; belt and braces, because a leftover file here is
    // invisible to the tester and catastrophic to the next launch.)
    QFile::remove(sentinelPath());

    bool const wasForced = m_forced;
    setState(State::Idle);
    emit finished(exitCode, status, wasForced, ms);
}

}  // namespace launch
