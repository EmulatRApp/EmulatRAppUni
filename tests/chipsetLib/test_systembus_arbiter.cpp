// ============================================================================
// tests/chipsetLib/test_systembus_arbiter.cpp
//   TsunamiChipset as the ISystemBus arbiter -- DRAM / I/O / NXM decode
// ============================================================================
// Project: EmulatR -- Alpha AXP / EV6 Architecture Emulator (V4)
// Copyright (C) 2025, 2026 eNVy Systems, Inc.  All rights reserved.
// Licensed under eNVy Systems Non-Commercial License v1.1
//
// Project Architect: Timothy Peer
// AI Collaboration:  Claude (Anthropic)
// ============================================================================
//
// Isolated proof of the bus arbiter's physical-address decode, with no
// MemDrainer / PipelineDriver / Machine involvement.  Exercises the three
// decode legs of TsunamiChipset::read/write (the memoryLib::ISystemBus
// override):
//
//   DRAM (isDramAddress)  -> delegate to the owned GuestMemory  (round-trip)
//   I/O window (>= MMIO)  -> route to mmioRead/mmioWrite (CSR/PCI)
//   unclaimed PA          -> reportNxm + BusStatus::BusError, MISC<NXM> latched
//
// The unclaimed leg is the SRM memory-sizing case: a probe at 0x20_0000_0000
// (128 GB) is far above configured DRAM and below the I/O window, so it must
// bus-error AND latch the Cchip NXM syndrome the firmware MCHK handler reads.
// ============================================================================

#include "doctest.h"

#include "chipsetLib/TsunamiChipset.h"
#include "chipsetLib/TsunamiVariant.h"
#include "chipsetLib/Tsunami21272_RegisterMap.h"
#include "memoryLib/ISystemBus.h"

TEST_CASE("TsunamiChipset ISystemBus arbiter -- DRAM / I/O / NXM decode") {
    using memoryLib::BusStatus;

    // 256 MB Tsunami config; the chipset owns and sizes GuestMemory from the
    // same memSizeBytes, so isDramAddress (AAR) and the backing agree.
    TsunamiChipset chipset(ChipsetVariant::Tsunami, /*cpuCount*/ 1,
                           /*memSizeBytes*/ 256ULL * 1024ULL * 1024ULL);
    memoryLib::ISystemBus& bus = chipset;

    SUBCASE("DRAM write/read round-trips through GuestMemory") {
        auto const w = bus.write(0x1000, 0xDEADBEEFCAFEF00DULL, 8);
        CHECK(w.status == BusStatus::Ok);

        auto const r = bus.read(0x1000, 8);
        CHECK(r.status == BusStatus::Ok);
        CHECK(r.data == 0xDEADBEEFCAFEF00DULL);
    }

    SUBCASE("Unclaimed PA -> NXM machine check + MISC.NXM latched") {
        // 0x20_0000_0000 (128 GB): above DRAM, below the I/O window.
        auto const n = bus.read(0x2000000000ULL, 8);
        CHECK(n.status == BusStatus::BusError);

        // reportNxm -> latchNxm set MISC<NXM> (bit 28).  Read MISC via the
        // Cchip CSR offset (0x0080); CPUID injection on read touches only
        // bits 1:0, so bit 28 reflects the latch.
        uint64_t const misc = chipset.cchip().read(0x0080, /*cpuId*/ 0);
        CHECK(((misc >> 28) & 0x1ULL) == 1ULL);
    }

    SUBCASE("I/O-window PA routes to the CSR dispatch (not NXM)") {
        // Cchip CSR base (0x801.A000.0000) is in the I/O window: must route.
        auto const io = bus.read(Tsunami21272::Base::kCchip_CSR, 8);
        CHECK(io.status == BusStatus::Ok);
    }

    SUBCASE("TIG-bus device registers (TsunamiTig) -- Halt OUT + R/W fidelity") {
        // smir (TIG+0x40) is the front-panel Halt gate: MUST read 0 = "no
        // halt" (all-ones here = "Halt Button is IN" -> `boot` refused).
        // STATUS-ONLY: a write must NOT be stored (a W1C handing back non-zero
        // would re-assert halt).  See TsunamiTig / project_tig_halt_register.
        CHECK(bus.read(0x80130000040ULL, 8).data == 0ULL);                 // smir = 0
        CHECK(bus.write(0x80130000040ULL, 0xFFULL, 8).status == BusStatus::Ok);
        CHECK(bus.read(0x80130000040ULL, 8).data == 0ULL);                 // still 0 (no store)

        // halt-IPI (0x3C0/0x5C0) and ipcr (0xA00) are R/W storage.
        CHECK(bus.write(0x801300003C0ULL, 1ULL, 8).status == BusStatus::Ok);
        CHECK(bus.read(0x801300003C0ULL, 8).data == 1ULL);                 // halt0 R/W
        CHECK(bus.write(0x80130000A00ULL, 0xABCDULL, 8).status == BusStatus::Ok);
        CHECK(bus.read(0x80130000A00ULL, 8).data == 0xABCDULL);            // ipcr0 R/W

        // clr_irq4 (TIG+0x440) is the IRQ4 (clock) acknowledge: write-to-clear
        // with no TIG latch -- absorb the write, always read 0 (modeled so the
        // unmodeled-TIG canary stays quiet; observed in the b dqa1 run).
        CHECK(bus.read(0x80130000440ULL, 8).data == 0ULL);                 // clr_irq4 = 0
        CHECK(bus.write(0x80130000440ULL, 1ULL, 8).status == BusStatus::Ok);
        CHECK(bus.read(0x80130000440ULL, 8).data == 0ULL);                 // still 0 (no store)

        // Arbiter/PLD revision (0x801_3800_0180) reads 0 (_PROVISIONAL,
        // display-only) and is read-only.
        CHECK(bus.read(0x80138000180ULL, 8).data == 0ULL);

        // An unmodeled in-window offset defaults to 0 / absorb -- NOT NXM.
        CHECK(bus.read(0x80130000200ULL, 8).status == BusStatus::Ok);
        CHECK(bus.read(0x80130000200ULL, 8).data == 0ULL);
        CHECK(bus.write(0x80130000200ULL, 7ULL, 8).status == BusStatus::Ok);
    }
}
