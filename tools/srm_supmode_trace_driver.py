#!/usr/bin/env python3
# ============================================================================
# tools/srm_supmode_trace_driver.py -- JRN-SUPMODE-001 Sec 12 boot driver
# ============================================================================
# Project: EmulatR -- Alpha AXP / EV6 Architecture Emulator (V5).
# Architect: Timothy Peer.  AI collaboration: Claude (Anthropic).
# Copyright (C) 2025, 2026 eNVy Systems, Inc.  Licensed eNVy Non-Commercial v1.1.
# ASCII(128) only.  Hex radix.
#
# WHY: drives the DS20 VMS conversational boot for the CHMS-corridor
# retire-trace capture (pair with tools/run_ds20_supmode_trace.sh, which
# exports EMULATR_PROBE_SUPMODE_ARM -- the window arms IN-ENGINE at the
# CHMS issuance, so this driver has NO arming step and NO Ctrl/P timing
# to hit).  Dialog, replayed from run ds20_v7_3_h8b_gateb_20260809_201623:
#     P00>>> b -fl 0,1 dka0
#     SYSBOOT> set startup_p1 "min"
#     SYSBOOT> c
#     Please enter date and time (DD-MMM-YYYY  HH:MM)  <now>
#     ... %STDRV ... %SYSTEM-F-ACCVIO (Species A follows the CHMS fail)
# On the first ACCVIO the CHMS era has passed and the window has fired;
# after --grace seconds (tail capture + later re-arms) the driver halts
# the guest with Ctrl/P and exits.  LFU-prompt clearing and the steady-
# prompt boot rule are inherited from srm_console_driver.py.
#
# Exit: 0 = ACCVIO seen (capture era reached), 1 = timeout, 2 = no connect.
# ============================================================================
import argparse, socket, sys, time
from datetime import datetime


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
    ap.add_argument('--boot', default='b -fl 0,1 dka0')
    ap.add_argument('--timeout', type=int, default=2700)
    ap.add_argument('--resend', type=int, default=45)
    ap.add_argument('--tries', type=int, default=4)
    ap.add_argument('--grace', type=int, default=90,
                    help='seconds to keep running after the first ACCVIO')
    ap.add_argument('--char-delay', type=float, default=0.08,
                    help='per-character send pacing (s).  The SYSBOOT-era '
                         'console input path drops burst input: a full-speed '
                         'line send lost all but the first char (2026-08-10 '
                         'run 103836, echo stalled at "SYSBOOT> s").  Human-'
                         'cadence pacing is what the working interactive '
                         'boots have always used.')
    ap.add_argument('--resume-at', default='boot',
                    choices=['boot', 'sysboot_set', 'sysboot_c', 'date',
                             'run'],
                    help='start the state machine here (guest already '
                         'mid-dialog); a Ctrl/U is sent first to clear any '
                         'partial input line')
    a = ap.parse_args()

    def log(msg):
        print('[%s] %s' % (datetime.now().strftime('%H:%M:%S'), msg),
              flush=True)

    s = connect(a.host, a.port)
    if s is None:
        print('FAIL: cannot connect to %s:%d' % (a.host, a.port))
        return 2
    log('connected to %s:%d' % (a.host, a.port))

    def paced_send(text):
        # one byte per --char-delay; the receive loop catches the echo on
        # the next pass.  CR is included by the caller.
        for ch in text:
            s.sendall(bytes([ch]))
            time.sleep(a.char_delay)

    buf = b''
    lfu = 0
    sends = 0
    mark = 0
    last_send = 0.0
    last_report = 0.0
    # dialog states: boot -> sysboot_set -> sysboot_c -> date -> run -> halt
    state = a.resume_at
    accvio_at = None
    verdict = None
    deadline = time.time() + a.timeout

    if state != 'boot':
        # resuming into a live dialog: clear any partial input line first
        time.sleep(1)
        s.sendall(b'\x15')                  # Ctrl/U -- line kill
        time.sleep(2)
        log('resume at %s -- Ctrl/U sent to clear partial line' % state)

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
            log('state=%s tail: %r'
                % (state, buf[-70:].decode('ascii', 'replace')))

        tailb = buf.rstrip()

        # --- LFU auto-entry clearing (srm_console_driver.py lineage) ---
        if tailb.endswith(b'standard console update:') and lfu < 10:
            time.sleep(1); s.sendall(b'\r'); lfu += 1
            buf += b'<CR>'
            log('LFU load prompt -> <return> (#%d)' % lfu)
            continue
        elif tailb.endswith(b'(ewa0),') and lfu < 10:
            time.sleep(1); s.sendall(b'\r'); lfu += 1
            buf += b'<CR>'
            log('LFU option-firmware device prompt -> <return> (#%d)' % lfu)
            continue
        elif tailb.endswith(b'UPD>') and lfu < 10:
            time.sleep(1); s.sendall(b'exit\r'); lfu += 1
            buf += b'<exit>'
            log('UPD> -> exit (#%d)' % lfu)
            continue

        if state == 'boot':
            if (sends < a.tries and b'(boot ' not in buf[mark:]
                    and tailb.endswith(b'P00>>>')
                    and time.time() - last_send > a.resend):
                time.sleep(1); paced_send(a.boot.encode('ascii') + b'\r')
                sends += 1; last_send = time.time(); mark = len(buf)
                log('boot sent (#%d): %s' % (sends, a.boot))
            if b'(boot ' in buf[mark:] or b'SYSBOOT>' in buf[mark:]:
                state = 'sysboot_set'; mark = len(buf)
                log('boot echoed -- awaiting SYSBOOT>')

        elif state == 'sysboot_set':
            if tailb.endswith(b'SYSBOOT>'):
                time.sleep(1)
                paced_send(b'set startup_p1 "min"\r')
                state = 'sysboot_c'; mark = len(buf)
                log('SYSBOOT> set startup_p1 "min"')

        elif state == 'sysboot_c':
            # only advance on a NEW prompt after our set-command echo
            if b'startup_p1' in buf[mark:] and tailb.endswith(b'SYSBOOT>'):
                time.sleep(1)
                paced_send(b'c\r')
                state = 'date'; mark = len(buf)
                log('SYSBOOT> c -- awaiting date prompt')

        elif state == 'date':
            if b'enter date and time' in buf[mark:] \
                    and tailb.endswith(b'HH:MM)'):
                time.sleep(1)
                stamp = datetime.now().strftime('%d-%b-%Y %H:%M').upper()
                paced_send(stamp.encode('ascii') + b'\r')
                state = 'run'; mark = len(buf)
                log('date entered: %s -- running to the CHMS era' % stamp)

        elif state == 'run':
            if accvio_at is None and b'%SYSTEM-F-ACCVIO' in buf[mark:]:
                accvio_at = time.time()
                log('ACCVIO observed -- CHMS era reached; %ds grace'
                    % a.grace)
            if accvio_at is not None \
                    and time.time() - accvio_at > a.grace:
                s.sendall(b'\x10')          # Ctrl/P -> halt to P00>>>
                state = 'halt'; mark = len(buf)
                log('grace over -- Ctrl/P sent')

        elif state == 'halt':
            if tailb.endswith(b'P00>>>'):
                verdict = 'PASS'
                break

    print('transcript tail:')
    print(buf[-900:].decode('ascii', 'replace'))
    print('VERDICT: %s' % (verdict or 'TIMEOUT (state=%s)' % state))
    return 0 if verdict == 'PASS' else 1


if __name__ == '__main__':
    sys.exit(main())
