// ============================================================================
// HwrpbBuilder.h -- lay down a fully-populated HWRPB into a guest physical page
// ============================================================================
// Project: EmulatR -- Alpha AXP / EV6 Architecture Emulator (V4)
// Copyright (C) 2025, 2026 eNVy Systems, Inc.  All rights reserved.
// Licensed under eNVy Systems Non-Commercial License v1.1
//
// Project Architect: Timothy Peer
// AI Collaboration:  Claude (Anthropic)
//
// Commercial use prohibited without separate license.
// Contact:        peert@envysys.com  |  https://envysys.com
// Documentation:  https://timothypeer.github.io/ASA-EMulatR-Project/
// ============================================================================
//
// Composes the firmware-OS data contract.  The builder takes a flat byte
// buffer (which the firmware then maps at the guest's HWRPB physical
// address, conventionally PFN 0) and writes:
//
//   [HwrpbHeader]                              320 bytes
//   [PerCpuSlot]  x cpu_count                  ~0x460 each
//   [CtbHeader]                                size_of_ctb (256-512+ bytes)
//   [CrbHeader]   + [CrbIoEntry] x io_count    48 + 24*N
//   [MemoryDescriptor]                         (24 + 56*cluster_count) bytes
//
// Then computes and stores the MEMDSC and HWRPB checksums.
//
// Section offsets are recorded in the appropriate HwrpbHeader::*_offset
// fields so the OS can locate everything at runtime.
//
// This file does NOT touch GuestMemory directly -- it operates on a
// caller-supplied byte buffer.  The caller (FirmwareDeviceManager
// Phase 0, or a unit test) is responsible for placing the buffer at the
// correct guest physical address.  This keeps the builder dependency-
// light and unit-testable in isolation.
//
// References:
//   - alpha_arch_ref.txt Section 26.1 (HWRPB layout)
//   - apu_hwrpb_def.h   (Digital SDL field-by-field)
//   - palcode_dsgn_gde.txt (PALcode bootstrap algorithm)
// ============================================================================

#ifndef EMULATR_DEVICELIB_HWRPB_BUILDER_H
#define EMULATR_DEVICELIB_HWRPB_BUILDER_H

#include "Hwrpb.h"
#include <cstddef>
#include <cstdint>

namespace deviceLib {
namespace hwrpb {

// ----------------------------------------------------------------------------
// Per-CPU configuration knobs supplied to the builder.  The builder will
// initialize each PerCpuSlot from one of these.  Length must equal
// HwrpbBuildSpec::cpu_count.
// ----------------------------------------------------------------------------
struct PerCpuConfig {
    uint64_t whami;             // CPU identifier (0..cpu_count-1)
    CpuType  cpu_type;          // EV4 / EV5 / EV6 / EV67 / ...
    uint64_t cpu_var;           // implementation variation (0 = default)
    uint64_t cpu_rev;           // silicon revision
    uint64_t pal_mem_pa;        // physical address of this CPU's PALcode image
    uint64_t pal_mem_len;       // length in bytes
    uint64_t pal_scr_pa;        // physical address of PAL scratch region
    uint64_t pal_scr_len;       // length in bytes
    uint32_t pal_rev;           // PALcode revision (e.g., 0x0140 for v1.40)
    uint32_t pal_var;           // PALcode variation (e.g., personality bits)
    uint64_t logout_mem_pa;     // physical address of machine-check logout area
    uint64_t logout_length;     // length in bytes
    bool     present;           // CPU physically present in this configuration
    bool     available;         // CPU eligible to run code
    bool     primary;           // the boot CPU
};

// ----------------------------------------------------------------------------
// One memory cluster description; the builder will pack these into the
// MemoryDescriptor at MEMDSC offset.
// ----------------------------------------------------------------------------
struct MemoryClusterSpec {
    uint64_t start_pfn;         // first page-frame number in this cluster
    uint64_t pfn_count;          // number of pages
    uint64_t usage;              // 0 = OS-usable; non-zero = console/firmware
};

// ----------------------------------------------------------------------------
// Top-level builder spec.
// ----------------------------------------------------------------------------
struct HwrpbBuildSpec {
    // ---- Identity ----
    uint64_t   hwrpb_pa;            // physical address where HWRPB will live
    SystemType system_type;         // Tsunami / Nautilus / ...
    uint64_t   system_variation;    // platform sub-variant
    uint64_t   system_revision;     // 4-char ASCII revision
    uint64_t   serial_number[2];    // 16-byte octaword (10 ASCII + pad)

    // ---- Identity / clocking ----
    uint64_t   intrclock_freq_x4096;  // interval clock interrupts/sec * 4096
    uint64_t   cycle_count_freq_hz;   // RPCC tick rate

    // ---- CPUs ----
    uint64_t            primary_cpu_id;  // WHAMI of boot CPU
    PerCpuConfig const* cpus;            // array of cpu_count entries
    uint64_t            cpu_count;

    // ---- Memory map ----
    MemoryClusterSpec const* clusters;
    uint64_t                 cluster_count;   // 1..3 per AARM

    // ---- Console (CTB) ----
    ConsoleType console_type;
    uint64_t    console_unit;
    uint64_t    console_dev_ipl;
    uint64_t    console_putchar_callback_pa;  // OS-side putchar entry

    // ---- Console Routine Block (CRB) ----
    uint64_t crb_dispatch_va;       // OS-callable firmware entry, virtual
    uint64_t crb_dispatch_pa;       // ditto, physical
    uint64_t crb_fixup_va;          // VA-fixup routine, virtual
    uint64_t crb_fixup_pa;          // ditto, physical
    uint64_t crb_io_pages_to_map;   // total page count of mapped firmware I/O
    CrbIoEntry const* crb_io_entries;
    uint64_t          crb_io_entry_count;

    // ---- Architectural metadata ----
    uint64_t pa_size_bits;          // physical-address width
    uint64_t max_valid_asn;         // maximum ASN this CPU honors
    uint64_t vptb_va;               // virtual page-table base
    uint64_t hwrpb_revision;        // default to kHwrpbRevisionCurrent if 0
};

// ----------------------------------------------------------------------------
// Lay out the HWRPB into a flat buffer.  The buffer is treated as a
// contiguous chunk of guest physical memory starting at spec.hwrpb_pa
// with a logical base address of buffer[0].  The builder writes:
//
//   1. HwrpbHeader at +0
//   2. PerCpuSlot array at the byte after the header
//   3. CtbHeader after the SLOT array
//   4. CrbHeader + CrbIoEntry array after the CTB
//   5. MemoryDescriptor after the CRB
//
// The corresponding *_offset fields in the header are populated as each
// section is laid down.  Checksums are computed last.
//
// Returns: total bytes written (the HWRPB's effective size).
// Returns 0 on validation error (insufficient buffer, invalid spec).
// ----------------------------------------------------------------------------
[[nodiscard]] size_t populateHwrpb(uint8_t*               buffer,
                                   size_t                 bufferSize,
                                   HwrpbBuildSpec const&  spec) noexcept;

// ----------------------------------------------------------------------------
// Default per-CPU spec for a single-CPU Tsunami-class EV6 system.
// Use as a starting point; override fields as platform requires.
// ----------------------------------------------------------------------------
constexpr PerCpuConfig defaultEv6PrimaryCpu() noexcept {
    PerCpuConfig c{};
    c.whami          = 0;
    c.cpu_type       = CpuType::EV6;
    c.cpu_var        = 0;
    c.cpu_rev        = 0;
    c.pal_mem_pa     = 0;        // populated when PALcode image is loaded
    c.pal_mem_len    = 0;
    c.pal_scr_pa     = 0;
    c.pal_scr_len    = 0;
    c.pal_rev        = 0;
    c.pal_var        = 0;
    c.logout_mem_pa  = 0;
    c.logout_length  = 0;
    c.present        = true;
    c.available      = true;
    c.primary        = true;
    return c;
}

}  // namespace hwrpb
}  // namespace deviceLib

#endif  // EMULATR_DEVICELIB_HWRPB_BUILDER_H
