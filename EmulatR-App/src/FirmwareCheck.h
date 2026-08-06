// ============================================================================
// src/FirmwareCheck.h -- firmware presence and platform-fit validation
// ============================================================================
// Project: EmulatR -- EmulatrLaunch (SPEC-LAUNCH-001 Rev D.2)
// Copyright (C) 2026 eNVy Systems, Inc.  All rights reserved.
// Licensed under eNVy Systems Non-Commercial License v1.1
//
// Project Architect: Timothy Peer
// AI Collaboration:  Claude (Anthropic)
//
// Version: 1.0.0-alpha   Date: 2026-07-29
// Spec:    Section 7 W2 -- "pick from files present in {run-dir}/firmware/,
//          validated by FirmwareCheck against the system's platform.  Never a
//          free-text path outside the run dir."
//
// SCOPE LIMIT -- READ THIS BEFORE STRENGTHENING THE CHECK.
// The platform fit below is a FILENAME HEURISTIC, and it is labelled as one
// everywhere it surfaces.  Deciding a platform from an image's CONTENT means
// knowing the SRM image header layout, which is gate G2 evidence this app does
// not have.  So the rule here is: warn loudly, never refuse.  A tester with a
// correctly-built image under an unusual name must still be able to boot.
// When G2 lands an authoritative identity check, it replaces classify()'s
// body -- the Candidate contract is already shaped for it.
// ============================================================================

#ifndef EMULATRLAUNCH_FIRMWARECHECK_H
#define EMULATRLAUNCH_FIRMWARECHECK_H

#include <QList>
#include <QString>

#include "SystemRecord.h"

namespace launch {
namespace FirmwareCheck {

enum class Match
{
    Match,      // filename names this system's platform
    Unknown,    // filename names no platform -- allowed, flagged
    Mismatch    // filename names a DIFFERENT platform -- allowed, warned hard
};

// Firmware image format, resolved the way the core resolves it
// (emulatrappuniv5/main.cpp): a .exe extension means an SRM-format image --
// the vendor's convention -- and anything else is a raw ROM dump.
enum class Format { Srm, Raw };

struct Candidate
{
    QString  fileName;          // bare name, as it appears in firmware/
    QString  absolutePath;
    QString  relativePath;      // "firmware/<name>" -- what goes in the ini
    qint64   sizeBytes = 0;
    Format   format    = Format::Raw;
    Platform impliedPlatform = Platform::Unknown;
    Match    match     = Match::Unknown;
    QString  note;              // one line, ready for a status label
};

// Lists every regular file in `firmwareDir` as a candidate, newest first.
// The readme this app drops there is excluded.  `platform` may be Unknown,
// in which case every Candidate::match is Unknown and only impliedPlatform is
// filled in -- that is the mode M5's platform inference uses.
QList<Candidate> scan(QString const& firmwareDir, Platform platform);

// Classifies one file.  Cheap: filename and size only, no image read.
Candidate classify(QString const& absolutePath, Platform platform);

// True when this file should be offered at all.  Filters the readme and
// obvious non-images (.txt, .md, .log) so the combo stays clean.
bool looksLikeFirmware(QString const& fileName);

}  // namespace FirmwareCheck
}  // namespace launch

#endif  // EMULATRLAUNCH_FIRMWARECHECK_H
