// ============================================================================
// src/TerminalBridge.cpp -- terminal-client discovery and Open Console
// ============================================================================
// Project: EmulatR -- EmulatrLaunch (SPEC-LAUNCH-001 Rev D.2)
// Copyright (C) 2026 eNVy Systems, Inc.  All rights reserved.
// Licensed under eNVy Systems Non-Commercial License v1.1
//
// Project Architect: Timothy Peer
// AI Collaboration:  Claude (Anthropic)
//
// Version: 1.0.0-alpha   Date: 2026-07-29
// Spec:    Section 7 W3, Section 10 D4
// ============================================================================

#include "TerminalBridge.h"

#include <QDesktopServices>
#include <QProcess>
#include <QUrl>

#include "ExeDiscovery.h"

namespace launch {
namespace TerminalBridge {

Availability probe()
{
    Availability a;
    ExeDiscovery::Result const r = ExeDiscovery::findTerminal();
    a.haveClient = r.found();
    a.exePath    = r.path;
    a.detail     = r.detail;
    return a;
}

QString manualConnectHint(QString const& host, int port)
{
    return QStringLiteral(
               "No terminal client was found. Connect your own terminal to "
               "%1 port %2 (telnet) to reach the console.")
        .arg(host).arg(port);
}

bool openConsole(QString const& host, int port, QString* error)
{
    if (port <= 0) {
        if (error) {
            *error = QStringLiteral("This system has no console port set.");
        }
        return false;
    }

    Availability const a = probe();

    if (a.haveClient) {
        // PuTTY's own telnet mode.  Detached: a console session must not die
        // with the launcher.
        QStringList const args = { QStringLiteral("-telnet"), host,
                                   QString::number(port) };
        qint64 pid = 0;
        if (QProcess::startDetached(a.exePath, args, QString(), &pid)) return true;

        if (error) {
            *error = QStringLiteral("Could not start the terminal client at %1.")
                         .arg(a.exePath);
        }
        // Fall through to the OS handler rather than giving up.
    }

    QUrl const url(QStringLiteral("telnet://%1:%2").arg(host).arg(port));
    if (QDesktopServices::openUrl(url)) return true;

    if (error) *error = manualConnectHint(host, port);
    return false;
}

}  // namespace TerminalBridge
}  // namespace launch
