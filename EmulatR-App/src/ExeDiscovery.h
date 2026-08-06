// ============================================================================
// src/ExeDiscovery.h -- discovery of sibling executables
// ============================================================================
// Project: EmulatR -- EmulatrLaunch (SPEC-LAUNCH-001 Rev D.2)
// Copyright (C) 2026 eNVy Systems, Inc.  All rights reserved.
// Licensed under eNVy Systems Non-Commercial License v1.1
//
// Project Architect: Timothy Peer
// AI Collaboration:  Claude (Anthropic)
//
// Version: 1.0.0-alpha   Date: 2026-07-29
// Spec:    Section 6 -- "first hit wins, result shown (and overridable) in
//          Settings".  PlatEd uses "the same three-step pattern"; W3 says the
//          terminal client does too.  Three call sites, one implementation --
//          hence this shared TU rather than the pattern written out three
//          times in three bridges.
//
// NOT IN THE SPEC'S FILE LIST.  Recorded here as a deliberate addition for
// review: SPEC-LAUNCH-001 Section 3 lists PlatEdBridge and TerminalBridge but
// no shared discovery unit, while Sections 6 and 7 W3 both say the pattern is
// identical.  Triplicating it would guarantee the three copies drift.
// ============================================================================

#ifndef EMULATRLAUNCH_EXEDISCOVERY_H
#define EMULATRLAUNCH_EXEDISCOVERY_H

#include <QString>
#include <QStringList>

namespace launch {
namespace ExeDiscovery {

// How a path was found.  Shown verbatim in Settings so a tester reporting
// "it picked the wrong exe" tells us which step fired.
enum class Source
{
    NotFound,
    UserOverride,     // step 1: explicit path in launcher settings
    Installed,        // step 2: the Setup Factory payload location
    DevTree,          // step 3: dev-machine build output, newest config first
    SystemPath,       // on PATH (terminal clients only)
};

struct Result
{
    QString path;         // absolute, native separators; empty when not found
    Source  source = Source::NotFound;
    QString detail;       // one line for the Settings row / status line

    bool found() const { return !path.isEmpty(); }
};

// Emulatr.exe.  Not resolving it disables Start with an explanatory status
// line -- never a modal at startup (Section 6).
Result findEmulatr();

// PlatEd.  Absent on a machine that has not installed it; only the
// "Open in PlatEd" affordance disables.  The launcher must ship and function
// before PlatEd does (Section 2).
Result findPlatEd();

// Terminal client for Open Console (W3).  PuTTY by preference, then the OS
// telnet handler as fallback -- with neither, the button disables and the
// status line shows host:port to connect manually.
Result findTerminal();

// Persisted overrides (Settings).  Empty clears the override.
void setOverride(char const* settingsKey, QString const& path);

// Every location a given probe considered, in order.  Feeds the Settings
// page's "searched here" detail so a missing exe is diagnosable without a log.
QStringList searchedPathsForEmulatr();
QStringList searchedPathsForPlatEd();
QStringList searchedPathsForTerminal();

}  // namespace ExeDiscovery
}  // namespace launch

#endif  // EMULATRLAUNCH_EXEDISCOVERY_H
