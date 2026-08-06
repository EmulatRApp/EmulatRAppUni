// ============================================================================
// src/LauncherWindow.cpp -- main window: tabs, global actions, preflight
// ============================================================================
// Project: EmulatR -- EmulatrLaunch (SPEC-LAUNCH-001 Rev D.2)
// Copyright (C) 2026 eNVy Systems, Inc.  All rights reserved.
// Licensed under eNVy Systems Non-Commercial License v1.1
//
// Project Architect: Timothy Peer
// AI Collaboration:  Claude (Anthropic)
//
// Version: 1.0.0-alpha   Date: 2026-07-29
// Spec:    Section 8, Section 9, Section 10, Section 11
// ============================================================================

#include "LauncherWindow.h"

#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QFontMetrics>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QProcess>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QStandardPaths>
#include <QStatusBar>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QTabWidget>
#include <QUrl>
#include <QVBoxLayout>

#include "EnvVarModel.h"
#include "EnvVarPanel.h"
#include "ExeDiscovery.h"
#include "FirmwareCheck.h"
#include "IniOverlay.h"
#include "NewSystemDialog.h"
#include "PlatEdBridge.h"
#include "RunDirSkeleton.h"
#include "StorageCreateDialog.h"
#include "SystemModel.h"
#include "TerminalBridge.h"
#include "Version.h"

namespace launch {

namespace {

constexpr char const* kConsoleHostLoopback = "127.0.0.1";

// ---------------------------------------------------------------------------
// The system-list row.  L4 asks for a "visible RUNNING badge" on the running
// row; E1 wants a broken system to read as broken without a modal.  Both are
// drawn here so the list itself carries the state.
// ---------------------------------------------------------------------------
class SystemListDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QSize sizeHint(QStyleOptionViewItem const& option,
                   QModelIndex const& index) const override
    {
        QSize s = QStyledItemDelegate::sizeHint(option, index);
        s.setHeight(qMax(s.height(), option.fontMetrics.height() * 2 + 14));
        return s;
    }

    void paint(QPainter* painter, QStyleOptionViewItem const& option,
               QModelIndex const& index) const override
    {
        QStyleOptionViewItem opt(option);
        initStyleOption(&opt, index);
        opt.text.clear();
        QStyle* const style = opt.widget ? opt.widget->style() : QApplication::style();
        style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, opt.widget);

        QString const name     = index.data(SystemModel::NameRole).toString();
        QString const platform = index.data(SystemModel::PlatformRole).toString();
        QString const runDir   = index.data(SystemModel::RunDirRole).toString();
        bool const    running  = index.data(SystemModel::RunningRole).toBool();
        auto const    health   = static_cast<Health>(index.data(SystemModel::HealthRole).toInt());
        int const     port     = index.data(SystemModel::ConsolePortRole).toInt();

        QRect r = option.rect.adjusted(8, 5, -8, -5);
        painter->save();

        bool const selected = option.state & QStyle::State_Selected;
        QColor const primary = selected ? option.palette.highlightedText().color()
                                        : option.palette.text().color();
        QColor secondary = primary;
        secondary.setAlpha(160);

        // ---- badge (drawn first so the title can be elided around it) -----
        QString badge;
        QColor  badgeColor;
        if (running) {
            badge = QStringLiteral("RUNNING");
            badgeColor = QColor(0x1b, 0x7f, 0x3b);
        } else if (health == Health::Broken) {
            badge = QStringLiteral("NEEDS ATTENTION");
            badgeColor = QColor(0xa6, 0x2b, 0x2b);
        }

        int badgeWidth = 0;
        if (!badge.isEmpty()) {
            QFont bf = option.font;
            bf.setPointSizeF(bf.pointSizeF() * 0.82);
            bf.setBold(true);
            QFontMetrics const bfm(bf);
            int const tw = bfm.horizontalAdvance(badge);
            badgeWidth = tw + 14;
            QRect const badgeRect(r.right() - badgeWidth, r.top() + 2,
                                  badgeWidth, bfm.height() + 4);
            painter->setPen(Qt::NoPen);
            painter->setBrush(badgeColor);
            painter->drawRoundedRect(badgeRect, 3, 3);
            painter->setFont(bf);
            painter->setPen(Qt::white);
            painter->drawText(badgeRect, Qt::AlignCenter, badge);
            badgeWidth += 10;
        }

        // ---- title --------------------------------------------------------
        QFont title = option.font;
        title.setBold(true);
        painter->setFont(title);
        painter->setPen(primary);
        QFontMetrics const tfm(title);
        QRect const titleRect(r.left(), r.top(), r.width() - badgeWidth, tfm.height());
        painter->drawText(titleRect, Qt::AlignLeft | Qt::AlignVCenter,
                          tfm.elidedText(name, Qt::ElideRight, titleRect.width()));

        // ---- subtitle -----------------------------------------------------
        QFont sub = option.font;
        sub.setPointSizeF(sub.pointSizeF() * 0.88);
        painter->setFont(sub);
        painter->setPen(secondary);
        QFontMetrics const sfm(sub);
        QString detail = platform;
        if (port > 0) detail += QStringLiteral("   |   console %1").arg(port);
        detail += QStringLiteral("   |   %1").arg(runDir);
        QRect const subRect(r.left(), r.top() + tfm.height() + 2, r.width(), sfm.height());
        painter->drawText(subRect, Qt::AlignLeft | Qt::AlignVCenter,
                          sfm.elidedText(detail, Qt::ElideMiddle, subRect.width()));

        painter->restore();
    }
};

// ---------------------------------------------------------------------------
// Settings dialog.  Section 6: the resolved paths are SHOWN and OVERRIDABLE.
// Small enough to live here rather than in its own translation unit.
// ---------------------------------------------------------------------------
class SettingsDialog : public QDialog
{
public:
    explicit SettingsDialog(int shutdownTimeoutMs, QWidget* parent = nullptr)
        : QDialog(parent)
    {
        setWindowTitle(QObject::tr("Settings"));
        setModal(true);

        auto* outer = new QVBoxLayout(this);
        auto* form  = new QFormLayout;

        auto addRow = [&](QString const& label, char const* key,
                          ExeDiscovery::Result const& found, QLineEdit*& edit) {
            edit = new QLineEdit(this);
            QSettings s = openSettings();
            edit->setText(s.value(QLatin1String(key)).toString());
            edit->setPlaceholderText(found.found()
                                         ? found.path
                                         : QObject::tr("not found -- set a path here"));
            auto* browse = new QPushButton(QObject::tr("Browse..."), this);
            connect(browse, &QPushButton::clicked, this, [this, edit] {
                QString const p = QFileDialog::getOpenFileName(
                    this, QObject::tr("Choose executable"), edit->text(),
                    QObject::tr("Programs (*.exe)"));
                if (!p.isEmpty()) edit->setText(QDir::toNativeSeparators(p));
            });
            auto* row = new QHBoxLayout;
            row->addWidget(edit, 1);
            row->addWidget(browse);
            form->addRow(label, row);

            auto* note = new QLabel(found.detail, this);
            note->setWordWrap(true);
            form->addRow(QString(), note);
        };

        addRow(QObject::tr("Emulatr.exe:"), keys::kEmulatrExeOverride,
               ExeDiscovery::findEmulatr(), m_emulatr);
        addRow(QObject::tr("PlatEd:"), keys::kPlatEdExeOverride,
               ExeDiscovery::findPlatEd(), m_platEd);
        addRow(QObject::tr("Terminal client:"), keys::kTerminalExeOverride,
               ExeDiscovery::findTerminal(), m_terminal);

        m_timeout = new QSpinBox(this);
        m_timeout->setRange(1, 300);
        m_timeout->setSuffix(QObject::tr(" seconds"));
        m_timeout->setValue(qMax(1, shutdownTimeoutMs / 1000));
        form->addRow(QObject::tr("Wait after Stop before offering Force Stop:"), m_timeout);

        outer->addLayout(form);

        auto* note = new QLabel(
            QObject::tr("Leaving a path blank uses automatic discovery: the "
                        "installed location first, then the development build "
                        "tree."), this);
        note->setWordWrap(true);
        outer->addWidget(note);

        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                             this);
        connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        outer->addWidget(buttons);

        resize(680, sizeHint().height());
    }

    void apply() const
    {
        ExeDiscovery::setOverride(keys::kEmulatrExeOverride, m_emulatr->text());
        ExeDiscovery::setOverride(keys::kPlatEdExeOverride, m_platEd->text());
        ExeDiscovery::setOverride(keys::kTerminalExeOverride, m_terminal->text());
        QSettings s = openSettings();
        s.setValue(QLatin1String(keys::kShutdownTimeoutMs), m_timeout->value() * 1000);
    }

private:
    QLineEdit* m_emulatr  = nullptr;
    QLineEdit* m_platEd   = nullptr;
    QLineEdit* m_terminal = nullptr;
    QSpinBox*  m_timeout  = nullptr;
};

void openInExplorer(QString const& path)
{
    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

}  // namespace

// ===========================================================================
// construction
// ===========================================================================
LauncherWindow::LauncherWindow(QWidget* parent) : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("%1  %2")
                       .arg(QLatin1String(kAppName), QLatin1String(kAppVersion)));

    m_model    = new SystemModel(this);
    m_envModel = new EnvVarModel(this);
    m_proc     = new EmulatorProcess(this);
    m_platEd   = new PlatEdBridge(this);
    m_watcher  = new QFileSystemWatcher(this);

    QString envError;
    m_envModel->loadRegistry(&envError);   // empty registry is expected until G2b

    buildUi();

    connect(m_proc, &EmulatorProcess::stateChanged,
            this, &LauncherWindow::onProcessStateChanged);
    connect(m_proc, &EmulatorProcess::finished,
            this, &LauncherWindow::onProcessFinished);
    connect(m_proc, &EmulatorProcess::escalationAvailable,
            this, &LauncherWindow::onEscalationAvailable);
    connect(m_platEd, &PlatEdBridge::finished,
            this, &LauncherWindow::onPlatEdFinished);
    connect(m_watcher, &QFileSystemWatcher::directoryChanged,
            this, &LauncherWindow::onRunDirChanged);

    m_model->load();

    QSettings s = openSettings();
    QByteArray const geom = s.value(QLatin1String(keys::kWindowGeometry)).toByteArray();
    if (!geom.isEmpty()) restoreGeometry(geom);
    else                 resize(920, 660);

    int row = m_model->rowForId(m_model->lastSelectedId());
    if (row < 0 && m_model->count() > 0) row = 0;
    selectRow(row);

    if (!envError.isEmpty()) {
        // A malformed registry must be visible, not silently empty.
        statusBar()->showMessage(envError, 15000);
    }
}

// ===========================================================================
// UI construction
// ===========================================================================
void LauncherWindow::buildUi()
{
    auto* central = new QWidget(this);
    auto* outer   = new QVBoxLayout(central);

    m_tabs = new QTabWidget(central);
    m_tabs->addTab(buildSystemsTab(), tr("Systems"));
    m_tabs->addTab(buildDetailsTab(), tr("Named System Details"));
    outer->addWidget(m_tabs, 1);
    outer->addWidget(buildBottomBar());

    setCentralWidget(central);
    statusBar();
}

QWidget* LauncherWindow::buildSystemsTab()
{
    auto* page   = new QWidget(this);
    auto* layout = new QVBoxLayout(page);

    // ---- the sole selector (M2) ------------------------------------------
    m_list = new QListView(page);
    m_list->setModel(m_model);
    m_list->setItemDelegate(new SystemListDelegate(m_list));
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_list->setAlternatingRowColors(true);
    m_list->setContextMenuPolicy(Qt::CustomContextMenu);
    layout->addWidget(m_list, 1);

    connect(m_list->selectionModel(), &QItemSelectionModel::currentRowChanged,
            this, &LauncherWindow::onSelectionChanged);
    connect(m_list, &QListView::customContextMenuRequested, this, [this](QPoint const& p) {
        if (!hasSelection()) return;
        QMenu menu(this);
        menu.addAction(tr("Rename..."), this, &LauncherWindow::onRenameSystem);
        menu.addAction(tr("Relocate run dir..."), this, &LauncherWindow::onRelocateSystem);
        menu.addSeparator();
        menu.addAction(tr("Open run directory"), this, &LauncherWindow::onOpenRunDirectory);
        menu.addAction(tr("Open logs folder"), this, &LauncherWindow::onOpenLogsFolder);
        menu.addSeparator();
        menu.addAction(tr("Remove from list..."), this, &LauncherWindow::onRemoveSystem);
        menu.exec(m_list->viewport()->mapToGlobal(p));
    });

    // ---- registry actions (M4/M5/M6) -------------------------------------
    auto* registryRow = new QHBoxLayout;
    m_newBtn    = new QPushButton(tr("New System..."), page);
    m_addBtn    = new QPushButton(tr("Add Existing..."), page);
    m_removeBtn = new QPushButton(tr("Remove"), page);
    registryRow->addWidget(m_newBtn);
    registryRow->addWidget(m_addBtn);
    registryRow->addWidget(m_removeBtn);
    registryRow->addStretch(1);
    layout->addLayout(registryRow);

    connect(m_newBtn, &QPushButton::clicked, this, &LauncherWindow::onNewSystem);
    connect(m_addBtn, &QPushButton::clicked, this, &LauncherWindow::onAddExisting);
    connect(m_removeBtn, &QPushButton::clicked, this, &LauncherWindow::onRemoveSystem);

    // ---- run state and preflight status ----------------------------------
    m_runState = new QLabel(page);
    m_runState->setWordWrap(true);
    layout->addWidget(m_runState);

    m_status = new QLabel(page);
    m_status->setWordWrap(true);
    m_status->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_status->setMinimumHeight(52);
    m_status->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    layout->addWidget(m_status);

    // ---- start / stop (L4: separate buttons) ------------------------------
    auto* runRow = new QHBoxLayout;
    m_startBtn = new QPushButton(tr("Start"), page);
    m_stopBtn  = new QPushButton(tr("Stop"), page);
    m_forceBtn = new QPushButton(tr("Force Stop"), page);
    m_fixBtn   = new QPushButton(tr("Fix"), page);
    m_showLogBtn = new QPushButton(tr("Show log"), page);

    m_startBtn->setDefault(true);
    m_forceBtn->setVisible(false);       // appears only after the timeout (L3)
    m_showLogBtn->setVisible(false);

    runRow->addWidget(m_fixBtn);
    runRow->addWidget(m_showLogBtn);
    runRow->addStretch(1);
    runRow->addWidget(m_startBtn);
    runRow->addWidget(m_stopBtn);
    runRow->addWidget(m_forceBtn);
    layout->addLayout(runRow);

    connect(m_startBtn, &QPushButton::clicked, this, &LauncherWindow::onStart);
    connect(m_stopBtn, &QPushButton::clicked, this, &LauncherWindow::onStop);
    connect(m_forceBtn, &QPushButton::clicked, this, &LauncherWindow::onForceStop);
    connect(m_showLogBtn, &QPushButton::clicked, this, &LauncherWindow::onShowLastLog);
    connect(m_fixBtn, &QPushButton::clicked, this, [this] {
        int const row = selectedRow();
        if (row < 0) return;
        switch (m_fix) {
            case FixAction::OpenFirmwareFolder:
                openInExplorer(m_model->at(row).firmwareDir());
                break;
            case FixAction::OpenRunDir:
                openInExplorer(m_model->at(row).runDir);
                break;
            case FixAction::ChooseFirmware:
                m_tabs->setCurrentIndex(1);
                if (m_firmware) m_firmware->setFocus();
                break;
            case FixAction::OpenSettings:
                onSettings();
                break;
            case FixAction::Relocate:
                onRelocateSystem();
                break;
            case FixAction::None:
                break;
        }
    });

    return page;
}

QWidget* LauncherWindow::buildDetailsTab()
{
    auto* page   = new QWidget(this);
    auto* layout = new QVBoxLayout(page);

    // ---- identity (W1: platform read-only; run dir read-only, D1) --------
    auto* identity = new QGroupBox(tr("System"), page);
    auto* idForm   = new QFormLayout(identity);
    m_platformLbl = new QLabel(identity);
    m_runDirLbl   = new QLabel(identity);
    m_runDirLbl->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_runDirLbl->setWordWrap(true);
    idForm->addRow(tr("Platform:"), m_platformLbl);
    idForm->addRow(tr("Run directory:"), m_runDirLbl);
    layout->addWidget(identity);

    // ---- firmware (W2) ----------------------------------------------------
    auto* fw     = new QGroupBox(tr("Firmware"), page);
    auto* fwForm = new QFormLayout(fw);
    m_firmware = new QComboBox(fw);
    fwForm->addRow(tr("Image:"), m_firmware);
    m_firmwareNote = new QLabel(fw);
    m_firmwareNote->setWordWrap(true);
    fwForm->addRow(QString(), m_firmwareNote);
    auto* fwOpen = new QPushButton(tr("Open firmware folder"), fw);
    fwForm->addRow(QString(), fwOpen);
    layout->addWidget(fw);

    connect(m_firmware, &QComboBox::currentIndexChanged,
            this, &LauncherWindow::onFirmwareChanged);
    connect(fwOpen, &QPushButton::clicked, this, [this] {
        int const row = selectedRow();
        if (row >= 0) openInExplorer(m_model->at(row).firmwareDir());
    });

    // ---- console + reserved service position (D4) ------------------------
    auto* consoleRow = new QHBoxLayout;

    auto* console     = new QGroupBox(tr("Console"), page);
    auto* consoleForm = new QFormLayout(console);
    m_consolePort = new QSpinBox(console);
    m_consolePort->setRange(1, 65535);
    m_consolePort->setKeyboardTracking(false);
    consoleForm->addRow(tr("Port:"), m_consolePort);

    m_exposeLan = new QCheckBox(tr("Allow connections from other machines"), console);
    m_exposeLan->setEnabled(false);
    m_exposeLan->setToolTip(
        tr("The bind address is fixed at 127.0.0.1 in this build. Emulatr.ini "
           "has no bind-address key yet, so the launcher will not write a "
           "setting the emulator would ignore."));
    consoleForm->addRow(QString(), m_exposeLan);

    m_openConsole = new QPushButton(tr("Open Console"), console);
    consoleForm->addRow(QString(), m_openConsole);

    m_consoleNote = new QLabel(console);
    m_consoleNote->setWordWrap(true);
    consoleForm->addRow(QString(), m_consoleNote);
    consoleRow->addWidget(console, 1);

    // D4: the Service group POSITION is reserved so the deferred
    // run-as-a-service controls can appear beside Console without relayout.
    // Reserved means exactly that -- no service UI ships in v1.
    m_serviceGroup = new QGroupBox(tr("Service"), page);
    auto* svcLayout = new QVBoxLayout(m_serviceGroup);
    auto* svcNote = new QLabel(
        tr("Running a system as a Windows service -- headless, surviving "
           "logoff, starting at boot -- is planned but not in this release. "
           "Open Console already targets the port rather than the process, so "
           "it will work unchanged when this arrives."), m_serviceGroup);
    svcNote->setWordWrap(true);
    svcLayout->addWidget(svcNote);
    svcLayout->addStretch(1);
    m_serviceGroup->setEnabled(false);
    consoleRow->addWidget(m_serviceGroup, 1);

    layout->addLayout(consoleRow);

    connect(m_consolePort, &QSpinBox::editingFinished,
            this, &LauncherWindow::onConsolePortChanged);
    connect(m_openConsole, &QPushButton::clicked, this, &LauncherWindow::onOpenConsole);

    // ---- devices: DELEGATED to PlatEd (W5, D2) ---------------------------
    auto* devices = new QGroupBox(tr("Devices"), page);
    auto* devLayout = new QVBoxLayout(devices);
    auto* devNote = new QLabel(
        tr("CPU, memory, SCSI, and network device configuration lives in "
           "PlatEd, which owns the platform manifest. This launcher never "
           "writes one."), devices);
    devNote->setWordWrap(true);
    devLayout->addWidget(devNote);

    m_platEdBtn = new QPushButton(tr("Open in PlatEd"), devices);
    devLayout->addWidget(m_platEdBtn);
    m_platEdNote = new QLabel(devices);
    m_platEdNote->setWordWrap(true);
    devLayout->addWidget(m_platEdNote);
    layout->addWidget(devices);

    connect(m_platEdBtn, &QPushButton::clicked, this, &LauncherWindow::onOpenInPlatEd);

    // ---- environment variables (W6) --------------------------------------
    auto* envBox = new QGroupBox(tr("Runtime environment variables"), page);
    auto* envLayout = new QVBoxLayout(envBox);
    m_envPanel = new EnvVarPanel(m_envModel, envBox);
    envLayout->addWidget(m_envPanel);
    layout->addWidget(envBox, 1);

    return page;
}

QWidget* LauncherWindow::buildBottomBar()
{
    auto* bar = new QWidget(this);
    auto* row = new QHBoxLayout(bar);
    row->setContentsMargins(0, 0, 0, 0);

    // D3: the bar's "Configure" IS the Open in PlatEd action -- one
    // affordance, present here per the mock and duplicated in Tab 1.
    m_configureBtn = new QPushButton(tr("Configure"), bar);
    m_makeDiskBtn  = new QPushButton(tr("Make Disk"), bar);
    m_logsBtn      = new QPushButton(tr("Open Logs"), bar);
    m_packageBtn   = new QPushButton(tr("Package Logs for Support"), bar);
    auto* settings = new QPushButton(tr("Settings..."), bar);

    row->addWidget(m_configureBtn);
    row->addWidget(m_makeDiskBtn);
    row->addWidget(m_logsBtn);
    row->addWidget(m_packageBtn);
    row->addStretch(1);
    row->addWidget(settings);

    connect(m_configureBtn, &QPushButton::clicked, this, &LauncherWindow::onOpenInPlatEd);
    connect(m_makeDiskBtn, &QPushButton::clicked, this, &LauncherWindow::onMakeDisk);
    connect(m_logsBtn, &QPushButton::clicked, this, &LauncherWindow::onOpenLogsFolder);
    connect(m_packageBtn, &QPushButton::clicked, this, &LauncherWindow::onPackageLogs);
    connect(settings, &QPushButton::clicked, this, &LauncherWindow::onSettings);

    return bar;
}

// ===========================================================================
// selection
// ===========================================================================
int LauncherWindow::selectedRow() const
{
    if (!m_list || !m_list->selectionModel()) return -1;
    QModelIndex const idx = m_list->selectionModel()->currentIndex();
    return idx.isValid() ? idx.row() : -1;
}

void LauncherWindow::selectRow(int row)
{
    if (!m_model->isValidRow(row)) {
        m_list->clearSelection();
        onSelectionChanged();
        return;
    }
    m_list->setCurrentIndex(m_model->index(row));
}

void LauncherWindow::onSelectionChanged()
{
    int const row = selectedRow();
    if (row >= 0) m_model->setLastSelectedId(m_model->at(row).id);

    m_envModel->setSystemId(row >= 0 ? m_model->at(row).id : QString());
    m_envPanel->setSystemSelected(row >= 0);

    rewatchSelection();
    refreshDetails();
    runPreflight();
}

void LauncherWindow::rewatchSelection()
{
    // L1: preflight is re-evaluated via a filesystem watcher over the selected
    // system's run dir.
    if (!m_watcher->directories().isEmpty())
        m_watcher->removePaths(m_watcher->directories());

    int const row = selectedRow();
    if (row < 0) return;
    SystemRecord const& r = m_model->at(row);
    for (QString const& d : { r.runDir, r.firmwareDir(), r.disksDir() }) {
        if (QDir(d).exists()) m_watcher->addPath(d);
    }
}

void LauncherWindow::onRunDirChanged(QString const&)
{
    rewatchSelection();       // a recreated directory needs re-adding
    refreshDetails();
    runPreflight();
}

// ===========================================================================
// details tab
// ===========================================================================
void LauncherWindow::refreshDetails()
{
    m_updatingWidgets = true;

    int const row = selectedRow();
    bool const have = row >= 0;

    if (!have) {
        m_platformLbl->setText(tr("--"));
        m_runDirLbl->setText(tr("--"));
        m_firmware->clear();
        m_firmwareNote->clear();
        m_consoleNote->clear();
        m_platEdNote->clear();
        m_updatingWidgets = false;
        refreshActionStates();
        return;
    }

    SystemRecord const& r = m_model->at(row);

    m_platformLbl->setText(QStringLiteral("%1  --  %2")
                               .arg(platformToString(r.platform),
                                    platformDescription(r.platform)));
    m_runDirLbl->setText(QDir::toNativeSeparators(r.runDir));

    // ---- firmware (W2): only files inside the run dir, never a free path --
    QString const previous = m_firmware->currentData().toString();
    m_firmware->clear();
    m_firmware->addItem(tr("-- none selected --"), QString());

    QList<FirmwareCheck::Candidate> const found =
        FirmwareCheck::scan(r.firmwareDir(), r.platform);
    for (FirmwareCheck::Candidate const& c : found) {
        m_firmware->addItem(c.fileName, c.relativePath);
    }

    IniOverlay ini;
    QString fromIni;
    if (ini.load(r.iniPath())) {
        fromIni = ini.value(QString::fromLatin1(IniOverlay::kSecRom),
                            QString::fromLatin1(IniOverlay::kKeyFirmware));
    }
    QString const want = previous.isEmpty() ? fromIni : previous;
    int const idx = m_firmware->findData(want);
    m_firmware->setCurrentIndex(idx >= 0 ? idx : 0);

    QString const chosen = m_firmware->currentData().toString();
    m_firmwareNote->clear();
    for (FirmwareCheck::Candidate const& c : found) {
        if (c.relativePath == chosen) { m_firmwareNote->setText(c.note); break; }
    }

    // ---- console (W3) -----------------------------------------------------
    int const port = m_model->consolePort(row);
    m_consolePort->setValue(port > 0 ? port : RunDirSkeleton::kDefaultConsolePort);

    TerminalBridge::Availability const term = TerminalBridge::probe();
    m_consoleNote->setText(
        port > 0 ? tr("%1 -- connect to %2 port %3.")
                       .arg(term.detail, QLatin1String(kConsoleHostLoopback)).arg(port)
                 : term.detail);

    // ---- PlatEd (W5) ------------------------------------------------------
    m_platEdNote->setText(
        m_platEd->isAvailable()
            ? tr("Manifest: %1").arg(QDir::toNativeSeparators(r.manifestPath()))
            : m_platEd->unavailableReason());

    m_updatingWidgets = false;
    refreshActionStates();
}

void LauncherWindow::onFirmwareChanged(int)
{
    if (m_updatingWidgets) return;
    int const row = selectedRow();
    if (row < 0) return;

    SystemRecord const& r = m_model->at(row);
    QString const rel = m_firmware->currentData().toString();

    IniOverlay ini;
    QString error;
    if (!ini.load(r.iniPath(), &error)) {
        QMessageBox::warning(this, tr("Firmware"), error);
        return;
    }
    if (!ini.setValue(QString::fromLatin1(IniOverlay::kSecRom),
                      QString::fromLatin1(IniOverlay::kKeyFirmware), rel)) {
        QMessageBox::warning(this, tr("Firmware"),
                             tr("internal: firmware key is not whitelisted"));
        return;
    }
    if (!ini.save(&error)) {
        // E3: never silently drop a setting.
        QMessageBox::warning(this, tr("Firmware"), error);
        return;
    }

    refreshDetails();
    runPreflight();
}

void LauncherWindow::onConsolePortChanged()
{
    if (m_updatingWidgets) return;
    int const row = selectedRow();
    if (row < 0) return;
    if (m_model->consolePort(row) == m_consolePort->value()) return;

    QString error;
    if (!m_model->setConsolePort(row, m_consolePort->value(), &error)) {
        QMessageBox::warning(this, tr("Console port"), error);
        m_updatingWidgets = true;
        m_consolePort->setValue(m_model->consolePort(row));
        m_updatingWidgets = false;
        return;
    }
    refreshDetails();
    runPreflight();
}

void LauncherWindow::onOpenConsole()
{
    int const row = selectedRow();
    if (row < 0) return;
    int const port = m_model->consolePort(row);

    QString error;
    if (!TerminalBridge::openConsole(QString::fromLatin1(kConsoleHostLoopback),
                                     port, &error)) {
        QMessageBox::information(this, tr("Open Console"), error);
    }
}

void LauncherWindow::onOpenInPlatEd()
{
    int const row = selectedRow();
    if (row < 0) return;

    QString error;
    if (!m_platEd->openManifest(m_model->at(row).manifestPath(), &error)) {
        QMessageBox::information(this, tr("PlatEd"), error);
        return;
    }
    refreshActionStates();
}

void LauncherWindow::onPlatEdFinished()
{
    // W5: revalidate on return so manifest changes are caught before Start.
    refreshDetails();
    runPreflight();
}

// ===========================================================================
// registry actions
// ===========================================================================
void LauncherWindow::onNewSystem()
{
    NewSystemDialog dlg(NewSystemDialog::Mode::CreateNew, this);
    if (dlg.exec() != QDialog::Accepted) return;

    QString error;
    QString const id = m_model->createSystem(dlg.systemName(), dlg.platform(),
                                             dlg.runDir(), &error);
    if (id.isEmpty()) {
        QMessageBox::warning(this, tr("New System"), error);
        return;
    }
    selectRow(m_model->rowForId(id));

    QMessageBox::information(
        this, tr("New System"),
        tr("\"%1\" is ready.\n\nEmulatR does not ship firmware. Copy your SRM "
           "image into the firmware folder, then choose it on the Named System "
           "Details tab.").arg(dlg.systemName()));
    openInExplorer(m_model->at(m_model->rowForId(id)).firmwareDir());
}

void LauncherWindow::onAddExisting()
{
    NewSystemDialog dlg(NewSystemDialog::Mode::AddExisting, this);
    if (dlg.exec() != QDialog::Accepted) return;

    // M5: the stated platform is validated against the contents before
    // registration.
    QString how;
    bool ambiguous = false;
    Platform const inferred = RunDirSkeleton::inferPlatform(dlg.runDir(), &how, &ambiguous);
    if (inferred != Platform::Unknown && inferred != dlg.platform()) {
        QMessageBox::StandardButton const answer = QMessageBox::question(
            this, tr("Add Existing System"),
            tr("You chose %1, but that directory looks like %2 (%3).\n\n"
               "Registering it as %1 anyway will very likely fail to boot. "
               "Continue?")
                .arg(platformToString(dlg.platform()), platformToString(inferred), how),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes) return;
    }

    QString error;
    QString const id = m_model->registerExisting(dlg.systemName(), dlg.platform(),
                                                 dlg.runDir(), &error);
    if (id.isEmpty()) {
        QMessageBox::warning(this, tr("Add Existing System"), error);
        return;
    }
    selectRow(m_model->rowForId(id));
}

void LauncherWindow::onRemoveSystem()
{
    int const row = selectedRow();
    if (row < 0) return;
    SystemRecord const& r = m_model->at(row);

    if (r.running) {
        QMessageBox::information(this, tr("Remove System"),
                                 tr("\"%1\" is running. Stop it first.").arg(r.name));
        return;
    }

    // M6: the dialog says explicitly that nothing on disk is deleted.
    QMessageBox box(this);
    box.setIcon(QMessageBox::Question);
    box.setWindowTitle(tr("Remove System"));
    box.setText(tr("Remove \"%1\" from the list?").arg(r.name));
    box.setInformativeText(
        tr("This removes the registration and this system's launcher settings "
           "only.\n\nNothing in the run directory is deleted:\n%1\n\nYou can "
           "add it back later with \"Add Existing...\".")
            .arg(QDir::toNativeSeparators(r.runDir)));
    box.setStandardButtons(QMessageBox::Yes | QMessageBox::Cancel);
    box.setDefaultButton(QMessageBox::Cancel);
    if (box.exec() != QMessageBox::Yes) return;

    m_model->removeSystem(row);
    selectRow(qMin(row, m_model->count() - 1));
}

void LauncherWindow::onRenameSystem()
{
    int const row = selectedRow();
    if (row < 0) return;

    bool ok = false;
    QString const name = QInputDialog::getText(
        this, tr("Rename System"), tr("System name:"), QLineEdit::Normal,
        m_model->at(row).name, &ok);
    if (!ok) return;

    QString error;
    if (!m_model->renameSystem(row, name, &error)) {
        QMessageBox::warning(this, tr("Rename System"), error);
        return;
    }
    selectRow(m_model->rowForName(name.trimmed()));
}

void LauncherWindow::onRelocateSystem()
{
    int const row = selectedRow();
    if (row < 0) return;
    SystemRecord const& r = m_model->at(row);

    QString const picked = QFileDialog::getExistingDirectory(
        this, tr("Where is \"%1\" now?").arg(r.name),
        QDir(r.runDir).exists() ? r.runDir : QDir::homePath());
    if (picked.isEmpty()) return;

    QString error;
    if (!m_model->relocateSystem(row, picked, &error)) {
        QMessageBox::warning(this, tr("Relocate run directory"), error);
        return;
    }
    onSelectionChanged();
}

// ===========================================================================
// preflight (L1)
// ===========================================================================
void LauncherWindow::runPreflight()
{
    int const row = selectedRow();
    m_fix = FixAction::None;

    if (row < 0) {
        m_preflightOk = false;
        setStatus({ m_model->count() == 0
                        ? tr("No systems yet. Choose \"New System...\" to create "
                             "one, or \"Add Existing...\" to register a run "
                             "directory you already have.")
                        : tr("Select a system.") }, {});
        refreshActionStates();
        return;
    }

    SystemRecord const& r = m_model->at(row);

    RunDirSkeleton::ValidationResult v = RunDirSkeleton::validate(r.runDir, r.platform);

    // Emulatr.exe resolved (Section 6).
    ExeDiscovery::Result const exe = ExeDiscovery::findEmulatr();
    if (!exe.found()) {
        v.problems << tr("Emulatr.exe could not be found. %1").arg(exe.detail);
        if (m_fix == FixAction::None) m_fix = FixAction::OpenSettings;
    }

    // W2 has a value.
    if (m_firmware && m_firmware->currentData().toString().isEmpty()) {
        v.problems << tr("No firmware image is selected for this system.");
    }

    // E10: console port claimed by another registered system.
    int const port = m_model->consolePort(row);
    if (port <= 0) {
        v.problems << tr("This system has no console port in Emulatr.ini, so "
                         "there is no way to reach P00>>>.");
    } else if (int const clash = m_model->rowClaimingPort(port, row); clash >= 0) {
        v.problems << tr("Console port %1 is also used by \"%2\". Two systems "
                         "cannot share a port.")
                          .arg(port).arg(m_model->at(clash).name);
    }

    // Pick the most useful Fix for the FIRST problem.
    if (m_fix == FixAction::None && !v.problems.isEmpty()) {
        QString const first = v.problems.first();
        if (first.contains(QLatin1String("Run directory is missing")))
            m_fix = FixAction::Relocate;
        else if (first.contains(QLatin1String("firmware"), Qt::CaseInsensitive))
            m_fix = first.contains(QLatin1String("selected"))
                        ? FixAction::ChooseFirmware : FixAction::OpenFirmwareFolder;
        else
            m_fix = FixAction::OpenRunDir;
    }

    m_preflightOk = v.problems.isEmpty();
    m_model->setHealth(row, m_preflightOk ? (v.notes.isEmpty() ? Health::Ok
                                                               : Health::Warning)
                                          : Health::Broken,
                       v.problems.isEmpty() ? QString() : v.problems.first());
    setStatus(v.problems, v.notes);
    refreshActionStates();
}

void LauncherWindow::setStatus(QStringList const& problems, QStringList const& notes)
{
    QStringList html;
    // HTML entity rather than a literal bullet: the house rule is ASCII-128
    // source, and QLabel renders the entity in rich-text mode.
    for (QString const& p : problems)
        html << QStringLiteral("&#8226; <b>%1</b>").arg(p.toHtmlEscaped());
    for (QString const& n : notes)
        html << QStringLiteral("&#8226; %1").arg(n.toHtmlEscaped());

    if (html.isEmpty() && hasSelection()) {
        html << tr("Ready to start.");
    }
    m_status->setText(html.join(QStringLiteral("<br>")));

    QString fixLabel;
    switch (m_fix) {
        case FixAction::OpenFirmwareFolder: fixLabel = tr("Open firmware folder"); break;
        case FixAction::OpenRunDir:         fixLabel = tr("Open run directory"); break;
        case FixAction::ChooseFirmware:     fixLabel = tr("Choose firmware"); break;
        case FixAction::OpenSettings:       fixLabel = tr("Settings..."); break;
        case FixAction::Relocate:           fixLabel = tr("Relocate run dir..."); break;
        case FixAction::None:               break;
    }
    m_fixBtn->setVisible(!fixLabel.isEmpty());
    m_fixBtn->setText(fixLabel);
}

void LauncherWindow::refreshActionStates()
{
    bool const have    = hasSelection();
    bool const running = m_proc->isRunning();
    int const  row     = selectedRow();
    bool const thisRunning = running && have
                          && m_proc->systemId() == m_model->at(row).id;

    // L4: single-flight.  While ANY system runs, Start is disabled for ALL of
    // them -- selecting another system and pressing Start is structurally
    // impossible, not merely warned against.
    m_startBtn->setEnabled(have && m_preflightOk && !running);
    m_stopBtn->setEnabled(thisRunning && m_proc->state() == EmulatorProcess::State::Running);
    m_forceBtn->setVisible(m_proc->state() == EmulatorProcess::State::Escalating);
    m_forceBtn->setEnabled(m_forceBtn->isVisible());

    m_removeBtn->setEnabled(have && !thisRunning);
    m_makeDiskBtn->setEnabled(have);
    m_logsBtn->setEnabled(have);
    m_packageBtn->setEnabled(have);

    bool const platEdOk = have && m_platEd->isAvailable() && !m_platEd->isRunning();
    m_platEdBtn->setEnabled(platEdOk);
    m_configureBtn->setEnabled(platEdOk);

    int const port = have ? m_model->consolePort(row) : -1;
    m_openConsole->setEnabled(have && port > 0);
    m_consolePort->setEnabled(have && !thisRunning);
    m_firmware->setEnabled(have && !running);

    if (running) {
        QString const name = m_proc->systemName();
        switch (m_proc->state()) {
            case EmulatorProcess::State::Running:
                m_runState->setText(tr("<b>%1 is running.</b> Use Open Console to "
                                       "reach the console.").arg(name.toHtmlEscaped()));
                break;
            case EmulatorProcess::State::Stopping:
                m_runState->setText(tr("<b>Stopping %1...</b> waiting for it to "
                                       "persist its flash state and exit.")
                                        .arg(name.toHtmlEscaped()));
                break;
            case EmulatorProcess::State::Escalating:
                m_runState->setText(
                    tr("<b>%1 has not exited.</b> Force Stop will kill it "
                       "immediately -- any flash state it had not yet written "
                       "is lost.").arg(name.toHtmlEscaped()));
                break;
            case EmulatorProcess::State::Idle:
                break;
        }
    } else {
        m_runState->clear();
    }
}

// ===========================================================================
// lifecycle
// ===========================================================================
void LauncherWindow::onStart()
{
    int const row = selectedRow();
    if (row < 0) return;

    runPreflight();
    if (!m_preflightOk) return;

    SystemRecord const& r = m_model->at(row);

    QString error;
    if (!RunDirSkeleton::ensureSubdirs(r.runDir, &error)) {
        QMessageBox::warning(this, tr("Start"), error);
        return;
    }

    ExeDiscovery::Result const exe = ExeDiscovery::findEmulatr();

    QProcessEnvironment const inherited = QProcessEnvironment::systemEnvironment();

    EmulatorProcess::StartRequest req;
    req.systemId        = r.id;
    req.systemName      = r.name;
    req.platform        = platformToString(r.platform);
    req.exePath         = exe.path;
    req.runDir          = r.runDir;
    req.firmwareRelPath = m_firmware->currentData().toString();
    req.consolePort     = m_model->consolePort(row);
    req.environment     = m_envModel->composeEnvironment(inherited);
    req.envForLog       = m_envModel->effectiveSelectionForLog();
    req.strippedForLog  = m_envModel->strippedFrom(inherited);

    if (!m_proc->start(req, &error)) {
        QMessageBox::warning(this, tr("Start"), error);
        return;
    }

    m_lastLogPath = m_proc->mirrorLogPath();
    m_showLogBtn->setVisible(true);
    m_model->setRunning(r.id, true);
    refreshActionStates();
    statusBar()->showMessage(tr("Started %1.").arg(r.name), 5000);
}

void LauncherWindow::onStop()
{
    if (!m_proc->isRunning()) return;
    m_proc->requestCleanShutdown();
    refreshActionStates();
}

void LauncherWindow::onForceStop()
{
    // L3: kill is never automatic, and the consequence is stated plainly.
    QMessageBox box(this);
    box.setIcon(QMessageBox::Warning);
    box.setWindowTitle(tr("Force Stop"));
    box.setText(tr("Kill %1 immediately?").arg(m_proc->systemName()));
    box.setInformativeText(
        tr("A forced kill skips the emulator's shutdown path. Any flash state "
           "it had not yet written to the .rom file is lost -- including SRM "
           "environment variables set at the console during this run.\n\n"
           "This is recorded in the run log as a forced stop."));
    box.setStandardButtons(QMessageBox::Yes | QMessageBox::Cancel);
    box.setDefaultButton(QMessageBox::Cancel);
    if (box.exec() != QMessageBox::Yes) return;

    m_proc->forceStop();
}

void LauncherWindow::onProcessStateChanged(EmulatorProcess::State)
{
    refreshActionStates();
}

void LauncherWindow::onEscalationAvailable()
{
    refreshActionStates();
    statusBar()->showMessage(
        tr("%1 has not exited. Force Stop is now available.").arg(m_proc->systemName()));
}

void LauncherWindow::onProcessFinished(int exitCode, QProcess::ExitStatus status,
                                       bool forced, qint64 durationMs)
{
    Q_UNUSED(status);

    QString const id = m_proc->systemId();
    if (!id.isEmpty()) m_model->setRunning(id, false);

    // L5: exit code and duration in the status area, disposition recorded.
    QString const summary =
        tr("Exited with code %1 after %2 s (%3).")
            .arg(exitCode)
            .arg(durationMs / 1000.0, 0, 'f', 1)
            .arg(forced ? tr("forced stop") : tr("clean shutdown"));

    statusBar()->showMessage(summary, 30000);
    m_showLogBtn->setVisible(!m_lastLogPath.isEmpty());

    if (exitCode != 0 || forced) {
        // Nonzero or forced exits surface a "Show log" action.
        QMessageBox box(this);
        box.setIcon(forced ? QMessageBox::Warning : QMessageBox::Information);
        box.setWindowTitle(tr("Emulator stopped"));
        box.setText(summary);
        if (!m_lastLogPath.isEmpty()) {
            box.setInformativeText(tr("The run log is at\n%1")
                                       .arg(QDir::toNativeSeparators(m_lastLogPath)));
            box.addButton(tr("Show log"), QMessageBox::AcceptRole);
        }
        box.addButton(QMessageBox::Close);
        box.exec();
        if (box.clickedButton() && box.buttonRole(box.clickedButton()) == QMessageBox::AcceptRole)
            onShowLastLog();
    }

    refreshActionStates();
    runPreflight();
}

// ===========================================================================
// storage and logs
// ===========================================================================
void LauncherWindow::onMakeDisk()
{
    int const row = selectedRow();
    if (row < 0) return;
    SystemRecord const& r = m_model->at(row);

    QString error;
    RunDirSkeleton::ensureSubdirs(r.runDir, &error);

    StorageCreateDialog dlg(r.disksDir(), r.name, this);
    if (dlg.exec() != QDialog::Accepted) return;

    statusBar()->showMessage(
        tr("Created %1").arg(QDir::toNativeSeparators(dlg.createdPath())), 10000);

    QMessageBox::information(
        this, tr("Create Virtual Disk"),
        tr("Created:\n%1\n\nAttach it to a controller in PlatEd to make the "
           "system see it.").arg(QDir::toNativeSeparators(dlg.createdPath())));
}

void LauncherWindow::onOpenLogsFolder()
{
    int const row = selectedRow();
    if (row < 0) return;
    SystemRecord const& r = m_model->at(row);
    RunDirSkeleton::ensureSubdirs(r.runDir, nullptr);
    openInExplorer(r.logsDir());
}

void LauncherWindow::onOpenRunDirectory()
{
    int const row = selectedRow();
    if (row >= 0) openInExplorer(m_model->at(row).runDir);
}

void LauncherWindow::onShowLastLog()
{
    if (m_lastLogPath.isEmpty()) return;
    QDesktopServices::openUrl(QUrl::fromLocalFile(m_lastLogPath));
}

void LauncherWindow::onPackageLogs()
{
    int const row = selectedRow();
    if (row < 0) return;
    SystemRecord const& r = m_model->at(row);

    QDir logs(r.logsDir());
    if (!logs.exists()) {
        QMessageBox::information(this, tr("Package Logs"),
                                 tr("This system has no logs folder yet."));
        return;
    }

    logs.setNameFilters({ QStringLiteral("run_launch_*.log") });
    logs.setSorting(QDir::Time);
    QFileInfoList const all = logs.entryInfoList(QDir::Files);
    if (all.isEmpty()) {
        QMessageBox::information(this, tr("Package Logs"),
                                 tr("There are no run logs to package yet."));
        return;
    }

    // L6: newest N mirror logs, EXCLUDING traces/ -- those are multi-gigabyte
    // and would make an unsendable archive.
    constexpr int kMaxLogs = 10;
    QStringList picked;
    for (QFileInfo const& fi : all) {
        picked << fi.absoluteFilePath();
        if (picked.size() >= kMaxLogs) break;
    }

    QString const stamp =
        QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss"));
    QString stem;
    for (QChar const ch : r.name) {
        stem += ch.isLetterOrNumber() ? ch : QLatin1Char('_');
    }
    QString const dest =
        QDir(QStandardPaths::writableLocation(QStandardPaths::DesktopLocation))
            .filePath(QStringLiteral("emulatr_logs_%1_%2.zip").arg(stem, stamp));

    // Compress-Archive is present on every supported Windows build and avoids
    // adding an archive dependency to a launcher that has no other use for one.
    QStringList quoted;
    for (QString const& p : picked)
        quoted << QStringLiteral("'%1'").arg(QDir::toNativeSeparators(p));

    QString const script =
        QStringLiteral("Compress-Archive -Force -DestinationPath '%1' -LiteralPath %2")
            .arg(QDir::toNativeSeparators(dest), quoted.join(QStringLiteral(",")));

    QProcess ps;
    ps.start(QStringLiteral("powershell.exe"),
             { QStringLiteral("-NoProfile"), QStringLiteral("-NonInteractive"),
               QStringLiteral("-Command"), script });
    if (!ps.waitForFinished(60000) || ps.exitCode() != 0) {
        QMessageBox::warning(
            this, tr("Package Logs"),
            tr("Could not build the archive.\n\n%1\n\nThe logs themselves are "
               "still in:\n%2")
                .arg(QString::fromLocal8Bit(ps.readAllStandardError()),
                     QDir::toNativeSeparators(r.logsDir())));
        return;
    }

    QMessageBox::information(
        this, tr("Package Logs"),
        tr("Packaged the %1 most recent run logs to:\n%2\n\nTrace output was "
           "not included -- it is far too large to send.")
            .arg(picked.size()).arg(QDir::toNativeSeparators(dest)));
    openInExplorer(QFileInfo(dest).absolutePath());
}

void LauncherWindow::onSettings()
{
    SettingsDialog dlg(m_proc->shutdownTimeoutMs(), this);
    if (dlg.exec() != QDialog::Accepted) return;
    dlg.apply();
    m_platEd->refresh();
    refreshDetails();
    runPreflight();
}

// ===========================================================================
// close
// ===========================================================================
void LauncherWindow::closeEvent(QCloseEvent* event)
{
    if (m_proc->isRunning()) {
        QMessageBox box(this);
        box.setIcon(QMessageBox::Question);
        box.setWindowTitle(tr("Close EmulatrLaunch"));
        box.setText(tr("%1 is still running.").arg(m_proc->systemName()));
        box.setInformativeText(
            tr("Closing the launcher does not stop the emulator, and you would "
               "lose the Stop button that shuts it down cleanly."));
        QPushButton* stopFirst = box.addButton(tr("Stop it first"), QMessageBox::AcceptRole);
        box.addButton(tr("Leave it running"), QMessageBox::DestructiveRole);
        QPushButton* cancel = box.addButton(QMessageBox::Cancel);
        box.setDefaultButton(stopFirst);
        box.exec();

        if (box.clickedButton() == cancel) { event->ignore(); return; }
        if (box.clickedButton() == stopFirst) {
            m_proc->requestCleanShutdown();
            event->ignore();
            return;
        }
    }

    QSettings s = openSettings();
    s.setValue(QLatin1String(keys::kWindowGeometry), saveGeometry());
    QMainWindow::closeEvent(event);
}

}  // namespace launch
