// ============================================================================
// src/EnvVarPanel.cpp -- the checkable table view over EnvVarModel
// ============================================================================
// Project: EmulatR -- EmulatrLaunch (SPEC-LAUNCH-001 Rev D.2)
// Copyright (C) 2026 eNVy Systems, Inc.  All rights reserved.
// Licensed under eNVy Systems Non-Commercial License v1.1
//
// Project Architect: Timothy Peer
// AI Collaboration:  Claude (Anthropic)
//
// Version: 1.0.0-alpha   Date: 2026-07-29
// Spec:    Section 7.1 V2/V3
// ============================================================================

#include "EnvVarPanel.h"

#include <QCheckBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLabel>
#include <QPushButton>
#include <QStyledItemDelegate>
#include <QSpinBox>
#include <QTableView>
#include <QVBoxLayout>

#include "EnvVarModel.h"

namespace launch {

namespace {

// Per-kind editors (V3).  Integer rows get a spin box so a tester cannot type
// letters into a variable the core will atoi(); everything else gets the
// default line edit.  Path rows are served by the Browse button rather than a
// delegate-embedded button, which behaves badly inside an item view.
class KindDelegate : public QStyledItemDelegate
{
public:
    explicit KindDelegate(EnvVarModel* model, QObject* parent = nullptr)
        : QStyledItemDelegate(parent), m_model(model) {}

    QWidget* createEditor(QWidget* parent, QStyleOptionViewItem const& option,
                          QModelIndex const& index) const override
    {
        EnvVarDef const* d = m_model ? m_model->defForRow(index.row()) : nullptr;
        if (d && d->kind == EnvVarDef::Kind::Integer) {
            auto* spin = new QSpinBox(parent);
            spin->setRange(0, 2147483647);
            return spin;
        }
        return QStyledItemDelegate::createEditor(parent, option, index);
    }

private:
    EnvVarModel* m_model;
};

}  // namespace

EnvVarPanel::EnvVarPanel(EnvVarModel* model, QWidget* parent)
    : QWidget(parent), m_model(model)
{
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);

    // ---- header row -------------------------------------------------------
    auto* header = new QHBoxLayout;
    m_showDev = new QCheckBox(tr("Show developer variables"), this);
    m_showDev->setChecked(m_model && m_model->showDeveloperVariables());
    m_showDev->setToolTip(
        tr("Developer variables change how the emulator executes, not just what "
           "it logs. Leave this off unless a developer asked you to turn it on."));
    header->addWidget(m_showDev);
    header->addStretch(1);

    m_browse = new QPushButton(tr("Browse..."), this);
    m_browse->setEnabled(false);
    m_browse->setToolTip(tr("Pick a folder or file for the selected path variable."));
    header->addWidget(m_browse);
    outer->addLayout(header);

    // ---- caution ----------------------------------------------------------
    m_caution = new QLabel(this);
    m_caution->setWordWrap(true);
    m_caution->setVisible(false);
    outer->addWidget(m_caution);

    // ---- table ------------------------------------------------------------
    m_view = new QTableView(this);
    m_view->setModel(m_model);
    m_view->setItemDelegateForColumn(EnvVarModel::ValueColumn, new KindDelegate(m_model, this));
    m_view->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_view->setSelectionMode(QAbstractItemView::SingleSelection);
    m_view->setAlternatingRowColors(true);
    m_view->verticalHeader()->setVisible(false);
    m_view->horizontalHeader()->setStretchLastSection(true);
    m_view->setEditTriggers(QAbstractItemView::DoubleClicked
                            | QAbstractItemView::SelectedClicked
                            | QAbstractItemView::EditKeyPressed);
    m_view->setColumnWidth(EnvVarModel::CheckColumn, 28);
    m_view->setColumnWidth(EnvVarModel::NameColumn, 240);
    m_view->setColumnWidth(EnvVarModel::ValueColumn, 160);
    m_view->horizontalHeader()->setSectionResizeMode(EnvVarModel::CheckColumn,
                                                     QHeaderView::Fixed);
    outer->addWidget(m_view, 1);

    // ---- empty-registry note ---------------------------------------------
    // The registry ships empty until gate G2b closes.  Saying so plainly beats
    // an empty table that looks broken.
    m_empty = new QLabel(this);
    m_empty->setWordWrap(true);
    m_empty->setText(
        tr("<b>No environment variables are available yet.</b><br>"
           "This panel renders only the curated registry produced by the "
           "environment-variable census (spec gate G2b). Until that census is "
           "signed off, no variable is offered here -- which is deliberate: "
           "a diagnostic knob presented without evidence of what it does is "
           "worse than no knob at all."));
    m_empty->setVisible(false);
    outer->addWidget(m_empty);

    connect(m_showDev, &QCheckBox::toggled, this, &EnvVarPanel::onShowDevToggled);
    connect(m_browse, &QPushButton::clicked, this, &EnvVarPanel::onBrowseForPath);
    if (m_view->selectionModel()) {
        connect(m_view->selectionModel(), &QItemSelectionModel::currentRowChanged,
                this, &EnvVarPanel::onSelectionChanged);
    }

    if (m_model) {
        m_showDev->setVisible(m_model->hasDeveloperVariables());
        bool const empty = m_model->registryIsEmpty();
        m_view->setVisible(!empty);
        m_empty->setVisible(empty);
    }
    refreshCaution();
}

void EnvVarPanel::setSystemSelected(bool selected)
{
    m_view->setEnabled(selected);
    m_showDev->setEnabled(selected);
    if (!selected) m_browse->setEnabled(false);
}

void EnvVarPanel::onShowDevToggled(bool on)
{
    if (m_model) m_model->setShowDeveloperVariables(on);
    refreshCaution();
}

void EnvVarPanel::refreshCaution()
{
    bool const on = m_showDev->isChecked() && m_showDev->isVisible();
    m_caution->setVisible(on);
    if (on) {
        // V2: "one-line caution when on".
        m_caution->setText(
            tr("Developer variables are shown. These change emulator execution, "
               "not just logging -- a run with one enabled is not a clean "
               "reproduction."));
    }
}

void EnvVarPanel::onSelectionChanged()
{
    EnvVarDef const* d = nullptr;
    if (m_model && m_view->currentIndex().isValid())
        d = m_model->defForRow(m_view->currentIndex().row());
    m_browse->setEnabled(d && d->kind == EnvVarDef::Kind::Path);
}

void EnvVarPanel::onBrowseForPath()
{
    if (!m_model) return;
    QModelIndex const cur = m_view->currentIndex();
    if (!cur.isValid()) return;
    EnvVarDef const* d = m_model->defForRow(cur.row());
    if (!d || d->kind != EnvVarDef::Kind::Path) return;

    QModelIndex const valueIndex = m_model->index(cur.row(), EnvVarModel::ValueColumn);
    QString const current = m_model->data(valueIndex, Qt::EditRole).toString();

    QString const picked = QFileDialog::getExistingDirectory(
        this, tr("Choose a folder for %1").arg(d->name), current);
    if (picked.isEmpty()) return;
    m_model->setData(valueIndex, picked, Qt::EditRole);
}

}  // namespace launch
