#!/usr/bin/env python3
# ============================================================================
# tools/gen_env_registry.py -- G2b census: the EMULATR_* runtime registry
# ============================================================================
# Project: EmulatR -- Alpha AXP / EV6 Architecture Emulator (V5).
# Architect: Timothy Peer.  AI collaboration: Claude (Anthropic).
# Copyright (C) 2026 eNVy Systems, Inc.  ASCII(128) only.  2026-08-14.
#
# Regenerates EmulatR-App/resources/data/emulatr_env_registry.tsv -- the ONLY
# source of rows for the launcher's Runtime Environment Variables panel
# (SPEC-LAUNCH-001 Section 7.1 V1).
#
# SSOT RULE: the CORE SOURCE is the truth for WHICH variables exist -- every
# row corresponds to a real getenv("EMULATR_*") in the core tree, anchored
# file:line.  The TSV is the truth for the CURATION (kind, default,
# description, tier): on regeneration, existing TSV rows keep their curated
# columns verbatim; only anchors are refreshed and new core knobs appended.
# Deleting a variable from the core removes its row (reported, not silent).
#
# Descriptions for NEW rows are harvested from the H&M reference topic
# (Topics/--Environment-Variables-(EMULA.xml) when it documents the name,
# else from the seed table below, else left empty for hand curation.
#
# TSV schema (EnvVarModel::loadRegistry):
#   name <TAB> kind(flag|path|integer|enum) <TAB> default <TAB> description
#        <TAB> tier(safe|dev|denied) <TAB> anchor
#
# Usage:  python tools/gen_env_registry.py          (from anywhere)
# ============================================================================
import re
import sys
import json
import codecs
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TSV = ROOT / "EmulatR-App" / "resources" / "data" / "emulatr_env_registry.tsv"
# Per-binary capability declaration (architect direction 2026-08-14): the
# populated JSON is staged beside non-release binaries; the empty envelope
# beside release ones, where the diagnostic layer is compiled out.  The
# launcher lists exactly what the binary it will launch declares.
JSON_OUT = ROOT / "config" / "env_registry.json"
JSON_EMPTY = ROOT / "config" / "env_registry_release.json"
HM_TOPIC = Path(r"D:\EmulatR\H&M\HMDocs\Topics\--Environment-Variables-(EMULA.xml")

# The core reads env through three spellings: std::getenv, and the envU64 /
# envq wrappers.  (std::filesystem::path("EMULATR_STOP") is the stop-sentinel
# FILENAME, not an environment read -- excluded by construction.)
GETENV_RE = re.compile(r'\b(?:getenv|envU64|envq)\(\s*"(EMULATR_[A-Z0-9_]+)"')

# ---- initial curation seeds (used ONLY when the TSV has no row yet) --------
# Tier: fail closed -- everything defaults to dev; safe is the short list a
# tester can flip without falsifying results; denied is quarantine.
SAFE = {
    "EMULATR_AUTOSNAP", "EMULATR_AUTOSNAP_PERIOD", "EMULATR_AUTOSNAP_KEEP",
    "EMULATR_TOY_MODE", "EMULATR_NO_PUTTY", "EMULATR_CONSOLE_MIRROR",
}
DENIED = {
    "EMULATR_RSCCWARP",                 # confirmed boot corruption
    "EMULATR_UDELAYWARP_LEGACY_UNSAFE", # the name says it
}

# Descriptions for knobs newer than the H&M topic (2026-08 instruments).
SEED_DESC = {
    "EMULATR_FAULT_RINGDUMP_VA":
        "Dump the decode-listing lookback ring when a fault raises on this "
        "virtual address (hex). The only trigger that can end a ring ON a "
        "faulting instruction, since faulting stores never retire. "
        "[diag builds]",
    "EMULATR_AUTOSNAP_PERIOD":
        "Cycles between periodic snapshot saves (default 50e9). Lower values "
        "give denser resume points; each save pauses the guest for a "
        "multi-GB write.",
    "EMULATR_AUTOSNAP_KEEP":
        "How many periodic snapshots to retain before pruning the oldest.",
    "EMULATR_UART_TRACE_MARKER":
        "Arm the decode-listing value/PC gate when this exact line is "
        "transmitted on the console UART (message-armed gating). [diag builds]",
    "EMULATR_FAST_DECOMPRESS":
        "Cold-boot acceleration: 'snapshot' resumes from the entry snapshot "
        "beside the firmware instead of running the guest decompressor.",
}

def scan_core():
    """name -> 'file:line' anchor (first hit wins), core tree only."""
    anchors = {}
    files = [ROOT / "main.cpp"]
    for lib in sorted(ROOT.glob("*Lib")):
        files += sorted(lib.rglob("*.cpp")) + sorted(lib.rglob("*.h"))
    for f in files:
        try:
            text = f.read_text(encoding="utf-8", errors="replace")
        except OSError:
            continue
        for i, line in enumerate(text.splitlines(), 1):
            for m in GETENV_RE.finditer(line):
                anchors.setdefault(m.group(1), f"{f.relative_to(ROOT).as_posix()}:{i}")
    return anchors

def harvest_hm():
    """name -> plain-text description from the H&M reference topic."""
    if not HM_TOPIC.is_file():
        return {}
    raw = HM_TOPIC.read_bytes()
    if raw.startswith(codecs.BOM_UTF8):
        raw = raw[3:]
    text = raw.decode("utf-8")
    pair_re = re.compile(
        r'Code Example">(EMULATR_[A-Z0-9_]+)</text></para>\s*</td>\s*<td>\s*'
        r'<para[^>]*>(.*?)</para>', re.S)
    out = {}
    for name, desc in pair_re.findall(text):
        desc = re.sub(r"<[^>]+>", "", desc)
        desc = (desc.replace("&amp;", "&").replace("&lt;", "<")
                    .replace("&gt;", ">").replace("&apos;", "'")
                    .replace("&quot;", '"'))
        desc = " ".join(desc.split())
        if desc:
            out.setdefault(name, desc)
    return out

def load_existing():
    """name -> (kind, default, description, tier) from the current TSV."""
    cur = {}
    if not TSV.is_file():
        return cur
    for line in TSV.read_text(encoding="utf-8").splitlines():
        if not line.strip() or line.startswith("#"):
            continue
        c = line.split("\t")
        if len(c) >= 5:
            cur[c[0].strip()] = (c[1].strip(), c[2].strip(),
                                 c[3].strip(), c[4].strip())
    return cur

def infer_kind(name):
    if re.search(r"_(FILE|DIR|NVRAM|ROM|CONFIG)$", name):
        return "path"
    if re.search(r"_(PORT|PERIOD|KEEP|FLOOR|THRESH|MAX|AFTER|LEN|CAP|WMIN|"
                 r"CYCLO|CYCHI|PCLO|PCHI|VA|PA|VAL|BIT|WINDOW|INSTRS)$", name):
        return "integer"
    if name in ("EMULATR_TOY_MODE", "EMULATR_FAST_DECOMPRESS",
                "EMULATR_AUTOSNAP", "EMULATR_CSERVE_START_MODE",
                "EMULATR_CSERVE_ROUTE", "EMULATR_PLATFORM"):
        return "enum"
    return "flag"

def main():
    anchors = scan_core()
    hm = harvest_hm()
    existing = load_existing()

    # Denied rows are quarantine, not documentation: they survive even when
    # the core no longer reads the name, so an old shell export can never
    # silently reach a child process again.
    for name, cur in existing.items():
        if cur[3] == "denied" and name not in anchors:
            anchors[name] = "quarantined (no core read)"

    rows, new_names, uncurated = [], [], []
    for name in sorted(anchors):
        if name in existing:
            kind, default, desc, tier = existing[name]
        else:
            new_names.append(name)
            kind = infer_kind(name)
            default = ""
            desc = SEED_DESC.get(name, hm.get(name, ""))
            if name.startswith("EMULATR_DIAG_") and "[diag builds]" not in desc:
                desc = (desc + " [diag builds]").strip()
            tier = ("safe" if name in SAFE
                    else "denied" if name in DENIED else "dev")
        if not desc:
            uncurated.append(name)
        rows.append((name, kind, default, desc, tier, anchors[name]))

    removed = sorted(set(existing) - set(anchors))
    hm_only = sorted(set(hm) - set(anchors))

    header = """\
# ============================================================================
# emulatr_env_registry.tsv -- curated EMULATR_* runtime variable registry
# ============================================================================
# Project: EmulatR -- EmulatrLaunch (SPEC-LAUNCH-001 Rev D.2)
# Spec:    Section 7.1 (V1/V2) -- the ONLY source of rows for the W6 panel.
#
# GATE G2b CLOSED 2026-08-14: generated by tools/gen_env_registry.py from the
# core tree's getenv("EMULATR_*") census.  Regeneration PRESERVES the curated
# columns of existing rows (kind/default/description/tier) and refreshes only
# the anchors; new core knobs are appended tier=dev (fail closed).  Edit
# curation HERE; edit existence in the CORE.
#
# Rows tagged [diag builds] act only in relwithdebinfo/debug binaries -- the
# diagnostic layer is compiled out of release (see the per-system Emulator
# binary selector in the Details tab).
#
# name<TAB>kind(flag|path|integer|enum)<TAB>default<TAB>description<TAB>tier(safe|dev|denied)<TAB>anchor
# ============================================================================
"""
    body = "\n".join("\t".join(r) for r in rows) + "\n"
    TSV.write_text(header + body, encoding="utf-8", newline="\n")

    print(f"wrote {TSV.relative_to(ROOT).as_posix()}: {len(rows)} rows "
          f"({len(new_names)} new, {len(existing)} curated kept, "
          f"{len(removed)} removed)")

    # ---- the per-binary JSON pair ------------------------------------------
    def envelope(variant, var_rows):
        return {
            "_comment": "Generated by tools/gen_env_registry.py -- edit the "
                        "TSV master, never this file.",
            "schema": 1,
            "variant": variant,
            "vars": [{"name": n, "kind": k, "default": d,
                      "description": ds, "tier": t, "anchor": a}
                     for n, k, d, ds, t, a in var_rows],
        }
    JSON_OUT.write_text(
        json.dumps(envelope("diagnostic", rows), indent=2) + "\n",
        encoding="utf-8", newline="\n")
    JSON_EMPTY.write_text(
        json.dumps(envelope("release", []), indent=2) + "\n",
        encoding="utf-8", newline="\n")
    print(f"wrote {JSON_OUT.relative_to(ROOT).as_posix()} (populated) and "
          f"{JSON_EMPTY.relative_to(ROOT).as_posix()} (empty envelope)")
    if removed:
        print("REMOVED (no longer in core):", ", ".join(removed))
    if uncurated:
        print(f"UNCURATED ({len(uncurated)} rows need descriptions):")
        for n in uncurated:
            print("   ", n, "->", anchors[n])
    if hm_only:
        print(f"H&M-DOCS-ONLY ({len(hm_only)} documented names with no core "
              f"getenv -- stale docs, sentinels, or compile-time flags):")
        print("   ", ", ".join(hm_only))
    return 0

if __name__ == "__main__":
    sys.exit(main())
