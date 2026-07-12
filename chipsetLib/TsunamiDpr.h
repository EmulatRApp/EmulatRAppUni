// ============================================================================
// TsunamiDpr.h -- ES40/ES45 RMC Dual-Port RAM (DPR) device model
// ============================================================================
// Project: ASA-EMulatR - Alpha AXP Architecture Emulator
// Copyright (C) 2025, 2026 eNVy Systems, Inc. All rights reserved.
// Licensed under eNVy Systems Non-Commercial License v1.1
//
// Project Architect: Timothy Peer
// AI Code Generation: Claude (Anthropic)
// ============================================================================
//
// FILE: chipsetLib/TsunamiDpr.h   (NEW, task #25)
// FUNCTION: TsunamiDpr (whole file)
// CHANGE: Model the Remote Management Console (RMC) Dual-Port RAM shared
//   between the RMC service microcontroller and the host Alpha CPUs.  With no
//   DPR modeled, the pc264 SRM reports "*** Error - TIG load failure ***"
//   during "starting drivers" (it reads the tig-load status byte and finds it
//   un-set) and leaves the Serial ROM / RMC ROM / RMC Flash ROM / Bcache fields
//   of `show config` blank.  Modeling the DPR is the FAITHFUL way to clear
//   those (a real ES40 has the RMC populate this RAM); it is NOT a mask.
//
// INTERFACE:
//   16 KB dual-port SRAM.  The host reaches it over the Tsunami/Typhoon TIG
//   bus, memory-mapped at host PA = kBase + (byteIndex << 6) -- one byte per
//   64-byte-strided slot (the same xtig() stride the TIG flash and TIG device
//   registers use).  Byte-wide reads/writes.  The RMC firmware polls/updates
//   the same bytes from its side; the RAM is the host<->service-processor
//   mailbox.  Layout = the "DPR structure" (pc264 codename "Clipper").
//
// PA MAP (no collision -- sits in the gap between the two TIG windows):
//   TIG flash    0x801_0000_0000 .. 0x801_0800_0000  (TsunamiChipset m_flash)
//   >>> DPR      0x801_1000_0000 .. 0x801_1010_0000  (THIS device) <<<
//   TIG devices  0x801_3000_0000 .. 0x801_4000_0000  (TsunamiTig m_tig)
//
// PROVENANCE (HRM-AUTHORITATIVE):
//   The field map is the Compaq AlphaServer ES40 Service Guide, EK-ES240-SV.A01,
//   Appendix C "DPR Address Layout", Table C-1 (and Appendix D/E for the 680
//   logout / power-supply / fatal registers).  Every offset below is verified
//   against that table -- notably 0xDA "Indicates TIG finished loading its code
//   (0xAA indicates done)", the byte the pc264 SRM checks for "TIG load
//   failure".  The AXPBox CDPR model was the original cross-check oracle; where
//   it diverged from the Service Guide the Guide wins (e.g. the I2C-done flag is
//   at 0xD9 per Table C-1, not 0xBA as AXPBox had it).  Values not enumerated by
//   the Guide (individual telemetry readings, revision strings) remain
//   representative placeholders and are marked inline.
//
// DETERMINISM (V4 best-effort-deterministic rule -- named trade-off):
//   AXPBox seeds the BCD power-up timestamp from host time()/localtime(), which
//   is nondeterministic.  We use a FIXED deterministic stamp (kBcd*) so boots
//   are byte-reproducible.  A real RMC reads its live battery clock; the value
//   here is a stable placeholder, not a live time source.
//
// TODO TABLE (greppable; entry + call-site comment removed together on wiring):
//   TODO(dpr-mp-start)      : the RMC secondary-CPU start commands (DPR 0x3428/
//                             0x3438/0x3448) latch a request but do NOT release
//                             a CPU -- EmulatR's CPU-release seam is separate,
//                             and the UP boot never writes these.  Wire to the
//                             real secondary bring-up when MP boot lands.
//   TODO(dpr-snapshot)      : DPR state is NOT serialized by Snapshot yet; the
//                             deferral is guarded by isAtResetState() (proves
//                             the cold path leaves the mutable region at reset).
//                             Add to Snapshot if the guard ever trips.
//   TODO(dpr-speed-prov)    : per-CPU speed bytes (+0x0b/+0x0c) come from the
//                             _PROVISIONAL model MHz; unify with the #24 model
//                             clock source so show config MHz cannot diverge.
//   TODO(dpr-memcfg)        : array config/size (0x80..0x87) are stub values;
//                             populate the real geometry (Table C-1 x64MB size
//                             encoding: 0x10=1GB) once memSizeBytes is plumbed
//                             into the ctor.  Not boot-critical (memory size is
//                             read from the Cchip AAR CSRs, task #6).
//   TODO(dpr-flash-f0)      : RMC flash-update (cmd 0xf0) completion code is
//                             unverified; AXPBox falls through to 0x81 (invalid)
//                             -- we return 0 (ok) pending the RMC spec.
//
// ASCII(128) only; hex radix; include guards (not #pragma once).
// ============================================================================
#ifndef CHIPSETLIB_TSUNAMIDPR_H
#define CHIPSETLIB_TSUNAMIDPR_H

#include <array>
#include <cstddef>
#include <cstdint>

// TsunamiDpr -- RMC Dual-Port RAM.  Constructed by TsunamiChipset; only DECODED
// for the RMC-bearing platform (ES40 == Typhoon variant), so DS10/DS20 remain
// byte-identical (the window is never consulted there).
class TsunamiDpr
{
public:
    // ---- PA window (byte at kBase + (index << 6)) --------------------------
    static constexpr uint64_t   kBase    = 0x80110000000ULL;  // 0x801_1000_0000
    static constexpr uint64_t   kSpan    = 0x100000ULL;       // 0x4000 bytes << 6
    static constexpr uint64_t   kEnd     = kBase + kSpan;     // 0x801_1010_0000
    static constexpr std::size_t kRamSize = 16 * 1024;        // 0x4000

    static constexpr bool decodes(uint64_t pa) noexcept {
        return pa >= kBase && pa < kEnd;
    }

    // cpuSpeedMHz is _PROVISIONAL (TODO(dpr-speed-prov)); ES40 pc264 is 500 MHz.
    explicit TsunamiDpr(int cpuCount = 1, int cpuSpeedMHz = 500) noexcept
        : m_cpuCount(clampCpu(cpuCount))
        , m_cpuSpeedMHz(cpuSpeedMHz)
    {
        reset();
    }

    void reset() noexcept {
        m_ram.fill(0);
        init();
    }

    uint64_t read(uint64_t pa, uint8_t /*width*/) const noexcept {
        std::size_t const a = idx(pa);
        return (a < kRamSize) ? static_cast<uint64_t>(m_ram[a]) : 0;
    }

    void write(uint64_t pa, uint8_t /*width*/, uint64_t data) noexcept {
        std::size_t const a = idx(pa);
        if (a >= kRamSize) return;
        m_ram[a] = static_cast<uint8_t>(data & 0xFFu);
        command(a);                       // RMC mailbox command dispatch
    }

    // Snapshot deferral guard (TODO(dpr-snapshot)): true when the mutable RMC
    // mailbox / scratch region is still at its reset value -- i.e. the captured
    // cold path never wrote a command.  The save path can assert this to PROVE
    // the deferral is safe; if it returns false, DPR must join Snapshot.
    bool isAtResetState() const noexcept {
        // Command mailbox (0xf9..0xff) + FRU/OCP scratch (0x3500..0x35ff) at 0.
        for (std::size_t a = 0xf9; a <= 0xff; ++a)
            if (m_ram[a] != 0) return false;
        for (std::size_t a = 0x3500; a <= 0x35ff; ++a)
            if (m_ram[a] != 0) return false;
        return true;
    }

private:
    static constexpr std::size_t idx(uint64_t pa) noexcept {
        return static_cast<std::size_t>((pa - kBase) >> 6);
    }
    static constexpr int clampCpu(int n) noexcept {
        return (n < 1) ? 1 : (n > 4 ? 4 : n);
    }

    // Fixed deterministic BCD power-up stamp (see DETERMINISM note).  Values are
    // BCD: 00:00:00 on 01-Jan-2006 (_PROVISIONAL placeholder, not a live clock).
    static constexpr uint8_t kBcdHour = 0x00;
    static constexpr uint8_t kBcdMin  = 0x00;
    static constexpr uint8_t kBcdSec  = 0x00;
    static constexpr uint8_t kBcdMday = 0x01;
    static constexpr uint8_t kBcdMon  = 0x01;
    static constexpr uint8_t kBcdYear = 0x06;   // years since 2000, BCD

    void put(std::size_t a, uint8_t v) noexcept {
        if (a < kRamSize) m_ram[a] = v;
    }

    // ---- Populate the DPR structure (AXPBox CDPR oracle; _PROVISIONAL) -------
    void init() noexcept {
        // Per-CPU status blocks at i*0x20.
        for (int i = 0; i < m_cpuCount; ++i) {
            std::size_t const b = static_cast<std::size_t>(i) * 0x20;
            put(b + 0x00, 0x01);                           // EV6 BIST
            put(b + 0x01, (i == 0) ? 0x80 : static_cast<uint8_t>(i)); // SROM status
            put(b + 0x02, 0x01);                           // STR status
            put(b + 0x03, 0x01);                           // CSC status
            put(b + 0x04, 0x01);                           // Pchip0 status
            put(b + 0x05, 0x01);                           // Pchip1 status
            put(b + 0x06, 0x01);                           // DIMx status
            put(b + 0x07, 0x01);                           // TIG bus status
            put(b + 0x08, 0xdd);                           // DPR test started
            put(b + 0x09, 0x01);                           // DPR status
            put(b + 0x0a, 0xff);                           // CPU speed status
            put(b + 0x0b, static_cast<uint8_t>(m_cpuSpeedMHz % 256)); // speed lo
            put(b + 0x0c, static_cast<uint8_t>(m_cpuSpeedMHz / 256)); // speed hi
            put(b + 0x10, kBcdHour);                       // BCD power-up time
            put(b + 0x11, kBcdMin);
            put(b + 0x12, kBcdSec);
            put(b + 0x13, kBcdMday);
            put(b + 0x14, kBcdMon);
            put(b + 0x15, kBcdYear);
            put(b + 0x16, 0x00);                           // no error
            put(b + 0x1e, 0x80);                           // CPU SROM sync (else cpu startup failure)
            put(b + 0x1f, 0x08);                           // cache size in MB (Bcache)
        }

        put(0xda, 0xaa);                                   // TIG load (see 0xda below)

        // DIMM config (array 0 present; further arrays left 0 = absent).
        // TODO(dpr-memcfg): stub geometry -- real config/size (Table C-1 x64MB:
        // 0x10=1GB) once memSizeBytes is plumbed in.  Not boot-critical.
        put(0x80, 0xf0);                                   // Array 0 config: twice-split, 8 DIMMs
        put(0x81, 0x01);                                   // Array 0 size: 0x01 = 64 MB (stub)

        // powerup failure bits (0x88..0x8b) and misconfigured-DIMM bits
        // (0x8c..0x8f) all zero = no failures (already 0 from fill).

        put(0x90, 0xff);                                   // psu / vterm present
        put(0x91, 0x00);                                   // psu ok bits
        put(0x92, 0x07);                                   // ac inputs valid
        put(0x93, 0x25);                                   // cpu 0 temp (C)
        put(0x94, 0x25);                                   // cpu 1 temp
        put(0x95, 0x25);                                   // cpu 2 temp
        put(0x96, 0x25);                                   // cpu 3 temp
        put(0x97, 0x25);                                   // pci 0 temp
        put(0x98, 0x25);                                   // pci 1 temp
        put(0x99, 0x25);                                   // pci 2 temp
        put(0x9a, 0x8b);                                   // fan 0 speed
        put(0x9b, 0x8b);                                   // fan 1 speed
        put(0x9c, 0x8b);                                   // fan 2 speed
        put(0x9d, 0x8b);                                   // fan 3 speed
        put(0x9e, 0x8b);                                   // fan 4 speed
        put(0x9f, 0x8b);                                   // fan 5 speed

        // 0xa0..0xa9 vector-680 fault info = 0 (already 0).
        put(0xaa, 0x00);                                   // fans good

        // RMC read-failure DIMM bits: MMB0 read ok, MMB1..3 absent (0xff).
        put(0xab, 0x00);
        put(0xac, 0xff);
        put(0xad, 0xff);
        put(0xae, 0xff);
        switch (m_cpuCount) {                              // I2C-read + CPU present map
        case 1:  put(0xaf, 0x0e); break;
        case 2:  put(0xaf, 0x0c); break;
        case 3:  put(0xaf, 0x08); break;
        default: put(0xaf, 0x00); break;                  // 4 CPUs
        }

        put(0xb0, 0x00);                                   // CPB (PCI backplane) I2C EEROM read: OK
        put(0xb1, 0x00);                                   // CSB (motherboard) I2C EEROM read: OK
        put(0xb2, 0x00);                                   // SCSI backplane read status: OK
        put(0xba, 0x00);                                   // RMC power-on error (0=ok; 1=flash corrupted)
        put(0xbb, 0x00);                                   // RMC flash update error status
        put(0xbc, 0x00);                                   // copy of PS input value
        put(0xbd, 0x00);                                   // I/O-expander byte (fatal errors)
        put(0xbe, 0x00);                                   // reason for system failure

        // EK-ES240-SV.A01 Table C-1: the I2C-done "0xBA = finished" flag lives at
        // location 0xD9 (AXPBox mislocated it at 0xBA -- corrected here).
        put(0xd9, 0xba);                                   // I2C done = finished

        put(0xda, 0xaa);                                   // tig load SUCCESS -- the byte the
                                                           // pc264 SRM checks; != 0xaa -> the
                                                           // "TIG load failure" console error.

        // Power supplies 0..2 telemetry blocks.
        put(0xdb, 0xf4); put(0xdc, 0x45); put(0xdd, 0x51); put(0xde, 0x37);
        put(0xdf, 0x8b); put(0xe0, 0xd6); put(0xe1, 0x49); put(0xe2, 0x4b);
        put(0xe4, 0xf5); put(0xe5, 0x45); put(0xe6, 0x51); put(0xe7, 0x37);
        put(0xe8, 0x8b); put(0xe9, 0xd6); put(0xea, 0x49); put(0xeb, 0x4b);
        put(0xed, 0xf6); put(0xee, 0x45); put(0xef, 0x51); put(0xf0, 0x37);
        put(0xf1, 0x8b); put(0xf2, 0xd6); put(0xf3, 0x49); put(0xf4, 0x4b);

        // SROM version string 0x3000.."V2.22G".
        static constexpr char kSromVer[] = { 'V','2','.','2','2','G' };
        for (std::size_t k = 0; k < sizeof(kSromVer); ++k)
            put(0x3000 + k, static_cast<uint8_t>(kSromVer[k]));
        // 0x3006..0x3008 remain 0 (string terminator/pad).

        // RMC on-chip code rev "V10" (letter + major/minor bytes).
        put(0x3009, 'V'); put(0x300a, 0x31); put(0x300b, 0x30);
        // RMC flash code rev "V10".
        put(0x300c, 'V'); put(0x300d, 0x31); put(0x300e, 0x30);
        // 0x300F:3010 -- Revision Field of the DPR structure (EK-ES240-SV.A01
        // Table C-1).  Left 0: the Guide names the field but not a rev value.

        put(0x3400, 0x08);                                 // SROM: Bcache size in MB
        put(0x3401, 0x08);                                 // SROM: Flash SROM valid (8=valid,0=invalid)
        put(0x3402, 0x00);                                 // SROM: errors determined by SROM

        for (int i = 0; i < m_cpuCount; ++i)
            put(0x3418 + 0x10 * static_cast<std::size_t>(i), 0xff); // waiting-to-jump flag

        for (std::size_t i = 0; i < 0x20; ++i)             // array->DIMM ID translation
            put(0x34a0 + i, static_cast<uint8_t>(i));
    }

    // ---- RMC mailbox command dispatch (AXPBox CDPR::WriteMem port) -----------
    // Invoked after the byte at index `a` is stored.  The host firmware writes a
    // command by filling 0xf9..0xfe then poking 0xff; the RMC side (us) reacts
    // and posts a completion code in 0xfc.  Exercised by interactive RMC
    // commands (show fru / set ocp / update flash), NOT the cold boot path.
    void command(std::size_t a) noexcept {
        switch (a) {
        case 0xff: {                                       // command trigger
            m_ram[0xfd] = m_ram[0xff];                     // echo id into response
            switch (m_ram[0xfe]) {                         // command code
            case 0x01: {                                   // FRU-Write
                uint8_t const fru = m_ram[0xfb];
                bool ok = false;
                if (fru >= 0x21 && fru <= 0x24) {          // per-CPU FRU
                    ok = ((fru - 0x20) <= m_cpuCount);
                } else {
                    switch (fru) {                         // valid FRU targets
                    case 0x01: case 0x02: case 0x03: case 0x04:
                    case 0x05: case 0x06: case 0x07: case 0x08:
                    case 0x25: case 0x26: case 0x27: case 0x28:
                    case 0x29: case 0x2a:
                    case 0x31: case 0x32: case 0x33:
                    case 0x3b: case 0x3c: case 0x3d: case 0x3e: case 0x3f:
                        ok = true; break;
                    default: ok = false; break;
                    }
                }
                if (ok) {
                    std::size_t const dstBase =
                        static_cast<std::size_t>(fru) * 0x100 + m_ram[0xfa];
                    std::size_t const srcBase = 0x3500u + m_ram[0xfa];
                    int const n = static_cast<int>(m_ram[0xf9]) + 1;
                    for (int i = 0; i < n; ++i) {
                        std::size_t const d = dstBase + static_cast<std::size_t>(i);
                        std::size_t const s = srcBase + static_cast<std::size_t>(i);
                        if (d < kRamSize && s < kRamSize)  // bounds guard (AXPBox lacked this)
                            m_ram[d] = m_ram[s];
                    }
                    m_ram[0xfc] = 0x00;                    // ok
                } else {
                    m_ram[0xfc] = 0x80;                    // error
                }
                break;
            }
            case 0x02:                                     // update baud rate
                m_ram[0xfc] = 0x00;
                break;
            case 0x03:                                     // OCP-Write
                m_ram[0xfc] = 0x00;
                break;
            case 0xf0:                                     // update RMC flash
                // TODO(dpr-flash-f0): AXPBox falls through to 0x81 here; we
                // return ok pending the authoritative RMC completion code.
                m_ram[0xfc] = 0x00;
                break;
            default:
                m_ram[0xfc] = 0x81;                        // invalid command code
                break;
            }
            break;
        }
        case 0xfd:                                          // end of command
            m_ram[0xff] = m_ram[0xfd];
            break;

        // Secondary-CPU start requests.  TODO(dpr-mp-start): EmulatR has no
        // CPU-release seam wired here and boots UP, so these are inert (never
        // written on a UP boot).  Wire to the real secondary bring-up for MP.
        case 0x3428:                                        // start CPU 1
        case 0x3438:                                        // start CPU 2
        case 0x3448:                                        // start CPU 3
            break;

        default:
            break;
        }
    }

    std::array<uint8_t, kRamSize> m_ram{};
    int m_cpuCount;
    int m_cpuSpeedMHz;
};

#endif  // CHIPSETLIB_TSUNAMIDPR_H
