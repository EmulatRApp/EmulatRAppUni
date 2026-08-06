// ============================================================================
// src/IniOverlay.cpp -- whitelisted ini read/modify/write with byte preservation
// ============================================================================
// Project: EmulatR -- EmulatrLaunch (SPEC-LAUNCH-001 Rev D.2)
// Copyright (C) 2026 eNVy Systems, Inc.  All rights reserved.
// Licensed under eNVy Systems Non-Commercial License v1.1
//
// Project Architect: Timothy Peer
// AI Collaboration:  Claude (Anthropic)
//
// Version: 1.0.0-alpha   Date: 2026-07-29
// Spec:    Section 7, Section 11 E3/E4, Section 12 (round-trip criterion)
// ============================================================================

#include "IniOverlay.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

namespace launch {

namespace {

bool isCommentStart(char c) { return c == ';' || c == '#'; }

// Trailing-comment scan.  A ';' or '#' begins a comment only when it follows
// whitespace or opens the value -- so a value like "a;b" survives, while the
// house idiom "memorySize = 4294967296   ; 4 GiB" is recognized.
int valueEndBeforeComment(QByteArray const& text, int from)
{
    bool prevWasSpace = true;   // start-of-value counts as a boundary
    for (int i = from; i < text.size(); ++i) {
        char const c = text.at(i);
        if (isCommentStart(c) && prevWasSpace) return i;
        prevWasSpace = (c == ' ' || c == '\t');
    }
    return text.size();
}

int trimEndBack(QByteArray const& text, int end, int floor)
{
    while (end > floor) {
        char const c = text.at(end - 1);
        if (c == ' ' || c == '\t') { --end; continue; }
        break;
    }
    return end;
}

bool sameName(QString const& a, QString const& b)
{
    return a.compare(b, Qt::CaseInsensitive) == 0;
}

}  // namespace

// ---------------------------------------------------------------------------
// load
// ---------------------------------------------------------------------------
bool IniOverlay::load(QString const& path, QString* error)
{
    m_lines.clear();
    m_warnings.clear();
    m_loaded = false;
    m_dirty  = false;
    m_path   = path;

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error) {
            *error = QStringLiteral("cannot read %1: %2")
                         .arg(QDir::toNativeSeparators(path), f.errorString());
        }
        return false;
    }
    QByteArray const bytes = f.readAll();
    f.close();

    // Split preserving each terminator exactly.  Mixed CRLF/LF in one file is
    // preserved per line rather than normalized.
    int i = 0;
    while (i < bytes.size()) {
        int nl = bytes.indexOf('\n', i);
        Line ln;
        if (nl < 0) {
            ln.text = bytes.mid(i);           // final line, unterminated
            ln.eol  = QByteArray();
            i = bytes.size();
        } else {
            int textEnd = nl;
            if (textEnd > i && bytes.at(textEnd - 1) == '\r') --textEnd;
            ln.text = bytes.mid(i, textEnd - i);
            ln.eol  = bytes.mid(textEnd, nl - textEnd + 1);   // "\r\n" or "\n"
            i = nl + 1;
        }
        m_lines.append(ln);
    }

    reindex();
    m_loaded = true;
    return true;
}

// ---------------------------------------------------------------------------
// reindex -- classify every line and record value spans
// ---------------------------------------------------------------------------
void IniOverlay::reindex()
{
    QString current;
    for (int idx = 0; idx < m_lines.size(); ++idx) {
        Line& ln = m_lines[idx];
        ln.isSection  = false;
        ln.isKey      = false;
        ln.keyName.clear();
        ln.sectionName.clear();
        ln.valueStart = ln.valueEnd = -1;
        ln.section    = current;

        QByteArray const& t = ln.text;
        int p = 0;
        while (p < t.size() && (t.at(p) == ' ' || t.at(p) == '\t')) ++p;
        if (p >= t.size()) continue;                       // blank
        if (isCommentStart(t.at(p))) continue;             // whole-line comment

        if (t.at(p) == '[') {
            int const close = t.indexOf(']', p + 1);
            if (close < 0) {
                m_warnings.append(
                    QStringLiteral("line %1: section header has no closing ']'; "
                                   "left verbatim").arg(idx + 1));
                continue;
            }
            current        = QString::fromUtf8(t.mid(p + 1, close - p - 1)).trimmed();
            ln.isSection   = true;
            ln.sectionName = current;
            ln.section     = current;
            continue;
        }

        int const eq = t.indexOf('=', p);
        if (eq < 0) {
            m_warnings.append(
                QStringLiteral("line %1: not a comment, section, or key=value; "
                               "left verbatim").arg(idx + 1));
            continue;
        }

        int keyEnd = trimEndBack(t, eq, p);
        if (keyEnd <= p) {
            m_warnings.append(
                QStringLiteral("line %1: empty key name; left verbatim").arg(idx + 1));
            continue;
        }
        ln.isKey   = true;
        ln.keyName = QString::fromUtf8(t.mid(p, keyEnd - p));

        int vs = eq + 1;
        while (vs < t.size() && (t.at(vs) == ' ' || t.at(vs) == '\t')) ++vs;
        int ve = valueEndBeforeComment(t, vs);
        ve = trimEndBack(t, ve, vs);
        ln.valueStart = vs;
        ln.valueEnd   = ve;

        if (current.isEmpty()) {
            m_warnings.append(
                QStringLiteral("line %1: key '%2' appears before any [section]; "
                               "left verbatim").arg(idx + 1).arg(ln.keyName));
        }
    }
}

// ---------------------------------------------------------------------------
// lookups
// ---------------------------------------------------------------------------
int IniOverlay::findKeyLine(QString const& section, QString const& key) const
{
    for (int i = 0; i < m_lines.size(); ++i) {
        Line const& ln = m_lines.at(i);
        if (ln.isKey && sameName(ln.section, section) && sameName(ln.keyName, key))
            return i;
    }
    return -1;
}

int IniOverlay::findSectionHeader(QString const& section) const
{
    for (int i = 0; i < m_lines.size(); ++i) {
        if (m_lines.at(i).isSection && sameName(m_lines.at(i).sectionName, section))
            return i;
    }
    return -1;
}

// Last line that still belongs to the section -- used as the insertion point
// for a new key.  Trailing blank lines are excluded so an inserted key lands
// snug under the section's existing content rather than after its blank
// separator.
int IniOverlay::lastContentLineOfSection(int headerIndex) const
{
    int last = headerIndex;
    for (int i = headerIndex + 1; i < m_lines.size(); ++i) {
        if (m_lines.at(i).isSection) break;
        if (!m_lines.at(i).text.trimmed().isEmpty()) last = i;
    }
    return last;
}

bool IniOverlay::hasKey(QString const& section, QString const& key) const
{
    return findKeyLine(section, key) >= 0;
}

QString IniOverlay::value(QString const& section, QString const& key,
                          QString const& defaultValue) const
{
    int const i = findKeyLine(section, key);
    if (i < 0) return defaultValue;
    Line const& ln = m_lines.at(i);
    return QString::fromUtf8(ln.text.mid(ln.valueStart, ln.valueEnd - ln.valueStart));
}

int IniOverlay::intValue(QString const& section, QString const& key,
                         int defaultValue, bool* ok) const
{
    QString const s = value(section, key);
    if (s.isEmpty()) { if (ok) *ok = false; return defaultValue; }
    bool good = false;
    // Accept the house 0x form as well as decimal.
    int const v = s.startsWith(QLatin1String("0x"), Qt::CaseInsensitive)
                      ? s.mid(2).toInt(&good, 16)
                      : s.toInt(&good, 10);
    if (ok) *ok = good;
    return good ? v : defaultValue;
}

// ---------------------------------------------------------------------------
// whitelist
// ---------------------------------------------------------------------------
bool IniOverlay::isWhitelisted(QString const& section, QString const& key)
{
    struct Pair { char const* sec; char const* key; };
    static Pair const kOwned[] = {
        { kSecSystem,  kKeyModel    },
        { kSecRom,     kKeyFirmware },
        { kSecConsole, kKeyPort     },
        { kSecStorage, kKeyDiskDir  },
    };
    for (Pair const& p : kOwned) {
        if (sameName(section, QString::fromLatin1(p.sec))
            && sameName(key, QString::fromLatin1(p.key)))
            return true;
    }
    return false;
}

QStringList IniOverlay::whitelistDescription()
{
    return {
        QStringLiteral("[System] model"),
        QStringLiteral("[ROM] firmwareImage"),
        QStringLiteral("[SRMConsole] port"),
        QStringLiteral("[Storage] diskDir"),
    };
}

// ---------------------------------------------------------------------------
// setValue
// ---------------------------------------------------------------------------
bool IniOverlay::setValue(QString const& section, QString const& key,
                          QString const& newValue)
{
    if (!isWhitelisted(section, key)) return false;
    if (!m_loaded) return false;

    QByteArray const v = newValue.toUtf8();

    int const li = findKeyLine(section, key);
    if (li >= 0) {
        Line& ln = m_lines[li];
        QByteArray const existing = ln.text.mid(ln.valueStart, ln.valueEnd - ln.valueStart);
        if (existing == v) return true;                    // no-op: stay clean
        ln.text.replace(ln.valueStart, ln.valueEnd - ln.valueStart, v);
        ln.valueEnd = ln.valueStart + v.size();
        m_dirty = true;
        return true;
    }

    // Key absent.  Choose an EOL that matches the file rather than the host:
    // a run dir edited on another machine keeps its own convention.
    QByteArray eol = QByteArrayLiteral("\r\n");
    for (Line const& ln : m_lines) {
        if (!ln.eol.isEmpty()) { eol = ln.eol; break; }
    }

    Line entry;
    entry.text = key.toUtf8() + QByteArrayLiteral(" = ") + v;
    entry.eol  = eol;

    int const hdr = findSectionHeader(section);
    if (hdr >= 0) {
        int const at = lastContentLineOfSection(hdr);
        // The line we insert after may be the file's final unterminated line;
        // give it a terminator or the new key would fuse onto it.
        if (m_lines[at].eol.isEmpty()) m_lines[at].eol = eol;
        m_lines.insert(at + 1, entry);
    } else {
        if (!m_lines.isEmpty() && m_lines.last().eol.isEmpty())
            m_lines.last().eol = eol;
        if (!m_lines.isEmpty() && !m_lines.last().text.trimmed().isEmpty()) {
            Line blank; blank.eol = eol;
            m_lines.append(blank);
        }
        Line header;
        header.text = QByteArrayLiteral("[") + section.toUtf8() + QByteArrayLiteral("]");
        header.eol  = eol;
        m_lines.append(header);
        m_lines.append(entry);
    }

    reindex();
    m_dirty = true;
    return true;
}

// ---------------------------------------------------------------------------
// serialize / save
// ---------------------------------------------------------------------------
QByteArray IniOverlay::serialize() const
{
    QByteArray out;
    int total = 0;
    for (Line const& ln : m_lines) total += ln.text.size() + ln.eol.size();
    out.reserve(total);
    for (Line const& ln : m_lines) { out += ln.text; out += ln.eol; }
    return out;
}

bool IniOverlay::save(QString* error)
{
    if (!m_loaded) {
        if (error) *error = QStringLiteral("no ini loaded");
        return false;
    }

    QSaveFile f(m_path);
    if (!f.open(QIODevice::WriteOnly)) {
        // E3: never silently drop a setting -- name the path and the reason.
        if (error) {
            *error = QStringLiteral("cannot write %1: %2")
                         .arg(QDir::toNativeSeparators(m_path), f.errorString());
        }
        return false;
    }
    QByteArray const bytes = serialize();
    if (f.write(bytes) != bytes.size() || !f.commit()) {
        if (error) {
            *error = QStringLiteral("write failed for %1: %2")
                         .arg(QDir::toNativeSeparators(m_path), f.errorString());
        }
        return false;
    }
    m_dirty = false;
    return true;
}

}  // namespace launch
