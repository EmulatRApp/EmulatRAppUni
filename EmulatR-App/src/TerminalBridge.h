// ============================================================================
// src/TerminalBridge.h -- terminal-client discovery and Open Console
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
//
// WHY THIS TARGETS A PORT AND NOT A PROCESS.  The emulator's serial console --
// the path to P00>>> -- is a TCP listener, and a terminal client latches to
// the PORT.  That keeps this surface identical whether the emulator runs as a
// launcher child today or under the deferred service host later (Section 15),
// which is why D4 calls Open Console "deliberately mode-agnostic".  Nothing
// here may ever reach for the child QProcess.
// ============================================================================

#ifndef EMULATRLAUNCH_TERMINALBRIDGE_H
#define EMULATRLAUNCH_TERMINALBRIDGE_H

#include <QString>

namespace launch {
namespace TerminalBridge {

struct Availability
{
    bool    haveClient = false;
    QString exePath;          // empty when falling back to the OS handler
    QString detail;           // one line for the status area
};

Availability probe();

// Spawns the discovered terminal client against host:port, detached -- the
// console outlives the launcher on purpose, so closing the launcher window
// does not tear down a session someone is typing into.
// Falls back to the OS telnet handler when PuTTY is absent.
bool openConsole(QString const& host, int port, QString* error);

// What to tell the user when no client could be launched at all.
QString manualConnectHint(QString const& host, int port);

}  // namespace TerminalBridge
}  // namespace launch

#endif  // EMULATRLAUNCH_TERMINALBRIDGE_H
