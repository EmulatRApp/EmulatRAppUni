#!/usr/bin/env python3
# ============================================================================
# tools/srm_console_driver.py -- scripted SRM console driver (telnet)
# ============================================================================
# Project: EmulatR -- Alpha AXP / EV6 Architecture Emulator (V5).
# Architect: Timothy Peer.  AI collaboration: Claude (Anthropic).
# Copyright (C) 2025, 2026 eNVy Systems, Inc.  Licensed eNVy Non-Commercial v1.1.
# ASCII(128) only.  Hex radix.
#
# WHY (TASK-BOOT-001 Sec 7 "promote scratchpad drivers"; JRN-SCSI-010)
#   Drives an EmulatR SRM console over its telnet port for unattended boot
#   verification.  Encodes the DS20 v7.3-2 console quirks learned during
#   the 2026-07-25 L0 recovery:
#     - the firmware may auto-enter the LFU during init; TWO prompts must
#       be cleared: the load-source prompt ("...or just hit <return> to
#       proceed with a standard console update:") -> bare CR, then
#       "UPD>" -> "exit".  This can happen more than once per cold boot.
#     - a TRANSIENT P00>>> can appear before the LFU auto-entry; a boot
#       sent there is swallowed.  The driver therefore only boots when
#       the receive buffer ENDS at P00>>> and re-sends (up to --tries)
#       if no "(boot " echo follows within --resend seconds.
#     - the console server admits ONE client; a second connection is
#       accepted-then-closed.  The driver reconnects on drops instead of
#       dying (a teardown race also produces an immediate close).
#   PASS/FAIL contract (defaults tuned for the L0/L1 era):
#     PASS  = --expect substring appears (default "NOIOVEC" -- the L0-open
#             success criterion until L1 falls)
#     FAIL  = the halt-0 wall ("halt code = 0" + "PC = 20000000" after a
#             boot echo), or timeout.
#
# USAGE
#   python3 tools/srm_console_driver.py [--port 10023] [--host 127.0.0.1]
#       [--boot "b dka0.0.0.8.0 -flags 0"] [--expect NOIOVEC]
#       [--timeout 1500] [--resend 45] [--tries 4] [--quiet]
#   Exit code 0 = PASS, 1 = FAIL/timeout, 2 = could not connect.
#   The emulator must already be running (this tool does not launch it;
#   pair with run_taskboot001_phase1.sh / run_ds20_bplus.sh or a bare
#   Emulatr.exe for default-path testing).
# ============================================================================
import argparse, socket, sys, time


def connect(host, port, attempts=60, delay=2):
    for _ in range(attempts):
        try:
            s = socket.create_connection((host, port), timeout=3)
            s.settimeout(5)
            return s
        except OSError:
            time.sleep(delay)
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--host', default='127.0.0.1')
    ap.add_argument('--port', type=int, default=10023)
    ap.add_argument('--boot', default='b dka0.0.0.8.0 -flags 0')
    ap.add_argument('--expect', default='NOIOVEC')
    ap.add_argument('--timeout', type=int, default=1500)
    ap.add_argument('--resend', type=int, default=45)
    ap.add_argument('--tries', type=int, default=4)
    ap.add_argument('--quiet', action='store_true')
    a = ap.parse_args()

    def log(msg):
        if not a.quiet:
            print(msg, flush=True)

    s = connect(a.host, a.port)
    if s is None:
        print('FAIL: cannot connect to %s:%d' % (a.host, a.port))
        return 2
    log('connected to %s:%d' % (a.host, a.port))

    buf = b''
    lfu = 0
    sends = 0
    mark = 0
    last_send = 0.0
    last_report = 0.0
    verdict = None
    boot = a.boot.encode('ascii') + b'\r'
    expect = a.expect.encode('ascii')
    deadline = time.time() + a.timeout

    while time.time() < deadline:
        try:
            c = s.recv(4096)
            if not c:
                log('socket closed -- reconnecting')
                time.sleep(2)
                s2 = connect(a.host, a.port, attempts=15)
                if s2 is None:
                    break
                s = s2
                continue
            buf += c.replace(b'\xff', b'')     # crude telnet-IAC strip
        except socket.timeout:
            pass

        if time.time() - last_report > 60:
            last_report = time.time()
            log('  tail: %r' % buf[-70:].decode('ascii', 'replace'))

        tailb = buf.rstrip()
        if tailb.endswith(b'standard console update:') and lfu < 10:
            time.sleep(1); s.sendall(b'\r'); lfu += 1
            buf += b'<CR>'
            log('LFU load prompt -> <return> (#%d)' % lfu)
        elif tailb.endswith(b'(ewa0),') and lfu < 10:
            # THIRD LFU prompt (2026-07-26): after "Option firmware files were
            # not found on CD or floppy" the firmware asks "please enter the
            # device on which the files are located(ewa0)," -- a bare CR takes
            # the default and moves on.  Without this the boot STALLS here
            # forever (observed: an N5 run sat at this prompt ~40 min while the
            # driver waited for a P00>>> that never came).
            time.sleep(1); s.sendall(b'\r'); lfu += 1
            buf += b'<CR>'
            log('LFU option-firmware device prompt -> <return> (#%d)' % lfu)
        elif tailb.endswith(b'UPD>') and lfu < 10:
            time.sleep(1); s.sendall(b'exit\r'); lfu += 1
            buf += b'<exit>'
            log('UPD> -> exit (#%d)' % lfu)

        if (sends < a.tries and b'(boot ' not in buf[mark:]
                and tailb.endswith(b'P00>>>')
                and time.time() - last_send > a.resend):
            time.sleep(1); s.sendall(boot)
            sends += 1; last_send = time.time(); mark = len(buf)
            log('boot sent (#%d): %s' % (sends, a.boot))

        if expect in buf:
            verdict = 'PASS'
            break
        if b'(boot ' in buf and b'halt code = 0' in buf \
                and b'PC = 20000000' in buf:
            verdict = 'FAIL-L0WALL'
            break

    print('transcript tail:')
    print(buf[-700:].decode('ascii', 'replace'))
    print('VERDICT: %s' % (verdict or 'TIMEOUT'))
    return 0 if verdict == 'PASS' else 1


if __name__ == '__main__':
    sys.exit(main())
