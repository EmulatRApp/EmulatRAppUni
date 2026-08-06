// ============================================================================
// src/StorageCreateDialog.h -- geometry/name prompt over DiskImageFactory
// ============================================================================
// Project: EmulatR -- EmulatrLaunch (SPEC-LAUNCH-001 Rev D.2)
// Copyright (C) 2026 eNVy Systems, Inc.  All rights reserved.
// Licensed under eNVy Systems Non-Commercial License v1.1
//
// Project Architect: Timothy Peer
// AI Collaboration:  Claude (Anthropic)
//
// Version: 1.0.0-alpha   Date: 2026-07-29
// Spec:    Section 9 S2 -- model combo from the verified table plus a Custom
//          entry with sanity bounds; name is a file stem; the resulting
//          {run-dir}/disks/<name>.<ext> and projected size are shown BEFORE
//          creation.  Placement is ALWAYS the selected system's disks/ --
//          there is no free path selection anywhere in this dialog.
// ============================================================================

#ifndef EMULATRLAUNCH_STORAGECREATEDIALOG_H
#define EMULATRLAUNCH_STORAGECREATEDIALOG_H

#include <QDialog>
#include <QList>

#include "DiskImageFactory.h"

class QComboBox;
class QDialogButtonBox;
class QLabel;
class QLineEdit;
class QSpinBox;
class QWidget;

namespace launch {

class StorageCreateDialog : public QDialog
{
    Q_OBJECT

public:
    StorageCreateDialog(QString const& disksDir, QString const& systemName,
                        QWidget* parent = nullptr);

    // Valid only after exec() returned Accepted.
    QString createdPath() const { return m_createdPath; }

private slots:
    void onModelChanged(int index);
    void refreshPreview();
    void onAccept();

private:
    DiskImageFactory::Geometry currentGeometry(QString* error) const;

    QString m_disksDir;
    QString m_createdPath;

    QList<DiskImageFactory::Geometry> m_table;

    QComboBox*        m_model      = nullptr;
    QWidget*          m_customBox  = nullptr;
    QSpinBox*         m_cyl        = nullptr;
    QSpinBox*         m_heads      = nullptr;
    QSpinBox*         m_sectors    = nullptr;
    QComboBox*        m_blockSize  = nullptr;
    QLineEdit*        m_name       = nullptr;
    QLabel*           m_geometry   = nullptr;
    QLabel*           m_preview    = nullptr;
    QLabel*           m_problem    = nullptr;
    QDialogButtonBox* m_buttons    = nullptr;
};

}  // namespace launch

#endif  // EMULATRLAUNCH_STORAGECREATEDIALOG_H
