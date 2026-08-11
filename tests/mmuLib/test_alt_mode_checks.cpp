// ============================================================================
// tests/mmuLib/test_alt_mode_checks.cpp -- doctest cases for the HW_LD/HW_ST
// alternate-mode + write-check access semantics (DTB_ALT_MODE modeling)
// ============================================================================
// Project: EmulatR -- Alpha AXP / EV6 Architecture Emulator (V5)
// Copyright (C) 2025, 2026 eNVy Systems, Inc.  All rights reserved.
// Licensed under eNVy Systems Non-Commercial License v1.1
//
// Project Architect: Timothy Peer
// AI Collaboration:  Claude (Anthropic)
//
// Commercial use prohibited without separate license.
// Contact:        peert@envysys.com  |  https://envysys.com
// ============================================================================
//
// WHY (JRN-SUPMODE-001 Sec 19): the VMS PAL PROBER/PROBEW corridor sets
// DTB_ALT_MODE to the probe mode and issues HW_LD Virtual/WrChk/Alt
// (TYPE 111) as its access check (EV6 Spec Rev 2.0 Sec 4.1.1 / 5.3.3).
// Un-modeled, the check passed as a plain current-mode load: PROBEW
// answered "User-writable" for the KES-write page holding DCL's command
// state (PTE ...147F09, UWE clear), DCL skipped its CHMS elevation, and
// the Species-A ACCVIO followed.  These cases pin the repaired
// semantics at the two seams the fix touched:
//   * applyTlbHit  -- per-mode read/write enable gating on the exact
//                     PTE protection measured in the fault (Sec 2 E3);
//   * translateData -- the altCheckMode override takes precedence over
//                     ambient cpu.mode (ruling precondition (1): the
//                     Virtual/Alt arm consumes the MODELED value, never
//                     ambient), and the WrChk read+write pair the
//                     drainer composes yields ACV at User.
//
// ============================================================================

#include "doctest.h"

#include "mmuLib/Ev6Translator.h"
#include "coreLib/CpuState.h"
#include "pteLib/AlphaPte.h"

namespace {

using coreLib::AccessKind;
using coreLib::Mode_Privilege;
using mmuLib::TranslationResult;

// The Species-A page's PTE, as measured (JRN-SUPMODE-001 Sec 2 E3 and
// the run-201215 trace): V=1, FOE, KRE|ERE|SRE|URE, KWE|EWE|SWE, UWE
// CLEAR; PFN 0x2782.  Write is Kernel/Exec/Super only.
constexpr uint64_t kSpeciesAPteRaw = (0x2782ULL << 32) | 0x00147F09ULL;

// The faulting VA (page 7FF9C000..7FF9DFFF).
constexpr uint64_t kSpeciesAVa = 0x7FF9DE90ULL;

pteLib::AlphaPte makePte(uint64_t raw)
{
    pteLib::AlphaPte p;
    p.raw = raw;
    return p;
}

} // namespace

TEST_CASE("applyTlbHit: KES-write PTE denies User write, allows User read")
{
    pteLib::AlphaPte const pte = makePte(kSpeciesAPteRaw);
    coreLib::PAType pa = 0;

    // Write: permitted at K/E/S, DENIED at User -- the Species-A truth.
    CHECK(mmuLib::applyTlbHit(pte, kSpeciesAVa, AccessKind::DataWrite,
                              Mode_Privilege::Kernel, pa)
          == TranslationResult::Success);
    CHECK(mmuLib::applyTlbHit(pte, kSpeciesAVa, AccessKind::DataWrite,
                              Mode_Privilege::Executive, pa)
          == TranslationResult::Success);
    CHECK(mmuLib::applyTlbHit(pte, kSpeciesAVa, AccessKind::DataWrite,
                              Mode_Privilege::Supervisor, pa)
          == TranslationResult::Success);
    CHECK(mmuLib::applyTlbHit(pte, kSpeciesAVa, AccessKind::DataWrite,
                              Mode_Privilege::User, pa)
          == TranslationResult::AccessViolation);

    // Read: KESU -- permitted at User (matches the PROBER result, which
    // was correct even pre-fix).
    CHECK(mmuLib::applyTlbHit(pte, kSpeciesAVa, AccessKind::DataRead,
                              Mode_Privilege::User, pa)
          == TranslationResult::Success);
}

TEST_CASE("translateData: altCheckMode override beats ambient cpu.mode")
{
    coreLib::CpuState cpu;
    cpu.mode  = Mode_Privilege::Kernel;   // ambient mode is privileged
    cpu.asn   = 0;
    cpu.m_spe = 0;                        // no kseg shortcut
    cpu.dtbMgr.insert(pteLib::TlbRealm::Dtb, kSpeciesAVa, cpu.asn,
                      makePte(kSpeciesAPteRaw));

    coreLib::PAType pa = 0;

    // Ambient (no override): kernel write passes.
    CHECK(mmuLib::Ev6Translator::translateData(
              cpu, kSpeciesAVa, AccessKind::DataWrite, pa)
          == TranslationResult::Success);

    // altCheckMode = User: the SAME write is checked at User and must
    // ACV even though cpu.mode is Kernel -- the override consumes the
    // modeled DTB_ALT_MODE value, never ambient state.  This is the
    // PROBEW acceptance bit: pre-fix this returned Success and DCL's
    // guard was defeated.
    CHECK(mmuLib::Ev6Translator::translateData(
              cpu, kSpeciesAVa, AccessKind::DataWrite, pa,
              /*forceKernelChecks*/ false, /*altCheckMode*/ 3)
          == TranslationResult::AccessViolation);

    // altCheckMode = Supervisor: write passes (SWE set) -- the mode the
    // real DCL runs its command loop at.
    CHECK(mmuLib::Ev6Translator::translateData(
              cpu, kSpeciesAVa, AccessKind::DataWrite, pa,
              /*forceKernelChecks*/ false, /*altCheckMode*/ 2)
          == TranslationResult::Success);

    // The WrChk pair the drainer composes for HW_LD TYPE 111 at
    // alt-mode User: read passes (URE), then the write-kind re-check
    // fails -- net verdict ACV, which the PAL converts to PROBEW R0=0.
    CHECK(mmuLib::Ev6Translator::translateDataAligned(
              cpu, kSpeciesAVa, 4, AccessKind::DataRead, pa,
              false, /*altCheckMode*/ 3)
          == TranslationResult::Success);
    CHECK(mmuLib::Ev6Translator::translateDataAligned(
              cpu, kSpeciesAVa, 4, AccessKind::DataWrite, pa,
              false, /*altCheckMode*/ 3)
          == TranslationResult::AccessViolation);
}
