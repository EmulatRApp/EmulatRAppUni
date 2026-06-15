// ============================================================================
// tests/chipsetLib/test_ticket03_interrupts.cpp
//   Ticket 3 -- interrupt matrix (DRIR -> DIM -> DIR) + PCI INTx promotion
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
// Pins the HRM-grounded interrupt matrix (DIRn = DRIR & DIMn, Sec. 6.3) and
// the V4 PCI-INTx -> DRIR convention (see docs/hrm_deviations.md).
//
// Per V4 doctest convention: CHECK only, never REQUIRE.
// ============================================================================

#include "doctest.h"

#include "chipsetLib/TsunamiCchip.h"
#include "chipsetLib/TsunamiChipset.h"
#include "chipsetLib/TsunamiVariant.h"
#include "chipsetLib/Tsunami21272_RegisterMap.h"

#include <cstdint>

using namespace Tsunami21272;

TEST_CASE("DRIR assert with matching DIM sets DIR for that CPU only")
{
    TsunamiCchip c(ChipsetVariant::Tsunami, 4, 1ULL << 30);
    c.write(Cchip::DIM0, 1ULL << 33);   // CPU0 unmasks bit 33
    c.assertInterrupt(33);
    CHECK(c.readDIR(0) == (1ULL << 33));
    CHECK(c.readDIR(1) == 0);           // CPU1 DIM=0 masks it
}

TEST_CASE("DRIR assert with mismatched DIM does not set DIR")
{
    TsunamiCchip c(ChipsetVariant::Tsunami, 4, 1ULL << 30);
    c.write(Cchip::DIM0, 1ULL << 33);
    c.assertInterrupt(34);              // different bit
    CHECK(c.readDIR(0) == 0);
}

TEST_CASE("Deassert clears DIR for all CPUs")
{
    TsunamiCchip c(ChipsetVariant::Tsunami, 4, 1ULL << 30);
    c.write(Cchip::DIM0, ~0ULL);
    c.write(Cchip::DIM1, ~0ULL);
    c.assertInterrupt(40);
    CHECK(c.readDIR(0) != 0);
    CHECK(c.readDIR(1) != 0);
    c.deassertInterrupt(40);
    CHECK(c.readDIR(0) == 0);
    CHECK(c.readDIR(1) == 0);
}

TEST_CASE("Per-CPU masks are independent")
{
    TsunamiCchip c(ChipsetVariant::Typhoon, 4, 8ULL << 30);
    c.write(Cchip::DIM0, 1ULL << 50);
    c.write(Cchip::DIM2, 1ULL << 50);
    c.assertInterrupt(50);
    CHECK(c.readDIR(0) == (1ULL << 50));
    CHECK(c.readDIR(1) == 0);
    CHECK(c.readDIR(2) == (1ULL << 50));
    CHECK(c.readDIR(3) == 0);
}

TEST_CASE("PCI INTx -> DRIR bit follows the V4 convention")
{
    CHECK(TsunamiChipset::pciIntxToDrirBit(0, 0) == 32);  // Pchip0 INTA
    CHECK(TsunamiChipset::pciIntxToDrirBit(0, 3) == 35);  // Pchip0 INTD
    CHECK(TsunamiChipset::pciIntxToDrirBit(1, 0) == 36);  // Pchip1 INTA
    CHECK(TsunamiChipset::pciIntxToDrirBit(1, 3) == 39);  // Pchip1 INTD
    CHECK(TsunamiChipset::pciIntxToDrirBit(1, 3) != 63);  // clears reserved bit 63
}

TEST_CASE("raisePciInterrupt lands in DRIR; lowerPciInterrupt clears it")
{
    TsunamiChipset cs(ChipsetVariant::Tsunami, 1, 1ULL << 30);
    cs.cchip().write(Cchip::DIM0, 1ULL << 32);   // unmask Pchip0 INTA bit
    cs.raisePciInterrupt(0, 0);
    CHECK(cs.cchip().readDIR(0) == (1ULL << 32));
    cs.lowerPciInterrupt(0, 0);
    CHECK(cs.cchip().readDIR(0) == 0);
}

// ============================================================================
// MISC arbitration (HRM 12.2 Cchip Firmware Initialization Sequence)
// ============================================================================
// 2026-05-30: SRM firmware uses MISC<ABT/ABW> arbitration to gate system
// init.  CPU W1Ss MISC<ABT[n]>, MB, reads MISC<ABW> -- if ABW[n]=1 it
// won and proceeds; if not, it waits for IPI from the winning CPU.  In
// V4-shallow (one CPU), the Cchip auto-promotes ABT[n] -> ABW[n] when
// there is no contender.  ACL (bit 24, WO) clears both fields to release
// the lock.  Without these, the SRM spins forever post-banner waiting
// for an IPI that cannot arrive.  See task #78 / #87.

TEST_CASE("MISC: W1S ABT[0] auto-promotes to ABW[0] (HRM 12.2 single-CPU arbitration win)")
{
    TsunamiCchip c(ChipsetVariant::Tsunami, 4, 1ULL << 30);

    uint64_t const abtMask = Spec::mask(Spec::Cchip::MISC::ABT);
    uint64_t const abwMask = Spec::mask(Spec::Cchip::MISC::ABW);
    uint64_t const abt0    = 1ULL << Spec::Cchip::MISC::ABT.lsb;   // bit 20
    uint64_t const abw0    = 1ULL << Spec::Cchip::MISC::ABW.lsb;   // bit 16

    // Pre-state: ABT and ABW are zero (no contender, no winner).
    uint64_t const initial = c.read(Cchip::MISC, 0);
    CHECK((initial & abtMask) == 0);
    CHECK((initial & abwMask) == 0);

    // SRM W1S sets ABT[0].  Cchip auto-promotes -> ABW[0] in same write.
    c.write(Cchip::MISC, abt0);

    uint64_t const after = c.read(Cchip::MISC, 0);
    CHECK((after & abt0) != 0);   // ABT[0] stays set (W1S took effect)
    CHECK((after & abw0) != 0);   // ABW[0] auto-promoted -- THE FIX
}

TEST_CASE("MISC: ACL (bit 24, WO) clears both ABT and ABW; ACL itself does not persist")
{
    TsunamiCchip c(ChipsetVariant::Tsunami, 4, 1ULL << 30);

    uint64_t const abtMask = Spec::mask(Spec::Cchip::MISC::ABT);
    uint64_t const abwMask = Spec::mask(Spec::Cchip::MISC::ABW);
    uint64_t const abt0    = 1ULL << Spec::Cchip::MISC::ABT.lsb;
    uint64_t const acl     = 1ULL << Spec::Cchip::MISC::ACL.lsb;   // bit 24

    // Arm: set ABT[0]; auto-promotion sets ABW[0].
    c.write(Cchip::MISC, abt0);
    uint64_t const armed = c.read(Cchip::MISC, 0);
    CHECK((armed & abtMask) != 0);
    CHECK((armed & abwMask) != 0);

    // Clear: write 1 to ACL.  Both ABT and ABW reset.
    c.write(Cchip::MISC, acl);
    uint64_t const cleared = c.read(Cchip::MISC, 0);
    CHECK((cleared & abtMask) == 0);
    CHECK((cleared & abwMask) == 0);
    // ACL is WO -- never reads back as set.
    CHECK((cleared & acl) == 0);
}

TEST_CASE("MISC: re-arm cycle (ABT set -> ACL clear -> ABT set again) re-promotes ABW")
{
    TsunamiCchip c(ChipsetVariant::Tsunami, 4, 1ULL << 30);

    uint64_t const abt0 = 1ULL << Spec::Cchip::MISC::ABT.lsb;
    uint64_t const abw0 = 1ULL << Spec::Cchip::MISC::ABW.lsb;
    uint64_t const acl  = 1ULL << Spec::Cchip::MISC::ACL.lsb;

    c.write(Cchip::MISC, abt0);                       // first try -> wins
    CHECK((c.read(Cchip::MISC, 0) & abw0) != 0);

    c.write(Cchip::MISC, acl);                        // release lock
    CHECK((c.read(Cchip::MISC, 0) & abw0) == 0);

    c.write(Cchip::MISC, abt0);                       // re-try after clear
    CHECK((c.read(Cchip::MISC, 0) & abw0) != 0);      // re-promoted
}

// ============================================================================
// MISC arbitration (HRM 12.2 Cchip Firmware Initialization Sequence)
// ============================================================================
// 2026-05-30: SRM firmware uses MISC<ABT/ABW> arbitration to gate system
// init.  CPU W1Ss MISC<ABT[n]>, MB, reads MISC<ABW> -- if ABW[n]=1 it
// won and proceeds; if not, it waits for IPI from the winning CPU.  In
// V4-shallow (one CPU), the Cchip auto-promotes ABT[n] -> ABW[n] when
// there is no contender.  ACL (bit 24, WO) clears both fields to release
// the lock.  Without these, the SRM spins forever post-banner waiting
// for an IPI that cannot arrive.  See task #78 / #87.

TEST_CASE("MISC: W1S ABT[0] auto-promotes to ABW[0] (HRM 12.2 single-CPU arbitration win)")
{
    TsunamiCchip c(ChipsetVariant::Tsunami, 4, 1ULL << 30);

    uint64_t const abtMask = Spec::mask(Spec::Cchip::MISC::ABT);
    uint64_t const abwMask = Spec::mask(Spec::Cchip::MISC::ABW);
    uint64_t const abt0    = 1ULL << Spec::Cchip::MISC::ABT.lsb;
    uint64_t const abw0    = 1ULL << Spec::Cchip::MISC::ABW.lsb;

    uint64_t const initial = c.read(Cchip::MISC, 0);
    CHECK((initial & abtMask) == 0);
    CHECK((initial & abwMask) == 0);

    c.write(Cchip::MISC, abt0);

    uint64_t const after = c.read(Cchip::MISC, 0);
    CHECK((after & abt0) != 0);
    CHECK((after & abw0) != 0);
}

TEST_CASE("MISC: ACL (bit 24, WO) clears both ABT and ABW; ACL itself does not persist")
{
    TsunamiCchip c(ChipsetVariant::Tsunami, 4, 1ULL << 30);

    uint64_t const abtMask = Spec::mask(Spec::Cchip::MISC::ABT);
    uint64_t const abwMask = Spec::mask(Spec::Cchip::MISC::ABW);
    uint64_t const abt0    = 1ULL << Spec::Cchip::MISC::ABT.lsb;
    uint64_t const acl     = 1ULL << Spec::Cchip::MISC::ACL.lsb;

    c.write(Cchip::MISC, abt0);
    uint64_t const armed = c.read(Cchip::MISC, 0);
    CHECK((armed & abtMask) != 0);
    CHECK((armed & abwMask) != 0);

    c.write(Cchip::MISC, acl);
    uint64_t const cleared = c.read(Cchip::MISC, 0);
    CHECK((cleared & abtMask) == 0);
    CHECK((cleared & abwMask) == 0);
    CHECK((cleared & acl) == 0);
}

TEST_CASE("MISC: re-arm cycle (ABT set -> ACL clear -> ABT set again) re-promotes ABW")
{
    TsunamiCchip c(ChipsetVariant::Tsunami, 4, 1ULL << 30);

    uint64_t const abt0 = 1ULL << Spec::Cchip::MISC::ABT.lsb;
    uint64_t const abw0 = 1ULL << Spec::Cchip::MISC::ABW.lsb;
    uint64_t const acl  = 1ULL << Spec::Cchip::MISC::ACL.lsb;

    c.write(Cchip::MISC, abt0);
    CHECK((c.read(Cchip::MISC, 0) & abw0) != 0);

    c.write(Cchip::MISC, acl);
    CHECK((c.read(Cchip::MISC, 0) & abw0) == 0);

    c.write(Cchip::MISC, abt0);
    CHECK((c.read(Cchip::MISC, 0) & abw0) != 0);
}
