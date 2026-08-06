// ============================================================================
// src/main.cpp -- EmulatrLaunch entry point
// ============================================================================
// Project: EmulatR -- EmulatrLaunch (SPEC-LAUNCH-001 Rev D.2)
// Copyright (C) 2026 eNVy Systems, Inc.  All rights reserved.
// Licensed under eNVy Systems Non-Commercial License v1.1
//
// Project Architect: Timothy Peer
// AI Collaboration:  Claude (Anthropic)
//
// Version: 1.0.0-alpha   Date: 2026-07-29
// Spec:    Section 1, Section 6 ("never a modal error at startup"),
//          Section 11 E5 (two launcher instances are allowed).
// ============================================================================

#include <QApplication>

#include "LauncherWindow.h"
#include "Version.h"

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    // Identity feeds QSettings' default constructor and the window title.  The
    // hive itself is opened through launch::openSettings() so every TU lands on
    // the same one regardless of these defaults.
    QCoreApplication::setOrganizationName(QString::fromLatin1(launch::kOrgName));
    QCoreApplication::setOrganizationDomain(QString::fromLatin1(launch::kOrgDomain));
    QCoreApplication::setApplicationName(QString::fromLatin1(launch::kAppName));
    QCoreApplication::setApplicationVersion(QString::fromLatin1(launch::kAppVersion));

    // Section 10: "Plain native style, no custom theming in v1."  Nothing is
    // set here on purpose.

    // E5: a second instance is ALLOWED.  No single-instance guard -- QSettings
    // last-writer-wins is acceptable for v1, and single-flight launch is
    // per-instance by design.

    launch::LauncherWindow window;
    window.show();

    // Section 6: a missing Emulatr.exe or PlatEd disables an affordance and
    // explains itself in the status line.  It never raises a modal at startup,
    // so nothing is checked here before show().

    return app.exec();
}
