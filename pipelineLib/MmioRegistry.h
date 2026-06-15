// ============================================================================
// MmioRegistry.h -- Data-driven MMIO range dispatch
// ============================================================================
// Project: ASA-EMulatR - Alpha AXP Architecture Emulator
// Copyright (C) 2025, 2026 eNVy Systems, Inc. All rights reserved.
// Licensed under eNVy Systems Non-Commercial License v1.1
//
// Project Architect: Timothy Peer
// AI Code Generation: Claude (Anthropic)
// ============================================================================
//
// TODO(deprecated) 2026-05-14:
//   This class is no longer wired into the production MMIO routing
//   path.  Machine.cpp now attaches the chipset directly to
//   GuestMemory's MMIO hooks via a thin offset-translator adapter
//   (see Machine.cpp::machineMmioRead/Write).  The registry abstraction
//   added indirection without flexibility V4 needs -- single Tsunami
//   chipset, single MMIO window.
//
//   Retained only because test_mmio_csc_roundtrip.cpp continues to
//   exercise the registry in isolation.  When that test migrates to
//   the direct attach path, both this header and the registry's
//   call sites can be removed.
//
//   Replacement: GuestMemory::attachMmioHooks(ctx, readFn, writeFn)
//   with the readFn/writeFn shape uint64_t(void*, uint64_t, uint8_t)
//   and void(void*, uint64_t, uint64_t, uint8_t) -- identical to the
//   per-device static handlers already exported by TsunamiChipset.
//
// ============================================================================
//
// PURPOSE:
//   PA-keyed function-pointer registry for MMIO surfaces. Devices register
//   a (startPa, endPa, ctx, readFn, writeFn, name) tuple at boot. The
//   pipeline's MEM stage consults the registry on every load/store before
//   falling through to GuestMemory; if a range matches, the device handler
//   is invoked with the offset within the range and the access width.
//
// SHAPE:
//   readFn(void* ctx, uint64_t offset, uint8_t width) -> uint64_t
//   writeFn(void* ctx, uint64_t offset, uint64_t value, uint8_t width) -> void
//
//   This is identical to the static handler shape already exported by
//   TsunamiChipset::mmioRead / mmioWrite, TsunamiCchip, TsunamiDchip,
//   TsunamiPchip -- they plug in with no shim.
//
// OFFSET CONTRACT:
//   The registry subtracts range.startPa from the absolute PA before
//   invoking the device handler. Devices always receive offsets within
//   their own region, never absolute PAs. This means a device does not
//   need to track its own base PA -- the base is owned by the registry
//   record. When firmware reassigns a PCI BAR, the registry record is
//   re-registered at the new base; the device code is unchanged.
//
// DISPATCH COST:
//   Linear scan of the range vector per access. For N up to about 32
//   the scan is dominated by the cost of the device handler itself.
//   If N grows past that, MmioRegistry should switch to a sorted vector
//   plus binary search; that is a private implementation change behind
//   the same public surface.
//
// NOT THREAD SAFE:
//   Single-threaded by design. Registration happens at boot, lookups
//   happen on the pipeline thread. Multi-threaded register/dispatch
//   would require a different storage strategy (e.g. RCU or shared_mutex).
// ============================================================================

#ifndef PIPELINELIB_MMIO_REGISTRY_H
#define PIPELINELIB_MMIO_REGISTRY_H

#include <cstdint>
#include <vector>

namespace pipelineLib {

// ============================================================================
// MmioRange -- one registration record
// ============================================================================

struct MmioRange
{
    uint64_t  startPa;       // inclusive
    uint64_t  endPa;         // exclusive
    void*     ctx;           // opaque device pointer passed to readFn/writeFn
    uint64_t (*readFn)(void* ctx, uint64_t offset, uint8_t width) noexcept;
    void     (*writeFn)(void* ctx, uint64_t offset, uint64_t value, uint8_t width) noexcept;
    const char* name;        // for debug logging; may be nullptr
};

// ============================================================================
// MmioRegistry
// ============================================================================

class MmioRegistry
{
public:
    MmioRegistry() noexcept = default;

    // ------------------------------------------------------------------------
    // Registration
    // ------------------------------------------------------------------------

    /**
     * @brief Register an MMIO range
     *
     * No range overlap check is performed. The first range that matches
     * a given PA wins, so register more-specific ranges before broader ones
     * if you need overlap behavior.
     */
    void registerRange(MmioRange const& range) noexcept
    {
        m_ranges.push_back(range);
    }

    /**
     * @brief Look up the range covering pa, or nullptr if none
     */
    MmioRange const* findRange(uint64_t pa) const noexcept
    {
        for (auto const& r : m_ranges) {
            if (pa >= r.startPa && pa < r.endPa) return &r;
        }
        return nullptr;
    }

    // ------------------------------------------------------------------------
    // Dispatch
    // ------------------------------------------------------------------------

    /**
     * @brief Try to service a read through a registered MMIO range
     * @param  pa        Absolute physical address
     * @param  width     Access width in bytes (1, 2, 4, or 8)
     * @param  valueOut  Filled in with the device handler's return value
     * @return true if a range matched and valueOut was written;
     *         false if pa is outside every registered range
     */
    bool tryRead(uint64_t pa, uint8_t width, uint64_t& valueOut) const noexcept
    {
        for (auto const& r : m_ranges) {
            if (pa >= r.startPa && pa < r.endPa) {
                valueOut = r.readFn(r.ctx, pa - r.startPa, width);
                return true;
            }
        }
        return false;
    }

    /**
     * @brief Try to service a write through a registered MMIO range
     * @return true if a range matched (write was dispatched);
     *         false if pa is outside every registered range
     */
    bool tryWrite(uint64_t pa, uint64_t value, uint8_t width) const noexcept
    {
        for (auto const& r : m_ranges) {
            if (pa >= r.startPa && pa < r.endPa) {
                r.writeFn(r.ctx, pa - r.startPa, value, width);
                return true;
            }
        }
        return false;
    }

    // ------------------------------------------------------------------------
    // Introspection
    // ------------------------------------------------------------------------

    size_t rangeCount() const noexcept { return m_ranges.size(); }

    MmioRange const* rangeAt(size_t i) const noexcept
    {
        return (i < m_ranges.size()) ? &m_ranges[i] : nullptr;
    }

    void clear() noexcept { m_ranges.clear(); }

private:
    std::vector<MmioRange> m_ranges;
};

} // namespace pipelineLib

#endif // PIPELINELIB_MMIO_REGISTRY_H
