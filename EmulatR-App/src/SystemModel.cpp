// ============================================================================
// src/SystemModel.cpp -- QSettings-backed registry of named systems
// ============================================================================
// Project: EmulatR -- EmulatrLaunch (SPEC-LAUNCH-001 Rev D.2)
// Copyright (C) 2026 eNVy Systems, Inc.  All rights reserved.
// Licensed under eNVy Systems Non-Commercial License v1.1
//
// Project Architect: Timothy Peer
// AI Collaboration:  Claude (Anthropic)
//
// Version: 1.0.0-alpha   Date: 2026-07-29
// Spec:    Section 4 (M1-M6), Section 11 E1/E9/E10
// ============================================================================

#include "SystemModel.h"

#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QUuid>
#include <algorithm>

#include "IniOverlay.h"
#include "RunDirSkeleton.h"
#include "Version.h"

namespace launch {

namespace {

constexpr char const* kSystemsGroup = "systems";

QString newGuid()
{
    return QUuid::createUuid().toString(QUuid::WithBraces);
}

QString settingsKey(QString const& id, char const* leaf)
{
    return QStringLiteral("%1/%2/%3")
        .arg(QLatin1String(kSystemsGroup), id, QLatin1String(leaf));
}

}  // namespace

SystemModel::SystemModel(QObject* parent) : QAbstractListModel(parent) {}

// ---------------------------------------------------------------------------
// QAbstractListModel
// ---------------------------------------------------------------------------
int SystemModel::rowCount(QModelIndex const& parent) const
{
    return parent.isValid() ? 0 : count();
}

QVariant SystemModel::data(QModelIndex const& index, int role) const
{
    if (!index.isValid() || !isValidRow(index.row())) return {};
    SystemRecord const& r = m_records.at(index.row());

    switch (role) {
        case Qt::DisplayRole:
        case NameRole:       return r.name;
        case IdRole:         return r.id;
        case PlatformRole:   return platformToString(r.platform);
        case RunDirRole:     return QDir::toNativeSeparators(r.runDir);
        case RunningRole:    return r.running;
        case HealthRole:     return static_cast<int>(r.health);
        case HealthNoteRole: return r.healthNote;
        case ConsolePortRole: return consolePort(index.row());

        case Qt::ToolTipRole: {
            QStringList lines;
            lines << QStringLiteral("%1  --  %2")
                         .arg(r.name, platformDescription(r.platform));
            lines << QDir::toNativeSeparators(r.runDir);
            int const port = consolePort(index.row());
            if (port > 0) lines << QStringLiteral("Console: 127.0.0.1:%1").arg(port);
            if (!r.healthNote.isEmpty()) lines << r.healthNote;
            if (r.running) lines << QStringLiteral("RUNNING");
            return lines.join(QLatin1Char('\n'));
        }
        default: break;
    }
    return {};
}

QHash<int, QByteArray> SystemModel::roleNames() const
{
    QHash<int, QByteArray> n = QAbstractListModel::roleNames();
    n[IdRole]          = "systemId";
    n[NameRole]        = "name";
    n[PlatformRole]    = "platform";
    n[RunDirRole]      = "runDir";
    n[RunningRole]     = "running";
    n[HealthRole]      = "health";
    n[HealthNoteRole]  = "healthNote";
    n[ConsolePortRole] = "consolePort";
    return n;
}

// ---------------------------------------------------------------------------
// load / persist  (M1)
// ---------------------------------------------------------------------------
void SystemModel::load()
{
    beginResetModel();
    m_records.clear();

    QSettings s = openSettings();
    s.beginGroup(QLatin1String(kSystemsGroup));
    QStringList const ids = s.childGroups();
    for (QString const& id : ids) {
        SystemRecord r;
        r.id       = id;
        r.name     = s.value(id + QStringLiteral("/name")).toString();
        r.platform = platformFromString(s.value(id + QStringLiteral("/platform")).toString());
        r.runDir   = s.value(id + QStringLiteral("/runDir")).toString();

        // A record that lost a field is still shown -- silently dropping a
        // system because one key went missing would look like data loss to a
        // tester.  It is surfaced as broken by preflight instead.
        if (r.id.isEmpty()) continue;
        if (r.name.isEmpty()) r.name = QStringLiteral("(unnamed system)");
        m_records.append(r);
    }
    s.endGroup();

    sortByName();
    endResetModel();
    emit systemsChanged();
}

void SystemModel::persist(int row) const
{
    if (!isValidRow(row)) return;
    SystemRecord const& r = m_records.at(row);
    QSettings s = openSettings();
    s.setValue(settingsKey(r.id, "name"),     r.name);
    s.setValue(settingsKey(r.id, "platform"), platformToString(r.platform));
    s.setValue(settingsKey(r.id, "runDir"),   r.runDir);
}

void SystemModel::persistAll() const
{
    for (int i = 0; i < count(); ++i) persist(i);
}

// ---------------------------------------------------------------------------
// lookups
// ---------------------------------------------------------------------------
int SystemModel::rowForId(QString const& id) const
{
    for (int i = 0; i < count(); ++i)
        if (m_records.at(i).id == id) return i;
    return -1;
}

int SystemModel::rowForRunDir(QString const& anyPath) const
{
    QString const key = runDirCompareKey(anyPath);
    for (int i = 0; i < count(); ++i)
        if (runDirCompareKey(m_records.at(i).runDir) == key) return i;
    return -1;
}

int SystemModel::rowForName(QString const& name, int ignoreRow) const
{
    for (int i = 0; i < count(); ++i) {
        if (i == ignoreRow) continue;
        if (m_records.at(i).name.compare(name, Qt::CaseInsensitive) == 0) return i;
    }
    return -1;
}

void SystemModel::sortByName()
{
    std::sort(m_records.begin(), m_records.end(),
              [](SystemRecord const& a, SystemRecord const& b) {
                  return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
              });
}

// ---------------------------------------------------------------------------
// createSystem (M4)
// ---------------------------------------------------------------------------
QString SystemModel::createSystem(QString const& name, Platform platform,
                                  QString const& runDir, QString* error)
{
    QString const trimmed = name.trimmed();
    if (trimmed.isEmpty()) {
        if (error) *error = QStringLiteral("A system needs a name.");
        return {};
    }
    if (platform == Platform::Unknown) {
        if (error) *error = QStringLiteral("Choose a platform: DS10, DS20, or ES40.");
        return {};
    }
    if (int const clash = rowForName(trimmed); clash >= 0) {
        if (error) {
            *error = QStringLiteral("A system named \"%1\" already exists.  Names "
                                    "must be unique, ignoring case.")
                         .arg(m_records.at(clash).name);
        }
        return {};
    }

    QString const canon = canonicalRunDir(runDir);
    if (int const clash = rowForRunDir(canon); clash >= 0) {
        // E9: the run dir is one-to-one with the system.
        if (error) {
            *error = QStringLiteral("%1 is already registered as \"%2\".")
                         .arg(QDir::toNativeSeparators(canon), m_records.at(clash).name);
        }
        return {};
    }

    int const port = nextFreeConsolePort();
    if (!RunDirSkeleton::create(canon, platform, port, error)) return {};

    SystemRecord r;
    r.id       = newGuid();
    r.name     = trimmed;
    r.platform = platform;
    r.runDir   = canonicalRunDir(canon);   // re-canonicalize: it exists now

    beginResetModel();
    m_records.append(r);
    sortByName();
    endResetModel();

    persist(rowForId(r.id));
    emit systemsChanged();
    return r.id;
}

// ---------------------------------------------------------------------------
// registerExisting (M5)
// ---------------------------------------------------------------------------
QString SystemModel::registerExisting(QString const& name, Platform platform,
                                      QString const& runDir, QString* error)
{
    QString const trimmed = name.trimmed();
    if (trimmed.isEmpty()) {
        if (error) *error = QStringLiteral("A system needs a name.");
        return {};
    }
    if (platform == Platform::Unknown) {
        if (error) {
            *error = QStringLiteral("The platform for this directory could not be "
                                    "determined; state it explicitly.");
        }
        return {};
    }
    if (int const clash = rowForName(trimmed); clash >= 0) {
        if (error) {
            *error = QStringLiteral("A system named \"%1\" already exists.")
                         .arg(m_records.at(clash).name);
        }
        return {};
    }

    QString const canon = canonicalRunDir(runDir);
    if (int const clash = rowForRunDir(canon); clash >= 0) {
        if (error) {
            *error = QStringLiteral("%1 is already registered as \"%2\".")
                         .arg(QDir::toNativeSeparators(canon), m_records.at(clash).name);
        }
        return {};
    }
    if (RunDirSkeleton::isUnderProgramFiles(canon)) {
        if (error) {
            *error = QStringLiteral(
                         "%1 is inside Program Files.  Writes there are "
                         "redirected to a VirtualStore copy and the emulator's "
                         "flash state is lost.  Move the run directory somewhere "
                         "writable first.")
                         .arg(QDir::toNativeSeparators(canon));
        }
        return {};
    }
    if (!RunDirSkeleton::probeWritable(canon, error)) return {};

    // The contract says logs/ and traces/ are created if absent -- do it now
    // rather than at Start so the folders exist for the tester to look in.
    RunDirSkeleton::ensureSubdirs(canon, nullptr);

    SystemRecord r;
    r.id       = newGuid();
    r.name     = trimmed;
    r.platform = platform;
    r.runDir   = canon;

    beginResetModel();
    m_records.append(r);
    sortByName();
    endResetModel();

    persist(rowForId(r.id));
    emit systemsChanged();
    return r.id;
}

// ---------------------------------------------------------------------------
// removeSystem (M6) -- registration and settings only, NEVER the directory
// ---------------------------------------------------------------------------
void SystemModel::removeSystem(int row)
{
    if (!isValidRow(row)) return;
    QString const id = m_records.at(row).id;

    beginRemoveRows(QModelIndex(), row, row);
    m_records.removeAt(row);
    endRemoveRows();

    // Removes name/platform/runDir AND the env/ subtree in one call (M1: all
    // per-system state keys off the GUID, so there is exactly one place to go).
    QSettings s = openSettings();
    s.remove(QStringLiteral("%1/%2").arg(QLatin1String(kSystemsGroup), id));
    if (s.value(QLatin1String(keys::kLastSelected)).toString() == id)
        s.remove(QLatin1String(keys::kLastSelected));

    if (m_runningId == id) m_runningId.clear();
    emit systemsChanged();
}

// ---------------------------------------------------------------------------
// renameSystem
// ---------------------------------------------------------------------------
bool SystemModel::renameSystem(int row, QString const& newName, QString* error)
{
    if (!isValidRow(row)) return false;
    QString const trimmed = newName.trimmed();
    if (trimmed.isEmpty()) {
        if (error) *error = QStringLiteral("A system needs a name.");
        return false;
    }
    if (int const clash = rowForName(trimmed, row); clash >= 0) {
        if (error) {
            *error = QStringLiteral("A system named \"%1\" already exists.")
                         .arg(m_records.at(clash).name);
        }
        return false;
    }

    QString const id = m_records.at(row).id;
    m_records[row].name = trimmed;
    persist(row);

    beginResetModel();
    sortByName();
    endResetModel();

    emit systemsChanged();
    Q_UNUSED(id);
    return true;
}

// ---------------------------------------------------------------------------
// relocateSystem (E1) -- keeps the GUID, so env selections survive the move
// ---------------------------------------------------------------------------
bool SystemModel::relocateSystem(int row, QString const& newRunDir, QString* error)
{
    if (!isValidRow(row)) return false;

    QString const canon = canonicalRunDir(newRunDir);
    if (int const clash = rowForRunDir(canon); clash >= 0 && clash != row) {
        if (error) {
            *error = QStringLiteral("%1 is already registered as \"%2\".")
                         .arg(QDir::toNativeSeparators(canon), m_records.at(clash).name);
        }
        return false;
    }
    if (RunDirSkeleton::isUnderProgramFiles(canon)) {
        if (error) {
            *error = QStringLiteral("%1 is inside Program Files and cannot be a run "
                                    "directory.")
                         .arg(QDir::toNativeSeparators(canon));
        }
        return false;
    }
    if (!RunDirSkeleton::probeWritable(canon, error)) return false;

    m_records[row].runDir = canon;
    persist(row);
    emit dataChanged(index(row), index(row));
    emit systemsChanged();
    return true;
}

// ---------------------------------------------------------------------------
// transient state
// ---------------------------------------------------------------------------
void SystemModel::setRunning(QString const& id, bool running)
{
    int const row = rowForId(id);
    if (row < 0) return;
    m_records[row].running = running;
    m_runningId = running ? id : QString();
    emit dataChanged(index(row), index(row));
    // L4: Start enablement for EVERY row depends on this, not just this row.
    emit systemsChanged();
}

void SystemModel::setHealth(int row, Health health, QString const& note)
{
    if (!isValidRow(row)) return;
    if (m_records.at(row).health == health && m_records.at(row).healthNote == note)
        return;
    m_records[row].health     = health;
    m_records[row].healthNote = note;
    emit dataChanged(index(row), index(row));
}

// ---------------------------------------------------------------------------
// console port (W3 / E10)
// ---------------------------------------------------------------------------
int SystemModel::consolePort(int row) const
{
    if (!isValidRow(row)) return -1;
    IniOverlay ini;
    if (!ini.load(m_records.at(row).iniPath())) return -1;
    bool ok = false;
    int const port = ini.intValue(QString::fromLatin1(IniOverlay::kSecConsole),
                                  QString::fromLatin1(IniOverlay::kKeyPort),
                                  RunDirSkeleton::kDefaultConsolePort, &ok);
    return ok ? port : -1;
}

bool SystemModel::setConsolePort(int row, int port, QString* error)
{
    if (!isValidRow(row)) return false;
    if (port < 1 || port > 65535) {
        if (error) *error = QStringLiteral("Port must be between 1 and 65535.");
        return false;
    }
    if (int const clash = rowClaimingPort(port, row); clash >= 0) {
        if (error) {
            *error = QStringLiteral("Port %1 is already used by \"%2\".  Two systems "
                                    "cannot share a console port.")
                         .arg(port).arg(m_records.at(clash).name);
        }
        return false;
    }

    IniOverlay ini;
    if (!ini.load(m_records.at(row).iniPath(), error)) return false;
    if (!ini.setValue(QString::fromLatin1(IniOverlay::kSecConsole),
                      QString::fromLatin1(IniOverlay::kKeyPort),
                      QString::number(port))) {
        if (error) *error = QStringLiteral("internal: console port is not a whitelisted key");
        return false;
    }
    if (!ini.save(error)) return false;

    emit dataChanged(index(row), index(row));
    return true;
}

int SystemModel::rowClaimingPort(int port, int ignoreRow) const
{
    if (port <= 0) return -1;
    for (int i = 0; i < count(); ++i) {
        if (i == ignoreRow) continue;
        if (consolePort(i) == port) return i;
    }
    return -1;
}

int SystemModel::nextFreeConsolePort() const
{
    int port = RunDirSkeleton::kDefaultConsolePort;
    while (port < 65535 && rowClaimingPort(port, -1) >= 0) ++port;
    return port;
}

// ---------------------------------------------------------------------------
// selection persistence
// ---------------------------------------------------------------------------
QString SystemModel::lastSelectedId() const
{
    QSettings s = openSettings();
    return s.value(QLatin1String(keys::kLastSelected)).toString();
}

void SystemModel::setLastSelectedId(QString const& id) const
{
    QSettings s = openSettings();
    s.setValue(QLatin1String(keys::kLastSelected), id);
}

}  // namespace launch
