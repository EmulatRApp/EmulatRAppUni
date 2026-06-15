// ============================================================================
// tests/deviceLib/test_iicpcf8584.cpp -- PCF8584 IIC controller model tests
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
// ============================================================================
//
// Exercises deviceLib/Tsunami/IicPcf8584.h against the behaviors the
// DS10 SRM firmware depends on (apisrm iic_driver.c WEBBRICK path,
// pc264_io.c iic_read_csr/iic_write_csr), per the driver-path table in
// journals/IIC_PCF8584_Specification.txt sec. 6:
//
//   - Reset state per datasheet sec 6.10: S1 = PIN|BB (idle, bus free).
//   - ireg_test register readback: S0' / S2 / S3 store-and-return.
//   - The literal probe sequence: address to S0, S1<-IIC_START (0xC5),
//     status shows PIN=0 + BB=0 + LRB=1 (complete, NAK), S1<-IIC_STOP
//     (0xC3) returns to idle.  Critically LAB stays 0 throughout --
//     LAB=1 is the unbounded 2000 ms retry loop that stalled powerup.
//   - iic_chip_reset byte family leaves the model idle.
//   - Non-byte access floats reads / drops writes (house width rule).
//   - Determinism: two instances, identical byte streams.
//
// Per house rule, doctest CHECK only -- never REQUIRE (V4 builds disable
// exceptions; REQUIRE expands to a static_assert that fails compile).
// ============================================================================

#include "doctest.h"

#include <cstdint>
#include <vector>

#include "deviceLib/Tsunami/IicPcf8584.h"

namespace {

// Rebased-offset byte helpers (the PciMemRange seam hands the model
// offset 0x0 = S0-area, 0x1 = S1).
uint8_t rd(IicPcf8584& c, uint16_t off)
{
    return static_cast<uint8_t>(c.ioRead(off, 1) & 0xFFu);
}

void wr(IicPcf8584& c, uint16_t off, uint8_t v)
{
    c.ioWrite(off, v, 1);
}

// Driver constants (apisrm iic_def.h) for sequence fidelity.
constexpr uint8_t kIicInit  = 0xC0;   // ES0 | PIN
constexpr uint8_t kIicStart = 0xC5;   // ES0 | PIN... per def: S0+STA+ACKB+ESO
constexpr uint8_t kIicStop  = 0xC3;   // S0+STO+ACKB+ESO+PIN

// v4: a small configured bus for the device-table tests -- NVRAM at
// 0xC0/0xC2/0xCE (the build_dsrdb RCM nodes), a FRU at 0xA2, status at 0x70.
std::vector<IicPcf8584::IicDevice> testBus()
{
    std::vector<IicPcf8584::IicDevice> v;
    auto add = [&](uint8_t addr, IicPcf8584::Kind k) {
        IicPcf8584::IicDevice d; d.address = addr; d.kind = k; v.push_back(d);
    };
    add(0xC0, IicPcf8584::Kind::Nvram);
    add(0xC2, IicPcf8584::Kind::Nvram);
    add(0xCE, IicPcf8584::Kind::Nvram);
    add(0xA2, IicPcf8584::Kind::FruEeprom);
    add(0x70, IicPcf8584::Kind::Status);
    return v;
}

} // namespace


TEST_CASE("IicPcf8584: reset state -- PIN set, bus free, no error bits")
{
    IicPcf8584 c;
    uint8_t const s = rd(c, IicPcf8584::kOffCtrlSt);
    CHECK(s == (IicPcf8584::kSt_PIN | IicPcf8584::kSt_BB));
    CHECK((s & IicPcf8584::kSt_LAB) == 0);    // never lost-arb
    CHECK((s & IicPcf8584::kSt_BER) == 0);    // never bus-error
}


TEST_CASE("IicPcf8584: ireg_test readback -- S0' / S2 / S3 store and return")
{
    IicPcf8584 c;

    // S0' own address (ES0=0, ES1=0, ES2=0): the driver writes 0x5B.
    wr(c, IicPcf8584::kOffCtrlSt, 0x00);
    wr(c, IicPcf8584::kOffData, 0x5B);
    CHECK(rd(c, IicPcf8584::kOffData) == 0x5B);

    // S2 clock register (ES1=1 per Table 5): webbrick writes 0x15
    // (45 kHz SCL + 6 MHz base).
    wr(c, IicPcf8584::kOffCtrlSt, IicPcf8584::kCtl_ES1);
    wr(c, IicPcf8584::kOffData, 0x15);
    CHECK(rd(c, IicPcf8584::kOffData) == 0x15);

    // S3 vector register (ES2=1 per Table 5).
    wr(c, IicPcf8584::kOffCtrlSt, IicPcf8584::kCtl_ES2);
    wr(c, IicPcf8584::kOffData, 0x97);        // IIC_VECTOR
    CHECK(rd(c, IicPcf8584::kOffData) == 0x97);
}


TEST_CASE("IicPcf8584: probe sequence -- instant completion with NAK, never LAB")
{
    IicPcf8584 c;

    // iic_rw_common: slave address byte into S0 (ES0 selected)...
    wr(c, IicPcf8584::kOffCtrlSt, IicPcf8584::kCtl_ES0);
    wr(c, IicPcf8584::kOffData, 0xA0);        // e.g. EEROM node, write dir

    // ... then START via S1 (IIC_START = 0xC5).
    wr(c, IicPcf8584::kOffCtrlSt, kIicStart);

    // iic_poll_rt reads S1: complete (PIN=0), bus owned (BB=0), and the
    // empty bus answered with no acknowledge (LRB=1).
    uint8_t const s = rd(c, IicPcf8584::kOffCtrlSt);
    CHECK((s & IicPcf8584::kSt_PIN) == 0);
    CHECK((s & IicPcf8584::kSt_BB) == 0);
    CHECK((s & IicPcf8584::kSt_LRB) != 0);
    CHECK((s & IicPcf8584::kSt_LAB) == 0);    // the stall bit stays 0

    // Driver issues STOP (IIC_STOP = 0xC3): idle again, bus free.
    wr(c, IicPcf8584::kOffCtrlSt, kIicStop);
    CHECK(rd(c, IicPcf8584::kOffCtrlSt) ==
          (IicPcf8584::kSt_PIN | IicPcf8584::kSt_BB));
}


TEST_CASE("IicPcf8584: iic_chip_reset byte family leaves the model idle")
{
    IicPcf8584 c;

    // pc264_io.c iic_chip_reset reference sequence (S1/S0 interleaved):
    wr(c, IicPcf8584::kOffCtrlSt, 0x80);      // PIN: software reset
    wr(c, IicPcf8584::kOffData, 0x5B);        // S0' own address
    wr(c, IicPcf8584::kOffCtrlSt, 0xA0);      // PIN | ES1
    wr(c, IicPcf8584::kOffData, 0x18);        // (clock family byte)
    wr(c, IicPcf8584::kOffCtrlSt, 0xC1);      // PIN | ES0 | ACK

    uint8_t const s = rd(c, IicPcf8584::kOffCtrlSt);
    CHECK(s == (IicPcf8584::kSt_PIN | IicPcf8584::kSt_BB));
    CHECK((s & IicPcf8584::kSt_LAB) == 0);
}


// v2 (2026-06-03): NAK'd-address semantics -- the driver never reads data
// after an address NAK (it errors on the nonzero status), but if it did,
// the floats must be harmless and the NAK status must persist until STOP.
TEST_CASE("IicPcf8584: NAK'd address -- status holds LRB, S0 floats")
{
    IicPcf8584 c;

    wr(c, IicPcf8584::kOffCtrlSt, IicPcf8584::kCtl_ES0);
    wr(c, IicPcf8584::kOffData, 0xA1);        // module EEPROM: absent
    wr(c, IicPcf8584::kOffCtrlSt, kIicStart);

    CHECK(rd(c, IicPcf8584::kOffCtrlSt) == IicPcf8584::kSt_LRB);
    CHECK(rd(c, IicPcf8584::kOffData) == 0xFF);                 // float
    CHECK(rd(c, IicPcf8584::kOffCtrlSt) == IicPcf8584::kSt_LRB);

    wr(c, IicPcf8584::kOffCtrlSt, kIicStop);
    CHECK(rd(c, IicPcf8584::kOffCtrlSt) ==
          (IicPcf8584::kSt_PIN | IicPcf8584::kSt_BB));
}


// ============================================================================
// v2 -- RCM NVRAM EEPROM bank (spec sec. 5A)
// ============================================================================

// The literal build_dsrdb access: fopen("iic_rcm_nvram0") -> iic node 0xC0,
// fseek(0x11) -> write-offset transaction, fread(1) -> read transaction.
// Driver contract P1/P2/P3: status 0x00 after every ACK'd byte; first S0
// read is the pipelined dummy (driver overwrites it); after IIC_NACK the
// next status read is 0x08 so the count-complete test passes.
TEST_CASE("IicPcf8584: rcm_nvram0 read sequence -- ACK, dummy, data, NACK done")
{
    IicPcf8584 c;
    c.configureDevices(testBus());

    // Seed the discriminator byte so the data phase is observable.
    uint8_t* rom = c.deviceImage(0xC0);
    CHECK(rom != nullptr);
    rom[0x11] = 0x5A;

    // Write transaction: set internal offset to 0x11, then STOP (the
    // driver closes every transaction with IIC_STOP before the next).
    wr(c, IicPcf8584::kOffCtrlSt, IicPcf8584::kCtl_ES0);
    wr(c, IicPcf8584::kOffData, 0xC0);        // node, write direction
    wr(c, IicPcf8584::kOffCtrlSt, kIicStart);
    CHECK(rd(c, IicPcf8584::kOffCtrlSt) == 0x00);   // P1: addr ACK'd
    wr(c, IicPcf8584::kOffData, 0x11);        // internal offset byte
    CHECK(rd(c, IicPcf8584::kOffCtrlSt) == 0x00);   // P1: byte ACK'd
    wr(c, IicPcf8584::kOffCtrlSt, kIicStop);

    // Read transaction: stage read address (idle), START.
    wr(c, IicPcf8584::kOffData, 0xC1);
    wr(c, IicPcf8584::kOffCtrlSt, kIicStart);
    CHECK(rd(c, IicPcf8584::kOffCtrlSt) == 0x00);

    // P2: dummy first, data second (driver stores then overwrites).
    CHECK(rd(c, IicPcf8584::kOffData) == 0xFF);     // dummy
    CHECK(rd(c, IicPcf8584::kOffData) == 0x5A);     // data[0x11]

    // P3: master NACK -> next status read shows LRB (0x08), then STOP.
    wr(c, IicPcf8584::kOffCtrlSt, kIicInit);        // 0xC0 = NACK here
    CHECK(rd(c, IicPcf8584::kOffCtrlSt) == IicPcf8584::kSt_LRB);
    wr(c, IicPcf8584::kOffCtrlSt, kIicStop);
    CHECK(rd(c, IicPcf8584::kOffCtrlSt) ==
          (IicPcf8584::kSt_PIN | IicPcf8584::kSt_BB));
}


// Driver idiom (iic_rw_common): each transaction is a complete
// stage-address / START / bytes / STOP unit -- the offset-write txn and
// the read txn are separated by STOP, not by a repeated START.
TEST_CASE("IicPcf8584: EEPROM write txn stores, read txn streams back")
{
    IicPcf8584 c;
    c.configureDevices(testBus());

    // Txn 1 (write): node 0xC2, offset 0x20, data 0xDE 0xAD, STOP.
    wr(c, IicPcf8584::kOffCtrlSt, IicPcf8584::kCtl_ES0);
    wr(c, IicPcf8584::kOffData, 0xC2);        // stage address (idle)
    wr(c, IicPcf8584::kOffCtrlSt, kIicStart);
    CHECK(rd(c, IicPcf8584::kOffCtrlSt) == 0x00);
    wr(c, IicPcf8584::kOffData, 0x20);        // internal offset
    wr(c, IicPcf8584::kOffData, 0xDE);        // data[0x20]
    wr(c, IicPcf8584::kOffData, 0xAD);        // data[0x21]
    CHECK(rd(c, IicPcf8584::kOffCtrlSt) == 0x00);
    wr(c, IicPcf8584::kOffCtrlSt, kIicStop);

    uint8_t const* rom = c.deviceImage(0xC2);
    CHECK(rom[0x20] == 0xDE);
    CHECK(rom[0x21] == 0xAD);

    // Txn 2 (re-aim offset): node 0xC2, offset 0x20 only, STOP.
    wr(c, IicPcf8584::kOffData, 0xC2);
    wr(c, IicPcf8584::kOffCtrlSt, kIicStart);
    wr(c, IicPcf8584::kOffData, 0x20);
    wr(c, IicPcf8584::kOffCtrlSt, kIicStop);

    // Txn 3 (read): node 0xC3, dummy, then the two bytes, NACK, STOP.
    wr(c, IicPcf8584::kOffData, 0xC3);        // read direction
    wr(c, IicPcf8584::kOffCtrlSt, kIicStart);
    CHECK(rd(c, IicPcf8584::kOffCtrlSt) == 0x00);
    CHECK(rd(c, IicPcf8584::kOffData) == 0xFF);   // P2 dummy
    CHECK(rd(c, IicPcf8584::kOffData) == 0xDE);
    CHECK(rd(c, IicPcf8584::kOffData) == 0xAD);
    wr(c, IicPcf8584::kOffCtrlSt, kIicInit);      // master NACK
    CHECK(rd(c, IicPcf8584::kOffCtrlSt) == IicPcf8584::kSt_LRB);
    wr(c, IicPcf8584::kOffCtrlSt, kIicStop);
    CHECK(rd(c, IicPcf8584::kOffCtrlSt) ==
          (IicPcf8584::kSt_PIN | IicPcf8584::kSt_BB));
}


TEST_CASE("IicPcf8584: controller reset preserves EEPROM content")
{
    IicPcf8584 c;
    c.configureDevices(testBus());
    c.deviceImage(0xCE)[0x00] = 0x42;
    c.reset();                                    // controller-only reset
    CHECK(c.deviceImage(0xCE)[0x00] == 0x42);      // separate chips persist
    CHECK(rd(c, IicPcf8584::kOffCtrlSt) ==
          (IicPcf8584::kSt_PIN | IicPcf8584::kSt_BB));
}


TEST_CASE("IicPcf8584: unknown nodes still NAK fast -- LAB never set")
{
    IicPcf8584 c;
    uint8_t const absent[] = { 0xA0, 0xA8, 0xAC, 0x9E, 0x40, 0xD0 };
    for (uint8_t node : absent) {
        wr(c, IicPcf8584::kOffCtrlSt, IicPcf8584::kCtl_ES0);
        wr(c, IicPcf8584::kOffData, node);
        wr(c, IicPcf8584::kOffCtrlSt, kIicStart);
        uint8_t const s = rd(c, IicPcf8584::kOffCtrlSt);
        CHECK(s == IicPcf8584::kSt_LRB);      // NAK, no LAB/BER
        wr(c, IicPcf8584::kOffCtrlSt, kIicStop);
    }
    CHECK(c.deviceImage(0xA0) == nullptr);         // not in the bank
}


TEST_CASE("IicPcf8584: non-byte access floats reads and drops writes")
{
    IicPcf8584 c;
    CHECK(c.ioRead(IicPcf8584::kOffCtrlSt, 4) == 0xFFull);
    c.ioWrite(IicPcf8584::kOffCtrlSt, 0x04, 8);   // dropped, no START
    CHECK(rd(c, IicPcf8584::kOffCtrlSt) ==
          (IicPcf8584::kSt_PIN | IicPcf8584::kSt_BB));
}


TEST_CASE("IicPcf8584: determinism -- identical byte streams, identical state")
{
    IicPcf8584 a;
    IicPcf8584 b;
    uint8_t const seq[][2] = {
        { IicPcf8584::kOffCtrlSt, 0x80 },
        { IicPcf8584::kOffData,   0x5B },
        { IicPcf8584::kOffCtrlSt, IicPcf8584::kCtl_ES0 },
        { IicPcf8584::kOffData,   0xA3 },
        { IicPcf8584::kOffCtrlSt, kIicStart },
        { IicPcf8584::kOffCtrlSt, kIicStop },
        { IicPcf8584::kOffCtrlSt, kIicInit },
    };
    for (auto const& s : seq) {
        wr(a, s[0], s[1]);
        wr(b, s[0], s[1]);
        CHECK(rd(a, IicPcf8584::kOffCtrlSt) ==
              rd(b, IicPcf8584::kOffCtrlSt));
        CHECK(rd(a, IicPcf8584::kOffData) ==
              rd(b, IicPcf8584::kOffData));
    }
}


// ============================================================================
// v4 (2026-06-07) -- manifest-driven device table (configureDevices)
// ============================================================================
// The hardcoded FRU/RCM banks are retired; presence is whatever Machine
// configures from the platform manifest.  A default instance is an empty bus
// (all probes NAK); the cases above rely on that.  These exercise the
// configured path, including the 0x70 status register that build_power_hw
// reads, and the snapshot content roundtrip.

TEST_CASE("IicPcf8584: empty bus by default -- every node absent")
{
    IicPcf8584 c;
    CHECK(c.deviceCount() == 0);
    CHECK(c.deviceImage(0xA2) == nullptr);
    CHECK(c.deviceImage(0xC0) == nullptr);
    CHECK(c.deviceImage(0x70) == nullptr);
}

TEST_CASE("IicPcf8584: configured FRU device is present and streams content")
{
    IicPcf8584 c;
    c.configureDevices(testBus());
    uint8_t* d = c.deviceImage(0xA2);              // FRU smb0
    CHECK(d != nullptr);
    d[0x7D] = 0xB5;                                // DEC JEDEC id byte

    wr(c, IicPcf8584::kOffCtrlSt, IicPcf8584::kCtl_ES0);
    wr(c, IicPcf8584::kOffData, 0xA2);             // write direction
    wr(c, IicPcf8584::kOffCtrlSt, kIicStart);
    CHECK(rd(c, IicPcf8584::kOffCtrlSt) == 0x00);  // ACK (present!)
    wr(c, IicPcf8584::kOffData, 0x7D);             // internal offset
    wr(c, IicPcf8584::kOffCtrlSt, kIicStop);
    wr(c, IicPcf8584::kOffData, 0xA3);             // read direction
    wr(c, IicPcf8584::kOffCtrlSt, kIicStart);
    CHECK(rd(c, IicPcf8584::kOffCtrlSt) == 0x00);
    CHECK(rd(c, IicPcf8584::kOffData) == 0xFF);    // P2 dummy
    CHECK(rd(c, IicPcf8584::kOffData) == 0xB5);    // configured content
    wr(c, IicPcf8584::kOffCtrlSt, kIicInit);
    wr(c, IicPcf8584::kOffCtrlSt, kIicStop);
}

TEST_CASE("IicPcf8584: 0x70 status register reads its byte (build_power_hw)")
{
    IicPcf8584 c;
    std::vector<IicPcf8584::IicDevice> bus;
    IicPcf8584::IicDevice s;
    s.address = 0x70; s.kind = IicPcf8584::Kind::Status; s.image[0] = 0x00;
    bus.push_back(s);
    c.configureDevices(bus);

    // build_power_hw: write the sub-address, then read one byte.
    wr(c, IicPcf8584::kOffCtrlSt, IicPcf8584::kCtl_ES0);
    wr(c, IicPcf8584::kOffData, 0x70);             // write direction
    wr(c, IicPcf8584::kOffCtrlSt, kIicStart);
    CHECK(rd(c, IicPcf8584::kOffCtrlSt) == 0x00);  // ACK -- present, not NAK!
    wr(c, IicPcf8584::kOffData, 0x00);             // sub-address
    wr(c, IicPcf8584::kOffCtrlSt, kIicStop);
    wr(c, IicPcf8584::kOffData, 0x71);             // read direction
    wr(c, IicPcf8584::kOffCtrlSt, kIicStart);
    CHECK(rd(c, IicPcf8584::kOffCtrlSt) == 0x00);
    CHECK(rd(c, IicPcf8584::kOffData) == 0xFF);    // P2 dummy
    CHECK(rd(c, IicPcf8584::kOffData) == 0x00);    // the status byte
}

TEST_CASE("IicPcf8584: status register absorbs data writes (read-mostly)")
{
    IicPcf8584 c;
    std::vector<IicPcf8584::IicDevice> bus;
    IicPcf8584::IicDevice s;
    s.address = 0x72; s.kind = IicPcf8584::Kind::Status; s.image[0] = 0x33;
    bus.push_back(s);
    c.configureDevices(bus);

    wr(c, IicPcf8584::kOffCtrlSt, IicPcf8584::kCtl_ES0);
    wr(c, IicPcf8584::kOffData, 0x72);
    wr(c, IicPcf8584::kOffCtrlSt, kIicStart);
    wr(c, IicPcf8584::kOffData, 0x00);             // sub-address
    wr(c, IicPcf8584::kOffData, 0xEE);             // data write -- absorbed
    wr(c, IicPcf8584::kOffCtrlSt, kIicStop);
    CHECK(c.deviceImage(0x72)[0] == 0x33);         // unchanged
}

TEST_CASE("IicPcf8584: content image roundtrip preserves writes")
{
    IicPcf8584 a;
    a.configureDevices(testBus());
    a.deviceImage(0xA2)[0x80] = 'Z';               // sys_serialnumb[0]
    a.deviceImage(0xC0)[0x10] = '9';

    std::vector<uint8_t> img(a.contentBytes());
    a.contentImage(img.data());

    IicPcf8584 b;
    b.configureDevices(testBus());                 // same identity first
    b.restoreContentImage(img.data(), img.size());
    CHECK(b.deviceImage(0xA2)[0x80] == 'Z');
    CHECK(b.deviceImage(0xC0)[0x10] == '9');
}
