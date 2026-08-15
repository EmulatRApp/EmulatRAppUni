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
//
// PRODUCT HIVE (architect direction 2026-08-14): the product is EmulatR; the
// launcher and PlatEditor are SUBSYSTEMS.  One hive --
// HKCU\Software\eNVy Systems\EmulatR -- shared by the whole family, with each
// subsystem namespacing its keys (launch_*, later platedit_*), so subsystems
// can never collide and one regedit path shows the whole product.
inline constexpr char const* kSettingsOrg  = "eNVy Systems";
inline constexpr char const* kSettingsApp  = "EmulatR";

// The old launcher-private hive, read once by the one-time migration below.
inline constexpr char const* kLegacySettingsApp = "EmulatrLaunch";

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

// Settings keys used across translation units.  ALL launcher keys carry the
// launch_ subsystem prefix -- see the product-hive note above.
namespace keys {
inline constexpr char const* kSystemsGroup     = "launch_systems";
inline constexpr char const* kLastSelected     = "launch_ui/lastSelectedSystem";
inline constexpr char const* kEmulatrExeOverride = "launch_paths/emulatrExe";
inline constexpr char const* kPlatEdExeOverride  = "launch_paths/platEdExe";
inline constexpr char const* kTerminalExeOverride = "launch_paths/terminalExe";
inline constexpr char const* kShutdownTimeoutMs  = "launch_run/shutdownTimeoutMs";
inline constexpr char const* kShowDevVars        = "launch_ui/showDeveloperVariables";
inline constexpr char const* kWindowGeometry     = "launch_ui/windowGeometry";
}  // namespace keys

// ---------------------------------------------------------------------------
// One-time migration from the old launcher-private hive (EmulatrLaunch) into
// the product hive with launch_ prefixes.  Read-only against the old hive;
// runs only while the new hive has no launch_systems group, so it can never
// clobber real state.  Call once at startup, before any model loads.
// ---------------------------------------------------------------------------
inline void migrateLegacyLaunchSettings()
{
    QSettings neu = openSettings();
    if (!neu.childGroups().filter(QStringLiteral("launch_systems")).isEmpty())
        return;                                    // already on the new hive
    QSettings old(QSettings::UserScope,
                  QString::fromLatin1(kSettingsOrg),
                  QString::fromLatin1(kLegacySettingsApp));
    QStringList const all = old.allKeys();
    if (all.isEmpty()) return;                     // nothing to migrate
    for (QString const& k : all)
        neu.setValue(QStringLiteral("launch_") + k, old.value(k));
}

}  // namespace launch

#endif  // EMULATRLAUNCH_VERSION_H
