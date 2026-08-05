#!/usr/bin/env python3
# ============================================================================
# tools/mkdisk.py -- raw disk-image generator for EmulatR (dqa install target)
# ============================================================================
# Project: EmulatR -- Alpha AXP / EV6 Architecture Emulator (V4)
# Copyright (C) 2025, 2026 eNVy Systems, Inc.  All rights reserved.
#
# Creates a blank, writable, raw flat disk image (512-byte sectors by default)
# for the emulated ATA fixed disk.  Two roles, one artifact: the read-proof
# target (so dqa0 returns LBN 0) and the empty install target the ISO installer
# writes the OS onto.  It does NOT synthesize a bootable OS or a filesystem.
#
# FILE 1: tools/mkdisk.py  FUNCTION: load_drive / create / verify / main
# CHANGE (2026-08-04, SPEC-STORAGE-001 S-2, C-5): config/disk_types.json is the
# storage SSOT.  Size is DERIVED from a catalog key and is never supplied: --type
# is REQUIRED and the size-only creation path is GONE.  --size survives only as a
# cross-check that must equal the profile capacity.  Rationale: an image whose
# geometry no SSOT entry defines is a device EmulatR does not support (C-1), and
# the manifest/editor/binder all resolve the same key namespace (C-3).  Behavior
# addressed: mkdisk could previously create any geometry with no catalog
# involvement, which is one of the three unbounded creation paths named in
# SPEC-STORAGE-001 Sec 2.2.
#
# Size comes from --type (a config/disk_types.json catalog key).  Schema 2 nests
# capacity under 'capacity'; schema 1 carried it at top level.  Image is sparse
# where the FS supports it;
# with --stamp (default) the first --stamp-sectors LBNs plus the last sector get
# a verifiable 32-byte header (magic + LBN + checksum) so a read can be proven to
# return the correct LBN.  Deterministic: same args -> identical bytes.
#
#   python tools/mkdisk.py dka1.img --type EMULATR-512M
#   python tools/mkdisk.py dka0.vdisk --type EMULATR-4G
#   python tools/mkdisk.py dka1.img --type EMULATR-512M --size 512M   # cross-check
#   python tools/mkdisk.py dka1.img --verify --type EMULATR-512M
# ============================================================================

import argparse
import json
import os
import struct
import sys

MAGIC = b"EMULATR-DSK\x00"            # 12 bytes
STAMP_LEN = 32
FNV64_OFFSET = 0xcbf29ce484222325
FNV64_PRIME = 0x100000001b3
MASK64 = (1 << 64) - 1


def fnv1a64(data: bytes) -> int:
    h = FNV64_OFFSET
    for b in data:
        h = ((h ^ b) * FNV64_PRIME) & MASK64
    return h


def make_stamp(lbn: int, sector_size: int) -> bytes:
    body = MAGIC + struct.pack("<I", sector_size) + struct.pack("<Q", lbn)  # 24 bytes
    chk = fnv1a64(body)
    return body + struct.pack("<Q", chk)                                    # 32 bytes


def parse_size(s: str) -> int:
    s = str(s).strip().upper()
    mult = 1
    if s and s[-1] in "KMGT":
        mult = {"K": 1024, "M": 1024**2, "G": 1024**3, "T": 1024**4}[s[-1]]
        s = s[:-1].strip()
    return int(s) * mult


def find_catalog(explicit):
    if explicit:
        return explicit
    here = os.path.dirname(os.path.abspath(__file__))
    for cand in (os.path.join(here, "..", "config", "disk_types.json"),
                 os.path.join(os.getcwd(), "config", "disk_types.json"),
                 os.path.join(os.getcwd(), "disk_types.json")):
        if os.path.isfile(cand):
            return cand
    return None


def load_drive(catalog_path, key):
    """Resolve KEY against the storage SSOT.

    SPEC-STORAGE-001 S-2c/d/e: dispatch on schema; union 'drives' and
    'custom_drives' (the latter SHADOWS by key, matching the runtime loader
    contract); a 'withdrawn' key exits with its recorded reason instead of a
    bare not-found.  Returns (drive, section, schema).
    """
    if catalog_path is None:
        sys.exit("mkdisk: no config/disk_types.json found (use --catalog)")
    with open(catalog_path, "r") as f:
        cat = json.load(f)

    schema = cat.get("schema", 1)
    if schema not in (1, 2):
        sys.exit("mkdisk: catalog %s declares schema %r; this tool understands "
                 "1 and 2" % (catalog_path, schema))

    drives = cat.get("drives", {}) or {}
    custom = cat.get("custom_drives", {}) or {}
    custom = dict((k, v) for k, v in custom.items() if not k.startswith("_"))

    if key in custom:                                    # S-2d: shadows 'drives'
        return custom[key], "custom_drives", schema
    if key in drives:
        return drives[key], "drives", schema

    wd = cat.get("withdrawn", {}) or {}
    if key in wd and not key.startswith("_"):            # S-2e: policy as an error
        e = wd[key]
        sys.exit("mkdisk: drive type '%s' is WITHDRAWN from the catalog.\n"
                 "  reason         : %s\n"
                 "  reinstate with : %s\n"
                 "EmulatR supports only devices with a held authority "
                 "(SPEC-STORAGE-001 C-2)."
                 % (key, e.get("reason", "(none recorded)"),
                    e.get("reinstate_with", "(none recorded)")))

    valid = sorted(list(drives) + list(custom))
    sys.exit("mkdisk: drive type '%s' not in catalog %s\n  valid keys: %s"
             % (key, catalog_path, ", ".join(valid)))


def drive_capacity(drv, schema):
    """(total_sectors, bytes_per_sector) from either schema.

    Schema 2 nests these under 'capacity' (SPEC-STORAGE-001 S-1); schema 1
    carried them at top level.  This is the dependency S-1 flags: nesting them
    would otherwise KeyError here.
    """
    src = drv.get("capacity", drv) if schema >= 2 else drv
    if "total_sectors" not in src:
        sys.exit("mkdisk: catalog entry has no total_sectors")
    return int(src["total_sectors"]), int(src.get("bytes_per_sector", 512))


def stamped_lbns(total, n_low, last):
    lbns = list(range(0, min(n_low, total)))
    if last and total > 0:
        lbns.append(total - 1)
    return sorted(set(lbns))


def create(args):
    # SPEC-STORAGE-001 C-5: one creation primitive, size DERIVED from the SSOT.
    drv, section, schema = load_drive(find_catalog(args.catalog), args.type)
    total_sectors, sector = drive_capacity(drv, schema)
    if total_sectors <= 0:
        sys.exit("mkdisk: '%s' declares total_sectors %d -- it is a "
                 "media-dependent entry (capacity binds from the attached "
                 "image) and cannot be used to CREATE one"
                 % (args.type, total_sectors))
    total_bytes = total_sectors * sector

    if args.size:                       # S-2b: cross-check only, never an override
        want = parse_size(args.size)
        if want != total_bytes:
            sys.exit("mkdisk: --size %s (%d bytes) does not match profile '%s' "
                     "(%d sectors x %d = %d bytes).  --size is a cross-check, "
                     "not an override (SPEC-STORAGE-001 C-5)."
                     % (args.size, want, args.type, total_sectors, sector,
                        total_bytes))

    if total_bytes <= 0 or total_bytes % sector != 0:
        sys.exit("mkdisk: size %d must be a positive multiple of sector %d"
                 % (total_bytes, sector))
    total_sectors = total_bytes // sector

    if os.path.exists(args.output) and not args.force:
        sys.exit("mkdisk: '%s' exists (use --force to overwrite)" % args.output)

    with open(args.output, "wb") as f:
        f.truncate(total_bytes)                          # sparse where supported
        if args.stamp:
            for lbn in stamped_lbns(total_sectors, args.stamp_sectors, True):
                f.seek(lbn * sector)
                f.write(make_stamp(lbn, sector))
    print("mkdisk: created '%s'  %d bytes  %d x %d-byte sectors  (%s)"
          % (args.output, total_bytes, total_sectors, sector,
             "stamped" if args.stamp else "blank"))
    # S-2d: name the section so an image built from a hand-edited entry is
    # traceable back to it.
    print("mkdisk: profile '%s' from %s (schema %d)"
          % (args.type, section, schema))
    return total_sectors, sector


def verify(args):
    sector = args.sector
    if args.type:
        drv, _section, schema = load_drive(find_catalog(args.catalog), args.type)
        _total, sector = drive_capacity(drv, schema)
    size = os.path.getsize(args.output)
    total = size // sector
    bad = 0
    with open(args.output, "rb") as f:
        for lbn in stamped_lbns(total, args.stamp_sectors, True):
            f.seek(lbn * sector)
            stamp = f.read(STAMP_LEN)
            exp = make_stamp(lbn, sector)
            if stamp != exp:
                bad += 1
                print("  LBN %d: stamp mismatch" % lbn)
    if bad:
        print("mkdisk --verify: %d mismatch(es)" % bad)
        return 1
    print("mkdisk --verify: OK  (%d sectors, %d-byte, stamps valid)" % (total, sector))
    return 0


def main():
    ap = argparse.ArgumentParser(description="EmulatR raw disk-image generator")
    ap.add_argument("output", help="output image path")
    ap.add_argument("--type", required=True,
                    help="SSOT drive key (config/disk_types.json 'drives' or "
                         "'custom_drives'). REQUIRED -- EmulatR creates images "
                         "only for devices the SSOT defines (SPEC-STORAGE-001 C-5)")
    ap.add_argument("--size", help="OPTIONAL cross-check (K/M/G/T, 1024-based): "
                                   "must equal the profile capacity; never an override")
    ap.add_argument("--sector", type=int, default=512, help="sector size (default 512)")
    ap.add_argument("--catalog", help="path to disk_types.json (default: auto-locate)")
    g = ap.add_mutually_exclusive_group()
    g.add_argument("--stamp", dest="stamp", action="store_true", default=True,
                   help="stamp boot-region sectors for read verification (default)")
    g.add_argument("--blank", dest="stamp", action="store_false",
                   help="all-zero sectors, no stamps")
    ap.add_argument("--stamp-sectors", type=int, default=256,
                    help="how many low LBNs to stamp (default 256) + the last")
    ap.add_argument("--force", action="store_true", help="overwrite an existing image")
    ap.add_argument("--verify", action="store_true", help="verify stamps instead of creating")
    args = ap.parse_args()

    if args.verify:
        sys.exit(verify(args))
    create(args)


if __name__ == "__main__":
    main()
