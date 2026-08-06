// ============================================================================
// src/ExeDiscovery.cpp -- discovery of sibling executables
// ============================================================================
// Project: EmulatR -- EmulatrLaunch (SPEC-LAUNCH-001 Rev D.2)
// Copyright (C) 2026 eNVy Systems, Inc.  All rights reserved.
// Licensed under eNVy Systems Non-Commercial License v1.1
//
// Project Architect: Timothy Peer
// AI Collaboration:  Claude (Anthropic)
//
// Version: 1.0.0-alpha   Date: 2026-07-29
// Spec:    Section 6, Section 7 W3
// ============================================================================

#include "ExeDiscovery.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>

#include "Version.h"

namespace launch {
namespace ExeDiscovery {

namespace {

// Step 2 -- the Setup Factory payload location (Section 6).
constexpr char const* kInstallDir =
    "C:/Program Files/eNVy Systems, Inc/asa-emulatR";

// Step 3 -- dev-tree build output.  Absent on tester machines, harmless.
constexpr char const* kDevTreeRoot =
    "D:/EmulatR/EmulatRAppUniV5/out/build";

// Newest config first, so a dev who just rebuilt Release does not silently
// keep launching a stale Debug binary.
QString newestInDevTree(QString const& exeName, QStringList* searched)
{
    QDir root(QString::fromLatin1(kDevTreeRoot));
    if (!root.exists()) {
        if (searched) *searched << QDir::toNativeSeparators(root.absolutePath())
                                       + QStringLiteral("  (no dev tree)");
        return {};
    }

    QString best;
    QDateTime bestTime;
    QStringList const configs = root.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (QString const& cfg : configs) {
        QString const candidate = root.filePath(cfg + QLatin1Char('/') + exeName);
        if (searched) *searched << QDir::toNativeSeparators(candidate);
        QFileInfo const fi(candidate);
        if (!fi.isFile()) continue;
        if (best.isEmpty() || fi.lastModified() > bestTime) {
            best     = fi.absoluteFilePath();
            bestTime = fi.lastModified();
        }
    }
    return best;
}

Result probe(char const* overrideKey, QString const& exeName,
             QStringList* searched)
{
    Result r;

    // 1. Explicit user override.
    QSettings s = openSettings();
    QString const override = s.value(QLatin1String(overrideKey)).toString();
    if (!override.isEmpty()) {
        if (searched) *searched << QDir::toNativeSeparators(override)
                                       + QStringLiteral("  (settings override)");
        if (QFileInfo(override).isFile()) {
            r.path   = QDir::toNativeSeparators(QFileInfo(override).absoluteFilePath());
            r.source = Source::UserOverride;
            r.detail = QStringLiteral("Using the path set in Settings.");
            return r;
        }
        // An override that no longer resolves must be LOUD, not silently
        // skipped -- otherwise the launcher runs a different binary than the
        // one the user believes they pinned.
        r.detail = QStringLiteral(
                       "The path set in Settings does not exist: %1.  Clear it "
                       "or point it at the current binary.")
                       .arg(QDir::toNativeSeparators(override));
        return r;
    }

    // 2. Installed location.
    QString const installed =
        QDir(QString::fromLatin1(kInstallDir)).filePath(exeName);
    if (searched) *searched << QDir::toNativeSeparators(installed);
    if (QFileInfo(installed).isFile()) {
        r.path   = QDir::toNativeSeparators(QFileInfo(installed).absoluteFilePath());
        r.source = Source::Installed;
        r.detail = QStringLiteral("Found in the installed location.");
        return r;
    }

    // 3. Dev tree.
    QString const dev = newestInDevTree(exeName, searched);
    if (!dev.isEmpty()) {
        r.path   = QDir::toNativeSeparators(dev);
        r.source = Source::DevTree;
        r.detail = QStringLiteral("Found in the development build tree (newest config).");
        return r;
    }

    r.detail = QStringLiteral("%1 was not found.").arg(exeName);
    return r;
}

}  // namespace

Result findEmulatr()
{
    Result r = probe(keys::kEmulatrExeOverride, QStringLiteral("Emulatr.exe"), nullptr);
    if (!r.found() && r.detail.isEmpty())
        r.detail = QStringLiteral("Emulatr.exe was not found.");
    return r;
}

Result findPlatEd()
{
    return probe(keys::kPlatEdExeOverride, QStringLiteral("PlatEd.exe"), nullptr);
}

Result findTerminal()
{
    // Steps 1-3 as usual, then the extra hops W3 asks for: the common PuTTY
    // install locations and PATH.
    Result r = probe(keys::kTerminalExeOverride, QStringLiteral("putty.exe"), nullptr);
    if (r.found()) return r;

    QStringList const common = {
        QStringLiteral("C:/Program Files/PuTTY/putty.exe"),
        QStringLiteral("C:/Program Files (x86)/PuTTY/putty.exe"),
    };
    for (QString const& c : common) {
        if (QFileInfo(c).isFile()) {
            r.path   = QDir::toNativeSeparators(c);
            r.source = Source::Installed;
            r.detail = QStringLiteral("Found PuTTY in its default install location.");
            return r;
        }
    }

    QString const onPath = QStandardPaths::findExecutable(QStringLiteral("putty"));
    if (!onPath.isEmpty()) {
        r.path   = QDir::toNativeSeparators(onPath);
        r.source = Source::SystemPath;
        r.detail = QStringLiteral("Found putty.exe on PATH.");
        return r;
    }

    r.path.clear();
    r.source = Source::NotFound;
    r.detail = QStringLiteral(
        "PuTTY was not found.  Open Console will fall back to the system "
        "telnet handler; if that is absent too, connect your own terminal to "
        "the host and port shown below.");
    return r;
}

void setOverride(char const* settingsKey, QString const& path)
{
    QSettings s = openSettings();
    if (path.trimmed().isEmpty()) s.remove(QLatin1String(settingsKey));
    else                          s.setValue(QLatin1String(settingsKey), path);
}

QStringList searchedPathsForEmulatr()
{
    QStringList out;
    probe(keys::kEmulatrExeOverride, QStringLiteral("Emulatr.exe"), &out);
    return out;
}

QStringList searchedPathsForPlatEd()
{
    QStringList out;
    probe(keys::kPlatEdExeOverride, QStringLiteral("PlatEd.exe"), &out);
    return out;
}

QStringList searchedPathsForTerminal()
{
    QStringList out;
    probe(keys::kTerminalExeOverride, QStringLiteral("putty.exe"), &out);
    out << QStringLiteral("C:\\Program Files\\PuTTY\\putty.exe")
        << QStringLiteral("C:\\Program Files (x86)\\PuTTY\\putty.exe")
        << QStringLiteral("putty.exe on PATH");
    return out;
}

}  // namespace ExeDiscovery
}  // namespace launch
