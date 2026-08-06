// ============================================================================
// src/SystemRecord.h -- the system value type
// ============================================================================
// Project: EmulatR -- EmulatrLaunch (SPEC-LAUNCH-001 Rev D.2)
// Copyright (C) 2026 eNVy Systems, Inc.  All rights reserved.
// Licensed under eNVy Systems Non-Commercial License v1.1
//
// Project Architect: Timothy Peer
// AI Collaboration:  Claude (Anthropic)
//
// Version: 1.0.0-alpha   Date: 2026-07-29
// Spec:    Section 4 -- the named system model.  A SYSTEM is the unit
//          everything else hangs from: one run directory, one platform,
//          bound at creation.
// ============================================================================

#ifndef EMULATRLAUNCH_SYSTEMRECORD_H
#define EMULATRLAUNCH_SYSTEMRECORD_H

#include <QDir>
#include <QFileInfo>
#include <QString>
#include <QStringList>

namespace launch {

// ---------------------------------------------------------------------------
// Platform.  Bound at creation, IMMUTABLE in v1 (Section 4 / Section 15).
// The string form is what lands in [System] model in Emulatr.ini, so the
// spelling here is the core's spelling -- do not prettify it.
// ---------------------------------------------------------------------------
enum class Platform { Unknown = 0, DS10, DS20, ES40 };

inline QString platformToString(Platform p)
{
    switch (p) {
        case Platform::DS10: return QStringLiteral("DS10");
        case Platform::DS20: return QStringLiteral("DS20");
        case Platform::ES40: return QStringLiteral("ES40");
        case Platform::Unknown: break;
    }
    return QString();
}

inline Platform platformFromString(QString const& s)
{
    QString const u = s.trimmed().toUpper();
    if (u == QLatin1String("DS10")) return Platform::DS10;
    if (u == QLatin1String("DS20")) return Platform::DS20;
    if (u == QLatin1String("ES40")) return Platform::ES40;
    return Platform::Unknown;
}

inline QStringList allPlatformNames()
{
    return { QStringLiteral("DS10"), QStringLiteral("DS20"), QStringLiteral("ES40") };
}

// Human-facing one-liner for the platform, used in dialogs and details.
inline QString platformDescription(Platform p)
{
    switch (p) {
        case Platform::DS10: return QStringLiteral("AlphaServer DS10 -- single 21264, Tsunami");
        case Platform::DS20: return QStringLiteral("AlphaServer DS20 -- 21264, Tsunami");
        case Platform::ES40: return QStringLiteral("AlphaServer ES40 -- 21264, Typhoon");
        case Platform::Unknown: break;
    }
    return QStringLiteral("unknown platform");
}

// The platform manifest filename the core derives from [System] model:
// <lower(model)>_platform.json.  PlatEd is invoked on this file (W5).
inline QString platformManifestFileName(Platform p)
{
    QString const s = platformToString(p);
    return s.isEmpty() ? QString() : s.toLower() + QStringLiteral("_platform.json");
}

// ---------------------------------------------------------------------------
// Health.  Not persisted -- recomputed by preflight and the run-dir watcher
// (Section 8 L1, Section 11 E1).  Carried on the record so the list can badge
// a broken system without a second lookup.
// ---------------------------------------------------------------------------
enum class Health { Unknown = 0, Ok, Warning, Broken };

// ---------------------------------------------------------------------------
// SystemRecord.  Section 4:
//   id        GUID, assigned at creation, IMMUTABLE.  The stable key for all
//             per-system persisted state -- renames and run-dir moves never
//             orphan settings.
//   name      display name, user-editable, unique case-insensitively.
//   platform  DS10 | DS20 | ES40, bound at creation, immutable in v1.
//   runDir    absolute, canonicalized, one-to-one with the system.
// ---------------------------------------------------------------------------
struct SystemRecord
{
    QString  id;
    QString  name;
    Platform platform = Platform::Unknown;
    QString  runDir;

    // Transient (not persisted).
    bool     running     = false;
    Health   health      = Health::Unknown;
    QString  healthNote;

    bool isValid() const
    {
        return !id.isEmpty() && !name.isEmpty()
            && platform != Platform::Unknown && !runDir.isEmpty();
    }

    // Paths inside the run-directory contract (Section 5).
    QString iniPath()       const { return QDir(runDir).filePath(QStringLiteral("Emulatr.ini")); }
    QString firmwareDir()   const { return QDir(runDir).filePath(QStringLiteral("firmware")); }
    QString disksDir()      const { return QDir(runDir).filePath(QStringLiteral("disks")); }
    QString logsDir()       const { return QDir(runDir).filePath(QStringLiteral("logs")); }
    QString tracesDir()     const { return QDir(runDir).filePath(QStringLiteral("traces")); }

    // The graceful-stop sentinel the core polls (systemLib/Machine.cpp:1261).
    // Default name in cwd is EMULATR_STOP; cwd is always this run dir (R1).
    QString stopSentinelPath() const
    {
        return QDir(runDir).filePath(QStringLiteral("EMULATR_STOP"));
    }

    QString manifestPath() const
    {
        QString const fn = platformManifestFileName(platform);
        if (fn.isEmpty()) return QString();
        QDir const d(runDir);
        QString const top = d.filePath(fn);
        if (QFileInfo::exists(top)) return top;
        QString const nested = d.filePath(QStringLiteral("config/") + fn);
        if (QFileInfo::exists(nested)) return nested;
        return top;  // canonical location even when absent
    }
};

// ---------------------------------------------------------------------------
// Canonicalization.  One-to-one system:runDir is enforced on this key, so it
// must be stable for a directory that exists AND one that has gone missing
// (E1).  QFileInfo::canonicalFilePath() returns empty for a missing path, so
// fall back to the cleaned absolute path.  Windows paths compare
// case-insensitively.
// ---------------------------------------------------------------------------
inline QString canonicalRunDir(QString const& path)
{
    QFileInfo const fi(path);
    QString const canon = fi.canonicalFilePath();
    QString const out = canon.isEmpty() ? QDir::cleanPath(fi.absoluteFilePath()) : canon;
    return QDir::toNativeSeparators(out);
}

inline QString runDirCompareKey(QString const& path)
{
#ifdef Q_OS_WIN
    return canonicalRunDir(path).toLower();
#else
    return canonicalRunDir(path);
#endif
}

}  // namespace launch

#endif  // EMULATRLAUNCH_SYSTEMRECORD_H
