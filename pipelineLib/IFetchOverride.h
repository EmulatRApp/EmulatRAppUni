// ============================================================================
// pipelineLib/IFetchOverride.h -- IF-stage instruction-fetch override
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
// Interface PipelineDriver consults at the IF stage before reading the
// instruction word from GuestMemory.  Returning true short-circuits
// the GuestMemory.read4 path -- the caller uses `out` directly as the
// fetched encoded word.  Returning false leaves the GuestMemory path
// to handle the fetch normally.
//
// The motivating use is V1's documented SRM-decompressor I-cache
// coherency: the firmware's copy loop overwrites its own instructions
// in guest memory, but real Alpha hardware keeps fetching the
// original bytes from the I-cache until an explicit IMB.  V4 has no
// I-cache, so without this override the IBox reads the corrupted
// bytes from guest memory and decodes garbage (typically zeros, which
// dispatch as CALL_PAL HALT and terminate the run).
//
// Concrete implementation: Machine inherits from IFetchOverride and
// returns true with bytes read from the immutable SRM payload buffer
// whenever the requested PA falls inside the loaded stub range.
//
// Lifetime: the override pointer passed to PipelineDriver::step/run
// must outlive the call.  Machine owns its own implementation, so
// passing `this` is sufficient and safe.
//
// ============================================================================

#ifndef PIPELINELIB_IFETCHOVERRIDE_H
#define PIPELINELIB_IFETCHOVERRIDE_H

#include <cstdint>

namespace pipelineLib {

class IFetchOverride
{
public:
    virtual ~IFetchOverride() = default;

    // Pre-fetch hook.  Called by PipelineDriver immediately before each
    // IF stage read, with the physical address the IBox is about to
    // fetch from.  Default no-op.  Concrete implementations use this
    // to fire one-shot side effects gated on the CPU reaching a
    // specific PC -- the canonical case being SRM PAL image relocation
    // (Step D of the SrmLoader protocol): when the decompressor stub
    // is about to JSR into its decompressed-but-not-yet-relocated
    // entry, the host emulator copies the mirror PA window up to
    // palBase so the impending fetch lands in real PALcode.
    //
    // Non-const: implementations may mutate guest memory and per-CPU
    // bookkeeping.  Cost is one virtual call per IF cycle; the common
    // post-trigger fast path inside the implementation should be a
    // boolean-flag early-out so steady-state cost is one branch.
    virtual void onBeforeFetch(uint64_t pa) noexcept { (void)pa; }

    // Try to satisfy an IF read at physical address `pa`.  On a hit,
    // populate `out` with the 32-bit instruction word and return true.
    // On a miss, return false and the caller falls back to GuestMemory.
    virtual bool tryFetch(uint64_t pa, uint32_t& out) const noexcept = 0;
};

} // namespace pipelineLib

#endif // PIPELINELIB_IFETCHOVERRIDE_H
