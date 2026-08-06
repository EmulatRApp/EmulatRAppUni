// ============================================================================
// src/NewSystemDialog.h -- the "New System..." and "Add Existing..." prompts
// ============================================================================
// Project: EmulatR -- EmulatrLaunch (SPEC-LAUNCH-001 Rev D.2)
// Copyright (C) 2026 eNVy Systems, Inc.  All rights reserved.
// Licensed under eNVy Systems Non-Commercial License v1.1
//
// Project Architect: Timothy Peer
// AI Collaboration:  Claude (Anthropic)
//
// Version: 1.0.0-alpha   Date: 2026-07-29
// Spec:    Section 4 M4 (new system: name, platform, location) and M5 (add
//          existing: browse, validate, INFER the platform, ask for a name).
//          Section 10 D1 -- "Browse..." became "Add Existing...".
//
// NOT IN THE SPEC'S FILE LIST.  M4 and M5 both need a prompt and they differ
// only in whether the platform is chosen or inferred, so one dialog with two
// modes beats two near-identical ones.
// ============================================================================

#ifndef EMULATRLAUNCH_NEWSYSTEMDIALOG_H
#define EMULATRLAUNCH_NEWSYSTEMDIALOG_H

#include <QDialog>

#include "SystemRecord.h"

class QComboBox;
class QDialogButtonBox;
class QLabel;
class QLineEdit;
class QPushButton;

namespace launch {

class NewSystemDialog : public QDialog
{
    Q_OBJECT

public:
    enum class Mode { CreateNew, AddExisting };

    explicit NewSystemDialog(Mode mode, QWidget* parent = nullptr);

    QString  systemName() const;
    Platform platform() const;
    QString  runDir() const;

private slots:
    void onBrowse();
    void revalidate();
    void inferForRunDir();

private:

    Mode        m_mode;
    QLineEdit*  m_name        = nullptr;
    QComboBox*  m_platform    = nullptr;
    QLineEdit*  m_location    = nullptr;
    QPushButton* m_browse     = nullptr;
    QLabel*     m_inference   = nullptr;
    QLabel*     m_problem     = nullptr;
    QLabel*     m_pathPreview = nullptr;
    QDialogButtonBox* m_buttons = nullptr;

    // In AddExisting mode: true once inference has run and produced a
    // confident answer, so the combo is informational rather than a guess the
    // user must confirm blind.
    bool m_platformInferred = false;
};

}  // namespace launch

#endif  // EMULATRLAUNCH_NEWSYSTEMDIALOG_H
