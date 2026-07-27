// ============================================================================
// tests/deviceLib/test_ncr53c810.cpp -- NCR 53C810 SCRIPTS engine + SCSI bus
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
// JRN-SCSI-001 P2 acceptance at the unit level: a hand-assembled SCRIPTS
// program (encodings per apisrm ref/n810_def.h; shape per ref/pke_script.mar
// -- table-indirect moves off DSA, phase-conditional jumps, INT vectors in
// DSPS) drives a full SELECT -> IDENTIFY -> INQUIRY -> DATA IN -> STATUS ->
// MSG IN transaction against a VirtualDiskDevice on RAM-backed media, all
// through the bulk-DMA seam.  Also: selection timeout (STO) on an empty id
// -- the mechanism the SRM pk driver uses to map the bus -- and READ(10).
//
// Per V4 doctest convention: CHECK only, never REQUIRE.
// ============================================================================

#include "doctest.h"

#include "deviceLib/Tsunami/Ncr53C810.h"
#include "deviceLib/scsi/IBlockMedia.h"
#include "deviceLib/scsi/VirtualDiskDevice.h"

#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

namespace {

// ---- RAM-backed IBlockMedia (64 blocks x 512) ------------------------------
struct RamMedia final : scsi::IBlockMedia {
    std::vector<uint8_t> blocks = std::vector<uint8_t>(64 * 512, 0);

    scsi::MediaStatus open() override  { return scsi::MediaStatus::Ok; }
    void close() override {}
    bool isOpen()     const override { return true; }
    bool isPresent()  const override { return true; }
    bool isReadOnly() const override { return false; }
    uint32_t blockSize()  const override { return 512; }
    uint64_t blockCount() const override { return 64; }
    scsi::MediaStatus read(uint64_t lba, uint32_t cnt, void* dst) override
    {
        std::memcpy(dst, blocks.data() + lba * 512, cnt * 512);
        return scsi::MediaStatus::Ok;
    }
    scsi::MediaStatus write(uint64_t lba, uint32_t cnt, const void* src) override
    {
        std::memcpy(blocks.data() + lba * 512, src, cnt * 512);
        return scsi::MediaStatus::Ok;
    }
};

// ---- test harness: HBA + guest RAM + disk ---------------------------------
struct Harness {
    deviceLib::Ncr53C810   hba;
    scsi::VirtualDiskDevice disk;
    std::vector<uint8_t>   ram = std::vector<uint8_t>(0x10000, 0);
    RamMedia*              media = nullptr;   // owned by disk

    Harness()
    {
        auto m = std::make_unique<RamMedia>();
        media  = m.get();
        disk.setMedia(std::move(m));
        hba.setDmaAccess(
            [this](uint64_t a, void* d, size_t n) {
                std::memcpy(d, ram.data() + a, n);
            },
            [this](uint64_t a, void const* s, size_t n) {
                std::memcpy(ram.data() + a, s, n);
            });
        hba.attachTarget(0, &disk);
    }

    void put32(uint32_t addr, uint32_t v)
    {
        ram[addr]     = uint8_t(v);
        ram[addr + 1] = uint8_t(v >> 8);
        ram[addr + 2] = uint8_t(v >> 16);
        ram[addr + 3] = uint8_t(v >> 24);
    }

    // regs
    void     w32(uint8_t off, uint32_t v) { hba.ioWrite(off, v, 4); }
    uint8_t  r8(uint8_t off)              { return uint8_t(hba.ioRead(off, 1)); }
    uint32_t r32(uint8_t off)             { return uint32_t(hba.ioRead(off, 4)); }

    // ---- hand-assembled pke-shaped script -------------------------------
    // Layout: script @0x1000, DSA @0x2000, msg_out @0x3000, cdb @0x3010,
    // data-in @0x3100 (room for 512+), status @0x3400, msg-in @0x3410.
    static constexpr uint32_t kScript = 0x1000, kDsa = 0x2000,
                              kMsgOut = 0x3000, kCdb = 0x3010,
                              kDatIn = 0x3100, kSts = 0x3400, kMsgIn = 0x3410;

    void buildScript(unsigned targetId)
    {
        uint32_t a = kScript;
        auto emit = [&](uint32_t w0, uint32_t w1) { put32(a, w0); put32(a + 4, w1); a += 8; };
        uint32_t const cmdBlk  = kScript + 5 * 8;
        uint32_t const datBlk  = kScript + 9 * 8;
        uint32_t const stsBlk  = kScript + 11 * 8;

        // sel atn=yes id, alt unused (D2)
        emit(0x41000000u | (targetId << 16), 0);
        // int 101 when NOT msg_out (wait, cmp_phase, sense=false)
        emit(0x98000000u | (6u << 24) | (1u << 17) | (1u << 16), 101);
        // move from 4, when msg_out (table indirect, wait)
        emit(0x18000000u | (1u << 28) | (6u << 24), 4);
        // jmp cmdBlk when cmd (wait, cmp_phase, sense=true)
        emit(0x80000000u | (2u << 24) | (1u << 19) | (1u << 17) | (1u << 16), cmdBlk);
        // int 102 (unconditional)
        emit(0x98000000u | (1u << 19), 102);
        // cmdBlk: move from 12 when cmd
        emit(0x18000000u | (1u << 28) | (2u << 24), 12);
        // jmp datBlk when dat_in
        emit(0x80000000u | (1u << 24) | (1u << 19) | (1u << 17) | (1u << 16), datBlk);
        // jmp stsBlk when sts
        emit(0x80000000u | (3u << 24) | (1u << 19) | (1u << 17) | (1u << 16), stsBlk);
        // int 103 (unconditional)
        emit(0x98000000u | (1u << 19), 103);
        // datBlk: move from 28 when dat_in
        emit(0x18000000u | (1u << 28) | (1u << 24), 28);
        // jmp stsBlk when sts
        emit(0x80000000u | (3u << 24) | (1u << 19) | (1u << 17) | (1u << 16), stsBlk);
        // stsBlk: move from 36 when sts
        emit(0x18000000u | (1u << 28) | (3u << 24), 36);
        // move 1 byte direct to kMsgIn when msg_in
        emit(0x08000000u | (7u << 24) | 1u, kMsgIn);
        // int 0 = ok (unconditional)
        emit(0x98000000u | (1u << 19), 0);
    }

    void buildDsa(uint32_t cdbLen, uint32_t datInLen)
    {
        put32(kDsa + 0, 0);                    // sel entry (unused by model)
        put32(kDsa + 4, 1);   put32(kDsa + 8, kMsgOut);   // msg_out {1, ptr}
        put32(kDsa + 12, cdbLen); put32(kDsa + 16, kCdb); // cmd
        put32(kDsa + 20, 0);  put32(kDsa + 24, 0);        // dat_out
        put32(kDsa + 28, datInLen); put32(kDsa + 32, kDatIn);
        put32(kDsa + 36, 1);  put32(kDsa + 40, kSts);     // status
        ram[kMsgOut] = 0x80;                   // IDENTIFY, LUN 0
    }

    void kick()
    {
        w32(0x10, kDsa);        // DSA
        w32(0x2C, kScript);     // DSP write -> SCRIPTS run to completion
    }
};

} // namespace

TEST_CASE("53C810 config identity is the io_device_list bind row")
{
    Harness h;
    CHECK(h.hba.pciConfigRead(0x00, 4) == 0x00011000u);   // NCR 53C810
    CHECK((h.hba.pciConfigRead(0x08, 4) >> 8) == 0x010000u);
    CHECK((h.hba.pciConfigRead(0x3C, 2) >> 8) == 0x01u);  // INTA
}

TEST_CASE("SCRIPTS INQUIRY transaction end-to-end")
{
    Harness h;
    h.buildScript(/*targetId*/ 0);
    h.buildDsa(/*cdbLen*/ 6, /*datInLen*/ 36);
    uint8_t const cdb[6] = { 0x12, 0, 0, 0, 36, 0 };      // INQUIRY, alloc 36
    std::memcpy(h.ram.data() + Harness::kCdb, cdb, 6);

    h.kick();

    // INT ok: DSTAT<SIR> pending, DSPS holds vector 0.
    CHECK((h.r8(0x14) & 0x01) != 0);          // ISTAT<DIP>
    CHECK(h.r32(0x30) == 0u);                 // DSPS = ok
    uint8_t const dstat = h.r8(0x0C);
    CHECK((dstat & 0x04) != 0);               // SIR
    CHECK((h.r8(0x14) & 0x01) == 0);          // DIP cleared by DSTAT read

    // INQUIRY payload landed in guest RAM via DMA.
    CHECK(h.ram[Harness::kDatIn + 0] == 0x00);            // direct-access
    CHECK(std::memcmp(h.ram.data() + Harness::kDatIn + 8, "EMULATR ", 8) == 0);
    CHECK(h.ram[Harness::kSts] == 0x00);                  // GOOD status
    CHECK(h.ram[Harness::kMsgIn] == 0x00);                // COMMAND COMPLETE
}

TEST_CASE("Selection of an empty id raises STO (how the pk driver maps the bus)")
{
    Harness h;
    h.buildScript(/*targetId*/ 3);            // nothing attached at id 3
    h.buildDsa(6, 36);
    h.kick();

    CHECK((h.r8(0x14) & 0x02) != 0);          // ISTAT<SIP>
    CHECK((h.r8(0x43) & 0x04) != 0);          // SIST1<STO>, clear-on-read
    CHECK((h.r8(0x14) & 0x02) == 0);          // cleared
}

TEST_CASE("SCRIPTS READ(10) moves media blocks into guest RAM")
{
    Harness h;
    for (int i = 0; i < 512; ++i)
        h.media->blocks[2 * 512 + i] = uint8_t(i * 3 + 1); // pattern @ LBA 2

    h.buildScript(0);
    h.buildDsa(/*cdbLen*/ 10, /*datInLen*/ 512);
    uint8_t const cdb[10] = { 0x28, 0, 0, 0, 0, 2, 0, 0, 1, 0 }; // READ10 lba2 cnt1
    std::memcpy(h.ram.data() + Harness::kCdb, cdb, 10);

    h.kick();

    CHECK(h.r32(0x30) == 0u);                 // ok
    (void) h.r8(0x0C);                        // drain DSTAT
    bool match = true;
    for (int i = 0; i < 512 && match; ++i)
        match = (h.ram[Harness::kDatIn + i] == uint8_t(i * 3 + 1));
    CHECK(match);
    CHECK(h.ram[Harness::kSts] == 0x00);
}

TEST_CASE("Unsupported LUN answers INQUIRY with qualifier 011b (no phantom units)")
{
    // 2026-07-25: the SRM class driver probes every LUN of a responding id;
    // LUN 1..7 must NOT look like disks or the console mints dka1..dka7
    // (observed live, gate run 3).  SPC: INQUIRY -> byte0 0x7F; other ops ->
    // CHECK CONDITION 05/25/00.
    Harness h;
    h.buildScript(0);
    h.buildDsa(6, 36);
    h.ram[Harness::kMsgOut] = 0x81;           // IDENTIFY, LUN 1
    uint8_t const cdb[6] = { 0x12, 0, 0, 0, 36, 0 };
    std::memcpy(h.ram.data() + Harness::kCdb, cdb, 6);

    h.kick();

    CHECK(h.r32(0x30) == 0u);                 // script completes (INT ok)
    (void) h.r8(0x0C);
    CHECK(h.ram[Harness::kDatIn + 0] == 0x7F); // qualifier 011b | type 1Fh
    CHECK(h.ram[Harness::kSts] == 0x00);       // INQUIRY itself is GOOD

    // A non-INQUIRY command to LUN 1 must CHECK CONDITION.
    h.buildDsa(6, 0);
    h.ram[Harness::kMsgOut] = 0x81;
    uint8_t const tur[6] = { 0x00, 0, 0, 0, 0, 0 };
    std::memcpy(h.ram.data() + Harness::kCdb, tur, 6);
    h.kick();
    (void) h.r8(0x0C);
    CHECK(h.ram[Harness::kSts] == 0x02);       // CHECK CONDITION
}

TEST_CASE("BAR rebind callbacks fire on 53C810 BAR re-program")
{
    Harness h;
    int regs = 0, unregs = 0;
    h.hba.setRangeCallbacks(
        [&](uint64_t, uint32_t len, bool isMem, IIoPortHandler*) {
            ++regs; CHECK(len == 0x100u); CHECK(isMem);
        },
        [&](uint64_t, uint32_t, bool, IIoPortHandler*) { ++unregs; });
    h.hba.pciConfigWrite(0x14, 0xFFFFFFFFu, 4);   // size probe: no movement
    CHECK(regs == 0);
    h.hba.pciConfigWrite(0x14, 0x00400000u, 4);
    CHECK(regs == 1);
    h.hba.pciConfigWrite(0x14, 0x00600000u, 4);
    CHECK(regs == 2);
    CHECK(unregs == 1);
}

// ============================================================================
// MODE SELECT (JRN-SCSI-026 Sec 7 -> JRN-SCSI-027).  OpenVMS SYSBOOT issues
// MODE SELECT(6) while bringing the boot device up; the device previously
// answered ILLEGAL REQUEST (invalid opcode) and SYSBOOT gave up with
// %SYSBOOT-F-LDFAIL before reading a single file byte.  These drive the
// target directly (the SCRIPTS path is covered by the transactions above).
// ============================================================================

namespace {

// Submit a CDB straight to a VirtualDiskDevice with an optional data-out
// parameter list, mirroring what the HBA hands the target.
scsi::ScsiCommand runCdb(scsi::VirtualDiskDevice& disk,
                         uint8_t const* cdb, uint8_t cdbLen,
                         uint8_t* buf = nullptr, uint32_t bufLen = 0)
{
    scsi::ScsiCommand cmd;
    cmd.cdb              = cdb;
    cmd.cdbLength        = cdbLen;
    cmd.lun              = 0;
    cmd.dataBuffer       = buf;
    cmd.dataBufferLength = bufLen;        // data-out: what the initiator sent
    disk.handleCommand(cmd);
    return cmd;
}

} // namespace

TEST_CASE("MODE SELECT(6) with no parameter list is accepted")
{
    Harness h;
    uint8_t const cdb[6] = { 0x15, 0x10, 0, 0, 0x00, 0 };   // list length 0
    scsi::ScsiCommand const r = runCdb(h.disk, cdb, 6);
    CHECK(r.status == scsi::ScsiStatus::Good);
}

TEST_CASE("MODE SELECT(6) accepts a block descriptor that keeps the block size")
{
    Harness h;
    uint8_t parm[12] = {};
    parm[3] = 8;                       // block descriptor length
    parm[4 + 5] = 0; parm[4 + 6] = 0x02; parm[4 + 7] = 0x00;   // 512 = media
    uint8_t const cdb[6] = { 0x15, 0x10, 0, 0, 12, 0 };
    scsi::ScsiCommand const r =
        runCdb(h.disk, cdb, 6, parm, sizeof(parm));
    CHECK(r.status == scsi::ScsiStatus::Good);

    // "Keep current" (block length 0) is equally legal.
    uint8_t parm0[12] = {};
    parm0[3] = 8;
    scsi::ScsiCommand const r0 =
        runCdb(h.disk, cdb, 6, parm0, sizeof(parm0));
    CHECK(r0.status == scsi::ScsiStatus::Good);
}

TEST_CASE("MODE SELECT(6) rejects a block size the media does not have")
{
    Harness h;
    uint8_t parm[12] = {};
    parm[3] = 8;
    parm[4 + 5] = 0; parm[4 + 6] = 0x04; parm[4 + 7] = 0x00;   // 1024 != 512
    uint8_t const cdb[6] = { 0x15, 0x10, 0, 0, 12, 0 };
    scsi::ScsiCommand const r =
        runCdb(h.disk, cdb, 6, parm, sizeof(parm));
    // Silently ignoring this would make every later LBA a lie.
    CHECK(r.status == scsi::ScsiStatus::CheckCondition);
    CHECK(r.senseValid);
}

TEST_CASE("MODE SELECT(6) with a truncated parameter list reports a length error")
{
    Harness h;
    uint8_t parm[12] = {};
    uint8_t const cdb[6] = { 0x15, 0x10, 0, 0, 12, 0 };
    // Initiator promised 12 bytes (CDB), but only 2 are available.
    scsi::ScsiCommand const r = runCdb(h.disk, cdb, 6, parm, 2);
    CHECK(r.status == scsi::ScsiStatus::CheckCondition);
}

TEST_CASE("MODE SELECT(10) shares the contract with the 6-byte form")
{
    Harness h;
    uint8_t parm[16] = {};
    parm[7] = 8;                       // block descriptor length (10-byte hdr)
    uint8_t const cdb[10] = { 0x55, 0x10, 0, 0, 0, 0, 0, 0, 16, 0 };
    scsi::ScsiCommand const r =
        runCdb(h.disk, cdb, 10, parm, sizeof(parm));
    CHECK(r.status == scsi::ScsiStatus::Good);
}
