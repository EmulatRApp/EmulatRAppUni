// ============================================================================
// src/EmulatorProcess.h -- subprocess wrapper: start, clean shutdown, escalation
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
//
// ---------------------------------------------------------------------------
// THE SHUTDOWN CHANNEL (gate G2a)
//
// The spec's L3 lists three candidate channels and says the choice is a
// core-tree contract, not a launcher invention.  It already exists, and it is
// candidate (c):
//
//     systemLib/Machine.cpp:1249-1272 -- the graceful-stop sentinel.
//     Machine::run() resolves a sentinel path once per run ($EMULATR_STOP_FILE
//     if set, else "EMULATR_STOP" in the CWD), pre-clears it so a stale file
//     cannot stop the new run, logs the resolved absolute path, and polls for
//     it in the per-cycle body.  When it appears, run() returns and ~Machine's
//     forceFlush() persists the flash NVRAM.  The comment there states the
//     motivating case exactly: "A hard taskkill /F skips the destructor and
//     loses the heal."
//
// This satisfies the Rev D.1 SELECTION CONSTRAINT without a core-tree change:
// creating a file is something a session-0 service wrapper can do as easily as
// an interactive launcher, so the deferred run-as-a-service host (Section 15)
// drives the identical channel.  Console control events would NOT have been
// operable that way.
//
// TERMINATE IS NEVER USED.  QProcess::terminate() posts WM_CLOSE, which a
// console-subsystem emulator never receives; calling it would look like a
// clean stop while actually doing nothing.  Force Stop uses kill(), is offered
// only after the visible timeout, and is recorded as forced.
// ---------------------------------------------------------------------------
// ============================================================================

#ifndef EMULATRLAUNCH_EMULATORPROCESS_H
#define EMULATRLAUNCH_EMULATORPROCESS_H

#include <QElapsedTimer>
#include <QObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QString>
#include <QStringList>

class QTimer;
class QFile;

namespace launch {

class EmulatorProcess : public QObject
{
    Q_OBJECT

public:
    enum class State
    {
        Idle,        // nothing running
        Running,     // started, healthy
        Stopping,    // sentinel written, waiting for a clean exit
        Escalating,  // timeout elapsed; Force Stop is offered
    };

    // Everything the invocation contract needs, assembled by the caller so
    // this class stays free of model and settings lookups.
    struct StartRequest
    {
        QString  systemId;
        QString  systemName;
        QString  platform;
        QString  exePath;            // absolute Emulatr.exe
        QString  runDir;             // becomes the child's working directory (R1)
        QString  firmwareRelPath;    // "firmware/<image>", relative to runDir
        int      consolePort = 0;
        QProcessEnvironment environment;
        QStringList envForLog;       // V5: effective EMULATR_* selection
        QStringList strippedForLog;  // denied names removed from the inherited env
    };

    explicit EmulatorProcess(QObject* parent = nullptr);
    ~EmulatorProcess() override;

    // L2.  Returns false with *error if the child could not be started.
    bool start(StartRequest const& request, QString* error);

    // L3.  Writes the stop sentinel and starts the escalation timer.  Safe to
    // call twice; the second call is a no-op.
    void requestCleanShutdown();

    // L3 escalation.  Only ever called from a user action, never automatically.
    void forceStop();

    State   state()          const { return m_state; }
    bool    isRunning()      const { return m_state != State::Idle; }
    QString mirrorLogPath()  const { return m_logPath; }
    QString systemId()       const { return m_request.systemId; }
    QString systemName()     const { return m_request.systemName; }

    // Visible and configurable, default 10 s (L3).
    int  shutdownTimeoutMs() const;
    void setShutdownTimeoutMs(int ms);

    // The exact command line used, for the status area and the log head.
    QString lastCommandLine() const { return m_commandLine; }

signals:
    void stateChanged(launch::EmulatorProcess::State state);

    // A line of the emulator's stdout/stderr, already mirrored to the log.
    void outputLine(QString const& line);

    // The clean-shutdown timeout elapsed.  The window offers Force Stop; this
    // class does NOT kill on its own.
    void escalationAvailable();

    // L5.  `forced` records the disposition for the log and the status area.
    void finished(int exitCode, QProcess::ExitStatus status, bool forced,
                  qint64 durationMs);

private slots:
    void onReadyRead();
    void onFinished(int exitCode, QProcess::ExitStatus status);
    void onErrorOccurred(QProcess::ProcessError error);
    void onShutdownTimeout();

private:
    void setState(State s);
    bool openMirrorLog(QString* error);
    void writeLogHead();
    void appendToLog(QString const& text);
    QString sentinelPath() const;

    QProcess*     m_process = nullptr;
    QTimer*       m_timer   = nullptr;
    QFile*        m_log     = nullptr;
    QString       m_logPath;
    QString       m_commandLine;
    QString       m_pending;        // partial line carried between reads
    StartRequest  m_request;
    State         m_state  = State::Idle;
    bool          m_forced = false;
    QElapsedTimer m_uptime;
};

}  // namespace launch

#endif  // EMULATRLAUNCH_EMULATORPROCESS_H
