// ============================================================================
// src/IniOverlay.h -- whitelisted ini read/modify/write with byte preservation
// ============================================================================
// Project: EmulatR -- EmulatrLaunch (SPEC-LAUNCH-001 Rev D.2)
// Copyright (C) 2026 eNVy Systems, Inc.  All rights reserved.
// Licensed under eNVy Systems Non-Commercial License v1.1
//
// Project Architect: Timothy Peer
// AI Collaboration:  Claude (Anthropic)
//
// Version: 1.0.0-alpha   Date: 2026-07-29
// Spec:    Section 7 -- "a read-modify-write overlay that touches only
//          whitelisted keys and preserves every other line, ordering, and
//          comment verbatim".  Section 11 E4: safe to point at a dev run dir
//          with exotic settings.  Section 12: round-trips a dev-grade
//          Emulatr.ini with zero diff outside whitelisted keys.
//
// WHY NOT QSettings(IniFormat): it normalizes case, reorders keys, drops every
// comment, and rewrites escaping.  Pointing it at a hand-maintained
// Emulatr.ini -- which in this project carries load-bearing dated commentary
// about model/firmware coherence -- would destroy that commentary on the first
// write.  Hence a line-preserving overlay rather than a parser.
// ============================================================================

#ifndef EMULATRLAUNCH_INIOVERLAY_H
#define EMULATRLAUNCH_INIOVERLAY_H

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>

namespace launch {

class IniOverlay
{
public:
    IniOverlay() = default;

    // ---- load / save ------------------------------------------------------

    // Reads the file into the line model.  Returns false (with *error set) on
    // an unreadable path.  An EMPTY file is a valid load -- a system whose ini
    // has been truncated is repairable by writing keys into it.
    bool load(QString const& path, QString* error = nullptr);

    // Atomic write-back via QSaveFile.  A no-change save is byte-identical to
    // the loaded content, including the final-newline state and mixed CRLF/LF.
    bool save(QString* error = nullptr);

    bool    isLoaded() const { return m_loaded; }
    QString path()     const { return m_path; }
    bool    isDirty()  const { return m_dirty; }

    // ---- read -------------------------------------------------------------

    bool    hasKey(QString const& section, QString const& key) const;
    QString value(QString const& section, QString const& key,
                  QString const& defaultValue = QString()) const;
    int     intValue(QString const& section, QString const& key,
                     int defaultValue, bool* ok = nullptr) const;

    // ---- write (whitelisted keys only) ------------------------------------

    // Sets a key, creating the key and/or the section if absent.  Refuses --
    // and returns false -- for any (section, key) outside the whitelist: the
    // launcher must never be the reason an unowned key changed.
    bool setValue(QString const& section, QString const& key, QString const& value);

    static bool isWhitelisted(QString const& section, QString const& key);
    static QStringList whitelistDescription();

    // ---- diagnostics ------------------------------------------------------

    // Non-fatal observations from load(): a key outside any section, a
    // malformed section header.  Surfaced by preflight, never thrown away.
    QStringList warnings() const { return m_warnings; }

    // Exact bytes the overlay would write right now.  The round-trip test in
    // Section 12 compares this against the bytes read.
    QByteArray  serialize() const;

    // ---- the owned keys ---------------------------------------------------
    // Section 7 W1-W4.  Named constants so the UI, preflight, and the
    // whitelist cannot drift apart.
    static constexpr char const* kSecSystem     = "System";
    static constexpr char const* kKeyModel      = "model";
    static constexpr char const* kSecRom        = "ROM";
    static constexpr char const* kKeyFirmware   = "firmwareImage";
    static constexpr char const* kSecConsole    = "SRMConsole";
    static constexpr char const* kKeyPort       = "port";
    static constexpr char const* kSecStorage    = "Storage";
    static constexpr char const* kKeyDiskDir    = "diskDir";

private:
    // One physical line, split so the terminator is preserved exactly.  A file
    // with no trailing newline round-trips because the last Line has an empty
    // eol.
    struct Line
    {
        QByteArray text;      // without terminator
        QByteArray eol;       // "\r\n", "\n", or "" for a final unterminated line
        QString    section;   // section this line belongs to ("" = preamble)
        bool       isSection = false;
        QString    sectionName;   // when isSection
        bool       isKey     = false;
        QString    keyName;       // when isKey
        int        valueStart = -1;   // byte offsets into `text`
        int        valueEnd   = -1;   // exclusive; excludes trailing comment/ws
    };

    void  reindex();
    int   findKeyLine(QString const& section, QString const& key) const;
    int   findSectionHeader(QString const& section) const;
    int   lastContentLineOfSection(int headerIndex) const;

    QString     m_path;
    QList<Line> m_lines;
    QStringList m_warnings;
    bool        m_loaded = false;
    bool        m_dirty  = false;
};

}  // namespace launch

#endif  // EMULATRLAUNCH_INIOVERLAY_H
