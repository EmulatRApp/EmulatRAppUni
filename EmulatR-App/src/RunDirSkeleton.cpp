// ============================================================================
// src/RunDirSkeleton.cpp -- run-directory skeleton creation and validation
// ============================================================================
// Project: EmulatR -- EmulatrLaunch (SPEC-LAUNCH-001 Rev D.2)
// Copyright (C) 2026 eNVy Systems, Inc.  All rights reserved.
// Licensed under eNVy Systems Non-Commercial License v1.1
//
// Project Architect: Timothy Peer
// AI Collaboration:  Claude (Anthropic)
//
// Version: 1.0.0-alpha   Date: 2026-07-29
// Spec:    Section 5, Section 4 M4/M5, Section 8 L1, Section 11 E1/E3
// ============================================================================

#include "RunDirSkeleton.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

#include "FirmwareCheck.h"
#include "IniOverlay.h"

namespace launch {
namespace RunDirSkeleton {

namespace {

QString normalizedForPrefix(QString const& p)
{
    QString s = QDir::cleanPath(QFileInfo(p).absoluteFilePath());
#ifdef Q_OS_WIN
    s = s.toLower();
#endif
    if (!s.endsWith(QLatin1Char('/'))) s += QLatin1Char('/');
    return s;
}

bool writeResourceTo(QString const& resourcePath, QString const& destPath,
                     QString* error)
{
    QFile src(resourcePath);
    if (!src.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("internal: missing resource %1").arg(resourcePath);
        return false;
    }
    QByteArray const bytes = src.readAll();
    src.close();

    QSaveFile dst(destPath);
    if (!dst.open(QIODevice::WriteOnly)) {
        if (error) {
            *error = QStringLiteral("cannot write %1: %2")
                         .arg(QDir::toNativeSeparators(destPath), dst.errorString());
        }
        return false;
    }
    if (dst.write(bytes) != bytes.size() || !dst.commit()) {
        if (error) {
            *error = QStringLiteral("write failed for %1: %2")
                         .arg(QDir::toNativeSeparators(destPath), dst.errorString());
        }
        return false;
    }
    return true;
}

}  // namespace

// ---------------------------------------------------------------------------
// R2 -- Program Files refusal
// ---------------------------------------------------------------------------
bool isUnderProgramFiles(QString const& path)
{
    if (path.isEmpty()) return false;
    QString const target = normalizedForPrefix(path);

    // Read the real locations from the environment rather than hardcoding
    // "C:\Program Files": a machine can have them on another volume, and a
    // localized Windows can name them differently.
    QStringList roots;
    for (char const* var : { "ProgramFiles", "ProgramFiles(x86)", "ProgramW6432" }) {
        QByteArray const v = qgetenv(var);
        if (!v.isEmpty()) roots << QString::fromLocal8Bit(v);
    }
    if (roots.isEmpty()) roots << QStringLiteral("C:/Program Files");

    for (QString const& r : roots) {
        if (target.startsWith(normalizedForPrefix(r))) return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// R2 -- writability probe
// ---------------------------------------------------------------------------
bool probeWritable(QString const& dir, QString* error)
{
    QDir const d(dir);
    if (!d.exists()) {
        if (error) {
            *error = QStringLiteral("%1 does not exist")
                         .arg(QDir::toNativeSeparators(dir));
        }
        return false;
    }

    QString const stamp =
        QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_hhmmsszzz"));
    QString const probe = d.filePath(QStringLiteral(".emulatrlaunch_probe_") + stamp);

    QFile f(probe);
    if (!f.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
        if (error) {
            *error = QStringLiteral("%1 is not writable: %2")
                         .arg(QDir::toNativeSeparators(dir), f.errorString());
        }
        return false;
    }
    f.write("probe", 5);
    f.close();
    if (!f.remove()) {
        // Wrote but could not clean up: still writable, but say so -- a
        // leftover probe file in a tester's run dir would be confusing.
        if (error) {
            *error = QStringLiteral("wrote but could not remove probe file %1")
                         .arg(QDir::toNativeSeparators(probe));
        }
        return true;
    }
    return true;
}

// ---------------------------------------------------------------------------
// R3 -- skeleton creation
// ---------------------------------------------------------------------------
bool create(QString const& runDir, Platform platform, int consolePort, QString* error)
{
    if (platform == Platform::Unknown) {
        if (error) *error = QStringLiteral("internal: cannot create a system with no platform");
        return false;
    }
    if (isUnderProgramFiles(runDir)) {
        if (error) {
            *error = QStringLiteral(
                         "%1 is inside Program Files.  Windows makes that "
                         "directory read-only and silently redirects writes to "
                         "a VirtualStore copy, which loses the emulator's flash "
                         "state.  Choose a location under Documents instead.")
                         .arg(QDir::toNativeSeparators(runDir));
        }
        return false;
    }

    QDir d(runDir);
    if (!d.exists() && !QDir().mkpath(runDir)) {
        if (error) {
            *error = QStringLiteral("cannot create %1")
                         .arg(QDir::toNativeSeparators(runDir));
        }
        return false;
    }
    if (!probeWritable(runDir, error)) return false;

    for (char const* sub : { "firmware", "disks", "logs", "traces" }) {
        if (!d.exists(QLatin1String(sub)) && !d.mkpath(QLatin1String(sub))) {
            if (error) {
                *error = QStringLiteral("cannot create %1")
                             .arg(QDir::toNativeSeparators(d.filePath(QLatin1String(sub))));
            }
            return false;
        }
    }

    // firmware_readme.txt -- overwritten on purpose if stale, it is ours.
    if (!writeResourceTo(QStringLiteral(":/templates/firmware_readme.txt"),
                         d.filePath(QStringLiteral("firmware/firmware_readme.txt")),
                         error)) {
        return false;
    }

    // Emulatr.ini -- NEVER overwrite an existing one.  "New System..." onto a
    // directory that already holds a configured system must not erase it.
    QString const iniPath = d.filePath(QStringLiteral("Emulatr.ini"));
    if (QFileInfo::exists(iniPath)) {
        if (error) {
            *error = QStringLiteral(
                         "%1 already exists.  Use \"Add Existing...\" to "
                         "register this directory instead of creating a new "
                         "system over it.")
                         .arg(QDir::toNativeSeparators(iniPath));
        }
        return false;
    }

    QFile tmpl(QStringLiteral(":/templates/Emulatr.ini.default"));
    if (!tmpl.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("internal: missing Emulatr.ini template");
        return false;
    }
    QByteArray seed = tmpl.readAll();
    tmpl.close();

    seed.replace("@MODEL@", platformToString(platform).toUtf8());
    seed.replace("@CONSOLEPORT@", QByteArray::number(consolePort));

    QSaveFile out(iniPath);
    if (!out.open(QIODevice::WriteOnly)) {
        if (error) {
            *error = QStringLiteral("cannot write %1: %2")
                         .arg(QDir::toNativeSeparators(iniPath), out.errorString());
        }
        return false;
    }
    if (out.write(seed) != seed.size() || !out.commit()) {
        if (error) {
            *error = QStringLiteral("write failed for %1: %2")
                         .arg(QDir::toNativeSeparators(iniPath), out.errorString());
        }
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// ensureSubdirs
// ---------------------------------------------------------------------------
bool ensureSubdirs(QString const& runDir, QString* error)
{
    QDir d(runDir);
    if (!d.exists()) {
        if (error) {
            *error = QStringLiteral("%1 does not exist")
                         .arg(QDir::toNativeSeparators(runDir));
        }
        return false;
    }
    for (char const* sub : { "logs", "traces", "disks", "firmware" }) {
        if (!d.exists(QLatin1String(sub)) && !d.mkpath(QLatin1String(sub))) {
            if (error) {
                *error = QStringLiteral("cannot create %1")
                             .arg(QDir::toNativeSeparators(d.filePath(QLatin1String(sub))));
            }
            return false;
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// validate
// ---------------------------------------------------------------------------
ValidationResult validate(QString const& runDir, Platform platform)
{
    ValidationResult r;

    QFileInfo const fi(runDir);
    if (runDir.isEmpty() || !fi.exists() || !fi.isDir()) {
        // E1: a deleted or unmounted run dir is a broken system, not a crash.
        r.health = Health::Broken;
        r.problems << QStringLiteral("Run directory is missing: %1")
                          .arg(QDir::toNativeSeparators(runDir));
        r.notes << QStringLiteral(
            "If the directory moved, use \"Relocate run dir...\" to re-point "
            "this system without losing its settings.");
        return r;
    }

    if (isUnderProgramFiles(runDir)) {
        r.health = Health::Broken;
        r.problems << QStringLiteral(
                          "Run directory is inside Program Files: %1.  Windows "
                          "redirects writes there to a VirtualStore copy, which "
                          "loses flash state.")
                          .arg(QDir::toNativeSeparators(runDir));
        return r;
    }

    QString probeErr;
    if (!probeWritable(runDir, &probeErr)) {
        r.health = Health::Broken;
        r.problems << probeErr;
        return r;
    }

    QDir const d(runDir);

    // Emulatr.ini present and parseable.
    QString const iniPath = d.filePath(QStringLiteral("Emulatr.ini"));
    if (!QFileInfo::exists(iniPath)) {
        r.problems << QStringLiteral("Emulatr.ini is missing from %1")
                          .arg(QDir::toNativeSeparators(runDir));
    } else {
        IniOverlay ini;
        QString iniErr;
        if (!ini.load(iniPath, &iniErr)) {
            r.problems << iniErr;                       // E3
        } else {
            for (QString const& w : ini.warnings()) {
                r.notes << QStringLiteral("Emulatr.ini %1").arg(w);
            }
            if (platform != Platform::Unknown) {
                QString const model = ini.value(QString::fromLatin1(IniOverlay::kSecSystem),
                                                QString::fromLatin1(IniOverlay::kKeyModel));
                Platform const inIni = platformFromString(model);
                if (inIni != Platform::Unknown && inIni != platform) {
                    // A live model/manifest incoherence is the documented way
                    // to hang the console in PALcode with no OPA0 output.
                    r.problems << QStringLiteral(
                                      "Emulatr.ini says [System] model = %1 but this "
                                      "system is registered as %2.  Booting a "
                                      "mismatched model and manifest hangs the "
                                      "console with no output.")
                                      .arg(model, platformToString(platform));
                }
            }
        }
    }

    // firmware/ with at least one platform-valid image (L1).
    if (!d.exists(QStringLiteral("firmware"))) {
        r.problems << QStringLiteral("firmware\\ folder is missing");
    } else {
        QList<FirmwareCheck::Candidate> const found =
            FirmwareCheck::scan(d.filePath(QStringLiteral("firmware")), platform);
        if (found.isEmpty()) {
            r.problems << QStringLiteral(
                "No firmware image in firmware\\.  EmulatR does not ship "
                "firmware -- copy your own SRM image into that folder.");
        } else {
            bool anyMatch = false;
            for (FirmwareCheck::Candidate const& c : found) {
                if (c.match != FirmwareCheck::Match::Mismatch) { anyMatch = true; break; }
            }
            if (!anyMatch) {
                r.problems << QStringLiteral(
                                  "Every image in firmware\\ looks like it is for "
                                  "another platform, not %1.")
                                  .arg(platformToString(platform));
            }
        }
    }

    if (!d.exists(QStringLiteral("disks"))) {
        r.notes << QStringLiteral("disks\\ folder is missing; it will be created.");
    }

    r.health = r.problems.isEmpty()
                   ? (r.notes.isEmpty() ? Health::Ok : Health::Warning)
                   : Health::Broken;
    return r;
}

// ---------------------------------------------------------------------------
// inferPlatform (M5)
// ---------------------------------------------------------------------------
Platform inferPlatform(QString const& runDir, QString* how, bool* ambiguous)
{
    QDir const d(runDir);
    QStringList reasons;
    Platform fromManifest = Platform::Unknown;
    Platform fromIni      = Platform::Unknown;
    Platform fromFirmware = Platform::Unknown;

    // 1. Platform manifest filename: <lower(model)>_platform.json.
    for (Platform p : { Platform::DS10, Platform::DS20, Platform::ES40 }) {
        QString const fn = platformManifestFileName(p);
        if (QFileInfo::exists(d.filePath(fn))
            || QFileInfo::exists(d.filePath(QStringLiteral("config/") + fn))) {
            fromManifest = p;
            reasons << QStringLiteral("manifest %1 is present").arg(fn);
            break;
        }
    }

    // 2. [System] model in Emulatr.ini.
    QString const iniPath = d.filePath(QStringLiteral("Emulatr.ini"));
    if (QFileInfo::exists(iniPath)) {
        IniOverlay ini;
        if (ini.load(iniPath)) {
            QString const model = ini.value(QString::fromLatin1(IniOverlay::kSecSystem),
                                            QString::fromLatin1(IniOverlay::kKeyModel));
            fromIni = platformFromString(model);
            if (fromIni != Platform::Unknown)
                reasons << QStringLiteral("Emulatr.ini has [System] model = %1").arg(model);
        }
    }

    // 3. Firmware filenames, corroboration only.
    if (d.exists(QStringLiteral("firmware"))) {
        QList<FirmwareCheck::Candidate> const found =
            FirmwareCheck::scan(d.filePath(QStringLiteral("firmware")), Platform::Unknown);
        for (FirmwareCheck::Candidate const& c : found) {
            if (c.impliedPlatform != Platform::Unknown) {
                fromFirmware = c.impliedPlatform;
                reasons << QStringLiteral("firmware image %1 names %2")
                               .arg(c.fileName, platformToString(c.impliedPlatform));
                break;
            }
        }
    }

    // Authority order, with disagreement reported rather than resolved.
    Platform best = Platform::Unknown;
    if (fromManifest != Platform::Unknown)      best = fromManifest;
    else if (fromIni != Platform::Unknown)      best = fromIni;
    else if (fromFirmware != Platform::Unknown) best = fromFirmware;

    bool conflict = false;
    for (Platform p : { fromManifest, fromIni, fromFirmware }) {
        if (p != Platform::Unknown && best != Platform::Unknown && p != best) conflict = true;
    }

    if (how) {
        *how = reasons.isEmpty()
                   ? QStringLiteral("Nothing in this directory names a platform.")
                   : reasons.join(QStringLiteral("; "));
        if (conflict) {
            *how += QStringLiteral(
                ".  These disagree, so the platform cannot be inferred safely.");
        }
    }
    if (ambiguous) *ambiguous = conflict;
    return conflict ? Platform::Unknown : best;
}

}  // namespace RunDirSkeleton
}  // namespace launch
