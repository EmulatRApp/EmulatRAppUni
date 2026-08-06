// ============================================================================
// src/PlatEdBridge.cpp -- PlatEd discovery and invocation
// ============================================================================
// Project: EmulatR -- EmulatrLaunch (SPEC-LAUNCH-001 Rev D.2)
// Copyright (C) 2026 eNVy Systems, Inc.  All rights reserved.
// Licensed under eNVy Systems Non-Commercial License v1.1
//
// Project Architect: Timothy Peer
// AI Collaboration:  Claude (Anthropic)
//
// Version: 1.0.0-alpha   Date: 2026-07-29
// Spec:    Section 6, Section 7 W5
// ============================================================================

#include "PlatEdBridge.h"

#include <QDir>
#include <QFileInfo>
#include <QProcess>

#include "ExeDiscovery.h"

namespace launch {

PlatEdBridge::PlatEdBridge(QObject* parent) : QObject(parent)
{
    m_process = new QProcess(this);
    connect(m_process, &QProcess::finished, this, [this](int, QProcess::ExitStatus) {
        emit finished();
    });
    refresh();
}

void PlatEdBridge::refresh()
{
    ExeDiscovery::Result const r = ExeDiscovery::findPlatEd();
    m_exePath = r.path;
    m_reason  = r.found()
                    ? QString()
                    : QStringLiteral(
                          "PlatEd is not installed on this machine, so device "
                          "configuration cannot be opened from here. Everything "
                          "else in the launcher works without it.");
}

bool PlatEdBridge::isRunning() const
{
    return m_process && m_process->state() != QProcess::NotRunning;
}

bool PlatEdBridge::openManifest(QString const& manifestPath, QString* error)
{
    if (!isAvailable()) {
        if (error) *error = m_reason;
        return false;
    }
    if (isRunning()) {
        if (error) {
            *error = QStringLiteral("PlatEd is already open. Close it before "
                                    "opening another manifest.");
        }
        return false;
    }
    if (manifestPath.isEmpty() || !QFileInfo::exists(manifestPath)) {
        // Not a launcher failure: a system whose manifest has not been authored
        // yet is a normal state, and PlatEd is exactly the tool that fixes it.
        if (error) {
            *error = QStringLiteral(
                         "This system has no platform manifest yet (%1). Create "
                         "it in PlatEd, then return here.")
                         .arg(QDir::toNativeSeparators(manifestPath));
        }
        return false;
    }

    m_process->setProgram(m_exePath);
    m_process->setArguments({ QDir::toNativeSeparators(manifestPath) });
    m_process->setWorkingDirectory(QFileInfo(manifestPath).absolutePath());
    m_process->start();

    if (!m_process->waitForStarted(5000)) {
        if (error) {
            *error = QStringLiteral("Could not start PlatEd: %1")
                         .arg(m_process->errorString());
        }
        return false;
    }
    return true;
}

}  // namespace launch
