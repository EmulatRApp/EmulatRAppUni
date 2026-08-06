// ============================================================================
// src/DiskImageFactory.cpp -- vDisk container creation (UI-free)
// ============================================================================
// Project: EmulatR -- EmulatrLaunch (SPEC-LAUNCH-001 Rev D.2)
// Copyright (C) 2026 eNVy Systems, Inc.  All rights reserved.
// Licensed under eNVy Systems Non-Commercial License v1.1
//
// Project Architect: Timothy Peer
// AI Collaboration:  Claude (Anthropic)
//
// Version: 1.0.0-alpha   Date: 2026-07-29
// Spec:    Section 9, Section 11 E7
// ============================================================================

#include "DiskImageFactory.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QStorageInfo>
#include <QStringList>

namespace launch {
namespace DiskImageFactory {

namespace {

QSet<QString> const& knownInterfaces()
{
    static QSet<QString> const k = {
        QStringLiteral("scsi"), QStringLiteral("sdi"),
        QStringLiteral("dssi"), QStringLiteral("lesi"),
    };
    return k;
}

// Windows reserved device names.  A container called "con.img" cannot be
// created and the failure would be baffling.
QSet<QString> const& reservedNames()
{
    static QSet<QString> const k = {
        QStringLiteral("con"), QStringLiteral("prn"), QStringLiteral("aux"),
        QStringLiteral("nul"),
        QStringLiteral("com1"), QStringLiteral("com2"), QStringLiteral("com3"),
        QStringLiteral("com4"), QStringLiteral("com5"), QStringLiteral("com6"),
        QStringLiteral("com7"), QStringLiteral("com8"), QStringLiteral("com9"),
        QStringLiteral("lpt1"), QStringLiteral("lpt2"), QStringLiteral("lpt3"),
        QStringLiteral("lpt4"), QStringLiteral("lpt5"), QStringLiteral("lpt6"),
        QStringLiteral("lpt7"), QStringLiteral("lpt8"), QStringLiteral("lpt9"),
    };
    return k;
}

qint64 parseInt(QString const& s, bool* ok)
{
    return s.trimmed().toLongLong(ok);
}

}  // namespace

QString Geometry::describe() const
{
    QString d = QStringLiteral("%1  %2 cyl x %3 heads x %4 sec = %5 blocks")
                    .arg(model)
                    .arg(cyl).arg(heads).arg(secTrk)
                    .arg(totalLbn);
    if (!capacity.isEmpty()) d += QStringLiteral("  (%1)").arg(capacity);
    return d;
}

// ---------------------------------------------------------------------------
// table
// ---------------------------------------------------------------------------
QList<Geometry> loadTable(QString* error)
{
    QList<Geometry> out;

    QFile f(QStringLiteral(":/data/dec_drive_geometries.tsv"));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error) *error = QStringLiteral("internal: drive geometry table is missing");
        return out;
    }

    QStringList problems;
    int lineNo = 0;
    while (!f.atEnd()) {
        ++lineNo;
        QString const line = QString::fromUtf8(f.readLine()).trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) continue;

        QStringList const c = line.split(QLatin1Char('\t'));
        if (c.size() < 7) continue;

        Geometry g;
        g.model = c.value(0).trimmed();
        g.iface = c.value(1).trimmed().toLower();
        if (!knownInterfaces().contains(g.iface)) continue;   // not a geometry row

        bool ok1 = false, ok2 = false, ok3 = false, ok4 = false, ok5 = false;
        g.secTrk     = parseInt(c.value(2), &ok1);
        g.heads      = parseInt(c.value(3), &ok2);
        g.cyl        = parseInt(c.value(4), &ok3);
        g.totalLbn   = parseInt(c.value(5), &ok4);
        g.blockBytes = parseInt(c.value(6), &ok5);
        g.capacity   = c.value(7).trimmed();
        g.note       = c.value(8).trimmed();

        if (!(ok1 && ok2 && ok3 && ok4 && ok5) || !g.isValid()) {
            problems << QStringLiteral("line %1 (%2): unparseable geometry")
                            .arg(lineNo).arg(g.model);
            continue;
        }

        // Cross-check the table against itself.  total_lbn is authoritative
        // (the header says so), but a row where CHS does not multiply out is
        // worth flagging rather than trusting blindly -- several SCSI rows are
        // nominal-ZBR and legitimately differ, so this is a note, not a
        // rejection, and the row is still offered.
        if (g.secTrk * g.heads * g.cyl != g.totalLbn && g.note != QStringLiteral("nominal_ZBR")) {
            problems << QStringLiteral("%1: CHS product %2 != total_lbn %3")
                            .arg(g.model)
                            .arg(g.secTrk * g.heads * g.cyl)
                            .arg(g.totalLbn);
        }

        out.append(g);
    }
    f.close();

    if (!problems.isEmpty() && error)
        *error = problems.join(QStringLiteral("; "));
    return out;
}

Geometry customGeometry(qint64 cyl, qint64 heads, qint64 secTrk,
                        qint64 blockBytes, QString* error)
{
    Geometry g;
    if (cyl <= 0 || cyl > kMaxCylinders) {
        if (error) *error = QStringLiteral("Cylinders must be between 1 and %1.")
                                .arg(kMaxCylinders);
        return g;
    }
    if (heads <= 0 || heads > kMaxHeads) {
        if (error) *error = QStringLiteral("Heads must be between 1 and %1.").arg(kMaxHeads);
        return g;
    }
    if (secTrk <= 0 || secTrk > kMaxSectors) {
        if (error) *error = QStringLiteral("Sectors per track must be between 1 and %1.")
                                .arg(kMaxSectors);
        return g;
    }
    if (blockBytes != 512 && blockBytes != 2048) {
        if (error) *error = QStringLiteral(
            "Block size must be 512 (disk) or 2048 (ISO-9660 optical).");
        return g;
    }

    qint64 const total = cyl * heads * secTrk;
    if (total * blockBytes > kMaxImageBytes) {
        if (error) *error = QStringLiteral("That geometry asks for %1, over the %2 limit.")
                                .arg(humanSize(total * blockBytes), humanSize(kMaxImageBytes));
        return g;
    }

    g.model      = QStringLiteral("Custom");
    g.iface      = QStringLiteral("scsi");
    g.cyl        = cyl;
    g.heads      = heads;
    g.secTrk     = secTrk;
    g.totalLbn   = total;
    g.blockBytes = blockBytes;
    g.capacity   = humanSize(total * blockBytes);
    return g;
}

// ---------------------------------------------------------------------------
// naming
// ---------------------------------------------------------------------------
bool isValidNameStem(QString const& stem, QString* reason)
{
    QString const s = stem.trimmed();
    if (s.isEmpty()) {
        if (reason) *reason = QStringLiteral("Give the disk a name.");
        return false;
    }
    if (s.size() > 64) {
        if (reason) *reason = QStringLiteral("Keep the name to 64 characters or fewer.");
        return false;
    }
    for (QChar const ch : s) {
        ushort const u = ch.unicode();
        if (u < 0x20 || u > 0x7E) {
            if (reason) *reason = QStringLiteral("Use plain ASCII characters only.");
            return false;
        }
    }
    for (QChar const bad : QStringLiteral("\\/:*?\"<>|")) {
        if (s.contains(bad)) {
            if (reason) {
                *reason = QStringLiteral("The name cannot contain any of  \\ / : * ? \" < > |");
            }
            return false;
        }
    }
    if (s.endsWith(QLatin1Char('.')) || s.endsWith(QLatin1Char(' '))) {
        if (reason) *reason = QStringLiteral("The name cannot end with a dot or a space.");
        return false;
    }
    if (reservedNames().contains(s.toLower())) {
        if (reason) {
            *reason = QStringLiteral("\"%1\" is a reserved Windows device name.").arg(s);
        }
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// creation
// ---------------------------------------------------------------------------
qint64 freeSpaceFor(QString const& path)
{
    QString probe = path;
    // Walk up to the nearest directory that exists -- the disks/ folder may
    // not have been created yet.
    while (!probe.isEmpty() && !QFileInfo::exists(probe)) {
        QString const parent = QFileInfo(probe).absolutePath();
        if (parent == probe) break;
        probe = parent;
    }
    if (probe.isEmpty()) return -1;

    QStorageInfo const info(probe);
    if (!info.isValid() || !info.isReady()) return -1;
    return info.bytesAvailable();
}

QString humanSize(qint64 bytes)
{
    if (bytes < 0) return QStringLiteral("unknown");
    double v = static_cast<double>(bytes);
    char const* units[] = { "bytes", "KiB", "MiB", "GiB", "TiB" };
    int u = 0;
    while (v >= 1024.0 && u < 4) { v /= 1024.0; ++u; }
    return u == 0 ? QStringLiteral("%1 bytes").arg(bytes)
                  : QStringLiteral("%1 %2").arg(v, 0, 'f', 2).arg(QLatin1String(units[u]));
}

bool create(QString const& destPath, Geometry const& geometry, QString* error)
{
    if (!geometry.isValid()) {
        if (error) *error = QStringLiteral("The geometry is not usable.");
        return false;
    }

    qint64 const size = geometry.imageSizeBytes();

    if (QFileInfo::exists(destPath)) {
        // Matches FileBlockMedia's own rule: "An existing file is NEVER
        // overwritten."  Silently replacing a disk image is data loss.
        if (error) {
            *error = QStringLiteral("%1 already exists.  Choose another name; the "
                                    "launcher never overwrites a disk image.")
                         .arg(QDir::toNativeSeparators(destPath));
        }
        return false;
    }

    QDir const parent(QFileInfo(destPath).absolutePath());
    if (!parent.exists() && !QDir().mkpath(parent.absolutePath())) {
        if (error) {
            *error = QStringLiteral("cannot create %1")
                         .arg(QDir::toNativeSeparators(parent.absolutePath()));
        }
        return false;
    }

    // E7: refuse when the volume cannot hold it, naming BOTH numbers.
    qint64 const freeBytes = freeSpaceFor(destPath);
    if (freeBytes >= 0 && freeBytes < size) {
        if (error) {
            *error = QStringLiteral(
                         "Not enough room: %1 needs %2 but only %3 is free on that "
                         "volume.")
                         .arg(QDir::toNativeSeparators(destPath),
                              humanSize(size), humanSize(freeBytes));
        }
        return false;
    }

    QFile f(destPath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
        if (error) {
            *error = QStringLiteral("cannot create %1: %2")
                         .arg(QDir::toNativeSeparators(destPath), f.errorString());
        }
        return false;
    }

    // Raw flat image: set the length and write nothing.  On NTFS the tail is
    // allocated-but-uninitialized, so this is instant and costs no physical
    // space until written -- the same behavior FileBlockMedia documents for
    // std::filesystem::resize_file.  The image reads back as zeros: a blank
    // install target.
    if (!f.resize(size)) {
        QString const why = f.errorString();
        f.close();
        f.remove();                     // no half-made container left behind
        if (error) {
            *error = QStringLiteral("could not size %1 to %2: %3")
                         .arg(QDir::toNativeSeparators(destPath), humanSize(size), why);
        }
        return false;
    }
    f.close();
    return true;
}

}  // namespace DiskImageFactory
}  // namespace launch
