// ============================================================================
// coreLib/VA_types.h -- shared address and access vocabulary for V4
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
// Common type aliases and small enums shared between the leaf
// executors, the translator, and the MEM-stage drainer.  Every
// participant in a memory access -- leaf, translator, page walker,
// permission check, fault classifier -- agrees on the spelling of
// "virtual address," "physical address," "access kind," and "mode"
// through this file.  Use uint64_t-backed type aliases for the address
// and ID quantities; use scoped enums for categorical values so that
// implicit-conversion bugs surface at compile time.
//
// This file is dependency-light on purpose.  It includes <cstdint>
// only, defines no functions besides small constexpr accessors, and is
// safe to pull from any V4 header without circular-include risk.
//
// ============================================================================

#ifndef CORELIB_VA_TYPES_H
#define CORELIB_VA_TYPES_H

#include <cstdint>

namespace coreLib {

// ---------------------------------------------------------------------------
// Address-and-id type aliases.
// ---------------------------------------------------------------------------
// All quantities below are 64-bit unsigned values; the aliases exist
// to make function signatures and struct fields read self-documentingly
// rather than to enforce strong typing.  When a caller has a uint64_t
// in hand, passing it to a function that takes VAType is implicit and
// intentional.
//
//   VAType   virtual address, full 64-bit (sign-extended on Alpha)
//   PAType   physical address, 44-bit on EV6 (upper bits are zero)
//   PFNType  page frame number, 28-bit on EV6 in the canonical PTE
//            layout, but stored as 64-bit for arithmetic convenience
//   ASNType  address space number, 8 bits on EV6 (DTB tag width)
//   SC_Type  size-class index, encodes the granularity hint (GH bits)
//            of a PTE -- 0 = 8KB, 1 = 64KB, 2 = 512KB, 3 = 4MB

using VAType   = uint64_t;
using PAType   = uint64_t;
using PFNType  = uint64_t;
using ASNType  = uint64_t;
using SC_Type  = uint8_t;


// ---------------------------------------------------------------------------
// Mode_Privilege -- current processor mode encoded by PS register.
// ---------------------------------------------------------------------------
// Per Alpha SRM the four privilege modes are kernel, executive,
// supervisor, and user.  EV6 hardware only distinguishes kernel from
// the rest at most translation gates, but the full four-way encoding
// is preserved for compatibility with PALcode and trace formatters.
// Numeric values match the PS<CM> field encoding so a register read
// can cast directly.
enum class Mode_Privilege : uint8_t {
    Kernel     = 0,
    Executive  = 1,
    Supervisor = 2,
    User       = 3,
};


// ---------------------------------------------------------------------------
// AccessKind -- what the access intends to do at the target address.
// ---------------------------------------------------------------------------
// Drives the permission check (read/write/execute), the FOR/FOW/FOE
// classification, and the I-stream-vs-D-stream realm selection at the
// TLB.  Instruction fetch is its own kind so that the translator can
// route to the ITB rather than the DTB and so that FOE is checked
// instead of FOR.
enum class AccessKind : uint8_t {
    DataRead   = 0,
    DataWrite  = 1,
    Execute    = 2,
};


// ---------------------------------------------------------------------------
// Realm -- which TLB the lookup targets.
// ---------------------------------------------------------------------------
// Maps directly from AccessKind: Execute -> I, otherwise D.  Carried
// as a separate enum because TLB shard managers split the I-side and
// D-side caches and want a categorical tag rather than a kind-derived
// boolean.
enum class Realm : uint8_t {
    I = 0,   // instruction-stream TLB (ITB)
    D = 1,   // data-stream TLB (DTB)
};


// Small helper: derive realm from access kind.  Inline to keep this
// header zero-cost.
constexpr Realm realmFor(AccessKind a) noexcept
{
    return (a == AccessKind::Execute) ? Realm::I : Realm::D;
}


// ---------------------------------------------------------------------------
// VA_CTL bit accessors.  Source: Alpha 21264/EV6 HRM Section 5.1.5.
// ---------------------------------------------------------------------------
// VA_CTL is a 64-bit IPR controlling data-stream virtual-address
// formation.  Field layout:
//
//   bits [63:30]  VPTB        -- virtual page table base for data stream
//   bits [29:3]   reserved
//   bit  [2]      VA_FORM_32  -- when set, VA_FORM uses 32-bit VPN layout
//   bit  [1]      VA_48       -- when set, 48-bit VA mode; else 43-bit
//   bit  [0]      B_ENDIAN    -- when set, big-endian byte order
//
// I_CTL has a parallel set of bits for the instruction stream.
//
// V1 ref: D:\EmulatR\EmulatRAppUni\coreLib\VA_core.h (isVA48 / isVA43 /
// isBitEndian).  These V4 accessors mirror the V1 API with the V4
// naming convention.

constexpr uint64_t kVaCtlBEndianBit   = uint64_t{1} << 0;
constexpr uint64_t kVaCtlVa48Bit      = uint64_t{1} << 1;
constexpr uint64_t kVaCtlVaForm32Bit  = uint64_t{1} << 2;
constexpr uint64_t kVaCtlVptbMask     = uint64_t{0xFFFFFFFFC0000000};

// Predicate: is the data stream in 48-bit VA mode?  When clear, the
// processor is in 43-bit VA mode (the EV6 default).
[[nodiscard]] constexpr bool vaCtlIsVa48(uint64_t vaCtl) noexcept
{
    return (vaCtl & kVaCtlVa48Bit) != 0;
}

// Predicate: 43-bit VA mode -- the complement of vaCtlIsVa48().
[[nodiscard]] constexpr bool vaCtlIsVa43(uint64_t vaCtl) noexcept
{
    return !vaCtlIsVa48(vaCtl);
}

// Predicate: big-endian byte order selected (VA_CTL[B_ENDIAN]).
[[nodiscard]] constexpr bool vaCtlIsBigEndian(uint64_t vaCtl) noexcept
{
    return (vaCtl & kVaCtlBEndianBit) != 0;
}

// Predicate: 32-bit VA_FORM VPN layout selected (VA_CTL[VA_FORM_32]).
[[nodiscard]] constexpr bool vaCtlIsVaForm32(uint64_t vaCtl) noexcept
{
    return (vaCtl & kVaCtlVaForm32Bit) != 0;
}

// Extract the data-stream Virtual Page Table Base (VA_CTL[63:30]),
// returned in place (masked, not shifted) for direct VA_FORM use.
[[nodiscard]] constexpr uint64_t vaCtlVptb(uint64_t vaCtl) noexcept
{
    return vaCtl & kVaCtlVptbMask;
}

} // namespace coreLib

#endif // CORELIB_VA_TYPES_H
