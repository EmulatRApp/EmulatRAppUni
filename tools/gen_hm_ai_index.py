#!/usr/bin/env python3
# ============================================================================
# tools/gen_hm_ai_index.py -- build the topic -> topic-file cross-reference
# ============================================================================
# Project: EmulatR -- Alpha AXP / EV6 Architecture Emulator (V5)
# Copyright (C) 2025, 2026 eNVy Systems, Inc.  All rights reserved.
# Licensed under eNVy Systems Non-Commercial License v1.1
#
# Project Architect: Timothy Peer
# AI Collaboration:  Claude (Anthropic)
# ============================================================================
#
# Reads the SINGLE SOURCE OF TRUTH -- the Help & Manual topic set
# (HMDocs/Topics/*.xml) and its table of contents (HMDocs/Maps/
# table_of_contents.xml) -- and emits a retrieval index at
#
#     HMDocs/Maps/ai_topic_index.md
#
# WHY THIS EXISTS.  A topic's FILENAME is a truncated, punctuation-mangled
# derivative of its title -- "Appendix DS20 / ES40 / ES45" is stored as
# `--Appendix-DS20-_-ES40-_-ES45.xml`.  So knowing which topic you want tells
# you almost nothing about which of the 160 files to open.  This index is the
# missing map in both directions: topic title -> topic file (section 1, the
# xref proper), and subject keyword -> topic file (section 4).  Sections 2-3
# add the TOC reading order and a health report over the same data.
#
# NOT PART OF THE H&M PROJECT (architect, 2026-08-11).  The output is a
# WORKING artifact for agents reading the documentation -- it is NOT a topic,
# and it must NOT be mapped into the project.  It is therefore absent from
# claudeRV4.hmxp and from table_of_contents.xml; since the .hmxp pulls in only
# `Maps/table_of_contents.xml`, a plain .md sitting in Maps/ is invisible to
# H&M and can neither be compiled into the guide nor shipped to end users.
# It lives in Maps/ purely to sit beside the topic set it describes.
#
# WRITE SCOPE.  This script writes EXACTLY ONE file, ai_topic_index.md, and it
# is the sole author of that file -- nothing else in the H&M tree is touched.
# Topic XML and the .hmxp are opened READ-ONLY.  The generated file is not a
# Help & Manual managed asset (it is a plain .md living beside file_index.md),
# so H&M will neither read nor rewrite it.
#
# SAFETY RAILS (mandatory for any H&M-tree write; see tools/sync_hm_version.py):
#   - refuse to run while Help & Manual holds the project open (lock file), so
#     we never index a half-saved topic set;
#   - back up the previous index into logs/ before overwriting;
#   - the inputs are parsed with a real XML parser, so a malformed topic is a
#     hard error rather than a silently-skipped row.
#
# USAGE
#     python tools/gen_hm_ai_index.py [--check] [--hmdocs <path>]
#
#     --check   parse and report, write nothing (exit 1 if the on-disk index
#               is stale).  Use this in a pre-commit or doc-sync check.
# ============================================================================

from __future__ import annotations

import argparse
import os
import re
import sys
import xml.etree.ElementTree as ET
from datetime import datetime, timezone

DEFAULT_HMDOCS = r"D:\emulatr\H&M\HMDocs"
LOG_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "logs")

INDEX_NAME = "ai_topic_index.md"
ABSTRACT_CHARS = 240


# ---------------------------------------------------------------------------
# safety rails
# ---------------------------------------------------------------------------
def find_lock(hmdocs: str) -> str | None:
    """Return the path of a Help & Manual lock file, if the app has the
    project open.  H&M drops a sidecar beside the .hmxp while editing."""
    for name in os.listdir(hmdocs):
        low = name.lower()
        if low.endswith((".lock", ".lck", ".~lock")) or low.startswith("~$"):
            return os.path.join(hmdocs, name)
    return None


def backup_previous(index_path: str) -> str | None:
    """Timestamped copy of the outgoing index into logs/, per house rule."""
    if not os.path.exists(index_path):
        return None
    os.makedirs(LOG_DIR, exist_ok=True)
    stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    dest = os.path.join(LOG_DIR, f"hm_index_backup_{stamp}_{INDEX_NAME}")
    with open(index_path, "rb") as src, open(dest, "wb") as dst:
        dst.write(src.read())
    return dest


# ---------------------------------------------------------------------------
# extraction
# ---------------------------------------------------------------------------
def text_of(node) -> str:
    """Flatten an H&M element's descendant text, collapsing whitespace."""
    return re.sub(r"\s+", " ", "".join(node.itertext())).strip()


def abstract_of(root) -> str:
    """First paragraph of real prose, as the topic's one-line abstract.

    Skips anything that is not prose ABOUT the topic: the <header> and any
    in-body Heading paragraph (both just restate the title, which tells a
    reader nothing they did not already have from the title column), pure-table
    paragraphs (flattened table text reads as noise), spacers, and AI TODO
    markers."""
    body = root.find("body")
    if body is None:
        return ""
    title = (root.findtext("title") or "").strip().lower()
    for para in body.iter("para"):
        if para.find("table") is not None:
            continue
        if (para.get("styleclass") or "").startswith("Heading"):
            continue
        txt = text_of(para)
        if len(txt) < 25:
            continue
        if txt.strip().lower().rstrip(".") == title.rstrip("."):
            continue
        if txt.startswith("<AI TODO>"):
            continue
        # rule-off dividers and banner lines ("REM ====...", "-----") carry no
        # meaning; a prose sentence is mostly letters and spaces
        letters = sum(c.isalnum() or c.isspace() for c in txt)
        if letters < 0.65 * len(txt):
            continue
        if len(txt) > ABSTRACT_CHARS:
            cut = txt.rfind(" ", 0, ABSTRACT_CHARS)
            txt = txt[: cut if cut > 0 else ABSTRACT_CHARS].rstrip(" ,;:") + "..."
        return txt
    return ""


def load_topics(topics_dir: str) -> dict:
    topics = {}
    for fname in sorted(os.listdir(topics_dir)):
        if not fname.lower().endswith(".xml"):
            continue
        path = os.path.join(topics_dir, fname)
        try:
            root = ET.parse(path).getroot()
        except ET.ParseError as exc:
            raise SystemExit(f"FATAL: {fname} is not well-formed XML: {exc}")
        href = fname[:-4]
        body = root.find("body")
        topics[href] = {
            "href": href,
            "title": (root.findtext("title") or href).strip(),
            "keywords": sorted(
                {k.text.strip() for k in root.iter("keyword") if k.text and k.text.strip()},
                key=str.lower,
            ),
            "abstract": abstract_of(root),
            "chars": len(text_of(body)) if body is not None else 0,
            "modified": (root.get("modified") or "")[:10],
        }
    return topics


def walk_toc(toc_path: str) -> list:
    """Flatten the TOC into (href, caption, depth, path) in reading order."""
    root = ET.parse(toc_path).getroot()
    out = []

    def walk(node, depth, trail):
        for child in node.findall("topicref"):
            caption = (child.findtext("caption") or "").strip()
            href = child.get("href", "")
            out.append(
                {
                    "href": href,
                    "caption": caption,
                    "depth": depth,
                    "path": " > ".join(trail + [caption]),
                }
            )
            walk(child, depth + 1, trail + [caption])

    walk(root, 0, [])
    return out


# ---------------------------------------------------------------------------
# rendering
# ---------------------------------------------------------------------------
def render(topics: dict, toc: list, hmdocs: str) -> str:
    placed = {e["href"] for e in toc}
    orphans = sorted(set(topics) - placed)
    missing = [e for e in toc if e["href"] not in topics]
    nokw = sorted(h for h in topics if not topics[h]["keywords"])

    kw_map = {}
    for href, t in topics.items():
        for kw in t["keywords"]:
            kw_map.setdefault(kw, []).append(href)

    generated = datetime.now(timezone.utc).strftime("%Y-%m-%d")
    L = []
    add = L.append

    esc = lambda s: s.replace("|", "\\|")
    section_of = {e["href"]: (e["path"].split(" > ")[0] if e["path"] else "")
                  for e in toc}

    add("# EmulatR — Topic Cross-Reference")
    add("")
    add("> **Not part of the Help & Manual project.** This is a working cross-")
    add("> reference *about* the guide, not a topic *in* it — it is deliberately")
    add("> absent from `claudeRV4.hmxp` and from `table_of_contents.xml`, so H&M")
    add("> neither compiles nor publishes it. **Do not map it into the project.**")
    add("> It lives here only to sit beside the topic set it describes.")
    add("")
    add("Maps a **documentation topic to the `.xml` topic-file that holds it**, so a")
    add("topic named in conversation can be opened directly instead of hunting")
    add("through `Topics/`. Filenames are truncated and punctuation-mangled versions")
    add("of their titles, which is exactly what makes the mapping worth writing down.")
    add("")
    add("**Resolve a topic → file:** find the title in §1, read `Topics/<file>`.")
    add("**Resolve a subject → topic:** find the term in §4, then look it up in §1.")
    add("")
    add("**Generated** by `tools/gen_hm_ai_index.py` on " + generated +
        " — do not hand-edit; regenerate.")
    add("")
    add(f"- Topic files: `HMDocs/Topics/` — {len(topics)} topics")
    add("- Table of contents: `HMDocs/Maps/table_of_contents.xml`")
    add(f"- Registered keywords: {sum(len(t['keywords']) for t in topics.values())} "
        f"instances across {len(kw_map)} unique terms")
    add("- Companion index: `file_index.md` (distribution fileset, not topics)")
    add("")
    add("---")
    add("")

    # ---- section 1: the xref proper, alphabetical ----------------------
    add("## 1. Topic → topic file")
    add("")
    add("Every topic, alphabetically by title. `Topic file` is relative to")
    add("`HMDocs/Topics/`. `Section` is the top-level chapter the topic sits under")
    add("in the TOC (`—` = not placed in the TOC).")
    add("")
    add("| Topic | Topic file | Section | Covers |")
    add("|---|---|---|---|")
    for href in sorted(topics, key=lambda h: topics[h]["title"].lower()):
        t = topics[href]
        sect = section_of.get(href) or "—"
        abstract = t["abstract"] or "_(no prose yet)_"
        add(f"| {esc(t['title'])} | `{href}.xml` | {esc(sect)} | {esc(abstract)} |")
    add("")
    add("---")
    add("")

    # ---- section 2: the same set, in TOC reading order -----------------
    add("## 2. Topic files, in table-of-contents order")
    add("")
    add("The same mapping in the manual's own reading order, so the structure of the")
    add("argument is visible: indentation mirrors TOC nesting.")
    add("")
    for entry in toc:
        href = entry["href"]
        t = topics.get(href)
        if entry["depth"] == 0:
            add("")
            add(f"### {esc(entry['caption'])}")
            add("")
            add("| Topic | Topic file | Keywords |")
            add("|---|---|---|")
        if t is None:
            add(f"| **{esc(entry['caption'])}** | `{href}.xml` "
                f"— **MISSING FILE** | — |")
            continue
        indent = "&nbsp;&nbsp;" * max(0, entry["depth"] - 1)
        kws = ", ".join(t["keywords"]) if t["keywords"] else "_none_"
        add(f"| {indent}{esc(t['title'])} | `{href}.xml` | {esc(kws)} |")
    add("")
    add("---")
    add("")

    # ---- section 3: orphans + gaps ------------------------------------
    add("## 3. Index health")
    add("")
    add(f"- Topics on disk: **{len(topics)}**")
    add(f"- Topics placed in the TOC: **{len(placed & set(topics))}**")
    add(f"- Topics with keywords: **{len(topics) - len(nokw)} / {len(topics)}**")
    add("")

    add("### Not in the table of contents")
    add("")
    if orphans:
        add("These topic files exist but no TOC entry points at them, so they render")
        add("into the build without a navigable path to reach them.")
        add("")
        add("| Topic | href | Keywords |")
        add("|---|---|---|")
        for href in orphans:
            t = topics[href]
            add(f"| {t['title']} | `{href}` | {len(t['keywords'])} |")
    else:
        add("None — every topic file is placed in the TOC.")
    add("")

    add("### TOC entries with no topic file")
    add("")
    if missing:
        add("| Caption | href |")
        add("|---|---|")
        for e in missing:
            add(f"| {e['caption']} | `{e['href']}` |")
    else:
        add("None — every TOC entry resolves to a topic file.")
    add("")

    add("### Topics not registered under any keyword")
    add("")
    if nokw:
        add("These are invisible to keyword search and unreachable via §4, so a")
        add("subject-first lookup will never surface them. Registering them is the")
        add("highest-value edit.")
        add("")
        add("| Topic | href | Body chars |")
        add("|---|---|---|")
        for href in nokw:
            t = topics[href]
            add(f"| {t['title']} | `{href}` | {t['chars']} |")
    else:
        add("None — every topic carries at least one keyword.")
    add("")
    add("---")
    add("")

    # ---- section 3: keyword -> topics ---------------------------------
    add("## 4. Keyword → topic file")
    add("")
    add("The subject-first way in. Every registered keyword and the topic file(s)")
    add("that claim it; a keyword claimed by several files marks a cross-cutting")
    add("concept. These are the same `<keywords>` H&M publishes as HTML")
    add("`<meta name=\"keywords\">`, so this table and the built guide agree.")
    add("")
    add("| Keyword | Topic file |")
    add("|---|---|")
    for kw in sorted(kw_map, key=str.lower):
        hrefs = ", ".join(f"`{h}.xml`" for h in sorted(kw_map[kw]))
        add(f"| {esc(kw)} | {hrefs} |")
    add("")

    return "\n".join(L) + "\n"


# ---------------------------------------------------------------------------
def main() -> int:
    ap = argparse.ArgumentParser(description="Generate the H&M AI topic index.")
    ap.add_argument("--hmdocs", default=DEFAULT_HMDOCS, help="HMDocs directory")
    ap.add_argument("--check", action="store_true",
                    help="report only; exit 1 if the on-disk index is stale")
    args = ap.parse_args()

    hmdocs = os.path.abspath(args.hmdocs)
    topics_dir = os.path.join(hmdocs, "Topics")
    maps_dir = os.path.join(hmdocs, "Maps")
    toc_path = os.path.join(maps_dir, "table_of_contents.xml")
    index_path = os.path.join(maps_dir, INDEX_NAME)

    for p in (topics_dir, toc_path):
        if not os.path.exists(p):
            print(f"FATAL: missing {p}", file=sys.stderr)
            return 2

    lock = find_lock(hmdocs)
    if lock:
        print(f"REFUSING: Help & Manual has the project open ({os.path.basename(lock)}).",
              file=sys.stderr)
        print("Close H&M and re-run, so the index reflects a fully-saved topic set.",
              file=sys.stderr)
        return 3

    topics = load_topics(topics_dir)
    toc = walk_toc(toc_path)
    rendered = render(topics, toc, hmdocs)

    existing = ""
    if os.path.exists(index_path):
        with open(index_path, "r", encoding="utf-8") as fh:
            existing = fh.read()

    # the generated-on line changes daily; compare everything else
    strip = lambda s: re.sub(r"^\*\*Generated\*\*.*$", "", s, flags=re.M)
    unchanged = strip(existing) == strip(rendered)

    print(f"topics: {len(topics)}   toc entries: {len(toc)}   "
          f"keywords: {sum(len(t['keywords']) for t in topics.values())}")

    if args.check:
        if unchanged:
            print("index is up to date")
            return 0
        print(f"STALE: {index_path} does not match the topic set", file=sys.stderr)
        return 1

    if unchanged:
        print("index already up to date; nothing written")
        return 0

    backup = backup_previous(index_path)
    if backup:
        print(f"backed up previous index -> {os.path.relpath(backup)}")

    with open(index_path, "w", encoding="utf-8", newline="\r\n") as fh:
        fh.write(rendered)
    print(f"wrote {index_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
