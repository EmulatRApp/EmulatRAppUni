// ============================================================================
// PlatformEditor/qt/main.cpp -- entry point (D-025 Qt Widgets frontend)
// ============================================================================
// Usage:
//   platedit_qt [manifest.json]
//   platedit_qt --selftest manifest.json      (headless acceptance: loads the
//       manifest offscreen, prints tree labels + finding counts, exits 0/1)
//
// Schema/catalog resolution order (first hit wins):
//   1. PLATEDIT_DATA_DIR environment variable
//   2. the executable's directory (deployed layout: schema/ + catalog/ beside)
//   3. the source tree (PLATEDIT_SOURCE_DIR compile definition; dev builds)
// ============================================================================

#include "MainWindow.h"

#include <QApplication>
#include <QDir>
#include <QFileInfo>

#include <cstdio>

using platedit::MainWindow;

static QString resolveDataDir() {
    const QByteArray env = qgetenv("PLATEDIT_DATA_DIR");
    if (!env.isEmpty() && QFileInfo::exists(QString::fromLocal8Bit(env) +
                                            "/schema/platform_schema.json"))
        return QString::fromLocal8Bit(env);
    const QString exeDir = QCoreApplication::applicationDirPath();
    if (QFileInfo::exists(exeDir + "/schema/platform_schema.json"))
        return exeDir;
#ifdef PLATEDIT_SOURCE_DIR
    if (QFileInfo::exists(QStringLiteral(PLATEDIT_SOURCE_DIR) +
                          QStringLiteral("/schema/platform_schema.json")))
        return QStringLiteral(PLATEDIT_SOURCE_DIR);
#endif
    return exeDir;   // last resort; MainWindow warns if policy fails to load
}

int main(int argc, char** argv) {
    QString manifest;
    bool selftest = false;
    for (int i = 1; i < argc; ++i) {
        const QString a = QString::fromLocal8Bit(argv[i]);
        if (a == QLatin1String("--selftest")) selftest = true;
        else manifest = a;
    }
    if (selftest) qputenv("QT_QPA_PLATFORM", "offscreen");

    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("EmulatR Platform Editor"));
    QApplication::setOrganizationName(QStringLiteral("eNVy Systems, Inc."));

    MainWindow w(resolveDataDir());

    if (selftest) {
        if (manifest.isEmpty() || !w.loadManifest(manifest, /*quiet=*/true)) {
            std::fprintf(stderr, "selftest: FAILED to load %s\n",
                         manifest.toLocal8Bit().constData());
            return 1;
        }
        const QStringList labels = w.topLabels(4);
        for (const QString& l : labels)
            std::printf("%s\n", l.toLocal8Bit().constData());
        std::printf("selftest: rows=%d findings=%d\n",
                    w.treeRowCount(), w.findingCount());
        return 0;
    }

    w.show();
    if (!manifest.isEmpty()) w.loadManifest(manifest);
    return app.exec();
}
