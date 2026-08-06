// ============================================================================
// src/Version.h -- EmulatrLaunch version identity
// ============================================================================
// Project: EmulatR -- EmulatrLaunch (SPEC-LAUNCH-001 Rev D.2)
// Copyright (C) 2026 eNVy Systems, Inc.  All rights reserved.
// Licensed under eNVy Systems Non-Commercial License v1.1
//
// Project Architect: Timothy Peer
// AI Collaboration:  Claude (Anthropic)
//
// Version: 1.0.0-alpha   Date: 2026-07-29
// Spec:    Section 3 -- "version lives in headers and the tree, never in
//          filenames".  This header is the tree's single version anchor.
// ============================================================================

#ifndef EMULATRLAUNCH_VERSION_H
#define EMULATRLAUNCH_VERSION_H

#include <QSettings>
#include <QString>

namespace launch {

inline constexpr char const* kAppName      = "EmulatrLaunch";
inline constexpr char const* kAppVersion   = "1.0.0-alpha";
inline constexpr char const* kOrgName      = "eNVy Systems, Inc.";
inline constexpr char const* kOrgDomain    = "envysys.com";

// QSettings scope key.  HKCU\Software\<kOrgName>\<kAppName> (Section 4, M1).
// Stable across versions on purpose: the system registry must survive an
// upgrade untouched.
inline constexpr char const* kSettingsOrg  = "eNVy Systems";
inline constexpr char const* kSettingsApp  = "EmulatrLaunch";

// Spec revision this build implements.  Bumped only with a signed-off spec.
inline constexpr char const* kSpecRevision = "SPEC-LAUNCH-001 Rev D.2";

// ---------------------------------------------------------------------------
// The launcher's QSettings hive (Section 4, M1).  HKCU scope, one place, so
// SystemModel / EnvVarModel / ExeDiscovery cannot drift onto different hives.
// Deliberately NOT keyed by version: deleting this hive must be survivable via
// "Add Existing..." (M3), and an upgrade must not orphan the system registry.
// ---------------------------------------------------------------------------
inline QSettings openSettings()
{
    return QSettings(QSettings::UserScope,
                     QString::fromLatin1(kSettingsOrg),
                     QString::fromLatin1(kSettingsApp));
}

// Settings keys used across translation units.
namespace keys {
inline constexpr char const* kLastSelected     = "ui/lastSelectedSystem";
inline constexpr char const* kEmulatrExeOverride = "paths/emulatrExe";
inline constexpr char const* kPlatEdExeOverride  = "paths/platEdExe";
inline constexpr char const* kTerminalExeOverride = "paths/terminalExe";
inline constexpr char const* kShutdownTimeoutMs  = "run/shutdownTimeoutMs";
inline constexpr char const* kShowDevVars        = "ui/showDeveloperVariables";
inline constexpr char const* kWindowGeometry     = "ui/windowGeometry";
}  // namespace keys

}  // namespace launch

#endif  // EMULATRLAUNCH_VERSION_H
