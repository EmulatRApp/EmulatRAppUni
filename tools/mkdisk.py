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
# Size comes from --type (a config/disk_types.json catalog key -> total_sectors)
# or --size (K/M/G/T, 1024-based).  The image is sparse where the FS supports it;
# with --stamp (default) the first --stamp-sectors LBNs plus the last sector get
# a verifiable 32-byte header (magic + LBN + checksum) so a read can be proven to
# return the correct LBN.  Deterministic: same args -> identical bytes.
#
#   python tools/mkdisk.py dqa0.img --type EMULATR-DISK-4G
#   python tools/mkdisk.py data.img --size 9G --sector 512
#   python tools/mkdisk.py dqa0.img --verify        # check the stamps
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
    with open(catalog_path, "r") as f:
        cat = json.load(f)
    drives = cat.get("drives", {})
    if key not in drives:
        sys.exit("mkdisk: drive type '%s' not in catalog %s (have: %s)"
                 % (key, catalog_path, ", ".join(sorted(drives))))
    return drives[key]


def stamped_lbns(total, n_low, last):
    lbns = list(range(0, min(n_low, total)))
    if last and total > 0:
        lbns.append(total - 1)
    return sorted(set(lbns))


def create(args):
    sector = args.sector
    if args.type:
        drv = load_drive(find_catalog(args.catalog), args.type)
        sector = int(drv.get("bytes_per_sector", sector))
        if args.size:
            total_bytes = parse_size(args.size)
        else:
            total_bytes = int(drv["total_sectors"]) * sector
    else:
        if not args.size:
            sys.exit("mkdisk: --size or --type is required")
        total_bytes = parse_size(args.size)

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
    return total_sectors, sector


def verify(args):
    sector = args.sector
    if args.type:
        drv = load_drive(find_catalog(args.catalog), args.type)
        sector = int(drv.get("bytes_per_sector", sector))
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
    ap.add_argument("--type", help="catalog drive model (config/disk_types.json key)")
    ap.add_argument("--size", help="capacity (K/M/G/T, 1024-based); overrides --type size")
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
