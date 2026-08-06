// ============================================================================
// src/StorageCreateDialog.cpp -- geometry/name prompt over DiskImageFactory
// ============================================================================
// Project: EmulatR -- EmulatrLaunch (SPEC-LAUNCH-001 Rev D.2)
// Copyright (C) 2026 eNVy Systems, Inc.  All rights reserved.
// Licensed under eNVy Systems Non-Commercial License v1.1
//
// Project Architect: Timothy Peer
// AI Collaboration:  Claude (Anthropic)
//
// Version: 1.0.0-alpha   Date: 2026-07-29
// Spec:    Section 9 S2, Section 11 E7
// ============================================================================

#include "StorageCreateDialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace launch {

namespace {
constexpr int kCustomIndexSentinel = -1;
}

StorageCreateDialog::StorageCreateDialog(QString const& disksDir,
                                         QString const& systemName,
                                         QWidget* parent)
    : QDialog(parent), m_disksDir(disksDir)
{
    setWindowTitle(tr("Create Virtual Disk"));
    setModal(true);

    auto* outer = new QVBoxLayout(this);

    auto* intro = new QLabel(
        tr("A new disk container will be created in the <b>%1</b> system's "
           "<tt>disks</tt> folder. The image is a raw flat file that reads back "
           "as zeros -- a blank drive, ready to be a target for an operating "
           "system installation.").arg(systemName.toHtmlEscaped()), this);
    intro->setWordWrap(true);
    outer->addWidget(intro);

    auto* form = new QFormLayout;

    // ---- drive model ------------------------------------------------------
    m_model = new QComboBox(this);
    QString tableError;
    m_table = DiskImageFactory::loadTable(&tableError);
    for (int i = 0; i < m_table.size(); ++i) {
        DiskImageFactory::Geometry const& g = m_table.at(i);
        m_model->addItem(QStringLiteral("%1  --  %2  (%3)")
                             .arg(g.model,
                                  g.capacity.isEmpty()
                                      ? DiskImageFactory::humanSize(g.imageSizeBytes())
                                      : g.capacity,
                                  g.iface.toUpper()),
                         QVariant(i));
    }
    m_model->addItem(tr("Custom geometry..."), QVariant(kCustomIndexSentinel));
    form->addRow(tr("Drive model:"), m_model);

    m_geometry = new QLabel(this);
    m_geometry->setWordWrap(true);
    form->addRow(QString(), m_geometry);

    // ---- custom geometry --------------------------------------------------
    m_customBox = new QGroupBox(tr("Custom geometry"), this);
    auto* cf = new QFormLayout(m_customBox);
    m_cyl = new QSpinBox(m_customBox);
    m_cyl->setRange(1, static_cast<int>(DiskImageFactory::kMaxCylinders));
    m_cyl->setValue(1024);
    m_heads = new QSpinBox(m_customBox);
    m_heads->setRange(1, static_cast<int>(DiskImageFactory::kMaxHeads));
    m_heads->setValue(16);
    m_sectors = new QSpinBox(m_customBox);
    m_sectors->setRange(1, static_cast<int>(DiskImageFactory::kMaxSectors));
    m_sectors->setValue(63);
    m_blockSize = new QComboBox(m_customBox);
    m_blockSize->addItem(tr("512 (fixed disk)"), 512);
    m_blockSize->addItem(tr("2048 (ISO-9660 optical)"), 2048);
    cf->addRow(tr("Cylinders:"), m_cyl);
    cf->addRow(tr("Heads:"), m_heads);
    cf->addRow(tr("Sectors per track:"), m_sectors);
    cf->addRow(tr("Block size:"), m_blockSize);
    m_customBox->setVisible(false);
    outer->addLayout(form);
    outer->addWidget(m_customBox);

    // ---- name and preview -------------------------------------------------
    auto* form2 = new QFormLayout;
    m_name = new QLineEdit(this);
    m_name->setPlaceholderText(tr("for example:  system_disk"));
    form2->addRow(tr("Disk name:"), m_name);
    outer->addLayout(form2);

    m_preview = new QLabel(this);
    m_preview->setWordWrap(true);
    m_preview->setTextInteractionFlags(Qt::TextSelectableByMouse);
    outer->addWidget(m_preview);

    m_problem = new QLabel(this);
    m_problem->setWordWrap(true);
    m_problem->setVisible(false);
    outer->addWidget(m_problem);

    if (!tableError.isEmpty()) {
        auto* warn = new QLabel(
            tr("Note on the geometry table: %1").arg(tableError.toHtmlEscaped()), this);
        warn->setWordWrap(true);
        outer->addWidget(warn);
    }

    m_buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    m_buttons->button(QDialogButtonBox::Ok)->setText(tr("Create"));
    outer->addWidget(m_buttons);

    connect(m_model, &QComboBox::currentIndexChanged, this, &StorageCreateDialog::onModelChanged);
    connect(m_name, &QLineEdit::textChanged, this, &StorageCreateDialog::refreshPreview);
    connect(m_cyl, &QSpinBox::valueChanged, this, &StorageCreateDialog::refreshPreview);
    connect(m_heads, &QSpinBox::valueChanged, this, &StorageCreateDialog::refreshPreview);
    connect(m_sectors, &QSpinBox::valueChanged, this, &StorageCreateDialog::refreshPreview);
    connect(m_blockSize, &QComboBox::currentIndexChanged, this, &StorageCreateDialog::refreshPreview);
    connect(m_buttons, &QDialogButtonBox::accepted, this, &StorageCreateDialog::onAccept);
    connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    onModelChanged(m_model->currentIndex());
    resize(560, sizeHint().height());
}

void StorageCreateDialog::onModelChanged(int index)
{
    Q_UNUSED(index);
    bool const custom = m_model->currentData().toInt() == kCustomIndexSentinel;
    m_customBox->setVisible(custom);

    if (!custom) {
        int const row = m_model->currentData().toInt();
        if (row >= 0 && row < m_table.size()) {
            DiskImageFactory::Geometry const& g = m_table.at(row);
            QString detail = g.describe();
            if (!g.note.isEmpty()) detail += QStringLiteral("  [%1]").arg(g.note);
            m_geometry->setText(detail);
            // Offer a sensible default stem the first time.
            if (m_name->text().isEmpty())
                m_name->setText(g.model.toLower());
        }
    } else {
        m_geometry->setText(tr("Enter a geometry below. The container size is "
                               "cylinders x heads x sectors x block size."));
    }
    refreshPreview();
}

DiskImageFactory::Geometry StorageCreateDialog::currentGeometry(QString* error) const
{
    if (m_model->currentData().toInt() == kCustomIndexSentinel) {
        return DiskImageFactory::customGeometry(m_cyl->value(), m_heads->value(),
                                                m_sectors->value(),
                                                m_blockSize->currentData().toLongLong(),
                                                error);
    }
    int const row = m_model->currentData().toInt();
    if (row >= 0 && row < m_table.size()) return m_table.at(row);
    if (error) *error = tr("Choose a drive model.");
    return {};
}

void StorageCreateDialog::refreshPreview()
{
    QString geomError;
    DiskImageFactory::Geometry const g = currentGeometry(&geomError);

    QString nameError;
    QString const stem = m_name->text().trimmed();
    bool const nameOk = DiskImageFactory::isValidNameStem(stem, &nameError);

    QString problem;
    if (!geomError.isEmpty())      problem = geomError;
    else if (!stem.isEmpty() && !nameOk) problem = nameError;

    if (g.isValid() && nameOk) {
        QString const path = QDir(m_disksDir).filePath(
            stem + QLatin1Char('.') + QLatin1String(DiskImageFactory::kContainerExtension));

        qint64 const freeBytes = DiskImageFactory::freeSpaceFor(m_disksDir);
        QString freeText = freeBytes < 0
                               ? tr("free space unknown")
                               : tr("%1 free on that volume")
                                     .arg(DiskImageFactory::humanSize(freeBytes));

        m_preview->setText(
            tr("<b>Will create</b><br><tt>%1</tt><br>%2 blocks of %3 bytes "
               "= <b>%4</b><br>%5")
                .arg(QDir::toNativeSeparators(path).toHtmlEscaped())
                .arg(g.totalLbn)
                .arg(g.blockBytes)
                .arg(DiskImageFactory::humanSize(g.imageSizeBytes()))
                .arg(freeText));

        if (freeBytes >= 0 && freeBytes < g.imageSizeBytes()) {
            problem = tr("Not enough free space: this image needs %1 but only %2 "
                         "is available.")
                          .arg(DiskImageFactory::humanSize(g.imageSizeBytes()),
                               DiskImageFactory::humanSize(freeBytes));
        }
    } else {
        m_preview->setText(tr("<b>Will create</b><br><i>-- name the disk to see "
                              "the exact path and size --</i>"));
    }

    m_problem->setVisible(!problem.isEmpty());
    m_problem->setText(problem);

    m_buttons->button(QDialogButtonBox::Ok)
        ->setEnabled(g.isValid() && nameOk && problem.isEmpty());
}

void StorageCreateDialog::onAccept()
{
    QString geomError;
    DiskImageFactory::Geometry const g = currentGeometry(&geomError);
    if (!g.isValid()) {
        QMessageBox::warning(this, tr("Create Virtual Disk"), geomError);
        return;
    }

    QString nameError;
    QString const stem = m_name->text().trimmed();
    if (!DiskImageFactory::isValidNameStem(stem, &nameError)) {
        QMessageBox::warning(this, tr("Create Virtual Disk"), nameError);
        return;
    }

    QString const path = QDir(m_disksDir).filePath(
        stem + QLatin1Char('.') + QLatin1String(DiskImageFactory::kContainerExtension));

    QString error;
    if (!DiskImageFactory::create(path, g, &error)) {
        QMessageBox::warning(this, tr("Create Virtual Disk"), error);
        return;
    }

    m_createdPath = path;
    accept();
}

}  // namespace launch
