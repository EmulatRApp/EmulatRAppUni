// ============================================================================
// src/DiskImageFactory.h -- vDisk container creation (UI-free)
// ============================================================================
// Project: EmulatR -- EmulatrLaunch (SPEC-LAUNCH-001 Rev D.2)
// Copyright (C) 2026 eNVy Systems, Inc.  All rights reserved.
// Licensed under eNVy Systems Non-Commercial License v1.1
//
// Project Architect: Timothy Peer
// AI Collaboration:  Claude (Anthropic)
//
// Version: 1.0.0-alpha   Date: 2026-07-29
// Spec:    Section 9 S1-S5, Section 11 E7
//
// ---------------------------------------------------------------------------
// CONTAINER FORMAT (gate G3)
//
// The format is owned by SPEC-SCSIH-001 / the IBlockMedia layer, and it is
// already decided in the core tree:
//
//     deviceLib/scsi/FileBlockMedia.h -- "Raw flat-image backing (approved
//     2026-06-12)".  Offset = lba * blockSize.  No header, no footer, no
//     metadata.  Its create_if_missing path makes a file of exactly
//     createBytes and notes "the file reads back as zeros (a blank install
//     target); on a sparse-capable FS resize_file does not pre-allocate".
//
// So a container is simply a file of total_lbn * block_bytes zero bytes, and
// this factory produces exactly that.  There is no launcher-local format
// invention here and there must never be one: if this file ever grows a
// header, the emulator stops being able to read its own disks.
//
// S1 makes this the SINGLE implementation of creation logic in the app, and
// S5 keeps the factory/UI split so a future mkdisk.exe is a thin wrapper.
// ---------------------------------------------------------------------------
// ============================================================================

#ifndef EMULATRLAUNCH_DISKIMAGEFACTORY_H
#define EMULATRLAUNCH_DISKIMAGEFACTORY_H

#include <QList>
#include <QString>

namespace launch {
namespace DiskImageFactory {

// One row of the verified DEC drive geometry table (Section 9 S2).
struct Geometry
{
    QString model;          // "RZ29"
    QString iface;          // scsi | sdi | dssi | lesi
    qint64  secTrk     = 0;
    qint64  heads      = 0;
    qint64  cyl        = 0;
    qint64  totalLbn   = 0;
    qint64  blockBytes = 512;
    QString capacity;       // marketed figure, e.g. "4.2G"
    QString note;

    bool isValid() const { return totalLbn > 0 && blockBytes > 0; }

    // The authoritative container size.  total_lbn is exact; the capacity
    // column is marketing and is never used for sizing.
    qint64 imageSizeBytes() const { return totalLbn * blockBytes; }

    QString describe() const;
};

// Parses resources/data/dec_drive_geometries.tsv.  Rows whose interface token
// is not recognized are skipped, so the device-name-prefix section of the
// core's combined table cannot leak in as bogus drives.
QList<Geometry> loadTable(QString* error = nullptr);

// A geometry assembled by hand in the dialog's Custom mode, with sanity
// bounds applied (S2).  Returns an invalid Geometry when out of bounds.
Geometry customGeometry(qint64 cyl, qint64 heads, qint64 secTrk,
                        qint64 blockBytes, QString* error = nullptr);

// Sanity bounds for Custom.  Generous but finite: the point is to stop a
// typo from asking for a petabyte, not to police plausible geometries.
inline constexpr qint64 kMaxCylinders  = 1000000;
inline constexpr qint64 kMaxHeads      = 255;
inline constexpr qint64 kMaxSectors    = 1000;
inline constexpr qint64 kMaxImageBytes = 2LL * 1024 * 1024 * 1024 * 1024;   // 2 TiB

// Creates the container.  Refuses to overwrite an existing file and refuses
// when the target volume has less free space than the image needs (E7).
// `error` names both numbers on a space refusal, as the spec requires.
bool create(QString const& destPath, Geometry const& geometry, QString* error);

// Free bytes on the volume holding `path` (or its nearest existing parent).
// -1 when it cannot be determined.
qint64 freeSpaceFor(QString const& path);

// Human-readable byte count, e.g. "4.01 GiB".
QString humanSize(qint64 bytes);

// True when `stem` is a usable file stem: ASCII, no path separators, no
// Windows-reserved characters or device names (S2).
bool isValidNameStem(QString const& stem, QString* reason = nullptr);

// The extension the launcher gives new containers.  The format carries no
// signature, so the extension is convention only -- FileBlockMedia opens any
// name.  Kept in one place so it is a one-line change if the project settles
// on another.
inline constexpr char const* kContainerExtension = "img";

}  // namespace DiskImageFactory
}  // namespace launch

#endif  // EMULATRLAUNCH_DISKIMAGEFACTORY_H
