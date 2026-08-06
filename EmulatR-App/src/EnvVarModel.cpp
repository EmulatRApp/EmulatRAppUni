// ============================================================================
// src/EnvVarModel.cpp -- registry-backed model of runtime environment variables
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
// ============================================================================

#include "EnvVarModel.h"

#include <QFile>
#include <QFont>
#include <QSettings>

#include "Version.h"

namespace launch {

namespace {

EnvVarDef::Kind kindFromString(QString const& s, bool* ok)
{
    QString const k = s.trimmed().toLower();
    if (ok) *ok = true;
    if (k == QLatin1String("flag"))    return EnvVarDef::Kind::Flag;
    if (k == QLatin1String("path"))    return EnvVarDef::Kind::Path;
    if (k == QLatin1String("integer")) return EnvVarDef::Kind::Integer;
    if (k == QLatin1String("enum"))    return EnvVarDef::Kind::Enum;
    if (ok) *ok = false;
    return EnvVarDef::Kind::Flag;
}

EnvVarDef::Tier tierFromString(QString const& s, bool* ok)
{
    QString const t = s.trimmed().toLower();
    if (ok) *ok = true;
    if (t == QLatin1String("safe"))   return EnvVarDef::Tier::Safe;
    if (t == QLatin1String("dev"))    return EnvVarDef::Tier::Dev;
    if (t == QLatin1String("denied")) return EnvVarDef::Tier::Denied;
    if (ok) *ok = false;
    return EnvVarDef::Tier::Denied;   // fail closed
}

QString kindLabel(EnvVarDef::Kind k)
{
    switch (k) {
        case EnvVarDef::Kind::Flag:    return QStringLiteral("on/off");
        case EnvVarDef::Kind::Path:    return QStringLiteral("path");
        case EnvVarDef::Kind::Integer: return QStringLiteral("integer");
        case EnvVarDef::Kind::Enum:    return QStringLiteral("choice");
    }
    return {};
}

}  // namespace

EnvVarModel::EnvVarModel(QObject* parent) : QAbstractTableModel(parent)
{
    QSettings s = openSettings();
    m_showDev = s.value(QLatin1String(keys::kShowDevVars), false).toBool();
}

// ---------------------------------------------------------------------------
// registry (V1)
// ---------------------------------------------------------------------------
bool EnvVarModel::loadRegistry(QString* error)
{
    beginResetModel();
    m_defs.clear();
    m_state.clear();
    m_visible.clear();

    QFile f(QStringLiteral(":/data/emulatr_env_registry.tsv"));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error) *error = QStringLiteral("internal: env registry resource is missing");
        endResetModel();
        return false;
    }

    QStringList problems;
    int lineNo = 0;
    while (!f.atEnd()) {
        ++lineNo;
        QString const line = QString::fromUtf8(f.readLine()).trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) continue;

        QStringList const cols = line.split(QLatin1Char('\t'));
        if (cols.size() < 5) {
            problems << QStringLiteral("line %1: expected at least 5 tab-separated "
                                       "columns, found %2").arg(lineNo).arg(cols.size());
            continue;
        }

        EnvVarDef d;
        d.name         = cols.value(0).trimmed();
        bool kindOk = false, tierOk = false;
        d.kind         = kindFromString(cols.value(1), &kindOk);
        d.defaultValue = cols.value(2).trimmed();
        d.description  = cols.value(3).trimmed();
        d.tier         = tierFromString(cols.value(4), &tierOk);
        d.anchor       = cols.value(5).trimmed();

        if (d.name.isEmpty()) {
            problems << QStringLiteral("line %1: empty variable name").arg(lineNo);
            continue;
        }
        if (!kindOk) {
            problems << QStringLiteral("line %1 (%2): unknown value kind '%3'")
                            .arg(lineNo).arg(d.name, cols.value(1).trimmed());
        }
        if (!tierOk) {
            // Fail closed and SAY SO.  An unrecognized tier silently becoming
            // "safe" is the one outcome the tiering exists to prevent.
            problems << QStringLiteral("line %1 (%2): unknown tier '%3'; treated as "
                                       "denied").arg(lineNo).arg(d.name, cols.value(4).trimmed());
        }

        m_defs.append(d);
        m_state.append(State{ false, d.defaultValue });
    }
    f.close();

    loadState();
    rebuildVisible();
    endResetModel();

    if (!problems.isEmpty() && error) {
        *error = QStringLiteral("Environment variable registry: %1")
                     .arg(problems.join(QStringLiteral("; ")));
        return false;
    }
    return true;
}

QStringList EnvVarModel::deniedNames() const
{
    QStringList out;
    for (EnvVarDef const& d : m_defs)
        if (d.tier == EnvVarDef::Tier::Denied) out << d.name;
    return out;
}

bool EnvVarModel::hasDeveloperVariables() const
{
    for (EnvVarDef const& d : m_defs)
        if (d.tier == EnvVarDef::Tier::Dev) return true;
    return false;
}

// ---------------------------------------------------------------------------
// visibility (V2)
// ---------------------------------------------------------------------------
void EnvVarModel::rebuildVisible()
{
    m_visible.clear();
    for (int i = 0; i < m_defs.size(); ++i) {
        EnvVarDef const& d = m_defs.at(i);
        if (d.tier == EnvVarDef::Tier::Denied) continue;              // never shown
        if (d.tier == EnvVarDef::Tier::Dev && !m_showDev) continue;
        m_visible.append(i);
    }
}

void EnvVarModel::setShowDeveloperVariables(bool show)
{
    if (m_showDev == show) return;
    beginResetModel();
    m_showDev = show;
    rebuildVisible();
    endResetModel();

    QSettings s = openSettings();
    s.setValue(QLatin1String(keys::kShowDevVars), show);
}

// ---------------------------------------------------------------------------
// per-system state (V4)
// ---------------------------------------------------------------------------
void EnvVarModel::setSystemId(QString const& systemId)
{
    if (m_systemId == systemId) return;
    beginResetModel();
    m_systemId = systemId;
    loadState();
    endResetModel();
    emit selectionChanged();
}

void EnvVarModel::loadState()
{
    for (int i = 0; i < m_defs.size(); ++i)
        m_state[i] = State{ false, m_defs.at(i).defaultValue };

    if (m_systemId.isEmpty()) return;

    QSettings s = openSettings();
    for (int i = 0; i < m_defs.size(); ++i) {
        QString const key = QStringLiteral("systems/%1/env/%2")
                                .arg(m_systemId, m_defs.at(i).name);
        if (!s.contains(key)) continue;
        QStringList const v = s.value(key).toStringList();
        if (v.isEmpty()) continue;
        m_state[i].checked = (v.value(0) == QLatin1String("1"));
        if (v.size() > 1) m_state[i].value = v.value(1);
    }
}

void EnvVarModel::saveState(QString const& name) const
{
    if (m_systemId.isEmpty()) return;
    int idx = -1;
    for (int i = 0; i < m_defs.size(); ++i)
        if (m_defs.at(i).name == name) { idx = i; break; }
    if (idx < 0) return;

    QSettings s = openSettings();
    QString const key = QStringLiteral("systems/%1/env/%2").arg(m_systemId, name);
    s.setValue(key, QStringList{ m_state.at(idx).checked ? QStringLiteral("1")
                                                         : QStringLiteral("0"),
                                 m_state.at(idx).value });
}

// ---------------------------------------------------------------------------
// QAbstractTableModel
// ---------------------------------------------------------------------------
int EnvVarModel::rowCount(QModelIndex const& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_visible.size());
}

int EnvVarModel::columnCount(QModelIndex const& parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

EnvVarDef const* EnvVarModel::defForRow(int visibleRow) const
{
    if (visibleRow < 0 || visibleRow >= m_visible.size()) return nullptr;
    return &m_defs.at(m_visible.at(visibleRow));
}

QVariant EnvVarModel::data(QModelIndex const& index, int role) const
{
    if (!index.isValid()) return {};
    int const vi = index.row();
    if (vi < 0 || vi >= m_visible.size()) return {};
    int const di = m_visible.at(vi);
    EnvVarDef const& d = m_defs.at(di);
    State const&     st = m_state.at(di);

    switch (role) {
        case Qt::CheckStateRole:
            if (index.column() == CheckColumn)
                return st.checked ? Qt::Checked : Qt::Unchecked;
            return {};

        case Qt::DisplayRole:
        case Qt::EditRole:
            switch (index.column()) {
                case NameColumn:  return d.name;
                case ValueColumn: return d.isFlag() ? QString() : st.value;
                case DescriptionColumn: return d.description;
                default: return {};
            }

        case Qt::ToolTipRole: {
            QStringList t;
            t << d.name;
            t << QStringLiteral("Kind: %1").arg(kindLabel(d.kind));
            if (!d.defaultValue.isEmpty())
                t << QStringLiteral("Default: %1").arg(d.defaultValue);
            if (d.tier == EnvVarDef::Tier::Dev)
                t << QStringLiteral("Developer variable.");
            if (!d.anchor.isEmpty())
                t << QStringLiteral("Core anchor: %1").arg(d.anchor);
            if (!d.description.isEmpty()) t << QString() << d.description;
            return t.join(QLatin1Char('\n'));
        }

        case Qt::FontRole:
            if (index.column() == NameColumn) {
                QFont f;
                f.setFamily(QStringLiteral("Consolas"));
                return f;
            }
            return {};

        default: break;
    }
    return {};
}

bool EnvVarModel::setData(QModelIndex const& index, QVariant const& value, int role)
{
    if (!index.isValid()) return false;
    int const vi = index.row();
    if (vi < 0 || vi >= m_visible.size()) return false;
    int const di = m_visible.at(vi);
    EnvVarDef const& d = m_defs.at(di);

    if (role == Qt::CheckStateRole && index.column() == CheckColumn) {
        m_state[di].checked = (value.toInt() == Qt::Checked);
        saveState(d.name);
        emit dataChanged(index, index.sibling(vi, ColumnCount - 1));
        emit selectionChanged();
        return true;
    }

    if (role == Qt::EditRole && index.column() == ValueColumn) {
        if (d.isFlag()) return false;              // flags have no value cell
        m_state[di].value = value.toString();
        saveState(d.name);
        emit dataChanged(index, index);
        emit selectionChanged();
        return true;
    }
    return false;
}

Qt::ItemFlags EnvVarModel::flags(QModelIndex const& index) const
{
    if (!index.isValid()) return Qt::NoItemFlags;
    Qt::ItemFlags f = Qt::ItemIsEnabled | Qt::ItemIsSelectable;

    if (index.column() == CheckColumn) {
        f |= Qt::ItemIsUserCheckable;
    } else if (index.column() == ValueColumn) {
        EnvVarDef const* d = defForRow(index.row());
        if (d && !d->isFlag()) f |= Qt::ItemIsEditable;
    }
    return f;
}

QVariant EnvVarModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) return {};
    switch (section) {
        case CheckColumn:       return QString();
        case NameColumn:        return QStringLiteral("Variable");
        case ValueColumn:       return QStringLiteral("Value");
        case DescriptionColumn: return QStringLiteral("What it does");
        default: return {};
    }
}

// ---------------------------------------------------------------------------
// launch composition (L2)
// ---------------------------------------------------------------------------
QProcessEnvironment EnvVarModel::composeEnvironment(
    QProcessEnvironment const& inherited) const
{
    QProcessEnvironment env = inherited;

    // 1. Strip every denied name, whatever its source.
    for (EnvVarDef const& d : m_defs)
        if (d.tier == EnvVarDef::Tier::Denied) env.remove(d.name);

    // 2. Insert checked rows.  Unchecked rows are NOT inserted -- and are also
    //    removed, so a stale value inherited from the launcher's own shell
    //    cannot turn on a diagnostic the tester left unticked.
    for (int i = 0; i < m_defs.size(); ++i) {
        EnvVarDef const& d  = m_defs.at(i);
        if (d.tier == EnvVarDef::Tier::Denied) continue;
        State const&     st = m_state.at(i);
        if (!st.checked) { env.remove(d.name); continue; }
        // A flag's mere presence is the signal; give it "1" so a consumer that
        // reads the value rather than the pointer still sees something true.
        env.insert(d.name, d.isFlag() && st.value.isEmpty() ? QStringLiteral("1")
                                                            : st.value);
    }
    return env;
}

QStringList EnvVarModel::effectiveSelectionForLog() const
{
    QStringList out;
    for (int i = 0; i < m_defs.size(); ++i) {
        EnvVarDef const& d = m_defs.at(i);
        if (d.tier == EnvVarDef::Tier::Denied) continue;
        State const& st = m_state.at(i);
        if (!st.checked) continue;
        out << QStringLiteral("%1=%2").arg(
            d.name, d.isFlag() && st.value.isEmpty() ? QStringLiteral("1") : st.value);
    }
    return out;
}

QStringList EnvVarModel::strippedFrom(QProcessEnvironment const& inherited) const
{
    QStringList out;
    for (EnvVarDef const& d : m_defs) {
        if (d.tier != EnvVarDef::Tier::Denied) continue;
        if (inherited.contains(d.name)) out << d.name;
    }
    return out;
}

}  // namespace launch
