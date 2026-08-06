// ============================================================================
// tests/chipsetLib/test_pchip_dma.cpp
//   JRN-PCI-001 (2026-08-02): Pchip DMA-window fidelity -- direct-map full
//   TBA<34:10> width, TLB-less scatter-gather walk, window-miss no-transfer,
//   PCTL<HOLE>, WSBA3 SG RO=1, CSR field masks, PERROR<SGE> latch/freeze.
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
// Authority: Tsunami/Typhoon 21272 HRM (EC-RE2CA-TE):
//   10.1.4.1 window hole; 10.1.4.2 + Table 10-5 direct map;
//   10.1.4.3 + Tables 10-6/10-7/10-8 scatter-gather; Table 10-36 WSBA3;
//   Table 10-38 TBA ADDR<34:10>; 10.2.5.6/7 PERROR/PERRMASK; 8.8.2.5 SGE.
//
// Per V4 doctest convention: CHECK only, never REQUIRE.
// ============================================================================

#include "doctest.h"

#include "chipsetLib/TsunamiPchip.h"

#include <cstdint>
#include <map>

namespace {

// Pchip CSR offsets (HRM Table 10-7, Pchip-relative).
constexpr uint64_t kWSBA0 = 0x0000, kWSBA1 = 0x0040, kWSBA3 = 0x00C0;
constexpr uint64_t kWSM0  = 0x0100, kWSM1  = 0x0140;
constexpr uint64_t kTBA0  = 0x0200, kTBA1  = 0x0240;
constexpr uint64_t kPCTL  = 0x0300;
constexpr uint64_t kPERROR   = 0x03C0;
constexpr uint64_t kPERRMASK = 0x0400;

} // namespace

TEST_CASE("direct-map translation carries TBA<34:32> (G-2 regression)")
{
    TsunamiPchip p;
    p.reset();
    // 1MB window at PCI 0x0010.0000 -> system 0x1.0010.0000 (bit 32 set).
    p.writeCSR(kWSBA0, 0x00100001ULL);            // ADDR|ENA
    p.writeCSR(kWSM0,  0x00000000ULL);            // 1MB
    p.writeCSR(kTBA0,  0x100100000ULL);           // TBA<34:10>, bit 32 live

    auto const x = p.translateDma(0x00123456ULL, 4);
    CHECK(x.hit);
    CHECK(x.pa == 0x100123456ULL);                // pre-fix: 0x00123456
}

TEST_CASE("window miss returns hit=false -- no identity fall-through (G-3)")
{
    TsunamiPchip p;
    p.reset();                                    // no windows enabled
    auto const x = p.translateDma(0x00300000ULL, 4);
    CHECK(!x.hit);
}

TEST_CASE("PCTL<HOLE> inhibits matching for 512KB..1MB-1 (HRM 10.1.4.1)")
{
    TsunamiPchip p;
    p.reset();
    // 1MB window at PCI 0 covering the hole range.
    p.writeCSR(kWSBA0, 0x00000001ULL);
    p.writeCSR(kWSM0,  0x00000000ULL);
    p.writeCSR(kTBA0,  0x00000000ULL);

    CHECK(p.translateDma(0x00080000ULL, 4).hit);  // hole disabled: hit

    uint64_t const pctl = p.readCSR(kPCTL);
    p.writeCSR(kPCTL, pctl | (1ULL << 5));        // HOLE=1
    CHECK(!p.translateDma(0x00080000ULL, 4).hit); // hole start inhibited
    CHECK(!p.translateDma(0x000FFFFFULL, 4).hit); // hole end inhibited
    CHECK(p.translateDma(0x0007FFFFULL, 4).hit);  // below hole: still hits
}

TEST_CASE("TLB-less SG walk translates through a guest PTE array (G-1)")
{
    TsunamiPchip p;
    p.reset();
    // SG 1MB window at PCI 0x0020.0000; PTE area at 0x7000 (1KB, aligned).
    p.writeCSR(kWSBA1, 0x00200003ULL);            // ADDR|SG|ENA
    p.writeCSR(kWSM1,  0x00000000ULL);            // 1MB -> 1KB PTE area
    p.writeCSR(kTBA1,  0x00007000ULL);

    std::map<uint64_t, uint64_t> pteMem;
    pteMem[0x7010] = (0x1234ULL << 1) | 0x1ULL;   // index 2: V=1, page 0x1234
    pteMem[0x7018] = 0x0ULL;                      // index 3: V=0
    p.setSgPteReader([&pteMem](uint64_t pa) -> uint64_t {
        auto it = pteMem.find(pa);
        return (it != pteMem.end()) ? it->second : 0x0ULL;
    });

    // PCI page index 2, offset 0x123 -> PA = 0x1234 << 13 | 0x123.
    auto const hitX = p.translateDma(0x00200000ULL + 2 * 0x2000 + 0x123, 4);
    CHECK(hitX.hit);
    CHECK(hitX.pa == ((0x1234ULL << 13) | 0x123ULL));

    // Invalid PTE (V=0): no transfer.
    auto const missX = p.translateDma(0x00200000ULL + 3 * 0x2000, 4);
    CHECK(!missX.hit);
}

TEST_CASE("invalid SG PTE latches PERROR<SGE> when enabled; freeze + LOST")
{
    TsunamiPchip p;
    p.reset();
    p.writeCSR(kWSBA1, 0x00200003ULL);
    p.writeCSR(kWSM1,  0x00000000ULL);
    p.writeCSR(kTBA1,  0x00007000ULL);
    p.setSgPteReader([](uint64_t) -> uint64_t { return 0x0ULL; }); // all V=0

    bool errorLevel = false;
    p.setErrorSignal([&errorLevel](bool a) { errorLevel = a; });

    // PERRMASK<SGE>=0: latch is fully gated off (HRM 10.2.5.7).
    (void) p.translateDma(0x00200000ULL, 8);
    CHECK(p.readCSR(kPERROR) == 0x0ULL);
    CHECK(!errorLevel);

    // Enable SGE (bit 4) and retry: PERROR<SGE> sets, register freezes,
    // b_error asserts, INFO carries the PCI address in ADDR<47:18>.
    p.writeCSR(kPERRMASK, 0x10ULL);
    (void) p.translateDma(0x00200000ULL, 8);
    uint64_t const perror = p.readCSR(kPERROR);
    CHECK((perror & 0x10ULL) != 0);                          // SGE
    CHECK(((perror >> 18) & 0x3FFFFFFFULL) == (0x00200000ULL >> 2));
    CHECK(errorLevel);

    // Second error while frozen: only LOST<0> can set.
    (void) p.translateDma(0x00202000ULL, 8);
    CHECK((p.readCSR(kPERROR) & 0x1ULL) != 0);               // LOST
    CHECK(((p.readCSR(kPERROR) >> 18) & 0x3FFFFFFFULL)
          == (0x00200000ULL >> 2));                          // info held

    // W1C of all error bits unfreezes, drops info, deasserts b_error.
    p.writeCSR(kPERROR, 0xFFFULL);
    CHECK(p.readCSR(kPERROR) == 0x0ULL);
    CHECK(!errorLevel);
}

TEST_CASE("WSBA3<SG> is RO=1 and MBZ fields are masked (Table 10-36, G-9)")
{
    TsunamiPchip p;
    p.reset();
    CHECK((p.readCSR(kWSBA3) & 0x2ULL) != 0);     // SG=1 out of reset

    p.writeCSR(kWSBA3, 0xFFF00001ULL);            // attempt SG=0
    CHECK((p.readCSR(kWSBA3) & 0x2ULL) != 0);     // still 1 (RO)

    p.writeCSR(kWSBA3, (1ULL << 39) | 0xABC00001ULL);
    CHECK((p.readCSR(kWSBA3) & (1ULL << 39)) != 0);   // DAC storable

    // MBZ enforcement on WSBA0: only ADDR<31:20>|SG|ENA survive.
    p.writeCSR(kWSBA0, 0xFFFFFFFFFFFFFFFFULL);
    CHECK(p.readCSR(kWSBA0) == 0xFFF00003ULL);
}
