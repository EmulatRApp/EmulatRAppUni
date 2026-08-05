#!/usr/bin/env python3
# ============================================================================
# tools/sync_hm_version.py -- mirror the CMake version into the H&M project
# ============================================================================
# Project: EmulatR -- Alpha AXP / EV6 Architecture Emulator (V5)
# Copyright (C) 2025, 2026 eNVy Systems, Inc.  All rights reserved.
# Licensed under eNVy Systems Non-Commercial License v1.1
#
# Project Architect: Timothy Peer
# AI Collaboration:  Claude (Anthropic)
# ============================================================================
#
# Reads the SINGLE SOURCE OF TRUTH -- project(Emulatr VERSION <maj>.<min>.<bld>)
# in the root CMakeLists.txt -- and mirrors the three numbers into the Help &
# Manual project's config-values:
#
#     <config-value name="versionmajor">1</config-value>
#     <config-value name="versionminor">5</config-value>
#     <config-value name="versionbuild">12</config-value>
#
# SCOPE (architect, 2026-08-04): H&M files under the EmulatR tree are
# READ/WRITE -- direct edits are permitted.  (This supersedes the earlier rule
# that treated the H&M tree as never-edited-by-tooling and made this script a
# lone sanctioned exception.)  The POLICY is lifted; this script's write scope
# is still DELIBERATELY NARROW: EXACTLY the three elements above, in the ONE
# file below.  It must never touch anything else in the .hmxp -- a version
# mirror has no business rewriting topic content, and that narrow scope is
# what makes the safety rails below sufficient.  The rails themselves (lock
# check, backup, byte-preserving patch) remain MANDATORY for any H&M write,
# by tooling or by hand: they guard against the app clobbering us on its next
# save, not against a policy.
#
# Safety rails:
#   - byte-level regex patch: UTF-8 BOM and line endings preserved verbatim
#   - idempotent: no write (and no backup) when the values already match
#   - each element must match EXACTLY once, else hard error (no partial write)
#   - refuses to write while an H&M lock file sits beside the project
#   - timestamped backup into <repo>/logs/ before any write
#
# Exit codes:  0 = in sync / updated / SKIP (hmxp absent, e.g. non-Windows host)
#              3 = H&M project locked (open in Help & Manual) -- retry later
#              4 = structural error (version unparseable, element count != 1,
#                  write/verify failure) -- investigate before rebuilding docs
#
# Usage: sync_hm_version.py [--cmake <CMakeLists.txt>] [--hmxp <claudeRV4.hmxp>]
#                           [--check-only]
# ============================================================================

import argparse
import re
import shutil
import sys
import time
from pathlib import Path

DEF_HMXP = Path(r"D:\EmulatR\H&M\HMDocs\claudeRV4.hmxp")
REPO = Path(__file__).resolve().parent.parent
DEF_CMAKE = REPO / "CMakeLists.txt"

FIELDS = ("versionmajor", "versionminor", "versionbuild")


def parse_cmake_version(cmake_path):
    text = cmake_path.read_text(encoding="utf-8", errors="replace")
    m = re.search(r"^project\(\s*Emulatr\s+VERSION\s+(\d+)\.(\d+)\.(\d+)",
                  text, re.MULTILINE)
    if not m:
        return None
    return m.group(1), m.group(2), m.group(3)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cmake", type=Path, default=DEF_CMAKE)
    ap.add_argument("--hmxp", type=Path, default=DEF_HMXP)
    ap.add_argument("--check-only", action="store_true")
    args = ap.parse_args()

    ver = parse_cmake_version(args.cmake)
    if ver is None:
        print("HM-VERSION-SYNC ERROR: no 'project(Emulatr VERSION x.y.z' line "
              "in %s" % args.cmake)
        return 4
    major, minor, build = ver

    if not args.hmxp.exists():
        print("HM-VERSION-SYNC SKIP: H&M project not present at %s "
              "(non-doc host?)" % args.hmxp)
        return 0

    # H&M lock convention: <name>.lck / <name>.hmxp.lck beside the project.
    for lck in (args.hmxp.with_suffix(args.hmxp.suffix + ".lck"),
                args.hmxp.with_suffix(".lck")):
        if lck.exists():
            print("HM-VERSION-SYNC LOCKED: %s exists -- close the project in "
                  "Help & Manual and re-run" % lck.name)
            return 3

    raw = args.hmxp.read_bytes()
    want = {"versionmajor": major, "versionminor": minor, "versionbuild": build}
    patched = raw
    changed = []
    for name, value in want.items():
        pat = re.compile(br'(<config-value name="' + name.encode("ascii") +
                         br'">)(\d+)(</config-value>)')
        hits = pat.findall(patched)
        if len(hits) != 1:
            print("HM-VERSION-SYNC ERROR: '%s' matched %d times (need exactly "
                  "1) in %s -- no write performed" %
                  (name, len(hits), args.hmxp.name))
            return 4
        cur = hits[0][1].decode("ascii")
        if cur != value:
            changed.append("%s %s->%s" % (name, cur, value))
            patched = pat.sub(br"\g<1>" + value.encode("ascii") + br"\g<3>",
                              patched, count=1)

    if not changed:
        print("HM-VERSION-SYNC OK: H&M already at %s.%s.%s (no write)" %
              (major, minor, build))
        return 0

    if args.check_only:
        print("HM-VERSION-SYNC DRIFT (check-only): %s" % ", ".join(changed))
        return 0

    logs = REPO / "logs"
    logs.mkdir(exist_ok=True)
    backup = logs / ("hm_version_backup_%s.hmxp" %
                     time.strftime("%Y%m%d_%H%M%S"))
    shutil.copy2(args.hmxp, backup)
    args.hmxp.write_bytes(patched)

    # Verify the write landed byte-for-byte.
    if args.hmxp.read_bytes() != patched:
        print("HM-VERSION-SYNC ERROR: post-write verify FAILED -- restore "
              "from %s" % backup)
        return 4
    print("HM-VERSION-SYNC UPDATED: %s (backup: %s)" %
          (", ".join(changed), backup.name))
    return 0


if __name__ == "__main__":
    sys.exit(main())
