// ============================================================================
// tests/deviceLib/test_hwpcb_context.cpp -- SPEC-SWPCTX-001 C2 pins
// ============================================================================
// Project: EmulatR -- Alpha AXP / EV6 Architecture Emulator (V5)
// Copyright (C) 2025, 2026 eNVy Systems, Inc.  All rights reserved.
// Licensed under eNVy Systems Non-Commercial License v1.1
//
// Project Architect: Timothy Peer
// AI Collaboration:  Claude (Anthropic)
// ============================================================================
//
// Behavioural pins for the SWPCTX C2 infrastructure (GATE-2 evidence):
//
//   - guest-physical HWPCB image read (16 quads, no translation)
//   - the SAVE-SET write policy: SPs + ASTEN/ASTSR + CPC-as-LONGWORD,
//     and NOTHING else (PTBR/ASN/FEN/UNQ/SCT untouched -- AARM 10-88,
//     apisrm hw_stl/p CPC, GATE-1 Q1/Q2/D4)
//   - FEN-quad packing: FEN<0> | PME<62> | DAT<63>
//     (apisrm ev6_vms_pal_defs.mar:345-350)
//   - CpuState<->Hwpcb CC arithmetic under the F-1 packed model:
//     store CPC = (offset+counter) mod 2^32; load offset = CPC - counter
//
// Uses CHECK only (exceptions disabled).
// ============================================================================

#include "doctest.h"

#include "coreLib/CpuState.h"
#include "deviceLib/HwpcbContext.h"
#include "memoryLib/GuestMemory.h"

#include <cstdint>

using coreLib::CpuState;
using deviceLib::hwrpb::Hwpcb;
using deviceLib::hwrpb::loadCpuFromHwpcb;
using deviceLib::hwrpb::readHwpcbFromGuest;
using deviceLib::hwrpb::storeCpuToHwpcb;
using deviceLib::hwrpb::writeHwpcbSaveSet;
using memoryLib::GuestMemory;
using memoryLib::MemStatus;

namespace {
constexpr uint64_t kPcbbPa = 0x40000;   // 128-byte aligned, well in range
}

TEST_CASE("HWPCB guest I/O: full image reads back what was written")
{
    GuestMemory mem(4ULL * 1024 * 1024);
    // Populate a recognizable image directly in guest memory.
    for (unsigned i = 0; i < 16; ++i) {
        CHECK(mem.write8(kPcbbPa + i * 8ULL,
                         0xA000000000000000ULL | i) == MemStatus::Ok);
    }
    Hwpcb img{};
    CHECK(readHwpcbFromGuest(mem, kPcbbPa, img) == MemStatus::Ok);
    CHECK(img.ksp  == 0xA000000000000000ULL);
    CHECK(img.usp  == 0xA000000000000003ULL);
    CHECK(img.ptbr == 0xA000000000000004ULL);
    CHECK(img.cc   == 0xA000000000000008ULL);
    CHECK(img.scratch[6] == 0xA00000000000000FULL);
}

TEST_CASE("HWPCB save set: writes KSP+AST+CPC(longword) and nothing else")
{
    // GATE-1 Q2 / audit PE-1 (2026-07-28): the save set is apisrm's exact
    // field set -- KSP, AST, CPC.  ESP/SSP/USP are guest-PAL-maintained in
    // the HWPCB at mode transitions; SWPCTX must NOT touch them (AARM
    // 10-90 Note), so their sentinels must survive the save.
    GuestMemory mem(4ULL * 1024 * 1024);
    // Sentinel background: every quad of the old image = 0xEE..EE | idx.
    for (unsigned i = 0; i < 16; ++i) {
        CHECK(mem.write8(kPcbbPa + i * 8ULL,
                         0xEEEEEEEE00000000ULL | i) == MemStatus::Ok);
    }
    Hwpcb save{};
    save.ksp = 0x1111; save.esp = 0x2222; save.ssp = 0x3333;
    save.usp = 0x4444; save.asten_sr = 0x5A;
    save.cc  = 0xCAFE0123ULL;            // CPC, 32-bit quantity

    CHECK(writeHwpcbSaveSet(mem, kPcbbPa, save) == MemStatus::Ok);

    uint64_t q = 0;
    CHECK(mem.read8(kPcbbPa + 0x00, q) == MemStatus::Ok); CHECK(q == 0x1111);
    CHECK(mem.read8(kPcbbPa + 0x30, q) == MemStatus::Ok); CHECK(q == 0x5A);
    // CPC is a LONGWORD store: high half of +0x40 preserved.
    CHECK(mem.read8(kPcbbPa + 0x40, q) == MemStatus::Ok);
    CHECK(q == (0xEEEEEEEE00000000ULL | 0xCAFE0123ULL));
    // ESP/SSP/USP: the live guest-maintained values survive untouched
    // even though the source struct carries stale mirrors.
    CHECK(mem.read8(kPcbbPa + 0x08, q) == MemStatus::Ok);
    CHECK(q == (0xEEEEEEEE00000000ULL | 1));
    CHECK(mem.read8(kPcbbPa + 0x10, q) == MemStatus::Ok);
    CHECK(q == (0xEEEEEEEE00000000ULL | 2));
    CHECK(mem.read8(kPcbbPa + 0x18, q) == MemStatus::Ok);
    CHECK(q == (0xEEEEEEEE00000000ULL | 3));
    // NOT in the save set -- sentinels intact: PTBR, ASN, FEN, UNQ/SCT.
    CHECK(mem.read8(kPcbbPa + 0x20, q) == MemStatus::Ok);
    CHECK(q == (0xEEEEEEEE00000000ULL | 4));
    CHECK(mem.read8(kPcbbPa + 0x28, q) == MemStatus::Ok);
    CHECK(q == (0xEEEEEEEE00000000ULL | 5));
    CHECK(mem.read8(kPcbbPa + 0x38, q) == MemStatus::Ok);
    CHECK(q == (0xEEEEEEEE00000000ULL | 7));
    CHECK(mem.read8(kPcbbPa + 0x48, q) == MemStatus::Ok);
    CHECK(q == (0xEEEEEEEE00000000ULL | 9));
}

TEST_CASE("FEN quad packing: FEN<0> | PME<62> | DAT<63> unpack + repack")
{
    Hwpcb img{};
    img.fen = (1ULL << 63) | (1ULL << 62) | 1ULL;   // DAT=1 PME=1 FEN=1
    CpuState cpu{};
    cpu.cycleCount = 0;
    loadCpuFromHwpcb(cpu, img);
    CHECK(cpu.fen == 1ULL);
    CHECK(cpu.pme == 1ULL);
    CHECK(cpu.dat == 1ULL);

    // Flip PME off, repack, verify bit positions.
    cpu.pme = 0;
    Hwpcb out{};
    storeCpuToHwpcb(out, cpu);
    CHECK(out.fen == ((1ULL << 63) | 1ULL));

    // FEN-only image: high bits stay clear.
    Hwpcb img2{};
    img2.fen = 1ULL;
    loadCpuFromHwpcb(cpu, img2);
    CHECK(cpu.fen == 1ULL);
    CHECK(cpu.pme == 0ULL);
    CHECK(cpu.dat == 0ULL);
}

TEST_CASE("CC arithmetic (F-1 packed): CPC round-trips across a swap pair")
{
    // Process A swapped out at counter C1 with offset O; its saved CPC
    // must equal (O + C1) mod 2^32.  Swapped back in at counter C2, the
    // restored offset must make (offset + counter) == saved CPC again.
    CpuState cpu{};
    cpu.cycleCount = 0x00001000ULL;                 // counter C1
    cpu.ccOffset   = 0xFFFFF000ULL;                 // offset O (32-bit field)

    Hwpcb out{};
    storeCpuToHwpcb(out, cpu);
    CHECK(out.cc == ((0xFFFFF000ULL + 0x1000ULL) & 0xFFFFFFFFULL));  // wraps

    cpu.cycleCount = 0x00050000ULL;                 // later, counter C2
    loadCpuFromHwpcb(cpu, out);
    CHECK(((cpu.ccOffset + cpu.cycleCount) & 0xFFFFFFFFULL)
          == (out.cc & 0xFFFFFFFFULL));
    // And the system timebase was never moved by the swap arithmetic.
    CHECK(cpu.cycleCount == 0x00050000ULL);
}
