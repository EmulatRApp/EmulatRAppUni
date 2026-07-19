// ============================================================================
// VgaTextConsole.h -- minimal VGA text-console interface (absorb + snapshot)
// ============================================================================
// Project: ASA-EMulatR - Alpha AXP Architecture Emulator
// Copyright (C) 2025, 2026 eNVy Systems, Inc. All rights reserved.
// Licensed under eNVy Systems Non-Commercial License v1.1
//
// Project Architect: Timothy Peer
// AI Code Generation: Claude (Anthropic)
// ============================================================================
//
// PURPOSE (JRN-VMB-006, 2026-07-18):
//   The DS20/DS10/DS25/ES40/ES45 SRM firmware drives a PC/VGA color-text
//   console: it paints character cells into the legacy framebuffer at PCI
//   memory 0xB8000-0xBFFFF and touches the VGA register file at I/O
//   0x3B0-0x3DF.  V5 modeled NO VGA, so every 0xB8000 write fell through the
//   TsunamiPchip outer path as UNHANDLED OUTER WRITE (writes lost).  This
//   device CLAIMS both apertures so those accesses land instead of faulting,
//   which is the concrete thing standing between the firmware's console-init
//   and forward progress toward the OS handoff.
//
// SCOPE (deliberately NOT a faithful VGA -- see the JRN-VMB-006 decision):
//   - Retain the text framebuffer (character + attribute bytes) so writes are
//     stored, not dropped.
//   - Answer the VGA register I/O benignly: store index/data registers, and
//     return a TOGGLING Input-Status-1 (0x3BA/0x3DA) so any firmware
//     vertical-retrace poll terminates whichever polarity it waits on.
//   - Offer a gated text-screen SNAPSHOT (env EMULATR_VGA_DUMP=<path>) written
//     at teardown, so what SRM painted is observable WITHOUT a host window,
//     display thread, or Qt surface.  Deterministic; no timing, no rendering.
//
// DEFERRED (tracked in journals/20260718_JRN-VMB-006_..., TODO table):
//   TODO(vga-graphics-tier): planar 4-plane graphics modes, CRTC timing, DAC
//     palette-to-pixel.  Only needed for a guest-OS graphics head.
//   TODO(vga-full-aperture): the 0xA0000-0xB0000 non-text aperture (needs the
//     TsunamiPchip 64 KiB registerPciMemRange span split).
//   TODO(vga-qt-view): optional read-only Qt view of the text framebuffer.
//
// INTERFACE:
//   Registered twice by TsunamiChipset::wireDevices():
//     memHandler() -> registerPciMemRange(0xB8000, 0xC0000, ...) -- the mem
//       handler receives the REBASED offset (0x0000-0x7FFF) in the port
//       parameter, per the IDeviceHandlers.h PciMemRange contract.
//     ioHandler()  -> registerIoPortRange(0x3B0, 0x3E0, ...) -- the I/O handler
//       receives the real VGA register port (0x3B0-0x3DF).
//   Two sub-handlers are used because a single IIoPortHandler could not tell
//   an I/O port 0x3C0 from a memory offset 0x3C0 (the port-parameter ranges
//   overlap).
//
// REFERENCES:
//   IBM VGA register model; AXPBox S3Trio64.cpp (text window 0xB8000, char
//   plane / attribute plane) as a cross-check only -- HRM is authoritative.
// ============================================================================

#ifndef DEVICELIB_TSUNAMI_VGATEXTCONSOLE_H
#define DEVICELIB_TSUNAMI_VGATEXTCONSOLE_H

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include "chipsetLib/IDeviceHandlers.h"  // IIoPortHandler

// ============================================================================
// VgaTextConsole -- text framebuffer (0xB8000) + VGA register I/O (0x3B0-0x3DF)
// ============================================================================
class VgaTextConsole
{
public:
    // Legacy color-text framebuffer window: 0xB8000-0xBFFFF (32 KiB).
    static constexpr uint32_t kTextWindowBytes = 0x8000;

    VgaTextConsole() noexcept
    {
        // Wire the sub-handlers back to this owner in the ctor BODY (not a
        // default member initializer) so MSVC never sees `this` in an NSDMI.
        m_memHandler.owner = this;
        m_ioHandler.owner  = this;
        m_text.fill(0x00);
        m_dumpPath = std::getenv("EMULATR_VGA_DUMP");  // nullptr when unset
    }

    ~VgaTextConsole()
    {
        // Snapshot the final screen at teardown when EMULATR_VGA_DUMP names a
        // file.  Guarded so the default (unset) path does no I/O.
        if (m_dumpPath != nullptr) {
            dumpTextScreen(m_dumpPath);
        }
    }

    IIoPortHandler* ioHandler()  noexcept { return &m_ioHandler; }
    IIoPortHandler* memHandler() noexcept { return &m_memHandler; }

    // ---- text framebuffer (0xB8000 aperture; offset is window-relative) ----
    uint64_t memRead(uint16_t offset, uint8_t width) noexcept
    {
        uint64_t out = 0;
        for (uint8_t i = 0; i < width; ++i) {
            uint32_t const idx = static_cast<uint32_t>(offset) + i;
            uint8_t const b = (idx < kTextWindowBytes) ? m_text[idx] : 0x00;
            out |= static_cast<uint64_t>(b) << (8u * i);
        }
        return out;
    }

    void memWrite(uint16_t offset, uint64_t value, uint8_t width) noexcept
    {
        for (uint8_t i = 0; i < width; ++i) {
            uint32_t const idx = static_cast<uint32_t>(offset) + i;
            if (idx < kTextWindowBytes) {
                m_text[idx] =
                    static_cast<uint8_t>((value >> (8u * i)) & 0xFFu);
                m_dirty = true;
            }
        }
    }

    // ---- VGA register I/O (0x3B0-0x3DF) ------------------------------------
    uint64_t portRead(uint16_t port, uint8_t /*width*/) noexcept
    {
        switch (port) {
        case 0x3C0: return m_attrIndex;                     // attribute index
        case 0x3C1: return m_attrData[m_attrIndex & 0x1Fu]; // attribute data
        case 0x3C4: return m_seqIndex;                      // sequencer index
        case 0x3C5: return m_seqData[m_seqIndex & 0x1Fu];   // sequencer data
        case 0x3C6: return 0xFF;                            // PEL mask (default)
        case 0x3C8: return m_dacIndex;                      // DAC write index
        case 0x3C9: return 0x00;                            // DAC data (not modeled)
        case 0x3CC: return m_miscOutput;                    // misc output (read)
        case 0x3CE: return m_gfxIndex;                      // graphics index
        case 0x3CF: return m_gfxData[m_gfxIndex & 0x0Fu];   // graphics data
        case 0x3B4: case 0x3D4: return m_crtcIndex;         // CRTC index
        case 0x3B5: case 0x3D5:                             // CRTC data
            return m_crtcData[m_crtcIndex & 0x3Fu];
        case 0x3BA: case 0x3DA:                             // input status 1
            // Toggle display-enable (bit0) and vertical-retrace (bit3) each
            // read so ANY firmware retrace-wait loop terminates regardless of
            // the polarity it waits on.  The read also resets the attribute
            // controller address/data flip-flop (real VGA behavior).
            m_attrFlipFlop = false;
            m_statusToggle ^= 0x09u;
            return m_statusToggle;
        default:
            return 0x00;                                    // other regs: benign 0
        }
    }

    void portWrite(uint16_t port, uint64_t value, uint8_t /*width*/) noexcept
    {
        uint8_t const v = static_cast<uint8_t>(value & 0xFFu);
        switch (port) {
        case 0x3C0:                                         // attribute: idx/data
            if (!m_attrFlipFlop) { m_attrIndex = v & 0x1Fu; }
            else                 { m_attrData[m_attrIndex & 0x1Fu] = v; }
            m_attrFlipFlop = !m_attrFlipFlop;
            break;
        case 0x3C2: m_miscOutput = v; break;                // misc output (write)
        case 0x3C4: m_seqIndex = v; break;
        case 0x3C5: m_seqData[m_seqIndex & 0x1Fu] = v; break;
        case 0x3C8: m_dacIndex = v; break;                  // DAC write index
        case 0x3CE: m_gfxIndex = v; break;
        case 0x3CF: m_gfxData[m_gfxIndex & 0x0Fu] = v; break;
        case 0x3B4: case 0x3D4: m_crtcIndex = v; break;
        case 0x3B5: case 0x3D5:
            m_crtcData[m_crtcIndex & 0x3Fu] = v; break;
        default: break;                                     // absorb the rest
        }
    }

    // Decode the color-text framebuffer (character at even byte, attribute at
    // odd) to an 80-column ASCII screen and append it to path.  Called at
    // teardown when EMULATR_VGA_DUMP names a file.  Deterministic; no timing.
    void dumpTextScreen(char const* path) const noexcept
    {
        if (path == nullptr) return;
        std::FILE* f = std::fopen(path, "ab");
        if (f == nullptr) return;
        constexpr uint32_t kCols = 80;
        constexpr uint32_t kRows = 50;   // upper bound; covers 25/43/50-row modes
        std::fprintf(f, "==== EMULATR VGA text framebuffer (0xB8000) ====\n");
        for (uint32_t r = 0; r < kRows; ++r) {
            char line[kCols + 1];
            for (uint32_t c = 0; c < kCols; ++c) {
                uint32_t const idx = (r * kCols + c) * 2u;   // char plane (even)
                uint8_t const ch =
                    (idx < kTextWindowBytes) ? m_text[idx] : 0x20;
                line[c] = (ch >= 0x20 && ch <= 0x7E)
                        ? static_cast<char>(ch) : ' ';
            }
            line[kCols] = '\0';
            std::fprintf(f, "%s\n", line);
        }
        std::fprintf(f, "==== end VGA framebuffer ====\n");
        std::fclose(f);
    }

private:
    // Sub-handlers forwarding the Pchip dispatch to the owner's typed methods.
    struct MemHandler : IIoPortHandler {
        VgaTextConsole* owner = nullptr;
        uint64_t ioRead(uint16_t port, uint8_t width) override {
            return owner->memRead(port, width);
        }
        void ioWrite(uint16_t port, uint64_t value, uint8_t width) override {
            owner->memWrite(port, value, width);
        }
    };
    struct IoHandler : IIoPortHandler {
        VgaTextConsole* owner = nullptr;
        uint64_t ioRead(uint16_t port, uint8_t width) override {
            return owner->portRead(port, width);
        }
        void ioWrite(uint16_t port, uint64_t value, uint8_t width) override {
            owner->portWrite(port, value, width);
        }
    };

    MemHandler m_memHandler;
    IoHandler  m_ioHandler;

    std::array<uint8_t, kTextWindowBytes> m_text{};   // 0xB8000 text/attr bytes
    std::array<uint8_t, 0x40> m_crtcData{};
    std::array<uint8_t, 0x20> m_seqData{};
    std::array<uint8_t, 0x20> m_attrData{};
    std::array<uint8_t, 0x10> m_gfxData{};
    uint8_t     m_crtcIndex   = 0x00;
    uint8_t     m_seqIndex    = 0x00;
    uint8_t     m_attrIndex   = 0x00;
    uint8_t     m_gfxIndex    = 0x00;
    uint8_t     m_dacIndex    = 0x00;
    uint8_t     m_miscOutput  = 0x00;
    uint8_t     m_statusToggle = 0x00;
    bool        m_attrFlipFlop = false;
    bool        m_dirty        = false;
    char const* m_dumpPath     = nullptr;   // EMULATR_VGA_DUMP target (or null)
};

#endif // DEVICELIB_TSUNAMI_VGATEXTCONSOLE_H
