// ============================================================================
// deviceLib/scsi/VirtualDiskDevice.h -- SCSI direct-access (disk) LUN target
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
// JRN-SCSI-001 P2: the direct-access (SBC) sibling of VirtualIsoDevice,
// backed by IBlockMedia (512-byte logical blocks).  Serves the NCR 53C810
// HBA's SCSI bus; command surface is the SRM pk class driver's boot set
// (apisrm scsi.c / dk driver): TEST UNIT READY, REQUEST SENSE, INQUIRY,
// MODE SENSE(6/10), READ CAPACITY(10), READ(6)/(10), WRITE(6)/(10),
// START STOP / PREVENT ALLOW (no-op good).  Unknown opcodes return
// CHECK CONDITION / ILLEGAL REQUEST with sense retained for REQUEST SENSE
// -- never silent, per the no-silent-absorbers house rule.
// ============================================================================
// TODO TABLE
// ============================================================================
//   TODO(DISK-MODEPAGES)  MODE SENSE page coverage is the MEASURED MINIMUM,
//     not the guest-demanded maximum (architect-directed 2026-08-06,
//     JRN-SCSI-042 Sec 14 rider).  Coverage vs the AXPBox reference (which
//     boots this disk) and known consumers:
//       page  AXPBox  EmulatR  consumer
//       00h   yes     yes      --
//       01h   yes     yes      DKDRIVER unit-init (MEASURED, the H-7 root
//                              cause)
//       02h   no      no       none observed (disconnect-reconnect)
//       03h   yes     yes      SRM + DKDRIVER
//       04h   yes     yes      SRM + DKDRIVER
//       05h   yes     no       none observed (flexible disk)
//       08h   yes     no       PLAUSIBLE NEXT ASK (caching; DKDRIVER-era
//                              drivers read and MODE SELECT it)
//       3Fh   yes     yes      DKDRIVER template 1A 00 3F 00 FF
//     PLUS: the PC field (cdb[2]<7:6> current/changeable/default/saved)
//     returns CURRENT values for all four codes, _PROVISIONAL -- a driver
//     requesting the CHANGEABLE mask before MODE SELECT gets current
//     values instead of a bitmask.
//     EVIDENCE GATE: N810-CHKCOND (unthrottled, H-7 rider) names any
//     rejected page in one log line -- extend coverage on sighting, or
//     close wholesale against the 53C895-era RZ family DM.
//     REMOVAL TRIGGER: a full boot with zero MODE SENSE N810-CHKCOND rows
//     and the PC field resolved against SCSI-2 8.3.3.
// ============================================================================
// CHANGE HISTORY
// ============================================================================
//   2026-08-06  Batch H-7 (architect-approved "go"): MODE SENSE page 01h.
//               ROOT CAUSE, MEASURED END TO END (JRN-SCSI-042 Sec 14): the
//               2026-08-05 crash dump's failed command is CDB
//               1A 00 01 00 FF 00 -- MODE SENSE(6), page 01h Read-Write
//               Error Recovery -- still latched at RAD+25C with status
//               longword 02 at RAD+258 and SCDRP$B_SENSE_KEY=05.  This
//               function's page gate (03h/04h/3Fh/00h only) answered
//               CHECK CONDITION 05/24/00; SYS$DKDRIVER latched ILLEGAL
//               REQUEST, abandoned device characterization (UCB DEVDEPEND
//               stuck at 604, never the profile geometry), and dropped
//               into an EXE$KP_TQE_WAIT timed readiness retry -- the
//               RC/TUR/RS poll measured at 46,519 operations, which held
//               the system-disk mount and parked every boot at the
//               OpenVMS banner.
//               FUNCTION: cmdModeSense.
//               CHANGE:  page 01h served: 12 bytes, code 0x01, length
//                        0x0A, ALL POLICY FIELDS ZERO (no AWRE/ARRE, zero
//                        retry counts).  The zero layout is DELIBERATE and
//                        reference-matched: AXPBox Disk.cpp
//                        SCSIMP_READ_WRITE_ERRREC serves exactly this
//                        (code+length, rest zero) and boots this same
//                        dka0.vdisk to V8.3 -- DKDRIVER requires the
//                        page's EXISTENCE, not its policy.  Page 01h also
//                        joins the 3Fh composite, ascending page order
//                        (01, 03, 04) per SCSI-2.  The gate still rejects
//                        genuinely unknown pages 05/24/00 -- S-12's
//                        never-truncated-under-GOOD rule stands.
//               PREDICTION (falsifiable, stated before the run): MODE
//                        SENSE page 01h returns GOOD, DEVDEPEND
//                        populates, the RC/TUR/RS poll TERMINATES, the
//                        boot advances past the banner.  If DKDRIVER then
//                        wants another page we lack (05h/08h are the
//                        AXPBox-coverage candidates), the H-7 companion
//                        row in Ncr53C810.h ledgerCmd names it in one
//                        line.
//   2026-08-04  SPEC-DISK-001 (architect-approved): atomic drive profiles.
//               FUNCTION: setProfile (new) / cmdInquiry / cmdModeSense.
//               CHANGE:  With a DriveProfile set (manifest "model" ->
//                        DriveProfile.h table), INQUIRY identity strings,
//                        byte 7 flags, RMB, and ANSI version come from the
//                        profile (closes JRN-AUD-003 S-4: byte7 was 0,
//                        suppressing CmdQue/Sync negotiation), and MODE
//                        SENSE serves pages 03h/04h/3Fh with profile
//                        geometry (closes S-12: pages were zero under
//                        GOOD).  Two-step probe honored: the header's
//                        mode-data-length always describes the FULL
//                        response so a short probe sizes its re-issue.
//                        DBD honored.  Unsupported page code -> CHECK
//                        CONDITION ILLEGAL REQUEST invalid field in CDB
//                        (05/24/00), never a truncated header under GOOD.
//                        PC field: current values for all PC codes,
//                        _PROVISIONAL.  Page layouts [CONFIRM] vs SCSI-2
//                        X3.131 mode page 3/4 definitions.
//                        WITHOUT a profile (default), behavior is
//                        byte-identical to the previous revision -- the
//                        legacy EMULATR identity and header+bd MODE SENSE
//                        -- so direct-constructed devices (tests, any
//                        non-manifest path) are unchanged.
//
//   2026-08-02  JRN-AUD-003 Batch G (architect-approved): S-3, allocation
//               length honored.
//               FUNCTION: cmdInquiry / cmdRequestSense / cmdModeSense (and
//               the LUN!=0 INQUIRY/REQUEST SENSE arms).
//               CHANGE:  Parameter-data commands now transfer
//                        min(allocation length, available) per SCSI-2
//                        8.2.5/8.2.10/8.2.14 (CDB byte 4; MODE SENSE(10)
//                        bytes 7-8).  Previously the full structure was
//                        returned regardless, so an initiator issuing the
//                        classic short probe (4-byte MODE SENSE header
//                        peek, small INQUIRY peek) was left with the nexus
//                        stuck in DATA IN and the next STATUS-phase move
//                        raised a phase mismatch the pke driver answers
//                        with a full port re-init (JRN-AUD-003 S-3).
// ============================================================================

#ifndef DEVICELIB_SCSI_VIRTUALDISKDEVICE_H
#define DEVICELIB_SCSI_VIRTUALDISKDEVICE_H

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>

#include "deviceLib/scsi/DriveProfile.h"
#include "deviceLib/scsi/IBlockMedia.h"
#include "deviceLib/scsi/ScsiCommand.h"
#include "deviceLib/scsi/ScsiSenseData.h"
#include "deviceLib/scsi/ScsiTypes.h"
#include "deviceLib/scsi/VirtualScsiDevice.h"

namespace scsi {

class VirtualDiskDevice final : public VirtualScsiDevice {
public:
    explicit VirtualDiskDevice(std::unique_ptr<IBlockMedia> media = nullptr) noexcept
    {
        setMedia(std::move(media));
    }

    void setMedia(std::unique_ptr<IBlockMedia> media) noexcept
    {
        m_media = std::move(media);
        if (m_media && !m_media->isOpen())
            (void) m_media->open();
    }

    bool hasMedia() const noexcept
    {
        return m_media && m_media->isOpen() && m_media->isPresent();
    }

    // SPEC-DISK-001: bind the atomic drive profile.  nullptr (default)
    // keeps legacy behavior exactly.  The caller (Machine storage attach)
    // validates media size against the profile BEFORE binding.
    void setProfile(DriveProfile const* p) noexcept { m_profile = p; }
    DriveProfile const* profile() const noexcept    { return m_profile; }

    // W-4: SCSI bus reset re-latches UNIT ATTENTION (power-on latches it
    // at construction).
    void busReset() noexcept override { m_unitAttention = true; }

    ScsiPeripheralDeviceType deviceType() const noexcept override
    {
        return ScsiPeripheralDeviceType::DirectAccessBlockDevice;
    }

    void handleCommand(ScsiCommand& cmd) noexcept override
    {
        cmd.dataTransferred = 0;
        // LUN discipline (SPC/SCSI-2; 2026-07-25 fix for the dka0-dka7
        // multi-enumeration): this target serves LUN 0 ONLY.  The SRM class
        // driver probes every LUN of a responding id -- an unsupported LUN
        // must answer INQUIRY with peripheral qualifier 011b / type 1Fh
        // ("no device at this LUN") and CHECK CONDITION (ILLEGAL REQUEST,
        // LOGICAL UNIT NOT SUPPORTED 05/25/00) for everything else, or the
        // console mints a phantom dk unit per LUN.
        if (cmd.lun != 0) {
            if (cmd.opcode() == ScsiOp::INQUIRY) {
                uint8_t buf[36] = {};
                buf[0] = 0x7F;                 // qualifier 011b | type 1Fh
                buf[2] = 0x02;
                buf[3] = 0x02;
                buf[4] = 31;
                // Batch G S-3: clamp to CDB allocation length (SCSI-2 8.2.5).
                cmd.dataTransferred = copyOut(cmd, buf,
                    clampAlloc(sizeof(buf), cmd.cdb[4]));
                cmd.setGood();
                return;
            }
            if (cmd.opcode() == ScsiOp::REQUEST_SENSE) {
                ScsiFixedSenseData const s = ScsiFixedSenseData::make(
                    ScsiSenseKey::IllegalRequest, 0x25, 0x00);
                // Batch G S-3: clamp to CDB allocation length (SCSI-2 8.2.14).
                cmd.dataTransferred = copyOut(cmd, s.bytes(),
                    clampAlloc(static_cast<uint32_t>(s.size()), cmd.cdb[4]));
                cmd.setGood();
                return;
            }
            check(cmd, ScsiSenseKey::IllegalRequest, 0x25, 0x00);
            return;
        }
        // W-4 (BRIEF-SCSI-040 Sec 4, architect-approved 2026-08-04): UNIT
        // ATTENTION.  Latched at power-on (construction) and on SCSI bus
        // reset (busReset()); delivered as CHECK CONDITION 06/29/00
        // (power on, reset, or bus device reset occurred) on the first
        // command OTHER than INQUIRY or REQUEST SENSE (SCSI-2 7.9: INQUIRY
        // succeeds without clearing UA; REQUEST SENSE reports without
        // clearing), and CLEARED once reported.  MEASURED consumer: run
        // 20260803_185257 -- with the RZ29L profile, DKDRIVER entered its
        // readiness/attention poll (RC/TUR/RS repeating, all GOOD, 131k
        // sessions) awaiting exactly this attention; instant readiness with
        // no UA is a state no real drive exhibits (SPEC-DISK-001 D-2
        // interaction).  Also lights DK Flags first_attn_seen VMS-side.
        if (m_unitAttention) {
            uint8_t const op = cmd.opcode();
            if (op != 0x12 /*INQUIRY*/ && op != 0x03 /*REQUEST SENSE*/) {
                m_unitAttention = false;           // reported ONCE
                check(cmd, ScsiSenseKey::UnitAttention, 0x29, 0x00);
                return;
            }
        }
        switch (cmd.opcode()) {
        case ScsiOp::TEST_UNIT_READY: cmdTestUnitReady(cmd); return;
        case ScsiOp::REQUEST_SENSE:   cmdRequestSense(cmd);  return;
        case ScsiOp::INQUIRY:         cmdInquiry(cmd);       return;
        case 0x1A: /* MODE SENSE(6) */  cmdModeSense(cmd, false); return;
        case ScsiOp::MODE_SENSE10:      cmdModeSense(cmd, true);  return;
        case 0x15: /* MODE SELECT(6) */ cmdModeSelect(cmd, false); return;
        case 0x55: /* MODE SELECT(10) */cmdModeSelect(cmd, true);  return;
        case ScsiOp::READ_CAPACITY10: cmdReadCapacity(cmd);  return;
        case ScsiOp::READ6:           cmdReadWrite(cmd, false, false); return;
        case ScsiOp::READ10:          cmdReadWrite(cmd, true,  false); return;
        case 0x0A: /* WRITE(6) */     cmdReadWrite(cmd, false, true);  return;
        case 0x2A: /* WRITE(10) */    cmdReadWrite(cmd, true,  true);  return;
        case 0x2F: /* VERIFY(10) */   good(cmd);             return;
        case ScsiOp::START_STOP_UNIT: good(cmd);             return;
        case ScsiOp::PREVENT_ALLOW:   good(cmd);             return;
        default:
            std::fprintf(stderr,
                "VirtualDiskDevice: UNSUPPORTED opcode 0x%02X (len %u) -> "
                "ILLEGAL REQUEST\n", cmd.opcode(), cmd.cdbLength);
            check(cmd, ScsiSenseKey::IllegalRequest, 0x20, 0x00); // invalid opcode
            return;
        }
    }

private:
    // ---- status helpers ---------------------------------------------------
    void good(ScsiCommand& cmd) noexcept
    {
        m_lastSense = ScsiFixedSenseData{};
        cmd.setGood();
    }
    void check(ScsiCommand& cmd, ScsiSenseKey key, uint8_t asc, uint8_t ascq) noexcept
    {
        ScsiFixedSenseData const s = ScsiFixedSenseData::make(key, asc, ascq);
        m_lastSense = s;
        cmd.setCheckCondition(s);
    }

    // ---- commands ---------------------------------------------------------
    void cmdTestUnitReady(ScsiCommand& cmd) noexcept
    {
        if (!hasMedia()) { check(cmd, ScsiSenseKey::NotReady, 0x3A, 0x00); return; }
        good(cmd);
    }

    void cmdRequestSense(ScsiCommand& cmd) noexcept
    {
        // Batch G S-3 (2026-08-02): transfer min(alloc, 18) -- SCSI-2 8.2.14.
        cmd.dataTransferred = copyOut(cmd, m_lastSense.bytes(),
            clampAlloc(static_cast<uint32_t>(m_lastSense.size()), cmd.cdb[4]));
        m_lastSense = ScsiFixedSenseData{};       // sense is consumed by delivery
        cmd.setGood();
    }

    void cmdInquiry(ScsiCommand& cmd) noexcept
    {
        // Standard INQUIRY data, 36 bytes (SPC-2 subset the SRM probes).
        // SPEC-DISK-001: identity comes from the bound profile ATOMICALLY
        // (vendor/product/revision/byte7/RMB/ANSI from one table entry);
        // without a profile, the legacy EMULATR identity is byte-identical
        // to the pre-profile revision.
        uint8_t buf[36] = {};
        buf[0] = static_cast<uint8_t>(deviceType());   // direct-access, connected
        if (m_profile != nullptr) {
            buf[1] = m_profile->inquiryRmb ? 0x80 : 0x00;
            buf[2] = m_profile->inquiryAnsiVersion;
            buf[3] = 0x02;                             // response data format
            buf[4] = 31;                               // additional length
            buf[7] = m_profile->inquiryByte7;          // S-4 closed: CmdQue/Sync
            std::memcpy(&buf[8],  m_profile->inquiryVendor,   8);
            std::memcpy(&buf[16], m_profile->inquiryProduct, 16);
            std::memcpy(&buf[32], m_profile->inquiryRevision, 4);
        } else {
            buf[1] = 0x00;                             // non-removable
            buf[2] = 0x02;                             // SCSI-2
            buf[3] = 0x02;                             // response data format
            buf[4] = 31;                               // additional length (36-5)
            std::memcpy(&buf[8],  "EMULATR ",         8);  // T10 vendor (8)
            std::memcpy(&buf[16], "VIRTUAL DISK    ", 16); // product id (16)
            std::memcpy(&buf[32], "0001",             4);  // revision (4)
        }
        // Batch G S-3 (2026-08-02): transfer min(alloc, 36) -- SCSI-2 8.2.5.
        cmd.dataTransferred = copyOut(cmd, buf,
            clampAlloc(sizeof(buf), cmd.cdb[4]));
        good(cmd);
    }

    void cmdModeSense(ScsiCommand& cmd, bool ten) noexcept
    {
        uint64_t const blocks = hasMedia() ? m_media->blockCount() : 0;
        uint32_t const bs     = hasMedia() ? m_media->blockSize()  : 512;
        uint32_t const alloc  = ten
            ? ((uint32_t(cmd.cdb[7]) << 8) | uint32_t(cmd.cdb[8]))
            :   uint32_t(cmd.cdb[4]);

        if (m_profile == nullptr) {
            // LEGACY (no profile bound): header + 8-byte block descriptor,
            // zero mode pages, any page code -- byte-identical to the
            // pre-SPEC-DISK-001 revision.
            uint8_t  buf[16] = {};
            uint32_t n;
            if (!ten) {
                buf[0] = 11;                   // mode data length (n-1)
                buf[3] = 8;                    // block descriptor length
                put24(&buf[5],  blocks > 0xFFFFFF ? 0 : static_cast<uint32_t>(blocks));
                put24(&buf[9],  bs);
                n = 12;
            } else {
                buf[1] = 14;                   // mode data length low (n-2)
                buf[7] = 8;                    // block descriptor length low
                put24(&buf[9],  blocks > 0xFFFFFF ? 0 : static_cast<uint32_t>(blocks));
                put24(&buf[13], bs);
                n = 16;
            }
            cmd.dataTransferred = copyOut(cmd, buf, (alloc < n) ? alloc : n);
            good(cmd);
            return;
        }

        // SPEC-DISK-001 Sec 6.3 (architect-approved): pages 03h/04h from the
        // profile; 3Fh returns all supported pages; page 00h returns header
        // (+ descriptor) only, GOOD -- permissive legacy shape [CONFIRM SRM
        // usage].  Any OTHER page code: CHECK CONDITION, ILLEGAL REQUEST,
        // invalid field in CDB (05/24/00) -- never a truncated header under
        // GOOD (JRN-AUD-003 S-12 closed).  PC field <7:6>: current values
        // returned for ALL PC codes, _PROVISIONAL.  Page layouts [CONFIRM]
        // vs SCSI-2 X3.131 (recalled field order; see SPEC-DISK-001).
        uint8_t const pageCode = cmd.cdb[2] & 0x3F;
        bool const dbd = (cmd.cdb[1] & 0x08) != 0;
        // Batch H-7 (2026-08-06): page 01h Read-Write Error Recovery.  The
        // MEASURED consumer is SYS$DKDRIVER unit init, CDB 1A 00 01 00 FF 00
        // (crash dump 2026-08-05, RAD+25C); rejecting it cost every boot
        // since JRN-SCSI-041 -- see the header entry.
        // TODO(DISK-MODEPAGES): coverage is the measured minimum -- see the
        // file-header TODO table for the full page matrix and the PC-field
        // caveat.  N810-CHKCOND names any page this gate rejects.
        bool const wantP1 = (pageCode == 0x01) || (pageCode == 0x3F);
        bool const wantP3 = (pageCode == 0x03) || (pageCode == 0x3F);
        bool const wantP4 = (pageCode == 0x04) || (pageCode == 0x3F);
        if (!wantP1 && !wantP3 && !wantP4 && pageCode != 0x00) {
            check(cmd, ScsiSenseKey::IllegalRequest, 0x24, 0x00);
            return;
        }

        uint8_t buf[80] = {};
        uint32_t const hdr = ten ? 8u : 4u;
        uint32_t pos = hdr;
        if (!dbd) {                            // 8-byte block descriptor
            // density 00 | number of blocks (3, BE; 0 if > 24 bits) |
            // reserved 00 | block length (3, BE)
            put24(&buf[pos + 1], blocks > 0xFFFFFF ? 0
                                                   : static_cast<uint32_t>(blocks));
            put24(&buf[pos + 5], bs);
            pos += 8;
        }
        if (wantP1) {                          // page 01h R-W Error Recovery
            // Batch H-7: 12 bytes, policy all-zero -- AXPBox-identical (the
            // configuration measured to boot); DKDRIVER needs existence,
            // not policy.  Ascending page order keeps 3Fh well-formed.
            uint8_t* p = &buf[pos];
            p[0] = 0x01; p[1] = 0x0A;          // code, page length 10
            pos += 12;
        }
        if (wantP3) {                          // page 03h Format Device
            uint8_t* p = &buf[pos];
            p[0]  = 0x03; p[1] = 0x16;         // code, page length 22
            p[10] = uint8_t(m_profile->sectorsPerTrack >> 8);
            p[11] = uint8_t(m_profile->sectorsPerTrack);
            p[12] = uint8_t(bs >> 8);
            p[13] = uint8_t(bs);
            p[15] = 0x01;                      // interleave 0001
            pos += 24;
        }
        if (wantP4) {                          // page 04h Rigid Disk Geometry
            uint8_t* p = &buf[pos];
            p[0] = 0x04; p[1] = 0x16;          // code, page length 22
            put24(&p[2], m_profile->cylinders);
            p[5] = uint8_t(m_profile->tracksPerCylinder);
            p[20] = uint8_t(m_profile->rotationRateRpm >> 8);
            p[21] = uint8_t(m_profile->rotationRateRpm);
            pos += 24;
        }
        // Header LAST: the mode-data-length field always describes the FULL
        // response, so the driver's SHORT first probe (header peek) reads
        // the true size and re-issues correctly -- the two-step sequence
        // whose absence left DK Flags 2 = 0.
        if (!ten) {
            buf[0] = uint8_t(pos - 1);         // does not count itself
            buf[3] = dbd ? 0 : 8;
        } else {
            buf[0] = uint8_t((pos - 2) >> 8);
            buf[1] = uint8_t(pos - 2);
            buf[6] = 0;
            buf[7] = dbd ? 0 : 8;
        }
        cmd.dataTransferred = copyOut(cmd, buf, (alloc < pos) ? alloc : pos);
        good(cmd);
    }

    // MODE SELECT(6)/(10) -- SCSI-2 8.2.8/8.2.9.  A data-OUT command: the
    // initiator sends a parameter list (header + optional block descriptor +
    // mode pages).  OpenVMS SYSBOOT issues MODE SELECT(6) while bringing the
    // boot device up; without it the device answered ILLEGAL REQUEST and
    // SYSBOOT gave up with %SYSBOOT-F-LDFAIL before ever reading a file
    // (JRN-SCSI-026 Sec 7).
    //
    // This device has FIXED geometry backed by the media object, so there is
    // nothing to reconfigure: the faithful answer is to VALIDATE the list and
    // report GOOD, rejecting only a request that would actually change
    // geometry (a block descriptor naming a different block length), which is
    // the one field a target must not silently ignore -- the initiator would
    // then compute every subsequent LBA against a size we are not using.
    void cmdModeSelect(ScsiCommand& cmd, bool ten) noexcept
    {
        if (!hasMedia()) { check(cmd, ScsiSenseKey::NotReady, 0x3A, 0x00); return; }

        // Parameter list length: CDB byte 4 (6-byte) or bytes 7..8 (10-byte).
        uint32_t const listLen = ten
            ? ((uint32_t(cmd.cdb[7]) << 8) | uint32_t(cmd.cdb[8]))
            :   uint32_t(cmd.cdb[4]);
        if (listLen == 0) { good(cmd); return; }   // legal: "no parameters"

        // Only inspect what the initiator actually delivered.  NOTE this must
        // come from dataBufferLength, NOT dataTransferred: handleCommand()
        // zeroes dataTransferred on entry (it is the target's OUTPUT), so a
        // data-out command reading it always sees 0.  (Caught by the tests
        // below -- the first cut used dataTransferred and rejected every
        // well-formed parameter list as truncated.)
        uint32_t const avail = (listLen <= cmd.dataBufferLength)
                             ? listLen
                             : cmd.dataBufferLength;
        uint32_t const hdr   = ten ? 8u : 4u;      // parameter list header size
        if (cmd.dataBuffer == nullptr || avail < hdr) {
            // Truncated list -- the initiator promised more than it sent.
            check(cmd, ScsiSenseKey::IllegalRequest, 0x1A, 0x00); // param list len err
            return;
        }

        // Block descriptor length lives at header byte 3 (6-byte) / 6..7 (10).
        uint32_t const bdLen = ten
            ? ((uint32_t(cmd.dataBuffer[6]) << 8) | uint32_t(cmd.dataBuffer[7]))
            :   uint32_t(cmd.dataBuffer[3]);
        if (bdLen >= 8 && avail >= hdr + 8) {
            uint8_t const* bd = cmd.dataBuffer + hdr;
            // Block descriptor bytes 5..7 = block length.  Zero means "keep
            // current" (SCSI-2), which every sane initiator uses.
            uint32_t const reqBs = (uint32_t(bd[5]) << 16)
                                 | (uint32_t(bd[6]) << 8) | uint32_t(bd[7]);
            uint32_t const curBs = m_media->blockSize();
            if (reqBs != 0 && reqBs != curBs) {
                std::fprintf(stderr,
                    "VirtualDiskDevice: MODE SELECT(%u) requests block size %u, "
                    "media is %u -- rejecting (INVALID FIELD IN PARAMETER LIST)\n",
                    ten ? 10u : 6u, reqBs, curBs);
                // 05/26/00 invalid field in parameter list.
                check(cmd, ScsiSenseKey::IllegalRequest, 0x26, 0x00);
                return;
            }
        }
        // Mode pages: accepted and applied to nothing.  This target exposes no
        // changeable parameters (its MODE SENSE returns header + block
        // descriptor with zero pages), so there is no state to update and no
        // page code that can be "unsupported" relative to what we advertise.
        good(cmd);
    }

    void cmdReadCapacity(ScsiCommand& cmd) noexcept
    {
        if (!hasMedia()) { check(cmd, ScsiSenseKey::NotReady, 0x3A, 0x00); return; }
        // SPEC-DISK-001 Sec 6.2: with a profile bound, capacity is the
        // PROFILE's (attach-time validation guarantees media >= profile;
        // a larger image's tail is deliberately unaddressable).  Returns
        // blocks - 1 = LAST LBA, never the count -- the source document's
        // "Sectors/drive 8,380,079" is the last LBA; do not "fix" this.
        uint64_t const nblk = (m_profile != nullptr)
                            ? m_profile->blocks : m_media->blockCount();
        uint64_t const last = nblk ? nblk - 1 : 0;
        uint32_t const bs   = (m_profile != nullptr)
                            ? m_profile->blockSize : m_media->blockSize();
        uint8_t buf[8];
        put32(&buf[0], last > 0xFFFFFFFFull ? 0xFFFFFFFFu
                                            : static_cast<uint32_t>(last));
        put32(&buf[4], bs);
        cmd.dataTransferred = copyOut(cmd, buf, sizeof(buf));
        good(cmd);
    }

    void cmdReadWrite(ScsiCommand& cmd, bool ten, bool isWrite) noexcept
    {
        if (!hasMedia()) { check(cmd, ScsiSenseKey::NotReady, 0x3A, 0x00); return; }
        if (isWrite && m_media->isReadOnly()) {
            check(cmd, ScsiSenseKey::DataProtect, 0x27, 0x00);
            return;
        }
        uint64_t lba;  uint32_t cnt;
        if (ten) {
            lba = (uint64_t(cmd.cdb[2]) << 24) | (uint64_t(cmd.cdb[3]) << 16)
                | (uint64_t(cmd.cdb[4]) << 8)  |  uint64_t(cmd.cdb[5]);
            cnt = (uint32_t(cmd.cdb[7]) << 8)  |  uint32_t(cmd.cdb[8]);
        } else {
            lba = (uint64_t(cmd.cdb[1] & 0x1F) << 16)
                | (uint64_t(cmd.cdb[2]) << 8) | uint64_t(cmd.cdb[3]);
            cnt = cmd.cdb[4] ? cmd.cdb[4] : 256;   // SBC: 0 means 256 blocks
        }
        if (cnt == 0) { good(cmd); return; }       // READ(10) cnt 0 = no-op
        uint32_t const bs = m_media->blockSize();
        if (lba + cnt > m_media->blockCount()) {
            check(cmd, ScsiSenseKey::IllegalRequest, 0x21, 0x00); // LBA out of range
            return;
        }
        uint64_t const bytes = uint64_t(cnt) * bs;
        if (bytes > cmd.dataBufferLength) {
            std::fprintf(stderr,
                "VirtualDiskDevice: %s lba=%llu cnt=%u (%llu B) exceeds buffer %u B\n",
                isWrite ? "WRITE" : "READ",
                static_cast<unsigned long long>(lba), cnt,
                static_cast<unsigned long long>(bytes), cmd.dataBufferLength);
            check(cmd, ScsiSenseKey::IllegalRequest, 0x24, 0x00);
            return;
        }
        MediaStatus const st = isWrite
            ? m_media->write(lba, cnt, cmd.dataBuffer)
            : m_media->read (lba, cnt, cmd.dataBuffer);
        if (st != MediaStatus::Ok) {
            check(cmd, ScsiSenseKey::MediumError, isWrite ? 0x0C : 0x11, 0x00);
            return;
        }
        cmd.dataTransferred = static_cast<uint32_t>(bytes);
        good(cmd);
    }

    // ---- little helpers ---------------------------------------------------
    static void put32(uint8_t* p, uint32_t v) noexcept
    {
        p[0] = uint8_t(v >> 24); p[1] = uint8_t(v >> 16);
        p[2] = uint8_t(v >> 8);  p[3] = uint8_t(v);
    }
    static void put24(uint8_t* p, uint32_t v) noexcept
    {
        p[0] = uint8_t(v >> 16); p[1] = uint8_t(v >> 8); p[2] = uint8_t(v);
    }
    uint32_t copyOut(ScsiCommand& cmd, uint8_t const* src, uint32_t n) noexcept
    {
        uint32_t const c = (n <= cmd.dataBufferLength) ? n : cmd.dataBufferLength;
        if (cmd.dataBuffer && c) std::memcpy(cmd.dataBuffer, src, c);
        return c;
    }
    // Batch G S-3 (2026-08-02): SCSI-2 allocation-length clamp for
    // parameter-data commands -- transfer min(alloc, available); alloc 0
    // legally means "no data".
    static uint32_t clampAlloc(uint32_t avail, uint32_t alloc) noexcept
    {
        return (alloc < avail) ? alloc : avail;
    }

    std::unique_ptr<IBlockMedia> m_media;
    ScsiFixedSenseData           m_lastSense{};
    DriveProfile const*          m_profile = nullptr;  // SPEC-DISK-001
    // W-4: power-on UA latched at construction; re-latched by busReset().
    // SCOPE NOTE (architect review 2026-08-04): UNIT ATTENTION is strictly
    // per-initiator-per-LUN; a single flag is correct for today's single-
    // initiator bus, but a future multi-initiator config must NOT inherit
    // this global -- promote to a per-initiator set at that point.
    bool m_unitAttention = true;
};

} // namespace scsi

#endif // DEVICELIB_SCSI_VIRTUALDISKDEVICE_H
