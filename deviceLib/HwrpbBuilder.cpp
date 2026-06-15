// ============================================================================
// HwrpbBuilder.cpp -- implementation of the HWRPB layout writer
// ============================================================================
// Project: EmulatR -- Alpha AXP / EV6 Architecture Emulator (V4)
// Copyright (C) 2025, 2026 eNVy Systems, Inc.  All rights reserved.
// Licensed under eNVy Systems Non-Commercial License v1.1
// ============================================================================
//
// See HwrpbBuilder.h for the API contract and design notes.
//
// Layout strategy:
//
//   The builder uses a simple bump-pointer allocator over the supplied
//   buffer.  Each section is quadword-aligned (Alpha requires this).  We
//   write directly through the buffer pointer (placement-new on POD is
//   safe; we use plain memcpy for portability).
//
//   The bump-pointer's offset into the buffer becomes the
//   HwrpbHeader::*_offset value for each section.  The header is written
//   FIRST (with placeholder offsets) and then PATCHED at the end once all
//   downstream sections have been placed and the offsets are known.
//
// ============================================================================

#include "HwrpbBuilder.h"

#include <cstring>

namespace deviceLib {
namespace hwrpb {

// ----------------------------------------------------------------------------
// Quadword-aligned bump pointer over a flat byte buffer.  Returns 0 on
// overflow; the caller checks the return value.
// ----------------------------------------------------------------------------
namespace {

constexpr size_t kQuadwordAlign = 8;

// Round size up to a quadword boundary (Alpha SDL fields are all 8-byte
// aligned; Alpha hardware faults on unaligned access without UAC fixup).
constexpr size_t alignUp(size_t n, size_t a) noexcept {
    return (n + (a - 1)) & ~(a - 1);
}

// Bump cursor `at` by `bytes` (rounded up to quadword), checking for
// overflow against `total`.  Returns new cursor or 0 on overflow.
[[nodiscard]] size_t bump(size_t at, size_t bytes, size_t total) noexcept {
    size_t const next = at + alignUp(bytes, kQuadwordAlign);
    return (next <= total) ? next : 0;
}

}  // namespace

// ----------------------------------------------------------------------------
// Checksum primitives.
// AARM Section 26.1 specifies a simple modulo-2^64 quadword sum over the
// covered range.
// ----------------------------------------------------------------------------
uint64_t computeHwrpbChecksum(HwrpbHeader const& hdr) noexcept {
    // Per AARM Table 26-1: checksum covers HWRPB+0 through HWRPB+280
    // inclusive (i.e., everything before the checksum field itself,
    // 280/8 = 35 quadwords + 1 = 36 quadwords inclusive).
    uint64_t const* p = reinterpret_cast<uint64_t const*>(&hdr);
    uint64_t sum = 0;
    constexpr size_t kCovered = 36;   // qwords 0..35 cover bytes 0..287
    for (size_t i = 0; i < kCovered; ++i) {
        sum += p[i];
    }
    return sum;
}

uint64_t computeMemDscChecksum(MemoryDescriptor const& md) noexcept {
    // MEMDSC checksum covers everything after the checksum field itself.
    // Sum reserved + cluster_count + cluster[0..N-1] qwords.
    uint64_t sum = md.reserved + md.cluster_count;
    uint64_t const* clusters =
        reinterpret_cast<uint64_t const*>(&md.cluster[0]);
    constexpr size_t kQwordsPerCluster = sizeof(MemoryCluster) / sizeof(uint64_t);
    size_t const total_qwords = md.cluster_count * kQwordsPerCluster;
    for (size_t i = 0; i < total_qwords; ++i) {
        sum += clusters[i];
    }
    return sum;
}

// ----------------------------------------------------------------------------
// Per-CPU SLOT initializer.
// ----------------------------------------------------------------------------
namespace {

void writeSlot(PerCpuSlot* slot, PerCpuConfig const& cfg) noexcept {
    std::memset(slot, 0, sizeof(PerCpuSlot));

    // HWPCB starts zeroed.  PTBR=0 means no MMU yet (boot ABI).  The OS
    // will SWPCTX to install its own HWPCB once it takes control.

    // State flags: assemble via the typed bit-field interface.  Bit
    // positions match the canonical apu_hwrpb_def.h SLOT$R_STATE_FLAGS
    // layout exactly so the OS's STATE-struct unpacking reads the
    // right values.
    slot->state.raw          = 0;
    slot->state.present      = cfg.present  ? 1 : 0;
    slot->state.available    = cfg.available ? 1 : 0;
    if (cfg.primary) {
        slot->state.bootstrap   = 1;   // BIP -- this CPU is bootstrapping
        slot->state.restart_cap = 1;   // RC  -- OS may restart on this CPU
    }
    slot->state.pal_valid    = 1;      // PV  -- PALcode metadata is valid
    slot->state.pal_loaded   = 1;      // PL  -- PALcode is loaded
    slot->state.halt_request = HaltRequest::ColdBoot;

    // PALcode metadata.
    slot->pal_mem_len = cfg.pal_mem_len;
    slot->pal_scr_len = cfg.pal_scr_len;
    slot->pal_mem_pa  = cfg.pal_mem_pa;
    slot->pal_scr_pa  = cfg.pal_scr_pa;
    slot->pal_rev     = cfg.pal_rev;
    slot->pal_var     = cfg.pal_var;

    // CPU identity.
    slot->cpu_type = static_cast<uint64_t>(cfg.cpu_type);
    slot->cpu_var  = cfg.cpu_var;
    slot->cpu_rev  = cfg.cpu_rev;

    // Logout area for machine-check delivery.
    slot->logout_mem_pa  = cfg.logout_mem_pa;
    slot->logout_length  = cfg.logout_length;

    // Halt-context fields default to zero (filled at first halt).
    slot->halt_code = static_cast<uint64_t>(HaltCode::Bootstrap);

    // PALcode revision per personality: index 0 unused, 1=VMS, 2=OSF.
    // Populate both with the same revision until the firmware loads
    // distinct OSF and VMS PAL images.
    slot->palcode_revs[1] = cfg.pal_rev;
    slot->palcode_revs[2] = cfg.pal_rev;
}

}  // namespace

// ----------------------------------------------------------------------------
// Main builder.
// ----------------------------------------------------------------------------
size_t populateHwrpb(uint8_t*              buffer,
                     size_t                bufferSize,
                     HwrpbBuildSpec const& spec) noexcept {
    // ---- Validation ----
    if (buffer == nullptr || bufferSize < sizeof(HwrpbHeader)) {
        return 0;
    }
    if (spec.cpu_count == 0 || spec.cpus == nullptr) {
        return 0;
    }
    if (spec.cluster_count == 0 || spec.cluster_count > 3 ||
        spec.clusters == nullptr) {
        return 0;
    }
    if (spec.crb_io_entry_count > 0 && spec.crb_io_entries == nullptr) {
        return 0;
    }

    // Zero the entire buffer up-front so anything we don't explicitly
    // populate reads as zero (matches AARM's "slots for nonexistent
    // processors are zeroed" requirement and the general convention for
    // reserved regions).
    std::memset(buffer, 0, bufferSize);

    // ---- Section 1: Per-CPU SLOT array ----
    size_t cursor = alignUp(sizeof(HwrpbHeader), kQuadwordAlign);
    size_t const slot_array_offset = cursor;
    size_t const slot_array_bytes  = spec.cpu_count * sizeof(PerCpuSlot);
    cursor = bump(cursor, slot_array_bytes, bufferSize);
    if (cursor == 0) return 0;

    PerCpuSlot* slots =
        reinterpret_cast<PerCpuSlot*>(buffer + slot_array_offset);
    for (uint64_t i = 0; i < spec.cpu_count; ++i) {
        writeSlot(&slots[i], spec.cpus[i]);
    }

    // ---- Section 2: CTB ----
    size_t const ctb_offset = cursor;
    cursor = bump(cursor, sizeof(CtbHeader), bufferSize);
    if (cursor == 0) return 0;

    CtbHeader* ctb = reinterpret_cast<CtbHeader*>(buffer + ctb_offset);
    ctb->cons_type   = static_cast<uint64_t>(spec.console_type);
    ctb->cons_unit   = spec.console_unit;
    ctb->length      = sizeof(CtbHeader);
    ctb->dev_ipl     = spec.console_dev_ipl;
    ctb->tc_putchar  = spec.console_putchar_callback_pa;
    // Other CTB fields stay zero -- terminal type / font / monitor
    // geometry are populated when a graphics console is actually wired up.

    // ---- Section 3: CRB (header + I/O entries) ----
    size_t const crb_offset = cursor;
    cursor = bump(cursor, sizeof(CrbHeader), bufferSize);
    if (cursor == 0) return 0;

    CrbHeader* crb = reinterpret_cast<CrbHeader*>(buffer + crb_offset);
    crb->dispatch_va    = spec.crb_dispatch_va;
    crb->dispatch_pa    = spec.crb_dispatch_pa;
    crb->fixup_va       = spec.crb_fixup_va;
    crb->fixup_pa       = spec.crb_fixup_pa;
    crb->nbr_of_entries = spec.crb_io_entry_count;
    crb->pages_to_map   = spec.crb_io_pages_to_map;

    if (spec.crb_io_entry_count > 0) {
        size_t const io_array_bytes =
            spec.crb_io_entry_count * sizeof(CrbIoEntry);
        cursor = bump(cursor, io_array_bytes, bufferSize);
        if (cursor == 0) return 0;
        // I/O entries land immediately after the CrbHeader.
        std::memcpy(buffer + crb_offset + sizeof(CrbHeader),
                    spec.crb_io_entries,
                    io_array_bytes);
    }

    // ---- Section 4: MEMDSC ----
    size_t const mddt_offset = cursor;
    size_t const mddt_bytes =
        offsetof(MemoryDescriptor, cluster) +
        spec.cluster_count * sizeof(MemoryCluster);
    cursor = bump(cursor, mddt_bytes, bufferSize);
    if (cursor == 0) return 0;

    MemoryDescriptor* mddt =
        reinterpret_cast<MemoryDescriptor*>(buffer + mddt_offset);
    mddt->reserved      = 0;
    mddt->cluster_count = spec.cluster_count;
    for (uint64_t i = 0; i < spec.cluster_count; ++i) {
        mddt->cluster[i] = MemoryCluster{};
        mddt->cluster[i].start_pfn  = spec.clusters[i].start_pfn;
        mddt->cluster[i].pfn_count  = spec.clusters[i].pfn_count;
        mddt->cluster[i].test_count = spec.clusters[i].pfn_count;
        mddt->cluster[i].usage      = spec.clusters[i].usage;
        // bitmap_va / bitmap_pa / bitmap_checksum stay zero unless the
        // emulator publishes a per-page validity bitmap (rare).
    }
    mddt->checksum = computeMemDscChecksum(*mddt);

    // ---- Section 5: HWRPB header (filled now that all offsets known) ----
    HwrpbHeader* hdr = reinterpret_cast<HwrpbHeader*>(buffer);
    hdr->hwrpb_pa             = spec.hwrpb_pa;
    hdr->identifier           = kHwrpbIdentifier;
    hdr->revision             = (spec.hwrpb_revision != 0)
                                  ? spec.hwrpb_revision
                                  : kHwrpbRevisionCurrent;
    hdr->hwrpb_size           = cursor;
    hdr->primary_cpu_id       = spec.primary_cpu_id;
    hdr->page_size            = kAlphaPageSize;
    hdr->pa_size_bits         = static_cast<uint32_t>(spec.pa_size_bits);
    hdr->extended_va_size     = 0;
    hdr->max_valid_asn        = spec.max_valid_asn;
    hdr->system_serial_number[0] = spec.serial_number[0];
    hdr->system_serial_number[1] = spec.serial_number[1];
    hdr->system_type          = static_cast<uint64_t>(spec.system_type);
    hdr->system_variation     = spec.system_variation;
    hdr->system_revision      = spec.system_revision;
    hdr->intrclock_freq       = spec.intrclock_freq_x4096;
    hdr->cycle_count_freq     = spec.cycle_count_freq_hz;
    hdr->vptb_va              = spec.vptb_va;
    hdr->reserved_arch        = 0;

    hdr->tbb_offset           = 0;                       // no TB hint block yet
    hdr->cpu_slot_count       = spec.cpu_count;
    hdr->cpu_slot_size        = sizeof(PerCpuSlot);
    hdr->cpu_slot_offset      = slot_array_offset;
    hdr->ctb_count            = 1;
    hdr->ctb_size             = sizeof(CtbHeader);
    hdr->ctb_offset           = ctb_offset;
    hdr->crb_offset           = crb_offset;
    hdr->mddt_offset          = mddt_offset;
    hdr->cdb_offset           = 0;                       // optional, absent
    hdr->fru_offset           = 0;                       // optional, absent

    hdr->terminal_save_va     = 0;
    hdr->terminal_save_pv     = 0;
    hdr->terminal_restore_va  = 0;
    hdr->terminal_restore_pv  = 0;
    hdr->cpu_restart_va       = 0;
    hdr->cpu_restart_pv       = 0;
    hdr->reserved_software    = 0;
    hdr->reserved_hardware    = 0;
    hdr->rxtx_block[0]        = 0;
    hdr->rxtx_block[1]        = 0;
    hdr->dsrdb_offset         = 0;

    hdr->checksum             = computeHwrpbChecksum(*hdr);

    return cursor;
}

}  // namespace hwrpb
}  // namespace deviceLib
