// ============================================================================
// src/FirmwareCheck.cpp -- firmware presence and platform-fit validation
// ============================================================================
// Project: EmulatR -- EmulatrLaunch (SPEC-LAUNCH-001 Rev D.2)
// Copyright (C) 2026 eNVy Systems, Inc.  All rights reserved.
// Licensed under eNVy Systems Non-Commercial License v1.1
//
// Project Architect: Timothy Peer
// AI Collaboration:  Claude (Anthropic)
//
// Version: 1.0.0-alpha   Date: 2026-07-29
// Spec:    Section 7 W2
// ============================================================================

#include "FirmwareCheck.h"

#include <QDir>
#include <QFileInfo>

namespace launch {
namespace FirmwareCheck {

namespace {

// Filename tokens that name a platform.  "cl67" is included because the ES40
// firmware in this project's own tree is the cl67 build, and a tester who
// received it under that name would otherwise be told it names no platform.
struct Token { char const* text; Platform platform; };

Token const kTokens[] = {
    { "ds10", Platform::DS10 },
    { "ds20", Platform::DS20 },
    { "es40", Platform::ES40 },
    { "cl67", Platform::ES40 },
};

}  // namespace

bool looksLikeFirmware(QString const& fileName)
{
    QString const lower = fileName.toLower();
    if (lower == QLatin1String("firmware_readme.txt")) return false;
    for (char const* ext : { ".txt", ".md", ".log", ".ini", ".json", ".zip", ".7z" }) {
        if (lower.endsWith(QLatin1String(ext))) return false;
    }
    if (fileName.startsWith(QLatin1Char('.'))) return false;
    return true;
}

Candidate classify(QString const& absolutePath, Platform platform)
{
    QFileInfo const fi(absolutePath);

    Candidate c;
    c.fileName     = fi.fileName();
    c.absolutePath = QDir::toNativeSeparators(fi.absoluteFilePath());
    c.relativePath = QStringLiteral("firmware/") + fi.fileName();
    c.sizeBytes    = fi.size();
    c.format       = fi.suffix().compare(QLatin1String("exe"), Qt::CaseInsensitive) == 0
                         ? Format::Srm : Format::Raw;

    QString const lower = c.fileName.toLower();
    for (Token const& t : kTokens) {
        if (lower.contains(QLatin1String(t.text))) { c.impliedPlatform = t.platform; break; }
    }

    if (platform == Platform::Unknown) {
        c.match = Match::Unknown;
        return c;
    }

    if (c.impliedPlatform == Platform::Unknown) {
        c.match = Match::Unknown;
        c.note  = QStringLiteral(
            "Name does not say which platform this image is for.  It will be "
            "used as-is; if the console never reaches P00>>>, suspect this "
            "first.");
    } else if (c.impliedPlatform == platform) {
        c.match = Match::Match;
        c.note  = QStringLiteral("Matches this system's platform (%1).")
                      .arg(platformToString(platform));
    } else {
        c.match = Match::Mismatch;
        c.note  = QStringLiteral(
                      "This image names %1, but the system is %2.  Booting a "
                      "mismatched image against this manifest is a known way to "
                      "hang in PALcode with no console output.")
                      .arg(platformToString(c.impliedPlatform),
                           platformToString(platform));
    }
    return c;
}

QList<Candidate> scan(QString const& firmwareDir, Platform platform)
{
    QList<Candidate> out;

    QDir d(firmwareDir);
    if (!d.exists()) return out;

    d.setFilter(QDir::Files | QDir::NoSymLinks);
    d.setSorting(QDir::Time);                     // newest first: the one just copied in
    QFileInfoList const entries = d.entryInfoList();

    for (QFileInfo const& fi : entries) {
        if (!looksLikeFirmware(fi.fileName())) continue;
        out.append(classify(fi.absoluteFilePath(), platform));
    }
    return out;
}

}  // namespace FirmwareCheck
}  // namespace launch
