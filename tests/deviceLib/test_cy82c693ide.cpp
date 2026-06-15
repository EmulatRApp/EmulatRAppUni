// ============================================================================
// tests/deviceLib/test_cy82c693ide.cpp -- CY82C693 IDE detection + ATAPI sig
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
// S2 of the IDE/ATAPI scaffold.  Exercises the dq_driver.c detection handshake:
// present ATAPI unit -> ready + 0x14/0xEB signature + IDENTIFY-DEVICE abort;
// absent unit -> contract C1 (BSY-clear "no device", never float-bus 0xFF).
// doctest CHECK only.
// ============================================================================

#include "doctest.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>     // S5: temp raw disk image for the ATA-disk tests
#include <cstring>
#include <string>
#include <vector>

#include "deviceLib/Tsunami/Cy82C693Ide.h"
#include "deviceLib/scsi/VirtualIsoDevice.h"

namespace {

uint8_t rd8(Cy82C693Ide& ide, uint16_t port)
{
    return static_cast<uint8_t>(ide.ioRead(port, 1) & 0xFFu);
}
void wr8(Cy82C693Ide& ide, uint16_t port, uint8_t v)
{
    ide.ioWrite(port, v, 1);
}

// Primary command-block register ports.

} // namespace

TEST_CASE("Cy82C693Ide: present ATAPI master -- ready, no BSY")
{
    Cy82C693Ide ide;
    scsi::VirtualIsoDevice cd;            // ATAPI CD (deviceType 0x05)
    ide.attachDevice(0, 0, &cd);          // primary master

    wr8(ide, 0x1F6, 0x00);                // drive/head: select master (bit4=0)
    uint8_t const st = rd8(ide, 0x1F7);   // status
    CHECK((st & Cy82C693Ide::kDRDY) != 0);  // device ready
    CHECK((st & Cy82C693Ide::kBSY)  == 0);  // not busy
    CHECK(st != 0xFF);
}

TEST_CASE("Cy82C693Ide: IDENTIFY DEVICE aborts with the ATAPI signature")
{
    Cy82C693Ide ide;
    scsi::VirtualIsoDevice cd;
    ide.attachDevice(0, 0, &cd);

    wr8(ide, 0x1F6, 0x00);                // select master
    wr8(ide, 0x1F7, Cy82C693Ide::kCMD_IDENTIFY);   // 0xEC

    uint8_t const st = rd8(ide, 0x1F7);
    CHECK((st & Cy82C693Ide::kERR) != 0);          // aborted
    CHECK(rd8(ide, 0x1F1) == Cy82C693Ide::kERR_ABRT);  // error reg = ABRT
    CHECK(rd8(ide, 0x1F4) == 0x14);                // cyl low  = ATAPI sig
    CHECK(rd8(ide, 0x1F5) == 0xEB);                // cyl high = ATAPI sig
}

TEST_CASE("Cy82C693Ide: absent slave -- C1 BSY-clear, never 0xFF")
{
    Cy82C693Ide ide;
    scsi::VirtualIsoDevice cd;
    ide.attachDevice(0, 0, &cd);          // only master populated

    wr8(ide, 0x1F6, 0x10);                // drive/head: select slave (bit4=1)
    uint8_t const st = rd8(ide, 0x1F7);
    CHECK((st & Cy82C693Ide::kBSY) == 0); // BSY clear (no phantom-busy hang)
    CHECK(st != 0xFF);                    // NOT float-bus
    CHECK((st & Cy82C693Ide::kDRDY) == 0);// no device ready
    CHECK(st == 0x00);
}

TEST_CASE("Cy82C693Ide: alt-status mirrors status without side effects")
{
    Cy82C693Ide ide;
    scsi::VirtualIsoDevice cd;
    ide.attachDevice(0, 0, &cd);

    wr8(ide, 0x1F6, 0x00);                // select master
    CHECK(rd8(ide, 0x3F6) == rd8(ide, 0x1F7));   // alt status == status
}

TEST_CASE("Cy82C693Ide: unknown command aborts")
{
    Cy82C693Ide ide;
    scsi::VirtualIsoDevice cd;
    ide.attachDevice(0, 0, &cd);

    wr8(ide, 0x1F6, 0x00);
    wr8(ide, 0x1F7, 0xB7);               // bogus command
    uint8_t const st = rd8(ide, 0x1F7);
    CHECK((st & Cy82C693Ide::kERR) != 0);
    CHECK(rd8(ide, 0x1F1) == Cy82C693Ide::kERR_ABRT);
}

TEST_CASE("Cy82C693Ide: empty controller -- both units absent answer C1")
{
    Cy82C693Ide ide;                     // nothing attached
    wr8(ide, 0x1F6, 0x00);
    CHECK(rd8(ide, 0x1F7) == 0x00);
    wr8(ide, 0x1F6, 0x10);
    CHECK(rd8(ide, 0x1F7) == 0x00);
}

// ---- S3: ATAPI packet transport -----------------------------------------

namespace {

uint16_t rd16(Cy82C693Ide& ide, uint16_t port)
{
    return static_cast<uint16_t>(ide.ioRead(port, 2) & 0xFFFFu);
}

// Issue PACKET on the primary channel and feed the 12-byte CDB as 6 words.
void issuePacket(Cy82C693Ide& ide, const uint8_t cdb[12], uint16_t byteCount)
{
    wr8(ide, 0x1F1, 0x00);                                // features = PIO
    wr8(ide, 0x1F4, static_cast<uint8_t>(byteCount & 0xFFu));
    wr8(ide, 0x1F5, static_cast<uint8_t>((byteCount >> 8) & 0xFFu));
    wr8(ide, 0x1F7, Cy82C693Ide::kCMD_PACKET);           // 0xA0
    for (int i = 0; i < 12; i += 2) {
        uint16_t const w = static_cast<uint16_t>(cdb[i] | (cdb[i + 1] << 8));
        ide.ioWrite(0x1F0, w, 2);
    }
}

} // namespace

TEST_CASE("Cy82C693Ide: PACKET INQUIRY round-trip returns CDROM (type 0x05)")
{
    Cy82C693Ide ide;
    scsi::VirtualIsoDevice cd;
    ide.attachDevice(0, 0, &cd);
    wr8(ide, 0x1F6, 0x00);                                // select master

    // After PACKET the device requests the CDB (DRQ set).
    wr8(ide, 0x1F1, 0x00);
    wr8(ide, 0x1F4, 36); wr8(ide, 0x1F5, 0);
    wr8(ide, 0x1F7, Cy82C693Ide::kCMD_PACKET);
    CHECK((rd8(ide, 0x1F7) & Cy82C693Ide::kDRQ) != 0);

    // Feed the 12-byte INQUIRY CDB.
    uint8_t const inquiry[12] = { 0x12, 0, 0, 0, 36, 0, 0, 0, 0, 0, 0, 0 };
    for (int i = 0; i < 12; i += 2) {
        uint16_t const w = static_cast<uint16_t>(inquiry[i] | (inquiry[i + 1] << 8));
        ide.ioWrite(0x1F0, w, 2);
    }

    // Data-in phase: DRQ set, byte count = 36.
    CHECK((rd8(ide, 0x1F7) & Cy82C693Ide::kDRQ) != 0);
    CHECK(rd8(ide, 0x1F4) == 36);

    uint8_t buf[36] = { 0 };
    for (int i = 0; i < 18; ++i) {
        uint16_t const w = rd16(ide, 0x1F0);
        buf[2 * i]     = static_cast<uint8_t>(w & 0xFFu);
        buf[2 * i + 1] = static_cast<uint8_t>((w >> 8) & 0xFFu);
    }
    CHECK((buf[0] & 0x1f) == 0x05);                       // CD/DVD peripheral type
    CHECK(buf[1] == 0x80);                                // removable
    CHECK((rd8(ide, 0x1F7) & Cy82C693Ide::kDRQ) == 0);    // drained -> DRQ clear
    CHECK((rd8(ide, 0x1F7) & Cy82C693Ide::kDRDY) != 0);
}

TEST_CASE("Cy82C693Ide: PACKET TEST UNIT READY (no media) -> not-ready sense")
{
    Cy82C693Ide ide;
    scsi::VirtualIsoDevice cd;
    ide.attachDevice(0, 0, &cd);
    wr8(ide, 0x1F6, 0x00);

    uint8_t const tur[12] = { 0x00, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    issuePacket(ide, tur, 0);

    CHECK((rd8(ide, 0x1F7) & Cy82C693Ide::kERR) != 0);    // CheckCondition
    CHECK((rd8(ide, 0x1F1) >> 4) == 0x02);                // sense key NOT READY in error<7:4>
}

TEST_CASE("Cy82C693Ide: PIDENTIFY returns an ATAPI identify (word0 config)")
{
    Cy82C693Ide ide;
    scsi::VirtualIsoDevice cd;
    ide.attachDevice(0, 0, &cd);
    wr8(ide, 0x1F6, 0x00);

    wr8(ide, 0x1F7, Cy82C693Ide::kCMD_PIDENTIFY);         // 0xA1
    CHECK((rd8(ide, 0x1F7) & Cy82C693Ide::kDRQ) != 0);
    CHECK(rd16(ide, 0x1F0) == 0x85C0);                    // ATAPI CD config word
}

// ---- S5: ATA fixed-disk path --------------------------------------------

namespace {

// Write a raw flat image of `sectors' 512-byte sectors; each sector's first
// dword (little-endian) is its own LBA, so a read can be verified by content.
void makeDiskImage(const char* path, uint32_t sectors)
{
    std::FILE* f = std::fopen(path, "wb");
    if (!f) return;
    std::vector<uint8_t> sec(512, 0);
    for (uint32_t lba = 0; lba < sectors; ++lba) {
        std::fill(sec.begin(), sec.end(), static_cast<uint8_t>(0));
        sec[0] = static_cast<uint8_t>(lba & 0xFFu);
        sec[1] = static_cast<uint8_t>((lba >> 8) & 0xFFu);
        sec[2] = static_cast<uint8_t>((lba >> 16) & 0xFFu);
        sec[3] = static_cast<uint8_t>((lba >> 24) & 0xFFu);
        std::fwrite(sec.data(), 1, 512, f);
    }
    std::fclose(f);
}

} // namespace

TEST_CASE("Cy82C693Ide: ATA disk presents non-ATAPI ready signature")
{
    const char* path = "test_dqa0_sig.img";
    makeDiskImage(path, 8);
    Cy82C693Ide ide;
    CHECK(ide.attachDisk(0, 0, path));            // primary master = ATA disk
    wr8(ide, 0x1F6, 0x00);                        // select master
    uint8_t const st = rd8(ide, 0x1F7);
    CHECK((st & Cy82C693Ide::kDRDY) != 0);
    CHECK((st & Cy82C693Ide::kBSY)  == 0);
    CHECK(rd8(ide, 0x1F4) == 0x00);               // NOT the ATAPI 0x14/0xEB sig
    CHECK(rd8(ide, 0x1F5) == 0x00);
    std::remove(path);
}

TEST_CASE("Cy82C693Ide: ATA IDENTIFY DEVICE reports LBA, capacity and model")
{
    const char* path = "test_dqa0_identify.img";
    makeDiskImage(path, 8);                        // 8 sectors
    Cy82C693Ide ide;
    CHECK(ide.attachDisk(0, 0, path));
    wr8(ide, 0x1F6, 0x00);                         // select master
    wr8(ide, 0x1F7, Cy82C693Ide::kCMD_IDENTIFY);   // 0xEC -- real block, no abort
    uint8_t const st = rd8(ide, 0x1F7);
    CHECK((st & Cy82C693Ide::kDRQ) != 0);
    CHECK((st & Cy82C693Ide::kERR) == 0);          // ATA disk does NOT abort 0xEC

    uint16_t id[256] = { 0 };
    for (int i = 0; i < 256; ++i) id[i] = rd16(ide, 0x1F0);

    CHECK(id[0] == 0x0040);                         // fixed, non-removable
    CHECK((id[49] & 0x0200) != 0);                  // LBA supported
    uint32_t const total = static_cast<uint32_t>(id[60])
                         | (static_cast<uint32_t>(id[61]) << 16);
    CHECK(total == 8u);                             // LBA28 total sectors

    char model[41] = { 0 };                         // words 27-46, byte-swapped
    for (int i = 0; i < 20; ++i) {
        model[2 * i]     = static_cast<char>((id[27 + i] >> 8) & 0xFFu);
        model[2 * i + 1] = static_cast<char>(id[27 + i] & 0xFFu);
    }
    CHECK(std::string(model).substr(0, 20) == "EMULATR VIRTUAL DISK");
    std::remove(path);
}

TEST_CASE("Cy82C693Ide: READ SECTORS streams the seeded LBA content")
{
    const char* path = "test_dqa0_read.img";
    makeDiskImage(path, 8);
    Cy82C693Ide ide;
    CHECK(ide.attachDisk(0, 0, path));
    wr8(ide, 0x1F6, 0x40);                         // LBA mode, master
    wr8(ide, 0x1F2, 1);                            // sector count = 1
    wr8(ide, 0x1F3, 3);                            // LBA low = 3
    wr8(ide, 0x1F4, 0); wr8(ide, 0x1F5, 0);
    wr8(ide, 0x1F7, Cy82C693Ide::kCMD_READ_SECTORS); // 0x20
    CHECK((rd8(ide, 0x1F7) & Cy82C693Ide::kDRQ) != 0);

    uint16_t const w0 = rd16(ide, 0x1F0);
    uint16_t const w1 = rd16(ide, 0x1F0);
    uint32_t const first = static_cast<uint32_t>(w0)
                         | (static_cast<uint32_t>(w1) << 16);
    CHECK(first == 3u);                            // seeded first dword == LBA
    for (int i = 2; i < 256; ++i) (void) rd16(ide, 0x1F0);  // drain the sector
    CHECK((rd8(ide, 0x1F7) & Cy82C693Ide::kDRQ) == 0);      // transfer complete
    std::remove(path);
}

TEST_CASE("Cy82C693Ide: READ SECTORS past end aborts, never hangs BSY")
{
    const char* path = "test_dqa0_oor.img";
    makeDiskImage(path, 4);
    Cy82C693Ide ide;
    CHECK(ide.attachDisk(0, 0, path));
    wr8(ide, 0x1F6, 0x40);
    wr8(ide, 0x1F2, 1);
    wr8(ide, 0x1F3, 10);                           // LBA 10 > 4 sectors
    wr8(ide, 0x1F7, Cy82C693Ide::kCMD_READ_SECTORS);
    uint8_t const st = rd8(ide, 0x1F7);
    CHECK((st & Cy82C693Ide::kERR) != 0);
    CHECK((st & Cy82C693Ide::kBSY) == 0);          // no phantom-busy wedge
    CHECK(rd8(ide, 0x1F1) == Cy82C693Ide::kERR_ABRT);
    std::remove(path);
}

TEST_CASE("Cy82C693Ide: missing/empty image leaves the slot absent (C1)")
{
    Cy82C693Ide ide;
    CHECK(ide.attachDisk(0, 0, "") == false);              // empty path
    CHECK(ide.attachDisk(0, 0, "no_such_file.img") == false);
    wr8(ide, 0x1F6, 0x00);
    CHECK(rd8(ide, 0x1F7) == 0x00);                        // absent: C1, not 0xFF
}
