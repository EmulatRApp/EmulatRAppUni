// ============================================================================
// src/RunDirSkeleton.h -- run-directory skeleton creation and validation
// ============================================================================
// Project: EmulatR -- EmulatrLaunch (SPEC-LAUNCH-001 Rev D.2)
// Copyright (C) 2026 eNVy Systems, Inc.  All rights reserved.
// Licensed under eNVy Systems Non-Commercial License v1.1
//
// Project Architect: Timothy Peer
// AI Collaboration:  Claude (Anthropic)
//
// Version: 1.0.0-alpha   Date: 2026-07-29
// Spec:    Section 5 (the run-directory contract, R1-R4), Section 4 M4/M5,
//          Section 8 L1 (preflight), Section 11 E1/E3.
//
// This TU is UI-free: every function reports through return values and out
// parameters so preflight can call it from a watcher without a dialog.
// ============================================================================

#ifndef EMULATRLAUNCH_RUNDIRSKELETON_H
#define EMULATRLAUNCH_RUNDIRSKELETON_H

#include <QString>
#include <QStringList>

#include "SystemRecord.h"

namespace launch {
namespace RunDirSkeleton {

// ---------------------------------------------------------------------------
// Validation outcome.  `problems` are blocking (Start stays disabled);
// `notes` are advisory.  Each entry is already plain language fit for the
// status line -- Section 8 L1 asks for exactly that, not an error code.
// ---------------------------------------------------------------------------
struct ValidationResult
{
    Health      health = Health::Unknown;
    QStringList problems;
    QStringList notes;

    bool ok() const { return problems.isEmpty(); }
};

// R2.  C:\Program Files (and the x86 / W6432 variants) is refused outright:
// it is read-only for a normal user and Windows' VirtualStore silently
// shadow-copies writes, which is how a flash .rom write-back gets lost.
bool isUnderProgramFiles(QString const& path);

// R2.  Create-and-delete a temp file in the directory.  The ONLY reliable
// writability test on Windows -- ACL inspection and QFileInfo::isWritable()
// both lie in the presence of virtualization and inherited denies.
bool probeWritable(QString const& dir, QString* error = nullptr);

// R3.  Creates {run-dir} and its skeleton: Emulatr.ini seeded from the
// template with the platform and console port applied, firmware/ carrying
// firmware_readme.txt, and empty disks/, logs/, traces/.
// Refuses to overwrite an existing Emulatr.ini.
bool create(QString const& runDir, Platform platform, int consolePort,
            QString* error = nullptr);

// Creates logs/ and traces/ if absent (Section 5: "created if absent").
// Cheap and idempotent; called before every launch.
bool ensureSubdirs(QString const& runDir, QString* error = nullptr);

// Section 5 contract check, used by "Add Existing..." and by preflight.
// `platform` may be Platform::Unknown when the caller has not bound one yet.
ValidationResult validate(QString const& runDir, Platform platform);

// M5.  Infers the platform from the directory's contents.  Order of
// authority: platform manifest filename, then [System] model in Emulatr.ini,
// then firmware image filenames as corroboration.  `how` receives a
// human-readable account of what the inference rested on, and `ambiguous` is
// set when two sources disagree -- M5 requires that the user be asked rather
// than have a guess registered.
Platform inferPlatform(QString const& runDir, QString* how = nullptr,
                       bool* ambiguous = nullptr);

// Default console port for a newly created system, and the search for the
// next unused one.  10023 is the core's own default ([SRMConsole] port).
inline constexpr int kDefaultConsolePort = 10023;

}  // namespace RunDirSkeleton
}  // namespace launch

#endif  // EMULATRLAUNCH_RUNDIRSKELETON_H
