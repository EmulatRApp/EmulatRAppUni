// ============================================================================
// src/LauncherWindow.h -- main window: tabs, global actions, preflight
// ============================================================================
// Project: EmulatR -- EmulatrLaunch (SPEC-LAUNCH-001 Rev D.2)
// Copyright (C) 2026 eNVy Systems, Inc.  All rights reserved.
// Licensed under eNVy Systems Non-Commercial License v1.1
//
// Project Architect: Timothy Peer
// AI Collaboration:  Claude (Anthropic)
//
// Version: 1.0.0-alpha   Date: 2026-07-29
// Spec:    Section 10 (UI, per the normative wireframe), Section 8 (lifecycle)
//
// WIREFRAME NOTE.  Section 10 names docs/launcher_wireframe.qml as NORMATIVE
// for window geometry, the two-tab structure, Tab 0's vertical rhythm, and the
// global bottom bar.  That file is not present in the tree, so this layout is
// built from the Section 10 prose and the D1-D4 deviations alone.  It needs a
// reconciliation pass against the mock before the UI is signed off.
//
// M2 is the invariant to protect when editing this file: the System List is
// the SOLE selector.  Every action -- Start, Stop, Make Disk, PlatEd, logs,
// Open Console -- binds to the current selection, and no second run-directory
// chooser may ever appear anywhere in this UI.
// ============================================================================

#ifndef EMULATRLAUNCH_LAUNCHERWINDOW_H
#define EMULATRLAUNCH_LAUNCHERWINDOW_H

#include <QMainWindow>
#include <QStringList>

#include "EmulatorProcess.h"
#include "SystemRecord.h"

class QCheckBox;
class QComboBox;
class QFileSystemWatcher;
class QGroupBox;
class QLabel;
class QListView;
class QPushButton;
class QSpinBox;
class QTabWidget;

namespace launch {

class EnvVarModel;
class EnvVarPanel;
class PlatEdBridge;
class SystemModel;

class LauncherWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit LauncherWindow(QWidget* parent = nullptr);

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    // System list (M4/M5/M6, E1)
    void onNewSystem();
    void onAddExisting();
    void onRemoveSystem();
    void onRenameSystem();
    void onRelocateSystem();
    void onSelectionChanged();

    // Lifecycle (L2-L5)
    void onStart();
    void onStop();
    void onForceStop();
    void onProcessStateChanged(launch::EmulatorProcess::State state);
    void onProcessFinished(int exitCode, QProcess::ExitStatus status, bool forced,
                           qint64 durationMs);
    void onEscalationAvailable();

    // Details tab (W2/W3/W5)
    void onFirmwareChanged(int index);
    void onConsolePortChanged();
    void onOpenConsole();
    void onOpenInPlatEd();
    void onPlatEdFinished();

    // Global bar (Section 9, L6)
    void onMakeDisk();
    void onOpenLogsFolder();
    void onOpenRunDirectory();
    void onPackageLogs();
    void onShowLastLog();
    void onSettings();

    // Preflight (L1)
    void runPreflight();
    void onRunDirChanged(QString const& path);

private:
    void buildUi();
    QWidget* buildSystemsTab();
    QWidget* buildDetailsTab();
    QWidget* buildBottomBar();

    void refreshDetails();
    void refreshActionStates();
    void rewatchSelection();
    void selectRow(int row);
    void setStatus(QStringList const& problems, QStringList const& notes);

    int  selectedRow() const;
    bool hasSelection() const { return selectedRow() >= 0; }

    // Models and services
    SystemModel*     m_model    = nullptr;
    EnvVarModel*     m_envModel = nullptr;
    EmulatorProcess* m_proc     = nullptr;
    PlatEdBridge*    m_platEd   = nullptr;
    QFileSystemWatcher* m_watcher = nullptr;

    // Tab 0
    QListView*   m_list         = nullptr;
    QPushButton* m_newBtn       = nullptr;
    QPushButton* m_addBtn       = nullptr;
    QPushButton* m_removeBtn    = nullptr;
    QPushButton* m_startBtn     = nullptr;
    QPushButton* m_stopBtn      = nullptr;
    QPushButton* m_forceBtn     = nullptr;
    QPushButton* m_fixBtn       = nullptr;
    QPushButton* m_showLogBtn   = nullptr;
    QLabel*      m_status       = nullptr;
    QLabel*      m_runState     = nullptr;

    // Tab 1
    QLabel*      m_platformLbl  = nullptr;
    QLabel*      m_runDirLbl    = nullptr;
    QComboBox*   m_firmware     = nullptr;
    QLabel*      m_firmwareNote = nullptr;
    QSpinBox*    m_consolePort  = nullptr;
    QCheckBox*   m_exposeLan    = nullptr;
    QLabel*      m_consoleNote  = nullptr;
    QPushButton* m_openConsole  = nullptr;
    QGroupBox*   m_serviceGroup = nullptr;
    QPushButton* m_platEdBtn    = nullptr;
    QLabel*      m_platEdNote   = nullptr;
    EnvVarPanel* m_envPanel     = nullptr;

    // Bottom bar
    QPushButton* m_configureBtn = nullptr;
    QPushButton* m_makeDiskBtn  = nullptr;
    QPushButton* m_logsBtn      = nullptr;
    QPushButton* m_packageBtn   = nullptr;

    QTabWidget*  m_tabs = nullptr;

    // Which "Fix" action the current preflight failure offers, if any.
    enum class FixAction { None, OpenFirmwareFolder, OpenRunDir, ChooseFirmware,
                           OpenSettings, Relocate };
    FixAction m_fix = FixAction::None;

    QString m_lastLogPath;
    bool    m_preflightOk = false;
    bool    m_updatingWidgets = false;   // guards programmatic widget updates
};

}  // namespace launch

#endif  // EMULATRLAUNCH_LAUNCHERWINDOW_H
