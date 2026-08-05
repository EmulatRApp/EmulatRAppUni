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
// 2026-08-03 Batch H-2 (JRN-SCSI-037): new section at the end covering the
// four constructs the VMS PKEDRIVER init script uses -- Memory Move (both
// directions incl. the chip's own BAR window, cited IID forms, No-Flush
// execute), relative transfer control (forward + negative), RW opc 5 SFBR
// sourcing, WAIT RESELECT park / ISTAT<SIGP> resume / CTEST2 read-clear,
// and the CALL -> MM -> RETURN TEMP-clobber interaction (asserts the
// silicon-true garbage return, guarding the D-ORACLE inversion).
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

    // W-4 (2026-08-04): consume the power-on UNIT ATTENTION (CHECK on the
    // first non-INQUIRY/REQUEST SENSE command) so tests of OTHER behavior
    // start from a quiesced device, the way a real bus looks after the
    // initiator's first attention exchange.
    void ackUnitAttention()
    {
        uint8_t const tur[6] = {};             // TEST UNIT READY
        scsi::ScsiCommand c;
        c.cdb = tur; c.cdbLength = 6; c.lun = 0;
        disk.handleCommand(c);                 // eats the UA CHECK
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
    h.ackUnitAttention();               // W-4: quiesce power-on UA
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
    h.ackUnitAttention();               // W-4: quiesce power-on UA
    uint8_t const cdb[6] = { 0x15, 0x10, 0, 0, 0x00, 0 };   // list length 0
    scsi::ScsiCommand const r = runCdb(h.disk, cdb, 6);
    CHECK(r.status == scsi::ScsiStatus::Good);
}

TEST_CASE("MODE SELECT(6) accepts a block descriptor that keeps the block size")
{
    Harness h;
    h.ackUnitAttention();               // W-4: quiesce power-on UA
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
    h.ackUnitAttention();               // W-4: quiesce power-on UA
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
    h.ackUnitAttention();               // W-4: quiesce power-on UA
    uint8_t parm[12] = {};
    uint8_t const cdb[6] = { 0x15, 0x10, 0, 0, 12, 0 };
    // Initiator promised 12 bytes (CDB), but only 2 are available.
    scsi::ScsiCommand const r = runCdb(h.disk, cdb, 6, parm, 2);
    CHECK(r.status == scsi::ScsiStatus::CheckCondition);
}

TEST_CASE("MODE SELECT(10) shares the contract with the 6-byte form")
{
    Harness h;
    h.ackUnitAttention();               // W-4: quiesce power-on UA
    uint8_t parm[16] = {};
    parm[7] = 8;                       // block descriptor length (10-byte hdr)
    uint8_t const cdb[10] = { 0x55, 0x10, 0, 0, 0, 0, 0, 0, 16, 0 };
    scsi::ScsiCommand const r =
        runCdb(h.disk, cdb, 10, parm, sizeof(parm));
    CHECK(r.status == scsi::ScsiStatus::Good);
}

// ============================================================================
// Batch H-2 (2026-08-03, JRN-SCSI-037): the four constructs the VMS
// PKEDRIVER init script uses, captured by TODO(N810-SCRIPTDUMP) at
// DSP 0xC00012D4.  Encodings per 53C895 DM Ch.6 + apisrm ref/n810_def.h.
// ============================================================================

TEST_CASE("H-2a: Memory Move copies RAM to RAM, clobbers DSPS/TEMP, zeroes DBC")
{
    Harness h;
    for (int i = 0; i < 8; ++i) h.ram[0x3000 + i] = uint8_t(0xA0 + i);
    h.put32(0x1000, 0xC0000008u);            // MM, count 8
    h.put32(0x1004, 0x3000u);                // source
    h.put32(0x1008, 0x3050u);                // destination (third dword)
    h.put32(0x100C, 0x98080000u);            // INT (unconditional)
    h.put32(0x1010, 0x2Au);                  //   vector 0x2A
    h.w32(0x10, 0x12345678u);                // DSA: must be PRESERVED
    h.w32(0x2C, 0x1000u);                    // DSP write -> run

    CHECK(h.r32(0x30) == 0x2Au);             // reached the INT after the MM
    bool copied = true;
    for (int i = 0; i < 8 && copied; ++i)
        copied = (h.ram[0x3050 + i] == uint8_t(0xA0 + i));
    CHECK(copied);
    CHECK(h.r32(0x1C) == 0x3058u);           // TEMP clobbered past dst end
    CHECK(h.r32(0x10) == 0x12345678u);       // DSA preserved (cited)
    (void) h.r8(0x0C);                       // drain SIR
}

TEST_CASE("H-2a: Memory Move reads the chip's OWN BAR window (DSA readback)")
{
    Harness h;
    h.hba.pciConfigWrite(0x14, 0x8000u, 4);  // MEM BAR at 0x8000
    h.w32(0x10, 0xAABBCCDDu);                // DSA value to read back
    h.put32(0x1000, 0xC0000004u);            // MM, count 4
    h.put32(0x1004, 0x8010u);                // src = own BAR + 0x10 = DSA
    h.put32(0x1008, 0x3080u);                // dst = RAM
    h.put32(0x100C, 0x98080000u);            // INT 0
    h.put32(0x1010, 0x0u);
    h.w32(0x2C, 0x1000u);

    uint32_t const got = uint32_t(h.ram[0x3080])
                       | (uint32_t(h.ram[0x3081]) << 8)
                       | (uint32_t(h.ram[0x3082]) << 16)
                       | (uint32_t(h.ram[0x3083]) << 24);
    CHECK(got == 0xAABBCCDDu);               // register file answered (cited)
    (void) h.r8(0x0C);

    // Reverse direction: RAM -> SCRATCHA through the BAR window.
    h.put32(0x3090, 0x11223344u);
    h.put32(0x1000, 0xC0000004u);
    h.put32(0x1004, 0x3090u);                // src RAM
    h.put32(0x1008, 0x8034u);                // dst = own BAR + 0x34 SCRATCHA
    h.w32(0x2C, 0x1000u);
    CHECK(h.r32(0x34) == 0x11223344u);
    (void) h.r8(0x0C);
}

TEST_CASE("H-2a: MM reserved bits 28:25 and A1:0 mismatch raise IID; bit 24 executes")
{
    Harness h;
    h.put32(0x1000, 0xC2000004u);            // bit 25 set: reserved (cited)
    h.put32(0x1004, 0x3000u);
    h.put32(0x1008, 0x3050u);
    h.w32(0x2C, 0x1000u);
    CHECK((h.r8(0x0C) & 0x01) != 0);         // DSTAT<IID>

    h.put32(0x1000, 0xC0000004u);            // alignment: src A1:0 != dst
    h.put32(0x1004, 0x3001u);
    h.put32(0x1008, 0x3050u);
    h.w32(0x2C, 0x1000u);
    CHECK((h.r8(0x0C) & 0x01) != 0);         // DSTAT<IID> (cited)

    h.ram[0x3000] = 0x5A;                    // bit 24 No Flush: EXECUTE
    h.put32(0x1000, 0xC1000004u);            // (_PROVISIONAL, loud row)
    h.put32(0x1004, 0x3000u);
    h.put32(0x1008, 0x3060u);
    h.put32(0x100C, 0x98080000u);
    h.put32(0x1010, 0x0u);
    h.w32(0x2C, 0x1000u);
    CHECK(h.ram[0x3060] == 0x5A);            // moved, not IID
    (void) h.r8(0x0C);
}

TEST_CASE("H-2b: relative transfer control, forward and negative offsets")
{
    Harness h;
    h.put32(0x1000, 0x80880000u);            // JUMP REL (w0<23>), uncond
    h.put32(0x1004, 0x10u);                  // dest = 0x1008 + 0x10 = 0x1018
    h.put32(0x1008, 0x98080000u);            // INT 999 -- must be skipped
    h.put32(0x100C, 999u);
    h.put32(0x1018, 0x98080000u);            // INT 0x42 -- the target
    h.put32(0x101C, 0x42u);
    h.w32(0x2C, 0x1000u);
    CHECK(h.r32(0x30) == 0x42u);
    (void) h.r8(0x0C);

    h.put32(0x1020, 0x80880000u);            // JUMP REL negative
    h.put32(0x1024, 0x00FFFFF0u);            // -0x10: 0x1028 - 0x10 = 0x1018
    h.w32(0x2C, 0x1020u);
    CHECK(h.r32(0x30) == 0x42u);             // landed on the same INT 0x42
    (void) h.r8(0x0C);
}

TEST_CASE("H-2c: RW opc 5 operates on SFBR, result to register")
{
    Harness h;
    h.hba.ioWrite(0x08, 0x3Cu, 1);           // SFBR = 0x3C
    h.hba.ioWrite(0x34, 0xFFu, 1);           // SCRATCHA0 = junk to overwrite
    h.put32(0x1000, 0x6C340F00u);            // SFBR AND 0x0F -> reg 0x34
    h.put32(0x1004, 0x0u);
    h.put32(0x1008, 0x98080000u);            // INT 0
    h.put32(0x100C, 0x0u);
    h.w32(0x2C, 0x1000u);
    CHECK(h.r8(0x34) == 0x0Cu);              // 0x3C & 0x0F, from SFBR
    CHECK(h.r8(0x08) == 0x3Cu);              // SFBR itself untouched
    (void) h.r8(0x0C);
}

TEST_CASE("H-2a+b interaction: CALL, Memory Move, RETURN returns to CLOBBERED TEMP")
{
    // 53C895 DM: TEMP is a Memory Move holding register.  On real silicon a
    // CALL -> MM -> RETURN sequence returns to garbage (dst end), NOT to the
    // call site.  The D-ORACLE inversion to guard against is our model
    // quietly preserving TEMP and making a script work that silicon breaks.
    Harness h;
    h.put32(0x1000, 0x88880000u);            // CALL REL
    h.put32(0x1004, 0x18u);                  // sub = 0x1008 + 0x18 = 0x1020
    h.put32(0x1008, 0x98080000u);            // INT 0x33: the CALL-site return
    h.put32(0x100C, 0x33u);                  //   -- must NOT be reached
    h.put32(0x1020, 0xC0000004u);            // sub: MM 4 bytes
    h.put32(0x1024, 0x3000u);                //   src
    h.put32(0x1028, 0x3050u);                //   dst  -> TEMP ends at 0x3054
    h.put32(0x102C, 0x90080000u);            // RETURN (to TEMP)
    h.put32(0x1030, 0x0u);
    h.put32(0x3054, 0x98080000u);            // the "garbage" landing: INT 0x77
    h.put32(0x3058, 0x77u);
    h.w32(0x2C, 0x1000u);
    CHECK(h.r32(0x30) == 0x77u);             // returned into dst+4, as silicon
    (void) h.r8(0x0C);
}

TEST_CASE("H-2d: WAIT RESELECT parks (no STO); ISTAT<SIGP> resumes; CTEST2 read-clears")
{
    Harness h;
    h.put32(0x1000, 0x50000000u);            // WAIT RESELECT, alt (absolute)
    h.put32(0x1004, 0x1010u);
    h.put32(0x1010, 0x98080000u);            // alternate path: INT 0x66
    h.put32(0x1014, 0x66u);
    h.w32(0x2C, 0x1000u);                    // run -> parks

    CHECK((h.r8(0x43) & 0x04) == 0);         // NO STO (old D2 shim gone)
    CHECK((h.r8(0x14) & 0x03) == 0);         // no SIP/DIP: parked, not ended
    CHECK((h.r8(0x14) & 0x20) == 0);         // SIGP not set yet

    h.hba.ioWrite(0x14, 0x20u, 1);           // ISTAT<SIGP>: break the wait
    CHECK(h.r32(0x30) == 0x66u);             // resumed at the alternate
    CHECK((h.r8(0x14) & 0x20) != 0);         // SIGP STILL set (not cleared
                                             // on resume -- pollers see it)
    CHECK((h.r8(0x1A) & 0x40) != 0);         // CTEST2<SIGP> reads set...
    CHECK((h.r8(0x1A) & 0x40) == 0);         // ...and read CLEARED it
    CHECK((h.r8(0x14) & 0x20) == 0);         // ISTAT agrees
    (void) h.r8(0x0C);
}

TEST_CASE("H-2d: SIGP already set at WAIT RESELECT jumps immediately, no park")
{
    Harness h;
    h.hba.ioWrite(0x14, 0x20u, 1);           // SIGP first (nothing parked)
    h.put32(0x1000, 0x50000000u);
    h.put32(0x1004, 0x1010u);
    h.put32(0x1010, 0x98080000u);            // INT 0x66 at the alternate
    h.put32(0x1014, 0x66u);
    h.w32(0x2C, 0x1000u);                    // executes: jumps at once
    CHECK(h.r32(0x30) == 0x66u);
    CHECK((h.r8(0x14) & 0x20) != 0);         // SIGP survives the jump
    (void) h.r8(0x1A);                       // CTEST2 read clears it
    CHECK((h.r8(0x14) & 0x20) == 0);
    (void) h.r8(0x0C);
}

TEST_CASE("H-3 shim: budget exhaustion poll-parks; host write wakes; loop converges")
{
    // The VMS pke idle idiom (JRN-SCSI-038): a SCRIPTS busy-poll on a
    // register the HOST will write.  Old behavior: 100k guard killed the
    // script silently.  Shim: park, wake on completed MMIO write.
    Harness h;
    h.hba.ioWrite(0x34, 0x00u, 1);           // SCRATCHA0 = 0: loop condition
    h.put32(0x1000, 0x76340000u);            // MOVE SCRATCHA0 to SFBR
    h.put32(0x1004, 0x0u);
    h.put32(0x1008, 0x808C0000u);            // JUMP REL IF SFBR==0x00...
    h.put32(0x100C, 0x00FFFFF0u);            //   -0x10 -> back to 0x1000
    h.put32(0x1010, 0x98080000u);            // INT 0x99 when poll satisfied
    h.put32(0x1014, 0x99u);
    h.w32(0x2C, 0x1000u);                    // run: spins 100k instrs, parks

    CHECK((h.r8(0x14) & 0x03) == 0);         // parked: no DIP/SIP, no kill
    CHECK((h.r8(0x0C) & 0x01) == 0);         // and NOT an IID

    h.hba.ioWrite(0x08, 0x00u, 1);           // unrelated write: wakes, polls
    CHECK((h.r8(0x14) & 0x03) == 0);         //   again, re-parks (still 0)

    h.hba.ioWrite(0x34, 0x5Au, 1);           // the host posts: condition set
    CHECK(h.r32(0x30) == 0x99u);             // woke, observed 0x5A, INT
    CHECK((h.r8(0x0C) & 0x04) != 0);         // SIR delivered
}

// ===========================================================================
// SPEC-DISK-001 drive profiles (2026-08-04): table sanity, INQUIRY identity,
// READ CAPACITY, MODE SENSE pages 03h/04h/3Fh + two-step + DBD + unsupported.
// ===========================================================================

TEST_CASE("SPEC-DISK-001 T-1..T-3/T-6-width: table sanity band + DEVDEPEND")
{
    for (auto const& p : scsi::kDriveProfiles) {
        CHECK(p.geometrySane());                       // T-1: +/-2% band
        CHECK(std::strlen(p.inquiryVendor)   == 8);    // exact widths
        CHECK(std::strlen(p.inquiryProduct)  == 16);
        CHECK(std::strlen(p.inquiryRevision) == 4);
    }
    CHECK(scsi::findDriveProfile("RZ29L")->devdepend() == 0x0E7C1471u); // T-2
    CHECK(scsi::findDriveProfile("RZ28L")->devdepend() == 0x0BE51056u); // T-3
    CHECK(scsi::findDriveProfile("RZ40")->devdepend()  == 0x149914A8u); // T-3
}

TEST_CASE("SPEC-DISK-001 T-4/T-5: image-size check + unknown mnemonic")
{
    CHECK(scsi::findDriveProfile("RZ99")  == nullptr);           // T-5
    CHECK(scsi::findDriveProfile("")      == nullptr);
    CHECK(scsi::findDriveProfile(nullptr) == nullptr);
    auto const* p = scsi::findDriveProfile("RZ29L");
    CHECK(scsi::checkProfileMedia(*p, p->blocks)
          == scsi::ProfileMediaCheck::Ok);
    CHECK(scsi::checkProfileMedia(*p, p->blocks - 1)             // T-4
          == scsi::ProfileMediaCheck::MediaSmaller);
    CHECK(scsi::checkProfileMedia(*p, p->blocks + 1)
          == scsi::ProfileMediaCheck::MediaLarger);
}

namespace {
// Direct device-level command helper (bypasses the SCRIPTS path -- the
// profile consumers are pure target behavior).
uint32_t runCmd(scsi::VirtualDiskDevice& d, uint8_t const* cdb, uint8_t len,
                uint8_t* buf, uint32_t bufLen, scsi::ScsiCommand& out)
{
    out = scsi::ScsiCommand{};
    out.cdb = cdb; out.cdbLength = len; out.lun = 0;
    out.dataDirection = scsi::ScsiDataDirection::DeviceToHost;
    out.dataBuffer = buf; out.dataBufferLength = bufLen;
    d.handleCommand(out);
    return out.dataTransferred;
}
} // namespace

TEST_CASE("SPEC-DISK-001 T-6..T-10: INQUIRY identity + READ CAPACITY from "
          "the profile")
{
    Harness h;
    h.ackUnitAttention();               // W-4: quiesce power-on UA
    h.disk.setProfile(scsi::findDriveProfile("RZ29L"));
    uint8_t buf[256]; scsi::ScsiCommand c;

    uint8_t const inq[6] = { 0x12, 0, 0, 0, 255, 0 };
    CHECK(runCmd(h.disk, inq, 6, buf, sizeof buf, c) == 36);     // T-6
    CHECK(std::memcmp(&buf[8],  "DEC     ",         8) == 0);
    CHECK(std::memcmp(&buf[16], "RZ29L-AA (C)DEC ", 16) == 0);
    CHECK(std::memcmp(&buf[32], "LYJ0",             4) == 0);
    // T-7 (REVISED 2026-08-04, JRN-AUD-004 DEC-1): byte 7 is 0x00, NOT 0x12.
    // This CHECK previously pinned 0x12 and was one of the three self-
    // referential premises named in JRN-AUD-004 PR-7 -- it defended a value
    // that had no byte-level witness (only the sm2drpnb FEATURE list) and
    // that measurably wedged the OpenVMS mount path: asserting CmdQue made
    // VMS enable tagged queuing (DK_FLAGS cmdq set) against an HBA that
    // silently swallows tag messages (N-7), giving 61,748 unexplained
    // operations on DKA0.  A non-queueing target must not advertise CmdQue.
    // Re-assert only with Phase C-3 + C-2 landed (DriveProfile.h header).
    CHECK(buf[7] == 0x00);                                       // T-7 no CmdQue/Sync
    CHECK(buf[2] == 0x02);                                       // SCSI-2

    uint8_t const inqShort[6] = { 0x12, 0, 0, 0, 8, 0 };
    CHECK(runCmd(h.disk, inqShort, 6, buf, sizeof buf, c) == 8); // T-8 clamp

    uint8_t const rc[10] = { 0x25, 0,0,0,0,0,0,0,0,0 };
    CHECK(runCmd(h.disk, rc, 10, buf, sizeof buf, c) == 8);
    uint32_t const last = (uint32_t(buf[0]) << 24) | (uint32_t(buf[1]) << 16)
                        | (uint32_t(buf[2]) << 8)  |  uint32_t(buf[3]);
    CHECK(last == 8380079u);                                     // T-9 blocks-1
    uint32_t const bs = (uint32_t(buf[4]) << 24) | (uint32_t(buf[5]) << 16)
                      | (uint32_t(buf[6]) << 8)  |  uint32_t(buf[7]);
    CHECK(bs == 512u);                                           // T-10
}

TEST_CASE("SPEC-DISK-001 T-11..T-16: MODE SENSE pages, two-step, DBD, "
          "unsupported page")
{
    Harness h;
    h.ackUnitAttention();               // W-4: quiesce power-on UA
    h.disk.setProfile(scsi::findDriveProfile("RZ29L"));
    uint8_t buf[256]; scsi::ScsiCommand c;

    uint8_t const msAll[6] = { 0x1A, 0, 0x3F, 0, 255, 0 };       // T-16 all pages
    CHECK(runCmd(h.disk, msAll, 6, buf, sizeof buf, c) == 60);   // 4+8+24+24
    CHECK(buf[0] == 59);              // mode data length (n-1)
    CHECK(buf[3] == 8);               // block descriptor present
    CHECK(buf[12] == 0x03);           // page 3 first (ascending)
    CHECK(buf[12 + 10] == 0x00);      // T-12 sectors/track BE hi
    CHECK(buf[12 + 11] == 113);       //   = 0x71
    CHECK(buf[12 + 12] == 0x02);      //   block bytes 512 BE
    CHECK(buf[12 + 13] == 0x00);
    CHECK(buf[36] == 0x04);           // page 4 second
    CHECK(buf[36 + 2] == 0x00);       // T-11 cylinders 3708 (3 bytes BE)
    CHECK(buf[36 + 3] == 0x0E);
    CHECK(buf[36 + 4] == 0x7C);
    CHECK(buf[36 + 5] == 20);         //   heads
    CHECK(buf[36 + 20] == 0x1C);      //   7200 RPM BE
    CHECK(buf[36 + 21] == 0x20);

    uint8_t const msShort[6] = { 0x1A, 0, 0x3F, 0, 4, 0 };       // T-13 two-step
    CHECK(runCmd(h.disk, msShort, 6, buf, sizeof buf, c) == 4);
    CHECK(buf[0] == 59);              // header still sizes the FULL response

    uint8_t const msDbd[6] = { 0x1A, 0x08, 0x04, 0, 255, 0 };    // T-14 DBD
    CHECK(runCmd(h.disk, msDbd, 6, buf, sizeof buf, c) == 28);   // 4+24, no bd
    CHECK(buf[3] == 0);               // bd length reads 00
    CHECK(buf[4] == 0x04);            // page 4 directly after header

    uint8_t const msBad[6] = { 0x1A, 0, 0x15, 0, 255, 0 };       // T-15
    runCmd(h.disk, msBad, 6, buf, sizeof buf, c);
    CHECK(c.status == scsi::ScsiStatus::CheckCondition);         // not GOOD
    uint8_t const rs[6] = { 0x03, 0, 0, 0, 18, 0 };
    CHECK(runCmd(h.disk, rs, 6, buf, sizeof buf, c) == 18);
    CHECK((buf[2] & 0x0F) == 0x05);   // ILLEGAL REQUEST
    CHECK(buf[12] == 0x24);           // invalid field in CDB
}

TEST_CASE("W-4 T-7/T-8: power-on UNIT ATTENTION delivered once (06/29/00), "
          "sense retrievable by the FOLLOWING REQUEST SENSE, then clear")
{
    Harness h;                                    // fresh: UA latched
    uint8_t buf[64]; scsi::ScsiCommand c;
    uint8_t const tur[6] = {};
    runCmd(h.disk, tur, 6, buf, sizeof buf, c);
    CHECK(c.status == scsi::ScsiStatus::CheckCondition);  // T-7: first cmd
    // Two-halves rule (architect review): CHECK cleared the PENDING flag
    // and STAGED the sense; REQUEST SENSE now delivers it.
    uint8_t const rs[6] = { 0x03, 0, 0, 0, 18, 0 };
    CHECK(runCmd(h.disk, rs, 6, buf, sizeof buf, c) == 18);
    CHECK(buf[0] == 0x70);                        // current, fixed format
    CHECK((buf[2] & 0x0F) == 0x06);               // UNIT ATTENTION
    CHECK(buf[7] == 0x0A);                        // additional length 10
    CHECK(buf[12] == 0x29);                       // ASC power-on/reset
    CHECK(buf[13] == 0x00);                       // ASCQ
    runCmd(h.disk, tur, 6, buf, sizeof buf, c);
    CHECK(c.status == scsi::ScsiStatus::Good);    // T-8: reported ONCE
    // Bus reset re-latches through the HOST seam (SCNTL1<RST> MMIO write).
    h.hba.ioWrite(0x01, 0x08u, 1);
    (void) h.r8(0x42);                            // clear SIST0<RST>
    runCmd(h.disk, tur, 6, buf, sizeof buf, c);
    CHECK(c.status == scsi::ScsiStatus::CheckCondition);  // re-latched
    runCmd(h.disk, rs, 6, buf, sizeof buf, c);
    CHECK(buf[12] == 0x29);
}

TEST_CASE("W-4 T-9/T-10: short-alloc REQUEST SENSE clamps; INQUIRY with UA "
          "pending is GOOD and does NOT consume the attention")
{
    Harness h;                                    // fresh: UA latched
    uint8_t buf[64]; scsi::ScsiCommand c;
    // T-10 (architect addition): INQUIRY must complete normally with UA
    // pending -- it is how an initiator identifies a device with a
    // contingency outstanding -- and must NOT eat the attention.  The
    // failure mode of "any command clears it" is another silent converged
    // loop, not a loud error.
    uint8_t const inq[6] = { 0x12, 0, 0, 0, 36, 0 };
    CHECK(runCmd(h.disk, inq, 6, buf, sizeof buf, c) == 36);
    CHECK(c.status == scsi::ScsiStatus::Good);
    uint8_t const tur[6] = {};
    runCmd(h.disk, tur, 6, buf, sizeof buf, c);
    CHECK(c.status == scsi::ScsiStatus::CheckCondition);  // UA survived INQ
    // T-9: the staged UA sense clamps through the standard path.
    uint8_t const rsShort[6] = { 0x03, 0, 0, 0, 8, 0 };
    CHECK(runCmd(h.disk, rsShort, 6, buf, sizeof buf, c) == 8);
    CHECK(buf[0] == 0x70);
    CHECK((buf[2] & 0x0F) == 0x06);
}

TEST_CASE("W-3: short DATA IN halts the SCRIPTS engine at the mismatch; "
          "DBC holds the residual; DCNTL<STD> resumes to completion")
{
    // BRIEF-SCSI-040 revised W-3 (JRN-SCSI-041): the 45,340-failure
    // TOOMUCHDATA storm was the script running THROUGH the MA -- moving
    // STATUS/MSG IN, zeroing DBC via MMs, merging completion with the
    // mismatch.  Silicon halts; the ISR reads DBC's residual intact.
    Harness h;
    h.buildScript(/*targetId*/ 0);
    h.buildDsa(/*cdbLen*/ 6, /*datInLen*/ 255);   // driver asks 255...
    uint8_t const cdb[6] = { 0x12, 0, 0, 0, 255, 0 };  // INQUIRY alloc 255
    std::memcpy(h.ram.data() + Harness::kCdb, cdb, 6);
    h.ram[Harness::kSts] = 0xEE;                  // sentinel: not yet moved

    h.kick();                                     // ...target has 36

    CHECK((h.r8(0x14) & 0x02) != 0);              // SIP (MA pending)
    CHECK(h.ram[Harness::kSts] == 0xEE);          // HALTED: no STATUS moved
    CHECK((h.r8(0x0C) & 0x04) == 0);              // no SIR: no completion INT
    uint32_t const dbc = h.r32(0x24) & 0x00FFFFFFu;
    CHECK(dbc == 255u - 36u);                     // residual INTACT at halt
    uint8_t const sist0 = h.r8(0x42);             // read-clears
    CHECK((sist0 & 0x80) != 0);                   // SIST0<MA>

    h.hba.ioWrite(0x3B, 0x04u, 1);                // DCNTL<STD>: resume
    CHECK(h.ram[Harness::kSts] == 0x00);          // STATUS moved: GOOD
    CHECK(h.ram[Harness::kMsgIn] == 0x00);        // COMMAND COMPLETE
    CHECK((h.r8(0x0C) & 0x04) != 0);              // SIR: completion INT now
    CHECK(h.r32(0x30) == 0u);                     // DSPS = ok vector
}

TEST_CASE("C-4: DSTAT<ABRT> with DIEN<ABRT> masked does not assert the "
          "interrupt line")
{
    // BRIEF-SCSI-040 C-4: the console-era DIEN in force is 0x65 -- bit 4
    // (ABRT) CLEAR.  Setting DSTAT<ABRT> unconditionally is correct;
    // asserting INTA for a masked source would manufacture an interrupt
    // silicon would not deliver.  updateIrq gates the line by DIEN.
    // [CONFIRM] ISTAT<DIP> composition for masked sources vs the 895 DM
    // DIEN/DSTAT tables -- this case pins the LINE only.
    Harness h;
    bool line = false;
    h.hba.setIntrCallback([&line](bool level) { line = level; });
    h.hba.ioWrite(0x39, 0x65u, 1);                // DIEN = 0x65 (ABRT masked)
    h.hba.ioWrite(0x14, 0x80u, 1);                // ISTAT<ABRT>
    CHECK(line == false);                         // masked: line stays low
    uint8_t const dstat = h.r8(0x0C);
    CHECK((dstat & 0x10) != 0);                   // DSTAT<ABRT> still set
    h.hba.ioWrite(0x14, 0x80u, 1);                // again, DIEN<ABRT> now set
    h.hba.ioWrite(0x39, 0x75u, 1);                //   (enable after pend:
    CHECK(line == true);                          //    line follows mask)
    (void) h.r8(0x0C);
    CHECK(line == false);                         // read-clear drops it
}

TEST_CASE("H-3.3b rw-op source reads use regRead8: the SCRIPT's CTEST2 "
          "read sees AND clears SIGP")
{
    // JRN-SCSI-039 root-cause candidate: MOVE CTEST2 to SFBR (the pke
    // +0x2A4 idiom) read m_reg[] raw, where SIGP is never composed --
    // the script always sampled bit6=0 and treated every SIGP wake as
    // spurious.  H-3.3b routes rw-op source reads through regRead8.
    Harness h;
    h.put32(0x1000, 0x761A0000u);            // MOVE CTEST2 to SFBR
    h.put32(0x1004, 0x0u);
    h.put32(0x1008, 0x98080000u);            // INT 0x77
    h.put32(0x100C, 0x77u);
    h.hba.ioWrite(0x14, 0x20u, 1);           // SIGP (nothing parked: latches)
    CHECK((h.r8(0x14) & 0x20) != 0);         // latched, host-visible
    h.w32(0x2C, 0x1000u);                    // run the two instructions
    CHECK(h.r32(0x30) == 0x77u);             // script completed
    CHECK((h.r8(0x08) & 0x40) != 0);         // SFBR captured SIGP (bit 6)
    CHECK((h.r8(0x14) & 0x20) == 0);         // and the SCRIPT's read CLEARED
                                             // it -- the H-2d contract on
                                             // the path the guest uses
    (void) h.r8(0x0C);
}

TEST_CASE("H-3.3a ISTAT<ABRT>: aborts a poll-parked script, raises "
          "DSTAT<ABRT> -> DIP, and the script does NOT resume")
{
    // JRN-SCSI-039 Sec 4: the pke TOUTROUT t=7 recovery writes
    // ISTAT<ABRT> and re-arms its timeout awaiting the abort-completion
    // interrupt.  On silicon ABRT stops the SCRIPTS processor and sets
    // DSTAT<ABRT> (DIP).  Before H-3.3a we stored the bit and stayed
    // parked; the driver's abort wait died on the silence.
    Harness h;
    h.hba.ioWrite(0x34, 0x00u, 1);           // SCRATCHA0 = 0: loop condition
    h.put32(0x1000, 0x76340000u);            // MOVE SCRATCHA0 to SFBR
    h.put32(0x1004, 0x0u);
    h.put32(0x1008, 0x808C0000u);            // JUMP REL IF SFBR==0x00...
    h.put32(0x100C, 0x00FFFFF0u);            //   -0x10 -> back to 0x1000
    h.put32(0x1010, 0x98080000u);            // INT 0x99 when poll satisfied
    h.put32(0x1014, 0x99u);
    h.w32(0x2C, 0x1000u);                    // run: spins 100k instrs, parks

    h.hba.ioWrite(0x14, 0x80u, 1);           // ISTAT<ABRT>: abort the script
    CHECK((h.r8(0x14) & 0x01) != 0);         // DIP pending
    uint8_t const dstat = h.r8(0x0C);        // read (clears)
    CHECK((dstat & 0x10) != 0);              // DSTAT<ABRT> set
    CHECK((h.r8(0x14) & 0x01) == 0);         // DIP cleared by the read

    h.hba.ioWrite(0x34, 0x5Au, 1);           // condition write AFTER abort:
    CHECK(h.r32(0x30) != 0x99u);             //   no resume, no INT -- the
    CHECK((h.r8(0x0C) & 0x04) == 0);         //   script is dead until a new
                                             //   DSP write starts a session
}
