// ============================================================================
// tests/test_launcher.cpp -- gate evidence for the UI-free translation units
// ============================================================================
// Project: EmulatR -- EmulatrLaunch (SPEC-LAUNCH-001 Rev D.2)
// Copyright (C) 2026 eNVy Systems, Inc.  All rights reserved.
// Licensed under eNVy Systems Non-Commercial License v1.1
//
// Project Architect: Timothy Peer
// AI Collaboration:  Claude (Anthropic)
//
// Version: 1.0.0-alpha   Date: 2026-07-29
// Spec:    Section 12 (acceptance criteria) and Section 13 G6, which names as
//          review evidence: the IniOverlay round-trip byte-preservation test,
//          denylist stripping verified by test, and DiskImageFactory output
//          against the format definition.
//
// No test framework: the launcher has no other use for one, and a dependency
// added for four assertions is a dependency a tester's build has to carry.
// Exit code 0 = all passed, 1 = at least one failed.
// ============================================================================

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QProcessEnvironment>
#include <QTemporaryDir>
#include <QTextStream>

#include "DiskImageFactory.h"
#include "EnvVarModel.h"
#include "FirmwareCheck.h"
#include "IniOverlay.h"
#include "RunDirSkeleton.h"
#include "SystemRecord.h"

using namespace launch;

namespace {

int g_failures = 0;
int g_checks   = 0;

QTextStream& out()
{
    static QTextStream s(stdout);
    return s;
}

void check(bool condition, QString const& what)
{
    ++g_checks;
    if (condition) {
        out() << "  PASS  " << what << "\n";
    } else {
        ++g_failures;
        out() << "  FAIL  " << what << "\n";
    }
    out().flush();
}

void checkEqual(QString const& actual, QString const& expected, QString const& what)
{
    bool const ok = actual == expected;
    check(ok, what);
    if (!ok) {
        out() << "        expected: " << expected << "\n"
              << "        actual  : " << actual << "\n";
        out().flush();
    }
}

void section(QString const& title)
{
    out() << "\n== " << title << "\n";
    out().flush();
}

bool writeFile(QString const& path, QByteArray const& bytes)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(bytes);
    f.close();
    return true;
}

QByteArray readFile(QString const& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    return f.readAll();
}

// ---------------------------------------------------------------------------
// A deliberately hostile ini: CRLF and LF mixed, comments in every position,
// a value with a trailing comment, exotic keys the launcher does not own, an
// unusual section, odd spacing, and NO trailing newline.  Section 11 E4 says
// the launcher must be safe to point at exactly this.
// ---------------------------------------------------------------------------
QByteArray hostileIni()
{
    QByteArray b;
    b += "; leading comment with CRLF\r\n";
    b += "# hash comment with LF\n";
    b += "\r\n";
    b += "[System]\r\n";
    b += "model       = DS20\r\n";
    b += "cpuCount    = 1   ; trailing comment must survive\r\n";
    b += "memorySize  = 4294967296   ; 4 GiB  (2026-07-03: raised from 1 GiB)\r\n";
    b += "\n";
    b += "[ExoticDevSection]\n";
    b += "someKeyTheLauncherDoesNotOwn = a;b\n";
    b += "  indentedKey\t=\tvalue with spaces  \n";
    b += "\r\n";
    b += "[ROM]\r\n";
    b += "firmwareImage = firmware/ds20_v7_3.exe\r\n";
    b += "firmwareSha256 =";                       // no trailing newline
    return b;
}

// ===========================================================================
void testIniRoundTrip(QString const& dir)
{
    section("IniOverlay -- round-trip byte preservation (G6, Section 12)");

    QString const path = QDir(dir).filePath(QStringLiteral("hostile.ini"));
    QByteArray const original = hostileIni();
    check(writeFile(path, original), "wrote the hostile fixture");

    IniOverlay ini;
    QString error;
    check(ini.load(path, &error), "loaded the hostile ini: " + error);

    check(ini.serialize() == original,
          "serialize() with no changes is byte-identical to the file read");

    check(ini.save(&error), "save() with no changes succeeded: " + error);
    check(readFile(path) == original,
          "a no-change save() leaves the file byte-identical on disk");

    // Reads
    checkEqual(ini.value(QStringLiteral("System"), QStringLiteral("model")),
               QStringLiteral("DS20"), "reads [System] model");
    checkEqual(ini.value(QStringLiteral("System"), QStringLiteral("memorySize")),
               QStringLiteral("4294967296"),
               "reads a value while excluding its trailing comment");
    checkEqual(ini.value(QStringLiteral("ExoticDevSection"),
                         QStringLiteral("someKeyTheLauncherDoesNotOwn")),
               QStringLiteral("a;b"),
               "a ';' not preceded by whitespace is part of the value, not a comment");
    checkEqual(ini.value(QStringLiteral("ExoticDevSection"), QStringLiteral("indentedKey")),
               QStringLiteral("value with spaces"),
               "reads a tab-separated, indented key");

    // Whitelist
    check(IniOverlay::isWhitelisted(QStringLiteral("ROM"), QStringLiteral("firmwareImage")),
          "[ROM] firmwareImage is whitelisted");
    check(!IniOverlay::isWhitelisted(QStringLiteral("System"), QStringLiteral("memorySize")),
          "[System] memorySize is NOT whitelisted");
    check(!ini.setValue(QStringLiteral("ExoticDevSection"),
                        QStringLiteral("someKeyTheLauncherDoesNotOwn"),
                        QStringLiteral("hijacked")),
          "setValue refuses a key outside the whitelist");
    check(ini.serialize() == original,
          "a refused setValue changed nothing");

    // Whitelisted write: exactly one line differs.
    check(ini.setValue(QStringLiteral("ROM"), QStringLiteral("firmwareImage"),
                       QStringLiteral("firmware/ds10_v7_3.exe")),
          "setValue accepts a whitelisted key");
    check(ini.save(&error), "saved the whitelisted change: " + error);

    QByteArray const after = readFile(path);
    QList<QByteArray> const beforeLines = original.split('\n');
    QList<QByteArray> const afterLines  = after.split('\n');
    check(beforeLines.size() == afterLines.size(),
          "the whitelisted write did not change the line count");

    int differing = 0;
    for (int i = 0; i < qMin(beforeLines.size(), afterLines.size()); ++i)
        if (beforeLines.at(i) != afterLines.at(i)) ++differing;
    check(differing == 1, QStringLiteral("exactly one line differs (found %1)").arg(differing));

    check(after.contains("firmware/ds10_v7_3.exe"), "the new firmware value is present");
    check(after.contains("; trailing comment must survive"), "trailing comments survived");
    check(after.contains("# hash comment with LF"), "hash comments survived");
    check(after.contains("[ExoticDevSection]"), "unknown sections survived");
    check(after.contains("someKeyTheLauncherDoesNotOwn = a;b"), "unknown keys survived");
    check(after.endsWith("firmwareSha256 ="),
          "the file still has no trailing newline (final-line state preserved)");
    check(after.contains("\r\n[System]"), "CRLF lines are still CRLF");
    check(after.contains("\n[ExoticDevSection]\n"), "LF-only lines are still LF");

    // Value replacement preserving the trailing comment on the SAME line.
    IniOverlay ini2;
    check(ini2.load(path, &error), "reloaded after write");
    check(ini2.setValue(QStringLiteral("SRMConsole"), QStringLiteral("port"),
                        QStringLiteral("10024")),
          "setValue creates a missing [SRMConsole] section");
    check(ini2.save(&error), "saved the created section: " + error);
    QByteArray const grown = readFile(path);
    check(grown.contains("[SRMConsole]"), "the new section was appended");
    check(grown.contains("port = 10024"), "the new key was written");
    check(grown.startsWith("; leading comment with CRLF"),
          "appending a section left the head of the file untouched");

    IniOverlay ini3;
    ini3.load(path, &error);
    checkEqual(QString::number(ini3.intValue(QStringLiteral("SRMConsole"),
                                             QStringLiteral("port"), -1)),
               QStringLiteral("10024"), "intValue reads the created port back");
}

// ===========================================================================
void testTemplateSeeding(QString const& dir)
{
    section("RunDirSkeleton -- skeleton creation and the seeded template");

    QString const runDir = QDir(dir).filePath(QStringLiteral("ds20_run"));
    QString error;
    check(RunDirSkeleton::create(runDir, Platform::DS20, 10023, &error),
          "created a DS20 run-dir skeleton: " + error);

    QDir const d(runDir);
    check(QFile::exists(d.filePath(QStringLiteral("Emulatr.ini"))), "Emulatr.ini seeded");
    check(d.exists(QStringLiteral("firmware")), "firmware/ created");
    check(d.exists(QStringLiteral("disks")), "disks/ created");
    check(d.exists(QStringLiteral("logs")), "logs/ created");
    check(d.exists(QStringLiteral("traces")), "traces/ created");
    check(QFile::exists(d.filePath(QStringLiteral("firmware/firmware_readme.txt"))),
          "firmware_readme.txt dropped for the tester");

    IniOverlay ini;
    check(ini.load(d.filePath(QStringLiteral("Emulatr.ini")), &error),
          "the seeded ini parses: " + error);
    checkEqual(ini.value(QStringLiteral("System"), QStringLiteral("model")),
               QStringLiteral("DS20"), "the platform token was substituted");
    checkEqual(ini.value(QStringLiteral("SRMConsole"), QStringLiteral("port")),
               QStringLiteral("10023"), "the console port token was substituted");
    check(!ini.serialize().contains("@MODEL@"), "no template tokens left behind");
    check(!ini.serialize().contains("@CONSOLEPORT@"), "no port token left behind");
    check(ini.warnings().isEmpty(),
          QStringLiteral("the seeded ini parses without warnings (%1)")
              .arg(ini.warnings().join(QStringLiteral("; "))));

    // Never overwrite an existing configured system.
    QString second;
    check(!RunDirSkeleton::create(runDir, Platform::DS10, 10024, &second),
          "create() refuses to overwrite an existing Emulatr.ini");

    // Platform inference (M5) round-trips off the seeded ini.
    QString how;
    bool ambiguous = false;
    check(RunDirSkeleton::inferPlatform(runDir, &how, &ambiguous) == Platform::DS20,
          "inferPlatform reads DS20 back out of the seeded directory");
    check(!ambiguous, "inference is unambiguous for a freshly seeded directory");

    // R2.
    check(RunDirSkeleton::isUnderProgramFiles(QStringLiteral("C:/Program Files/x/run")),
          "isUnderProgramFiles catches a Program Files path");
    check(!RunDirSkeleton::isUnderProgramFiles(runDir),
          "a Documents-style path is not flagged");
}

// ===========================================================================
void testGeometryTable(QString const& dir)
{
    section("DiskImageFactory -- the verified table and the G3 container format");

    QString tableNote;
    QList<DiskImageFactory::Geometry> const table = DiskImageFactory::loadTable(&tableNote);
    check(table.size() == 28,
          QStringLiteral("the table has the documented 28 drive models (found %1)")
              .arg(table.size()));
    if (!tableNote.isEmpty()) out() << "        table note: " << tableNote << "\n";

    // Spot-check against the core tree's own values.
    DiskImageFactory::Geometry rz29;
    for (DiskImageFactory::Geometry const& g : table)
        if (g.model == QLatin1String("RZ29")) rz29 = g;
    check(rz29.isValid(), "RZ29 is present");
    check(rz29.totalLbn == 8407200, "RZ29 total_lbn matches the core table");
    check(rz29.imageSizeBytes() == 8407200LL * 512,
          "RZ29 container size is total_lbn * block_bytes exactly");

    DiskImageFactory::Geometry ra82;
    for (DiskImageFactory::Geometry const& g : table)
        if (g.model == QLatin1String("RA82")) ra82 = g;
    check(ra82.isValid() && ra82.secTrk * ra82.heads * ra82.cyl == ra82.totalLbn,
          "RA82 CHS multiplies out to its total_lbn");

    // Actually create one and verify the format: a raw flat file of exactly
    // that many bytes, with no header.
    QString error;
    DiskImageFactory::Geometry const small =
        DiskImageFactory::customGeometry(64, 4, 32, 512, &error);
    check(small.isValid(), "a custom geometry validates: " + error);
    check(small.imageSizeBytes() == 64LL * 4 * 32 * 512, "custom size multiplies out");

    QString const img = QDir(dir).filePath(QStringLiteral("test_disk.img"));
    check(DiskImageFactory::create(img, small, &error), "created a container: " + error);
    check(QFile(img).size() == small.imageSizeBytes(),
          "the container is exactly total_lbn * block_bytes on disk");

    // Raw flat means the first bytes are data, not a signature.
    QFile f(img);
    f.open(QIODevice::ReadOnly);
    QByteArray const head = f.read(64);
    f.close();
    check(head == QByteArray(64, '\0'),
          "the container reads back as zeros -- raw flat, no header (G3)");

    check(!DiskImageFactory::create(img, small, &error),
          "create() refuses to overwrite an existing image");

    // Sanity bounds.
    QString boundsError;
    check(!DiskImageFactory::customGeometry(0, 4, 32, 512, &boundsError).isValid(),
          "zero cylinders is rejected");
    check(!DiskImageFactory::customGeometry(64, 4, 32, 4096, &boundsError).isValid(),
          "an unsupported block size is rejected");

    // Name validation.
    QString reason;
    check(DiskImageFactory::isValidNameStem(QStringLiteral("system_disk"), &reason),
          "a plain stem is accepted");
    check(!DiskImageFactory::isValidNameStem(QStringLiteral("bad/name"), &reason),
          "a path separator is rejected");
    check(!DiskImageFactory::isValidNameStem(QStringLiteral("con"), &reason),
          "a reserved Windows device name is rejected");
    check(!DiskImageFactory::isValidNameStem(QString(), &reason),
          "an empty stem is rejected");
}

// ===========================================================================
void testEnvDenylist()
{
    section("EnvVarModel -- absent-is-not-empty and denylist stripping (G6)");

    EnvVarModel model;
    QString error;
    model.loadRegistry(&error);
    if (!error.isEmpty()) out() << "        registry note: " << error << "\n";

    check(model.deniedNames().contains(QStringLiteral("EMULATR_RSCCWARP")),
          "EMULATR_RSCCWARP is in the denied tier");

    // A denied variable must never be offered in the table, even with the
    // developer toggle on.
    model.setShowDeveloperVariables(true);
    bool visible = false;
    for (int r = 0; r < model.rowCount(); ++r) {
        EnvVarDef const* d = model.defForRow(r);
        if (d && d->name == QLatin1String("EMULATR_RSCCWARP")) visible = true;
    }
    check(!visible, "a denied variable is never displayed, even to developers");
    model.setShowDeveloperVariables(false);

    // The load-bearing case: it is set in the LAUNCHER's own environment.
    QProcessEnvironment inherited;
    inherited.insert(QStringLiteral("PATH"), QStringLiteral("C:\\Windows"));
    inherited.insert(QStringLiteral("EMULATR_RSCCWARP"), QStringLiteral("1"));

    QProcessEnvironment const child = model.composeEnvironment(inherited);
    check(!child.contains(QStringLiteral("EMULATR_RSCCWARP")),
          "a denied variable is stripped from the child environment");
    check(child.contains(QStringLiteral("PATH")),
          "unrelated inherited variables are preserved");
    check(model.strippedFrom(inherited).contains(QStringLiteral("EMULATR_RSCCWARP")),
          "the stripped variable is reported for the log head");

    // Absent is not empty: an unchecked row must not appear at all.
    check(model.effectiveSelectionForLog().isEmpty(),
          "nothing is enabled by default, so the log head records none");
}

// ===========================================================================
void testFirmwareCheck(QString const& dir)
{
    section("FirmwareCheck -- platform fit is a warning, never a refusal");

    QString const fw = QDir(dir).filePath(QStringLiteral("fwtest"));
    QDir().mkpath(fw);
    writeFile(QDir(fw).filePath(QStringLiteral("ds20_v7_3.exe")), QByteArray(16, 'x'));
    writeFile(QDir(fw).filePath(QStringLiteral("es40_cl67.exe")), QByteArray(16, 'x'));
    writeFile(QDir(fw).filePath(QStringLiteral("mystery_rom.bin")), QByteArray(16, 'x'));
    writeFile(QDir(fw).filePath(QStringLiteral("firmware_readme.txt")), QByteArray("hi"));

    QList<FirmwareCheck::Candidate> const found = FirmwareCheck::scan(fw, Platform::DS20);
    check(found.size() == 3, QStringLiteral("the readme is excluded from candidates (%1)")
                                 .arg(found.size()));

    int match = 0, mismatch = 0, unknown = 0;
    for (FirmwareCheck::Candidate const& c : found) {
        if (c.match == FirmwareCheck::Match::Match)         ++match;
        else if (c.match == FirmwareCheck::Match::Mismatch) ++mismatch;
        else                                                ++unknown;

        if (c.fileName == QLatin1String("ds20_v7_3.exe")) {
            check(c.format == FirmwareCheck::Format::Srm,
                  ".exe is classified as an SRM-format image (core convention)");
            checkEqual(c.relativePath, QStringLiteral("firmware/ds20_v7_3.exe"),
                       "the relative path is what goes in the ini");
        }
        if (c.fileName == QLatin1String("mystery_rom.bin"))
            check(c.format == FirmwareCheck::Format::Raw,
                  "a non-.exe image is classified as a raw ROM dump");
    }
    check(match == 1, "the DS20 image matches");
    check(mismatch == 1, "the ES40 image is flagged as a mismatch");
    check(unknown == 1, "the unnamed image is unknown, not rejected");
}

}  // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);

    QTemporaryDir tmp;
    if (!tmp.isValid()) {
        out() << "could not create a temporary directory\n";
        return 1;
    }

    out() << "EmulatrLaunch test run -- SPEC-LAUNCH-001 Rev D.2 gate evidence\n";
    out() << "scratch: " << tmp.path() << "\n";

    testIniRoundTrip(tmp.path());
    testTemplateSeeding(tmp.path());
    testGeometryTable(tmp.path());
    testEnvDenylist();
    testFirmwareCheck(tmp.path());

    out() << "\n----------------------------------------------------------------\n";
    out() << (g_failures == 0 ? "ALL PASSED" : "FAILURES") << "   "
          << (g_checks - g_failures) << "/" << g_checks << " checks passed\n";
    out().flush();
    return g_failures == 0 ? 0 : 1;
}
