// ============================================================================
// src/EnvVarPanel.h -- the checkable table view over EnvVarModel
// ============================================================================
// Project: EmulatR -- EmulatrLaunch (SPEC-LAUNCH-001 Rev D.2)
// Copyright (C) 2026 eNVy Systems, Inc.  All rights reserved.
// Licensed under eNVy Systems Non-Commercial License v1.1
//
// Project Architect: Timothy Peer
// AI Collaboration:  Claude (Anthropic)
//
// Version: 1.0.0-alpha   Date: 2026-07-29
// Spec:    Section 7.1 V2/V3 -- columns [checkbox] | name | value |
//          description; value editable per kind; flags hide the value cell;
//          NO free-form "add variable" row in v1 (deferred, Section 15) so the
//          denylist keeps its meaning.
// ============================================================================

#ifndef EMULATRLAUNCH_ENVVARPANEL_H
#define EMULATRLAUNCH_ENVVARPANEL_H

#include <QWidget>

class QCheckBox;
class QLabel;
class QPushButton;
class QTableView;

namespace launch {

class EnvVarModel;

class EnvVarPanel : public QWidget
{
    Q_OBJECT

public:
    explicit EnvVarPanel(EnvVarModel* model, QWidget* parent = nullptr);

    // Greys the whole panel when no system is selected, without hiding it --
    // a vanishing panel reads as a bug.
    void setSystemSelected(bool selected);

private slots:
    void onShowDevToggled(bool on);
    void onBrowseForPath();
    void onSelectionChanged();

private:
    void refreshCaution();

    EnvVarModel* m_model  = nullptr;
    QTableView*  m_view   = nullptr;
    QCheckBox*   m_showDev = nullptr;
    QLabel*      m_caution = nullptr;
    QLabel*      m_empty   = nullptr;
    QPushButton* m_browse  = nullptr;
};

}  // namespace launch

#endif  // EMULATRLAUNCH_ENVVARPANEL_H
