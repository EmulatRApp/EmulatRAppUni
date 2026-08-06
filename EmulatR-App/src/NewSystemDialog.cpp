// ============================================================================
// src/NewSystemDialog.cpp -- the "New System..." and "Add Existing..." prompts
// ============================================================================
// Project: EmulatR -- EmulatrLaunch (SPEC-LAUNCH-001 Rev D.2)
// Copyright (C) 2026 eNVy Systems, Inc.  All rights reserved.
// Licensed under eNVy Systems Non-Commercial License v1.1
//
// Project Architect: Timothy Peer
// AI Collaboration:  Claude (Anthropic)
//
// Version: 1.0.0-alpha   Date: 2026-07-29
// Spec:    Section 4 M4/M5, Section 5 R2
// ============================================================================

#include "NewSystemDialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QStandardPaths>
#include <QVBoxLayout>

#include "RunDirSkeleton.h"

namespace launch {

namespace {

QString defaultParentDir()
{
    // M4: default %USERPROFILE%\Documents\EmulatR\<name>.
    QString const docs =
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    return QDir(docs).filePath(QStringLiteral("EmulatR"));
}

// A name a tester types is not necessarily a legal directory name.
QString folderStemFor(QString const& name)
{
    QString stem;
    for (QChar const ch : name) {
        ushort const u = ch.unicode();
        bool const ok = (u >= 'a' && u <= 'z') || (u >= 'A' && u <= 'Z')
                     || (u >= '0' && u <= '9') || ch == QLatin1Char('_')
                     || ch == QLatin1Char('-');
        stem += ok ? ch : QLatin1Char('_');
    }
    while (stem.contains(QLatin1String("__"))) stem.replace(QLatin1String("__"),
                                                            QLatin1String("_"));
    stem = stem.trimmed();
    while (stem.startsWith(QLatin1Char('_'))) stem.remove(0, 1);
    while (stem.endsWith(QLatin1Char('_')))   stem.chop(1);
    return stem;
}

}  // namespace

NewSystemDialog::NewSystemDialog(Mode mode, QWidget* parent)
    : QDialog(parent), m_mode(mode)
{
    setWindowTitle(mode == Mode::CreateNew ? tr("New System") : tr("Add Existing System"));
    setModal(true);

    auto* outer = new QVBoxLayout(this);

    auto* intro = new QLabel(this);
    intro->setWordWrap(true);
    intro->setText(
        mode == Mode::CreateNew
            ? tr("A system is one virtual Alpha machine: one run directory, one "
                 "platform. The platform is fixed when the system is created -- "
                 "to run a different machine, create another system.")
            : tr("Register a run directory that already exists. Nothing inside it "
                 "is modified, beyond creating the <tt>logs</tt> and <tt>traces</tt> "
                 "folders if they are missing."));
    outer->addWidget(intro);

    auto* form = new QFormLayout;

    // ---- location (first in AddExisting: everything else follows from it) --
    m_location = new QLineEdit(this);
    m_browse   = new QPushButton(tr("Browse..."), this);
    auto* locRow = new QHBoxLayout;
    locRow->addWidget(m_location, 1);
    locRow->addWidget(m_browse);

    m_name = new QLineEdit(this);
    m_platform = new QComboBox(this);
    m_platform->addItem(tr("-- choose --"), QString());
    for (QString const& p : allPlatformNames()) {
        m_platform->addItem(QStringLiteral("%1  --  %2")
                                .arg(p, platformDescription(platformFromString(p))),
                            p);
    }

    if (mode == Mode::CreateNew) {
        form->addRow(tr("System name:"), m_name);
        form->addRow(tr("Platform:"), m_platform);
        form->addRow(tr("Location:"), locRow);
        m_location->setText(QDir::toNativeSeparators(defaultParentDir()));
    } else {
        form->addRow(tr("Run directory:"), locRow);
        form->addRow(tr("Platform:"), m_platform);
        form->addRow(tr("System name:"), m_name);
    }
    outer->addLayout(form);

    m_inference = new QLabel(this);
    m_inference->setWordWrap(true);
    m_inference->setVisible(mode == Mode::AddExisting);
    outer->addWidget(m_inference);

    m_pathPreview = new QLabel(this);
    m_pathPreview->setWordWrap(true);
    m_pathPreview->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_pathPreview->setVisible(mode == Mode::CreateNew);
    outer->addWidget(m_pathPreview);

    m_problem = new QLabel(this);
    m_problem->setWordWrap(true);
    m_problem->setVisible(false);
    outer->addWidget(m_problem);

    m_buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    m_buttons->button(QDialogButtonBox::Ok)
        ->setText(mode == Mode::CreateNew ? tr("Create") : tr("Add"));
    outer->addWidget(m_buttons);

    connect(m_browse, &QPushButton::clicked, this, &NewSystemDialog::onBrowse);
    connect(m_name, &QLineEdit::textChanged, this, &NewSystemDialog::revalidate);
    // In AddExisting mode the run directory drives inference, which then calls
    // revalidate() itself.  Wiring textChanged straight to revalidate() as well
    // would let the two call each other.
    if (mode == Mode::AddExisting)
        connect(m_location, &QLineEdit::textChanged, this, &NewSystemDialog::inferForRunDir);
    else
        connect(m_location, &QLineEdit::textChanged, this, &NewSystemDialog::revalidate);
    connect(m_platform, &QComboBox::currentIndexChanged, this, &NewSystemDialog::revalidate);
    connect(m_buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    resize(600, sizeHint().height());
    revalidate();
}

QString NewSystemDialog::systemName() const { return m_name->text().trimmed(); }

Platform NewSystemDialog::platform() const
{
    return platformFromString(m_platform->currentData().toString());
}

QString NewSystemDialog::runDir() const
{
    QString const loc = m_location->text().trimmed();
    if (m_mode == Mode::AddExisting) return loc;

    // M4: the location field names the PARENT; the system gets its own folder
    // under it so two systems created into Documents\EmulatR cannot collide.
    QString const stem = folderStemFor(systemName());
    if (stem.isEmpty() || loc.isEmpty()) return loc;
    return QDir(loc).filePath(stem);
}

void NewSystemDialog::onBrowse()
{
    QString const start = m_location->text().trimmed();
    QString const picked = QFileDialog::getExistingDirectory(
        this,
        m_mode == Mode::CreateNew ? tr("Choose where to create the system")
                                  : tr("Choose the existing run directory"),
        start.isEmpty() ? QDir::homePath() : start);
    if (picked.isEmpty()) return;
    m_location->setText(QDir::toNativeSeparators(picked));
    if (m_mode == Mode::AddExisting) inferForRunDir();
}

// M5: infer, and when the sources disagree, say so and make the user state it.
void NewSystemDialog::inferForRunDir()
{
    QString const dir = m_location->text().trimmed();
    if (dir.isEmpty() || !QDir(dir).exists()) {
        m_inference->setText(QString());
        m_platformInferred = false;
        return;
    }

    QString how;
    bool ambiguous = false;
    Platform const p = RunDirSkeleton::inferPlatform(dir, &how, &ambiguous);

    if (p != Platform::Unknown) {
        int const idx = m_platform->findData(platformToString(p));
        if (idx >= 0) m_platform->setCurrentIndex(idx);
        m_platformInferred = true;
        m_inference->setText(tr("Platform inferred as <b>%1</b>: %2")
                                 .arg(platformToString(p), how.toHtmlEscaped()));
    } else {
        m_platformInferred = false;
        m_inference->setText(
            ambiguous
                ? tr("<b>The platform could not be inferred.</b> %1 Choose it "
                     "yourself -- the choice is checked against the directory "
                     "before the system is registered.").arg(how.toHtmlEscaped())
                : tr("Nothing in that directory names a platform. Choose it "
                     "yourself."));
    }

    // Suggest a name from the folder if the user has not typed one.
    if (m_name->text().trimmed().isEmpty()) {
        QString const leaf = QDir(dir).dirName();
        if (!leaf.isEmpty()) {
            m_name->setText(p == Platform::Unknown
                                ? leaf
                                : QStringLiteral("%1 (%2)").arg(platformToString(p), leaf));
        }
    }
    revalidate();
}

void NewSystemDialog::revalidate()
{
    QString problem;

    QString const name = systemName();
    QString const loc  = m_location->text().trimmed();
    Platform const p   = platform();

    QString const target = runDir();

    if (loc.isEmpty()) {
        problem = m_mode == Mode::CreateNew ? tr("Choose where to create the system.")
                                            : tr("Choose the run directory.");
    } else if (name.isEmpty()) {
        problem = tr("Give the system a name.");
    } else if (p == Platform::Unknown) {
        problem = tr("Choose the platform.");
    } else if (RunDirSkeleton::isUnderProgramFiles(target)) {
        problem = tr("That location is inside Program Files. Windows makes it "
                     "read-only and silently redirects writes to a VirtualStore "
                     "copy, which loses the emulator's saved flash state. Pick "
                     "somewhere under Documents instead.");
    } else if (m_mode == Mode::AddExisting && !QDir(target).exists()) {
        problem = tr("That directory does not exist.");
    } else if (m_mode == Mode::CreateNew && folderStemFor(name).isEmpty()) {
        problem = tr("That name has no letters or digits to make a folder name "
                     "from. Add some.");
    } else if (m_mode == Mode::CreateNew && QDir(target).exists()
               && !QDir(target).isEmpty()) {
        problem = tr("%1 already exists and is not empty. Use \"Add Existing...\" "
                     "if that is already a system.")
                      .arg(QDir::toNativeSeparators(target));
    }

    if (m_mode == Mode::CreateNew && !target.isEmpty()) {
        m_pathPreview->setText(tr("<b>Run directory</b><br><tt>%1</tt>")
                                   .arg(QDir::toNativeSeparators(target).toHtmlEscaped()));
    }

    m_problem->setVisible(!problem.isEmpty());
    m_problem->setText(problem);
    m_buttons->button(QDialogButtonBox::Ok)->setEnabled(problem.isEmpty());
}

}  // namespace launch
