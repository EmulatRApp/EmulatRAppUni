// ============================================================================
// src/EnvVarModel.h -- registry-backed model of runtime environment variables
// ============================================================================
// Project: EmulatR -- EmulatrLaunch (SPEC-LAUNCH-001 Rev D.2)
// Copyright (C) 2026 eNVy Systems, Inc.  All rights reserved.
// Licensed under eNVy Systems Non-Commercial License v1.1
//
// Project Architect: Timothy Peer
// AI Collaboration:  Claude (Anthropic)
//
// Version: 1.0.0-alpha   Date: 2026-07-29
// Spec:    Section 7 W6, Section 7.1 V1-V5
//
// THE TWO INVARIANTS THIS CLASS EXISTS TO HOLD
//
// 1. ABSENT IS NOT EMPTY.  An unchecked variable must be ABSENT from the child
//    environment, not set to "".  getenv() consumers in the core test the
//    POINTER (`if (getenv("X"))`) as often as the value, so setting an empty
//    string would switch a diagnostic ON.  composeEnvironment() therefore
//    never inserts an unchecked name.
//
// 2. DENIED MEANS STRIPPED.  A denied-tier variable is removed from the child
//    environment even when it is set in the LAUNCHER's own inherited
//    environment -- a developer who exported EMULATR_RSCCWARP in their shell
//    before starting the launcher must not silently poison every system they
//    launch from it.  Quarantining a variable stays a data change (add a row
//    with tier=denied) rather than a code change.
// ============================================================================

#ifndef EMULATRLAUNCH_ENVVARMODEL_H
#define EMULATRLAUNCH_ENVVARMODEL_H

#include <QAbstractTableModel>
#include <QList>
#include <QProcessEnvironment>
#include <QString>
#include <QStringList>

namespace launch {

// ---------------------------------------------------------------------------
// One registry row (Section 7.1 V1).  Produced by the G2b census, never
// invented in this application.
// ---------------------------------------------------------------------------
struct EnvVarDef
{
    enum class Kind { Flag, Path, Integer, Enum };
    enum class Tier { Safe, Dev, Denied };

    QString name;
    Kind    kind = Kind::Flag;
    QString defaultValue;
    QString description;
    Tier    tier = Tier::Dev;      // fail closed: an unclassified row is not "safe"
    QString anchor;                // core-tree file:line of the getenv

    bool isFlag() const { return kind == Kind::Flag; }
};

class EnvVarModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    enum Column { CheckColumn = 0, NameColumn, ValueColumn, DescriptionColumn, ColumnCount };

    explicit EnvVarModel(QObject* parent = nullptr);

    // ---- registry ---------------------------------------------------------

    // Parses resources/data/emulatr_env_registry.tsv.  Returns false with
    // *error on a malformed file; a registry that will not parse must be
    // visible, not silently empty.
    bool loadRegistry(QString* error = nullptr);

    int  registryRowCount() const { return static_cast<int>(m_defs.size()); }
    bool registryIsEmpty()  const { return m_defs.isEmpty(); }

    // Rows whose tier is neither safe nor dev.  Never displayed, never
    // settable, always stripped.
    QStringList deniedNames() const;

    // ---- per-system state (V4) --------------------------------------------

    // Binds the model to a system GUID and reloads its selections from
    // QSettings.  An empty id detaches (no system selected).
    void setSystemId(QString const& systemId);
    QString systemId() const { return m_systemId; }

    // ---- visibility (V2) --------------------------------------------------
    void setShowDeveloperVariables(bool show);
    bool showDeveloperVariables() const { return m_showDev; }
    bool hasDeveloperVariables() const;

    // ---- QAbstractTableModel ---------------------------------------------
    int      rowCount(QModelIndex const& parent = QModelIndex()) const override;
    int      columnCount(QModelIndex const& parent = QModelIndex()) const override;
    QVariant data(QModelIndex const& index, int role = Qt::DisplayRole) const override;
    bool     setData(QModelIndex const& index, QVariant const& value,
                     int role = Qt::EditRole) override;
    Qt::ItemFlags flags(QModelIndex const& index) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;

    // The definition behind a VISIBLE row, for the panel's editors.
    EnvVarDef const* defForRow(int visibleRow) const;

    // ---- launch composition (L2, V5) --------------------------------------

    // The child environment: the launcher's own environment, minus every
    // denied name, plus the checked rows.  See the header comment for why
    // this is the only correct order of operations.
    QProcessEnvironment composeEnvironment(QProcessEnvironment const& inherited) const;

    // "NAME=value" for every checked row, for the mirror-log head (V5).
    QStringList effectiveSelectionForLog() const;

    // Names that were present in `inherited` and got stripped.  Logged so a
    // developer is told their shell export was overridden rather than
    // wondering why it had no effect.
    QStringList strippedFrom(QProcessEnvironment const& inherited) const;

signals:
    void selectionChanged();

private:
    struct State { bool checked = false; QString value; };

    void rebuildVisible();
    void loadState();
    void saveState(QString const& name) const;

    QList<EnvVarDef> m_defs;
    QList<State>     m_state;      // parallel to m_defs
    QList<int>       m_visible;    // indices into m_defs
    QString          m_systemId;
    bool             m_showDev = false;
};

}  // namespace launch

#endif  // EMULATRLAUNCH_ENVVARMODEL_H
