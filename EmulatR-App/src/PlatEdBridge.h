// ============================================================================
// src/PlatEdBridge.h -- PlatEd discovery and invocation
// ============================================================================
// Project: EmulatR -- EmulatrLaunch (SPEC-LAUNCH-001 Rev D.2)
// Copyright (C) 2026 eNVy Systems, Inc.  All rights reserved.
// Licensed under eNVy Systems Non-Commercial License v1.1
//
// Project Architect: Timothy Peer
// AI Collaboration:  Claude (Anthropic)
//
// Version: 1.0.0-alpha   Date: 2026-07-29
// Spec:    Section 2, Section 7 W5, Section 10 D2/D3
//
// DEVICE CONFIGURATION IS DELEGATED, NOT DUPLICATED.  PlatEd owns device
// topology, manifest authoring, and the SPEC-SCSIH-001 vDisk hierarchy.  This
// launcher NEVER writes a manifest and reads one only as far as preflight
// requires.  Re-scoping manifest editing into the launcher would fork it
// across two applications, which the spec explicitly rejects.
//
// PlatEd is a SEPARATE PROCESS (embedding it as an in-process widget is
// deferred, Section 15).  With PlatEd absent, only this one affordance
// disables -- the launcher must ship and function before PlatEd does.
// ============================================================================

#ifndef EMULATRLAUNCH_PLATEDBRIDGE_H
#define EMULATRLAUNCH_PLATEDBRIDGE_H

#include <QObject>
#include <QString>

class QProcess;

namespace launch {

class PlatEdBridge : public QObject
{
    Q_OBJECT

public:
    explicit PlatEdBridge(QObject* parent = nullptr);

    // Re-runs discovery.  Cheap; called when Settings change.
    void refresh();

    bool    isAvailable() const { return !m_exePath.isEmpty(); }
    QString exePath() const { return m_exePath; }

    // One line explaining why the affordance is disabled, for the status area.
    QString unavailableReason() const { return m_reason; }

    bool isRunning() const;

    // Spawns PlatEd on `manifestPath`.  Returns false with *error when PlatEd
    // is absent, already running, or the manifest does not exist.
    bool openManifest(QString const& manifestPath, QString* error);

signals:
    // W5: "On PlatEd exit, the launcher re-runs preflight so manifest changes
    // are revalidated before Start."  This is why PlatEd is spawned as a
    // watched child rather than detached.
    void finished();

private:
    QProcess* m_process = nullptr;
    QString   m_exePath;
    QString   m_reason;
};

}  // namespace launch

#endif  // EMULATRLAUNCH_PLATEDBRIDGE_H
