// ============================================================================
// src/SystemModel.h -- QSettings-backed registry of named systems
// ============================================================================
// Project: EmulatR -- EmulatrLaunch (SPEC-LAUNCH-001 Rev D.2)
// Copyright (C) 2026 eNVy Systems, Inc.  All rights reserved.
// Licensed under eNVy Systems Non-Commercial License v1.1
//
// Project Architect: Timothy Peer
// AI Collaboration:  Claude (Anthropic)
//
// Version: 1.0.0-alpha   Date: 2026-07-29
// Spec:    Section 4 (M1-M6) -- THE reference structure of the application.
//          Section 10 D1/M2 -- the System List is the SOLE selector; there is
//          no run-directory combo anywhere in this application.
//
// M3 RECOVERABILITY is the load-bearing property here: QSettings is a registry
// of POINTERS, not the source of truth.  Deleting the hive must cost the user
// nothing but a few "Add Existing..." clicks, because the run directories
// remain self-describing.  Nothing launcher-critical may ever live only here.
// ============================================================================

#ifndef EMULATRLAUNCH_SYSTEMMODEL_H
#define EMULATRLAUNCH_SYSTEMMODEL_H

#include <QAbstractListModel>
#include <QList>
#include <QString>

#include "SystemRecord.h"

namespace launch {

class SystemModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Roles
    {
        IdRole = Qt::UserRole + 1,
        NameRole,
        PlatformRole,
        RunDirRole,
        RunningRole,
        HealthRole,
        HealthNoteRole,
        ConsolePortRole,
    };

    explicit SystemModel(QObject* parent = nullptr);

    // ---- QAbstractListModel ----------------------------------------------
    int      rowCount(QModelIndex const& parent = QModelIndex()) const override;
    QVariant data(QModelIndex const& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    // ---- registry ---------------------------------------------------------
    void load();                        // M1: read the hive
    void persist(int row) const;        // write one record
    void persistAll() const;

    int  count() const { return static_cast<int>(m_records.size()); }
    bool isValidRow(int row) const { return row >= 0 && row < count(); }
    SystemRecord const& at(int row) const { return m_records.at(row); }

    int rowForId(QString const& id) const;
    int rowForRunDir(QString const& anyPath) const;             // canonical compare
    int rowForName(QString const& name, int ignoreRow = -1) const;  // case-insensitive

    // ---- mutation (Section 4 M4-M6) ---------------------------------------

    // M4.  Creates the run-dir skeleton, registers the record, returns its id.
    // Returns an empty string and sets *error on refusal.  Refusals: duplicate
    // name, duplicate canonical run dir (E9), Program Files (R2), unwritable.
    QString createSystem(QString const& name, Platform platform,
                         QString const& runDir, QString* error);

    // M5.  Registers an EXISTING run directory.  Does not create or modify it
    // beyond the logs/ and traces/ the contract says are created if absent.
    QString registerExisting(QString const& name, Platform platform,
                             QString const& runDir, QString* error);

    // M6.  Deletes the registration and its per-system settings ONLY.  The run
    // directory and every byte in it are left alone -- callers must say so in
    // the confirmation dialog.
    void removeSystem(int row);

    bool renameSystem(int row, QString const& newName, QString* error);

    // E1.  Re-points a system at a moved run directory, keeping the GUID and
    // therefore every per-system setting (including the W6 env selections).
    bool relocateSystem(int row, QString const& newRunDir, QString* error);

    // ---- transient state --------------------------------------------------
    void setRunning(QString const& id, bool running);
    QString runningId() const { return m_runningId; }
    bool anyRunning() const { return !m_runningId.isEmpty(); }

    void setHealth(int row, Health health, QString const& note);

    // ---- console port (W3 / E10) ------------------------------------------
    // The port is PER-SYSTEM STATE held in the system's own Emulatr.ini, not
    // in QSettings -- so it travels with the run directory and stays true
    // whether the emulator is started by this launcher or from a shell.
    int consolePort(int row) const;
    bool setConsolePort(int row, int port, QString* error);

    // E10.  Returns the row of another registered system already claiming
    // `port`, or -1.  Only registered systems are visible to this check; a
    // port held by an unrelated process surfaces at Start as the emulator's
    // own bind error.
    int rowClaimingPort(int port, int ignoreRow) const;

    // An unclaimed port for a new system, starting from the core default.
    int nextFreeConsolePort() const;

    // ---- selection persistence -------------------------------------------
    QString lastSelectedId() const;
    void setLastSelectedId(QString const& id) const;

signals:
    void systemsChanged();

private:
    void sortByName();

    QList<SystemRecord> m_records;
    QString             m_runningId;
};

}  // namespace launch

#endif  // EMULATRLAUNCH_SYSTEMMODEL_H
