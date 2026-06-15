// ============================================================================
// Scb.h -- System Control Block layout per AARM Section 14.6
// ============================================================================
// Project: EmulatR -- Alpha AXP / EV6 Architecture Emulator (V4)
// Copyright (C) 2025, 2026 eNVy Systems, Inc.  All rights reserved.
// Licensed under eNVy Systems Non-Commercial License v1.1
//
// Project Architect: Timothy Peer
// AI Collaboration:  Claude (Anthropic)
// ============================================================================
//
// The System Control Block (SCB) is the OS-managed dispatch table that
// PALcode uses to deliver exceptions, interrupts, and machine checks to
// kernel-mode handlers.  It is NOT the same as the PALcode entry vectors
// at palBase + offset (those are in coreLib/Ev6EntryVectors.h).
//
// Trap delivery chain on Alpha:
//
//     Hardware fault fires
//        |
//        v
//     +-------------------------------+
//     | Entry vector                  |   <- coreLib::ev6::entryForFault
//     | palBase + offset              |      PipelineDriver::retire wires
//     | PAL mode, PAL-private state   |      this in V4 (Step 5 commit).
//     +-------------------------------+
//        |  PALcode walks the SCB
//        v
//     +-------------------------------+
//     | SCB                           |   <- this file describes the layout
//     | pointed to by SCBB IPR        |      The OS owns and populates the
//     | array<ScbEntry, N>            |      block; V4 just provides storage
//     | indexed by trap byte-offset   |      for SCBB and the type for any
//     +-------------------------------+      diagnostic overlay.
//        |
//        v
//     OS kernel-mode handler runs (no longer in PAL mode)
//
// V4 does NOT perform the SCB walk -- that is PALcode's job.  V4 provides:
//   1. coreLib::CpuState::scbb -- IPR storage, accessible via CALL_PAL
//      MFPR_SCBB / MTPR_SCBB intrinsics (palBoxLib/grains/PalEntries.cpp).
//   2. ScbEntry POD (this file) -- byte-precise layout so V4 leaves can
//      overlay an SCB region of guest memory for diagnostics or test
//      assertions.
//   3. Named offset constants (this file) for the AARM-canonical entries
//      so V4 code referencing SCB entries uses canonical names instead
//      of magic byte offsets.
//
// References:
//   - alpha_arch_ref.txt Section 14.6 (~line 26586+) -- canonical layout
//     and per-group entry tables (Tables 14-7 through 14-13).
//   - apisrm/apisrm/ref/scb_def.* if present in the source tree.
// ============================================================================

#ifndef EMULATR_DEVICELIB_SCB_H
#define EMULATR_DEVICELIB_SCB_H

#include <cstddef>
#include <cstdint>

namespace deviceLib {
namespace scb {

// ============================================================================
// SCB entry layout (16 bytes per AARM 14.6).
// ============================================================================
#pragma pack(push, 1)

struct alignas(8) ScbEntry {
    uint64_t vector_va;     // virtual address of the kernel-mode handler
    uint64_t parameter;     // arbitrary qword passed to the handler
                            // (typically in R4 per AARM 14.7.4)
};
static_assert(sizeof(ScbEntry) == 16, "SCB entry is 16 bytes per AARM 14.6");

#pragma pack(pop)

// ============================================================================
// SCB sizing (per AARM 14.6).
// ============================================================================
constexpr std::size_t kScbMinSize     = 8  * 1024;  // 8 KB minimum
constexpr std::size_t kScbMaxSize     = 32 * 1024;  // 32 KB maximum
constexpr std::size_t kScbMinEntries  = 512;        // 512 entries minimum
constexpr std::size_t kScbMaxEntries  = 2048;       // 2048 entries maximum
constexpr std::size_t kScbEntryStride = 16;         // each entry is 16 bytes

// SCB must be page-aligned and physically contiguous.  Alpha page size = 8KB.
constexpr std::size_t kScbPageSize = 8192;

// ----------------------------------------------------------------------------
// Compute the byte offset for the Nth SCB entry.
// ----------------------------------------------------------------------------
constexpr std::size_t scbOffsetForIndex(std::size_t index) noexcept {
    return index * kScbEntryStride;
}

// ============================================================================
// Architecturally-defined SCB byte offsets (per AARM Section 14.6 Tables
// 14-7 through 14-13).  Use these instead of hard-coded magic numbers when
// writing test fixtures or diagnostic readers.
// ============================================================================

// ---- Faults (Table 14-7, byte offsets 0x000..0x0F0) ----
namespace Faults {
constexpr uint64_t FloatingDisabled       = 0x010;  // FEN fault
constexpr uint64_t AccessControlViolation = 0x080;  // ACV
constexpr uint64_t TranslationNotValid    = 0x090;  // TNV
constexpr uint64_t FaultOnRead            = 0x0A0;  // FOR
constexpr uint64_t FaultOnWrite           = 0x0B0;  // FOW
constexpr uint64_t FaultOnExecute         = 0x0C0;  // FOE
}  // namespace Faults

// ---- Arithmetic Traps (Table 14-8, byte offsets 0x200..0x230) ----
namespace Arithmetic {
constexpr uint64_t ArithmeticTrap = 0x200;          // FP / integer overflow
}  // namespace Arithmetic

// ---- Asynchronous System Traps (Table 14-9, byte offsets 0x240..0x270) ----
//   Per-mode AST delivery.  Handler runs at IPL 2, kernel mode, kernel stack.
namespace Ast {
constexpr uint64_t Kernel     = 0x240;
constexpr uint64_t Executive  = 0x250;
constexpr uint64_t Supervisor = 0x260;
constexpr uint64_t User       = 0x270;
}  // namespace Ast

// ---- Data Alignment Traps (Table 14-10, byte offsets 0x280..0x3F0) ----
namespace DataAlign {
constexpr uint64_t UnalignedAccess = 0x280;
}  // namespace DataAlign

// ---- Other Synchronous Traps (Table 14-11, byte offsets 0x400..0x4F0) ----
//   Mostly the CALL_PAL-driven traps (BPT, BUGCHK, IIT, IOT, GENTRAP, CHM*).
namespace SyncTraps {
constexpr uint64_t BreakpointTrap          = 0x400;  // CALL_PAL BPT       (Kernel)
constexpr uint64_t BugcheckTrap            = 0x410;  // CALL_PAL BUGCHK    (Kernel)
constexpr uint64_t IllegalInstruction      = 0x420;  // OPCDEC fallout    (Kernel)
constexpr uint64_t IllegalOperand          = 0x430;  //                    (Kernel)
constexpr uint64_t GenerateSoftwareTrap    = 0x440;  // CALL_PAL GENTRAP   (Kernel)
constexpr uint64_t ChangeModeKernel        = 0x480;  // CALL_PAL CHMK      (Kernel)
constexpr uint64_t ChangeModeExecutive     = 0x490;  // CALL_PAL CHME      (MostPriv)
constexpr uint64_t ChangeModeSupervisor    = 0x4A0;  // CALL_PAL CHMS      (MostPriv)
constexpr uint64_t ChangeModeUser          = 0x4B0;  // CALL_PAL CHMU      (Current)
}  // namespace SyncTraps

// ---- Software Interrupts (Table 14-12, byte offsets 0x500..0x5F0) ----
//   15 levels (1..15); offset 0x500 is unused per AARM.  Handler runs at
//   the target IPL, kernel mode, kernel stack.
namespace SwIrq {
constexpr uint64_t Level1  = 0x510;
constexpr uint64_t Level2  = 0x520;
constexpr uint64_t Level3  = 0x530;
constexpr uint64_t Level4  = 0x540;
constexpr uint64_t Level5  = 0x550;
constexpr uint64_t Level6  = 0x560;
constexpr uint64_t Level7  = 0x570;
constexpr uint64_t Level8  = 0x580;
constexpr uint64_t Level9  = 0x590;
constexpr uint64_t Level10 = 0x5A0;
constexpr uint64_t Level11 = 0x5B0;
constexpr uint64_t Level12 = 0x5C0;
constexpr uint64_t Level13 = 0x5D0;
constexpr uint64_t Level14 = 0x5E0;
constexpr uint64_t Level15 = 0x5F0;

// Compute the SCB byte offset for software-interrupt level [1..15].
// Returns 0 (an unused slot) for level 0 or out-of-range input.
constexpr uint64_t levelOffset(unsigned level) noexcept {
    return (level >= 1 && level <= 15) ? (0x500 + level * 0x10) : 0;
}
}  // namespace SwIrq

// ---- Processor Hardware Interrupts and Machine Checks (Table 14-13,
//      byte offsets 0x600..0x6F0).  Handler runs at the per-vector target
//      IPL listed below, kernel mode, kernel stack.
namespace HwIrq {
constexpr uint64_t IntervalClock              = 0x600;  // IPL 22
constexpr uint64_t Interprocessor             = 0x610;  // IPL 22 (IPI)
constexpr uint64_t SystemCorrectableMcheck    = 0x620;  // IPL 20
constexpr uint64_t ProcessorCorrectableMcheck = 0x630;  // IPL 31
constexpr uint64_t Powerfail                  = 0x640;  // IPL 30
constexpr uint64_t PerformanceMonitor         = 0x650;  // IPL 29
constexpr uint64_t SystemMcheck               = 0x660;  // IPL 31
constexpr uint64_t ProcessorMcheck            = 0x670;  // IPL 31
// 0x680..0x6E0 are reserved -- processor-specific (e.g., SROM console)
constexpr uint64_t PassiveRelease             = 0x6F0;  // IPL 20-23
}  // namespace HwIrq

// ---- Reserved (architecturally unused) ----
//   Byte offsets 0x700..0x7F0 are documented as "Unused" in AARM 14.6.

// ---- I/O Device Interrupts (Table 14-14, byte offsets 0x800..0x7FF0) ----
//   Dynamic; vectors are assigned by system software at boot time.
namespace IoIrq {
constexpr uint64_t Base = 0x800;    // first I/O-device-interrupt slot
constexpr uint64_t Last = 0x7FF0;   // last (must be < SCB end)

// Compute byte offset for a dynamically-assigned I/O vector index.
// Index 0 returns Base; indices increase by 0x10 per slot.  Returns 0
// (the unused first SCB slot) for offsets outside the I/O region.
constexpr uint64_t ioVectorOffset(unsigned index) noexcept {
    uint64_t const off = Base + index * 0x10;
    return (off <= Last) ? off : 0;
}
}  // namespace IoIrq

// ============================================================================
// Compile-time spot-checks: a few well-known offsets stay anchored.
// ============================================================================
static_assert(Faults::FloatingDisabled       == 0x010, "AARM 14.6 Table 14-7");
static_assert(Faults::AccessControlViolation == 0x080, "AARM 14.6 Table 14-7");
static_assert(Arithmetic::ArithmeticTrap     == 0x200, "AARM 14.6 Table 14-8");
static_assert(DataAlign::UnalignedAccess     == 0x280, "AARM 14.6 Table 14-10");
static_assert(SyncTraps::BreakpointTrap      == 0x400, "AARM 14.6 Table 14-11");
static_assert(SyncTraps::ChangeModeKernel    == 0x480, "AARM 14.6 Table 14-11");
static_assert(SwIrq::Level15                 == 0x5F0, "AARM 14.6 Table 14-12");
static_assert(SwIrq::levelOffset(1)          == 0x510, "SwIrq::levelOffset(1)");
static_assert(SwIrq::levelOffset(15)         == 0x5F0, "SwIrq::levelOffset(15)");
static_assert(SwIrq::levelOffset(0)          == 0,     "level 0 is invalid");
static_assert(SwIrq::levelOffset(16)         == 0,     "level 16 out of range");
static_assert(HwIrq::IntervalClock           == 0x600, "AARM 14.6 Table 14-13");
static_assert(HwIrq::SystemMcheck            == 0x660, "AARM 14.6 Table 14-13");
static_assert(IoIrq::Base                    == 0x800, "AARM 14.6 Section 14.6.8");

}  // namespace scb
}  // namespace deviceLib

#endif  // EMULATR_DEVICELIB_SCB_H
