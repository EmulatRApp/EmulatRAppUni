#!/usr/bin/env python3
# ============================================================================
# tools/env_size_audit.py -- SIZE-AWARE SRM environment-variable audit/diff
# ============================================================================
# Project: EmulatR -- Alpha AXP / EV6 Architecture Emulator (V5).
# ASCII(128) only.  Hex radix for addresses, decimal for byte counts.
#
# WHY SIZE MATTERS (JRN-SCSI-006 Sec 9, JRN-SCSI-007)
#   APB reads console EVs through the HWRPB CRB callback a0 = 0x22
#   (cbfunc$k_get_env = 34, confirmed in apisrm ref/apu_callbacks_def.h).
#   cb_get_env (ref/call_backs.c) returns, in R0:
#       low  longword = ev->size          (FULL size, even if truncated)
#       high longword = 0x00000000 success
#                       0x20000000 buffer TOO SMALL   (size < ev->size)
#                       0xc0000000 EV not found       (low longword also 0)
#   APB's callers gate on that:  bits 29:31 of the high longword nonzero, OR a
#   zero low longword, and the resolver call is SKIPPED SILENTLY.  Caller A
#   (0x200016a4) supplies a SIXTEEN BYTE buffer; caller B (0x2000e328) supplies
#   0x100.  So an EV that is merely LONGER than the caller's buffer changes
#   control flow.  Comparing printed text alone cannot see that -- hence this
#   tool reports BYTE SIZE as a first-class field.
#
# USAGE
#   python3 tools/env_size_audit.py <transcript>              # audit one
#   python3 tools/env_size_audit.py <golden> <candidate>      # diff two
#   options:
#     --buffer <n>   caller buffer size to test against (default 16)
#     --all          list every EV, not just findings
#
# INPUT
#   Any console transcript containing "show"-style output lines of the form
#     name<2+ spaces>value        (value may be empty)
#   Prompts (">>>") and command echoes are ignored.
#
# CAVEAT (do not skip)
#   A transcript from EmulatR's SYNTHETIC console (prompt "EmulatR>>>") and one
#   from the real firmware (prompt "P00>>>") are DIFFERENT SUBSYSTEMS with
#   different env stores.  Only the firmware-path transcript bears on the APB
#   callback gate.  This tool prints the prompt style it detected so a
#   cross-subsystem comparison cannot be made by accident.
# ============================================================================
import os, re, sys

# EVs the APB boot path reads, with the envid$ codes from apu_callbacks_def.h
BOOT_CRITICAL = {
    "auto_action": 1, "boot_dev": 2, "bootcmd_dev": 3, "booted_dev": 4,
    "boot_file": 5, "booted_file": 6, "boot_osflags": 7, "booted_osflags": 8,
    "boot_reset": 9, "dump_dev": 10, "enable_audit": 11, "license": 12,
    "char_set": 13, "language": 14, "tty_dev": 15,
    "op_updown": 64, "op_size": 65, "scsi_hostids": 66, "fast_scsi": 67,
}
# Per-EV buffer the APB caller supplies, from the disassembly (JRN-SCSI-006
# Sec 8.2).  Caller A (0x200016a4) passes a3 = 0x10 for envid 8; caller B
# (0x2000e328) passes 0x100.  Only the EV a caller actually requests can be
# judged OVERSIZE -- a long boot_dev is harmless if nothing reads it into a
# small buffer, so do NOT apply caller A's 16 bytes across the board.
BUFFERS = {8: 16}
DEFAULT_BUFFER = 256
# A transcript taken at the prompt BEFORE any boot legitimately has empty
# booted_* EVs -- boot.c writes booted_dev/booted_file/booted_osflags only when
# the `b` command runs (ev_write, just before "base = %x" is printed).  So EMPTY
# is only a FINDING in a transcript that shows a boot attempt.
BOOT_ATTEMPTED = re.compile(r"base\s*=\s*[0-9a-f]+|reading\s+\d+\s+blocks|APB", re.I)
WRITTEN_BY_BOOT = {4, 6, 8}          # booted_dev, booted_file, booted_osflags

LINE = re.compile(r"^([a-z][a-z0-9_]*)[ \t]{2,}(.*?)[ \t]*$", re.I)
PROMPT = re.compile(r"(P\d\d>>>|EmulatR>>>|\w+>>>|>>>)")


def parse(path):
    evs, prompts, booted = {}, {}, False
    with open(path, 'r', encoding='latin1', errors='replace') as fh:
        for raw in fh:
            if BOOT_ATTEMPTED.search(raw):
                booted = True
            line = raw.rstrip("\r\n")
            m = PROMPT.search(line)
            if m:
                prompts[m.group(1)] = prompts.get(m.group(1), 0) + 1
                line = line[m.end():]           # strip prompt + echoed command
                if not line.strip() or line.lstrip().startswith(("sho", "show", "b ", "boot")):
                    continue
            m = LINE.match(line)
            if m:
                name, val = m.group(1).lower(), m.group(2)
                if name in ("bus", "slot", "pal"):   # show config table noise
                    continue
                evs[name] = val
            else:
                m2 = re.match(r"^([a-z][a-z0-9_]*)\s*$", line, re.I)
                if m2 and m2.group(1).lower() in BOOT_CRITICAL:
                    evs[m2.group(1).lower()] = ""    # present but EMPTY
    return evs, prompts, booted


def show(tag, path, evs, prompts):
    style = ", ".join("%s x%d" % (k, v) for k, v in sorted(prompts.items())) or "(none)"
    print("%-10s %s" % (tag + ":", os.path.basename(path)))
    print("%-10s %d EVs parsed   prompt style: %s" % ("", len(evs), style))


def findings(evs, buf, booted):
    out = []
    for name, code in sorted(BOOT_CRITICAL.items(), key=lambda kv: kv[1]):
        b = BUFFERS.get(code, buf if code in BUFFERS else DEFAULT_BUFFER)
        if name not in evs:
            out.append(("ABSENT", name, code, "-", "not shown in this transcript"))
            continue
        v = evs[name]
        n = len(v.encode('latin1'))
        if n == 0:
            if code in WRITTEN_BY_BOOT and not booted:
                out.append(("empty-ok", name, code, 0,
                            "written by boot.c ev_write only during `b` -- expected empty pre-boot"))
            else:
                out.append(("EMPTY", name, code, 0,
                            "size 0 -> caller gate 2 (low longword == 0) SKIPS the resolver"))
        elif n > b:
            out.append(("OVERSIZE", name, code, n,
                        "size %d > caller buffer %d -> cb_get_env 0x20000000 -> gate 1 SKIPS" % (n, b)))
    return out


def main():
    args = [a for a in sys.argv[1:]]
    buf, show_all = 16, False
    if "--all" in args:
        show_all = True; args.remove("--all")
    if "--buffer" in args:
        i = args.index("--buffer"); buf = int(args[i + 1], 0); del args[i:i + 2]
    if not args:
        print(__doc__); sys.exit(1)

    a_evs, a_pr, a_boot = parse(args[0])
    print("=" * 78)
    show("REFERENCE", args[0], a_evs, a_pr)
    b_evs = b_pr = None
    if len(args) > 1:
        b_evs, b_pr, b_boot = parse(args[1])
        show("CANDIDATE", args[1], b_evs, b_pr)
        if set(a_pr) and set(b_pr) and not (set(a_pr) & set(b_pr)):
            print("\n*** WARNING: different prompt styles -- these may be DIFFERENT")
            print("    SUBSYSTEMS (synthetic console vs real firmware).  A diff across")
            print("    them says nothing about the APB callback gate.  See header.")
    print("=" * 78)

    print("\n-- boot-path EV findings (caller buffers: envid 8 -> 16 bytes, others -> %d) --" % DEFAULT_BUFFER)
    print("   reference transcript shows a boot attempt: %s" % ("YES" if a_boot else "NO -- booted_* are expected empty"))
    rows = findings(a_evs, buf, a_boot)
    if not rows:
        print("   none")
    for kind, name, code, n, why in rows:
        print("   %-8s %-16s envid %-3d size %-4s %s" % (kind, name, code, n, why))

    if b_evs is not None:
        print("\n-- REFERENCE vs CANDIDATE, boot-path EVs --")
        print("   %-16s %-5s %-26s %-26s" % ("name", "envid", "reference (size)", "candidate (size)"))
        for name, code in sorted(BOOT_CRITICAL.items(), key=lambda kv: kv[1]):
            in_a, in_b = name in a_evs, name in b_evs
            if not in_a and not in_b:
                continue
            va = a_evs.get(name); vb = b_evs.get(name)
            sa = "-" if va is None else "%d" % len(va.encode('latin1'))
            sb = "-" if vb is None else "%d" % len(vb.encode('latin1'))
            mark = " " if va == vb else "*"
            if mark == "*" or show_all:
                print(" %s %-16s %-5d %-26s %-26s" % (
                    mark, name, code,
                    "%r (%s)" % (va, sa) if va is not None else "ABSENT",
                    "%r (%s)" % (vb, sb) if vb is not None else "ABSENT"))
        print("   ('*' marks a difference; size is BYTES, the gate input)")


if __name__ == "__main__":
    main()
