// ============================================================================
// tests/palBoxLib/test_swpctx.cpp -- SPEC-SWPCTX-001 C3 pins (T1-T6)
// ============================================================================
// Project: EmulatR -- Alpha AXP / EV6 Architecture Emulator (V5)
// Copyright (C) 2025, 2026 eNVy Systems, Inc.  All rights reserved.
// Licensed under eNVy Systems Non-Commercial License v1.1
//
// Project Architect: Timothy Peer
// AI Collaboration:  Claude (Anthropic)
// ============================================================================
//
// Pins for the faithful VMS SWPCTX leaf (CALL_PAL 0x0005), per the brief
// Sec 7 test plan and the GATE-1 answers doc:
//
//   T1  round-trip: swap A->B->A restores the save-set fields of A
//   T2  physical access: swap allocates NO TB entries
//   T3  TB semantics: non-ASM entries die on swap, ASM=1 survives (Q3(b))
//   T4  GH-span invalidation: a GH=3 entry's FULL 4MB reach dies --
//       probed at a NON-BASE page (the cross-page compose lesson)
//   T5  CC continuity: CPC per the F-1 packed model round-trips
//   T6  atomicity/ordering: single grain, no fault, no divert; the
//       misaligned-R16 fault path touches NO state
//
// Uses CHECK only (exceptions disabled).
// ============================================================================

#include "doctest.h"

#include "coreLib/BoxResult.h"
#include "coreLib/CpuState.h"
#include "coreLib/ExecCtx.h"
#include "coreLib/InstructionGrain.h"
#include "deviceLib/HwpcbContext.h"
#include "grainFactoryLib/generated/SemanticFlagsEnum.h"
#include "grainFactoryLib/generated/GrainsForward.h"
#include "memoryLib/GuestMemory.h"
#include "pteLib/AlphaPte.h"
#include "pteLib/TlbEntry.h"

#include <cstdint>

using namespace coreLib;
using grainFactory::GrainSem;
using memoryLib::GuestMemory;
using memoryLib::MemStatus;
using pteLib::AlphaPte;
using pteLib::TlbRealm;

namespace {

constexpr uint64_t kPcbbA = 0x40000;   // 128-byte aligned HWPCB images
constexpr uint64_t kPcbbB = 0x40080;

InstructionGrain makeSwpctxGrain()
{
    InstructionGrain g{};
    g.pc        = 0x2F09C;                       // the famous call site
    g.encoded   = 0x00000005u;                   // CALL_PAL SWPCTX
    g.primaryOp = 0x00;
    g.box       = Box::PalBox;
    g.semFlags  = GrainSem::S_PalFormat | GrainSem::S_PalIntrinsic
                | GrainSem::S_WritesRa | GrainSem::S_WritesInt
                | GrainSem::S_PalVms;
    g.execFn    = &palBox::execSwpctx_vms;
    return g;
}

// Write a recognizable HWPCB image for "process X" into guest memory.
void seedHwpcb(GuestMemory& mem, uint64_t pa, uint64_t tag,
               uint64_t ptbrPfn, uint64_t asn, uint64_t cpc)
{
    (void)mem.write8(pa + 0x00, 0x1000 + tag);       // ksp
    (void)mem.write8(pa + 0x08, 0x2000 + tag);       // esp
    (void)mem.write8(pa + 0x10, 0x3000 + tag);       // ssp
    (void)mem.write8(pa + 0x18, 0x4000 + tag);       // usp
    (void)mem.write8(pa + 0x20, ptbrPfn);            // ptbr (PFN form)
    (void)mem.write8(pa + 0x28, asn);                // asn
    (void)mem.write8(pa + 0x30, 0x50 + tag);         // asten_sr
    (void)mem.write8(pa + 0x38, 1ULL);               // fen quad (FEN=1)
    (void)mem.write8(pa + 0x40, cpc);                // cpc
    for (unsigned i = 9; i < 16; ++i) {
        (void)mem.write8(pa + i * 8ULL, 0xEE00 + i); // UNQ/SCT sentinels
    }
}

} // anonymous namespace


TEST_CASE("SWPCTX T1+T5: A->B->A round-trips save-set fields and CPC")
{
    GuestMemory mem(4ULL * 1024 * 1024);
    seedHwpcb(mem, kPcbbA, /*tag*/ 0xA00, /*ptbr*/ 0x800, /*asn*/ 3, /*cpc*/ 0x100);
    seedHwpcb(mem, kPcbbB, /*tag*/ 0xB00, /*ptbr*/ 0x900, /*asn*/ 4, /*cpc*/ 0x9999);

    CpuState cpu{};
    cpu.cycleCount = 0x40;                     // counter C0
    ExecCtx ctx{};
    ctx.cpu    = &cpu;
    ctx.memory = &mem;
    InstructionGrain const g = makeSwpctxGrain();

    // Boot-strap: adopt context A (pcbb==0 path skips the save -- T6-adj).
    cpu.intReg[16] = kPcbbA;
    BoxResult r = palBox::execSwpctx_vms(g, ctx);
    CHECK(r.faultCode == kNoFault);
    CHECK(cpu.pcbb == kPcbbA);
    CHECK(cpu.intReg[30] == 0x1A00ULL);        // live SP = A's KSP
    CHECK(cpu.asn == 3);
    // A's CC installed: offset+counter == A's CPC (F-1 model).
    CHECK(((cpu.ccOffset + cpu.cycleCount) & 0xFFFFFFFFULL) == 0x100ULL);

    // Run "in A" for a while; dirty the live state that the save set owns.
    // The ESP/SSP/USP CpuState mirrors are deliberately dirtied too: per
    // GATE-1 Q2 / audit PE-1 (2026-07-28) SWPCTX must NOT write them back
    // -- the HWPCB is their live home (guest PAL maintains them at mode
    // transitions), so the stale mirrors must be discarded, not saved.
    cpu.cycleCount += 0x1000;
    cpu.intReg[30]  = 0x1A08;                  // A pushed something
    cpu.esp = 0x2A08; cpu.ssp = 0x3A08; cpu.usp = 0x4A08;
    cpu.asten_sr = 0x5F;
    uint64_t const cpcAtLeaveA =
        (cpu.ccOffset + cpu.cycleCount) & 0xFFFFFFFFULL;

    // Swap A -> B.
    cpu.intReg[16] = kPcbbB;
    r = palBox::execSwpctx_vms(g, ctx);
    CHECK(r.faultCode == kNoFault);
    CHECK(cpu.pcbb == kPcbbB);
    CHECK(cpu.intReg[30] == 0x1B00ULL);
    CHECK(cpu.asn == 4);
    // PE-3 pin (2026-07-28): the swap installs the new ASN into the
    // DTB fill-staging registers too (apisrm :249-285) -- otherwise
    // every post-swap DTB fill is tagged with the OLD ASN and can
    // never hit under a nonzero-ASN process.
    CHECK(cpu.dtbAsn0 == 4);
    CHECK(cpu.dtbAsn1 == 4);
    CHECK(((cpu.ccOffset + cpu.cycleCount) & 0xFFFFFFFFULL) == 0x9999ULL);

    // PE-1 pin: the A->B save did NOT write the stale ESP/SSP/USP mirrors
    // into A's HWPCB -- the guest-maintained seeds survive.
    {
        uint64_t q = 0;
        CHECK(mem.read8(kPcbbA + 0x08, q) == MemStatus::Ok);
        CHECK(q == 0x2A00ULL);                 // seed, NOT the 0x2A08 mirror
        CHECK(mem.read8(kPcbbA + 0x10, q) == MemStatus::Ok);
        CHECK(q == 0x3A00ULL);
        CHECK(mem.read8(kPcbbA + 0x18, q) == MemStatus::Ok);
        CHECK(q == 0x4A00ULL);
    }

    // B runs.
    cpu.cycleCount += 0x777;

    // Swap B -> A: every save-set field of A must come back exactly.
    cpu.intReg[16] = kPcbbA;
    r = palBox::execSwpctx_vms(g, ctx);
    CHECK(r.faultCode == kNoFault);
    CHECK(cpu.pcbb == kPcbbA);
    CHECK(cpu.intReg[30] == 0x1A08ULL);        // A's PUSHED ksp, not the seed
    // Mirrors reload from A's HWPCB (the live home): the seeds, not the
    // discarded 0x?A08 values dirtied above.
    CHECK(cpu.esp == 0x2A00ULL);
    CHECK(cpu.ssp == 0x3A00ULL);
    CHECK(cpu.usp == 0x4A00ULL);
    CHECK(cpu.asten_sr == 0x5FULL);
    CHECK(cpu.asn == 3);
    // T5: CPC continuity -- A resumes charged exactly what it left with.
    CHECK(((cpu.ccOffset + cpu.cycleCount) & 0xFFFFFFFFULL) == cpcAtLeaveA);
    // Save-set discipline on B's image: PTBR/ASN/FEN/UNQ untouched.
    uint64_t q = 0;
    CHECK(mem.read8(kPcbbB + 0x20, q) == MemStatus::Ok); CHECK(q == 0x900ULL);
    CHECK(mem.read8(kPcbbB + 0x28, q) == MemStatus::Ok); CHECK(q == 4ULL);
    CHECK(mem.read8(kPcbbB + 0x38, q) == MemStatus::Ok); CHECK(q == 1ULL);
    CHECK(mem.read8(kPcbbB + 0x48, q) == MemStatus::Ok); CHECK(q == 0xEE09ULL);
}

TEST_CASE("SWPCTX T2+T3+T4: TB semantics -- TBIAP, ASM survival, GH span")
{
    GuestMemory mem(4ULL * 1024 * 1024);
    seedHwpcb(mem, kPcbbA, 0xA00, 0x800, 3, 0);
    seedHwpcb(mem, kPcbbB, 0xB00, 0x900, 4, 0);

    CpuState cpu{};
    ExecCtx ctx{};
    ctx.cpu    = &cpu;
    ctx.memory = &mem;
    InstructionGrain const g = makeSwpctxGrain();

    cpu.intReg[16] = kPcbbA;
    CHECK(palBox::execSwpctx_vms(g, ctx).faultCode == kNoFault);

    // Populate the TBs as "process A" (asn 3): a private page, an ASM
    // page, and a private GH=3 block.
    AlphaPte const priv = AlphaPte::makeValid(0x100, true, true,
                                              false, false, /*asm=*/false);
    AlphaPte const glob = AlphaPte::makeValid(0x200, true, true,
                                              false, false, /*asm=*/true);
    AlphaPte gh3 = AlphaPte::makeValid(0x800, true, true,
                                       false, false, /*asm=*/false);
    gh3.setGh(3);
    cpu.dtbMgr.insert(TlbRealm::Dtb, /*va=*/0x10000,     /*asn=*/3, priv);
    cpu.dtbMgr.insert(TlbRealm::Dtb, /*va=*/0x20000,     /*asn=*/3, glob);
    cpu.dtbMgr.insert(TlbRealm::Dtb, /*va=*/0x88000000,  /*asn=*/3, gh3, 3);
    cpu.itbMgr.insert(TlbRealm::Itb, /*va=*/0x30000,     /*asn=*/3, priv);
    CHECK(cpu.dtbMgr.lookup(TlbRealm::Dtb, 0x10000, 3).isHit());
    CHECK(cpu.dtbMgr.lookup(TlbRealm::Dtb, 0x88004000, 3).isHit()); // GH page 2

    // Swap A -> B.
    cpu.intReg[16] = kPcbbB;
    CHECK(palBox::execSwpctx_vms(g, ctx).faultCode == kNoFault);

    // T3: private translations are gone; ASM=1 survives (HRM 4.1.7).
    CHECK_FALSE(cpu.dtbMgr.lookup(TlbRealm::Dtb, 0x10000, 3).isHit());
    CHECK_FALSE(cpu.itbMgr.lookup(TlbRealm::Itb, 0x30000, 3).isHit());
    CHECK(cpu.dtbMgr.lookup(TlbRealm::Dtb, 0x20000, 3).isHit());
    // T4: the GH=3 block's FULL span is gone -- probe a NON-BASE page.
    CHECK_FALSE(cpu.dtbMgr.lookup(TlbRealm::Dtb, 0x88004000, 3).isHit());
    CHECK_FALSE(cpu.dtbMgr.lookup(TlbRealm::Dtb, 0x883FE000, 3).isHit());
    // T2: the swap itself allocated nothing (HWPCB access is physical).
    CHECK_FALSE(cpu.dtbMgr.lookup(TlbRealm::Dtb, kPcbbA, 3).isHit());
    CHECK_FALSE(cpu.dtbMgr.lookup(TlbRealm::Dtb, kPcbbB, 4).isHit());
}

TEST_CASE("SWPCTX T6: misaligned R16 faults with NO state change; success "
          "path is a single clean grain")
{
    GuestMemory mem(4ULL * 1024 * 1024);
    seedHwpcb(mem, kPcbbA, 0xA00, 0x800, 3, 0);

    CpuState cpu{};
    ExecCtx ctx{};
    ctx.cpu    = &cpu;
    ctx.memory = &mem;
    InstructionGrain const g = makeSwpctxGrain();

    cpu.intReg[16] = kPcbbA;
    CHECK(palBox::execSwpctx_vms(g, ctx).faultCode == kNoFault);
    uint64_t const pcbbBefore = cpu.pcbb;
    uint64_t const r30Before  = cpu.intReg[30];

    // Misaligned R16 (bit 6 set is not enough -- must be 128-aligned).
    cpu.intReg[16] = kPcbbA + 0x40;
    BoxResult const rBad = palBox::execSwpctx_vms(g, ctx);
    CHECK(rBad.faultCode == kFaultOpcDec);     // named deviation (ILLOP arch.)
    CHECK(cpu.pcbb == pcbbBefore);             // no state touched
    CHECK(cpu.intReg[30] == r30Before);
    CHECK(rBad.divertTarget == 0);             // no divert packed

    // Success path: no fault, no divert, R30 installed -- one grain does
    // the whole swap (atomicity is structural: no intermediate retire).
    cpu.intReg[16] = kPcbbA;
    BoxResult const rOk = palBox::execSwpctx_vms(g, ctx);
    CHECK(rOk.faultCode == kNoFault);
    CHECK(rOk.divertTarget == 0);
}
