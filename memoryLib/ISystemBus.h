// ============================================================================
// memoryLib/ISystemBus.h -- abstract system-bus seam (CPU <-> Tsunami)
// ============================================================================
// Project: EmulatR -- Alpha AXP / EV6 Architecture Emulator (V4)
// Copyright (C) 2025, 2026 eNVy Systems, Inc.  All rights reserved.
// Licensed under eNVy Systems Non-Commercial License v1.1
//
// Project Architect: Timothy Peer
// AI Collaboration:  Claude (Anthropic)
// ============================================================================
//
// The CPU core emits 44-bit physical addresses for Load, Store, and Fetch and
// knows NOTHING about system topology -- not what is RAM, not what is MMIO,
// not where RAM ends.  It issues every physical transaction through this
// abstract bus.  The concrete implementer (TsunamiChipset) is the sole
// authority on physical-address decoding: it routes a PA to backing DRAM,
// to the Pchip / MMIO windows, or -- when nothing claims the address -- raises
// a Non-eXistent-Memory (NXM) machine check by latching chipset syndrome
// state and returning MemStatus::BusError.
//
// Layering: this interface lives in memoryLib (where MemStatus is defined) so
// that the CPU (pipelineLib / coreLib ExecCtx) depends only on the interface,
// never on chipsetLib.  chipsetLib depends on memoryLib to implement it; the
// graph stays acyclic once GuestMemory's MMIO hook back-edge is removed.
//
// Width is in bytes (1, 2, 4, or 8), matching GuestMemory's readN/writeN.
// ============================================================================

#ifndef MEMORYLIB_ISYSTEMBUS_H
#define MEMORYLIB_ISYSTEMBUS_H

#include <cstdint>

namespace memoryLib {

// ---------------------------------------------------------------------------
// BusStatus -- bus-transaction outcome.  The bus owns the fault taxonomy;
// memoryLib::MemStatus is now the dumb store's trivial Ok-only return, so
// BusResult uses its own status rather than depending on GuestMemory.
// ---------------------------------------------------------------------------
//   Ok       : transaction completed; BusResult.data holds the read payload
//              (0 on a write).
//   BusError : NXM -- no claimant decoded the PA; the chipset has latched
//              MISC.NXM / NXM_SRC.  The CPU translates this into
//              kFaultBusError -> MCHK trap delivery.
// ---------------------------------------------------------------------------
enum class BusStatus : uint8_t {
    Ok       = 0,
    BusError = 1,
};

struct BusResult {
    BusStatus status = BusStatus::Ok;
    uint64_t  data   = 0;
};

// ---------------------------------------------------------------------------
// ISystemBus -- the single CPU<->system seam.
// ---------------------------------------------------------------------------
// read  : data load.  Honours the LL/SC monitor only via separate set/clear
//         on the locked variants (the bus owns the monitor); a plain read has
//         no reservation side effect.
// write : data store.  The implementer invalidates any LL/SC reservation on
//         the touched cache line as a side effect of every write.
// fetch : instruction-stream read.  Bypasses LL/SC entirely and is the seam an
//         IFetchOverride (SRM-stub fetch) layers onto.
//
// All three take a full 44-bit PA and a byte width; none take a cpuId in this
// first cut (single-CPU integration), but the implementer defaults MISC.CPUID
// / NXM_SRC to 0 and the signature is forward-compatible with an added cpuId.
// ---------------------------------------------------------------------------
class ISystemBus {
public:
    virtual ~ISystemBus() = default;

    [[nodiscard]] virtual BusResult read (uint64_t pa, uint8_t width) noexcept = 0;
    [[nodiscard]] virtual BusResult write(uint64_t pa, uint64_t value,
                                          uint8_t width) noexcept = 0;
    [[nodiscard]] virtual BusResult fetch(uint64_t pa, uint8_t width) noexcept = 0;
};

} // namespace memoryLib

#endif // MEMORYLIB_ISYSTEMBUS_H
