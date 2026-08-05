// ============================================================================
// deviceLib/scsi/DriveProfile.h -- SPEC-DISK-001 atomic drive profile table
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
// SPEC-DISK-001 (architect-approved 2026-08-03/04): a drive profile is
// ATOMIC -- identity strings, INQUIRY flags, block count, and geometry all
// come from ONE table entry keyed by mnemonic.  No consumer synthesizes any
// of them independently.  Any pairing where identity and geometry come from
// different sources is a latent defect whether or not it happens to boot.
//
// AUTHORITY: EK-SM2DR-PN.B01 "RZ28L, RZ29L, and RZ40 ... Product Notes"
// (DEC, 1998; ./Processor Support/sm2drpnb.txt) READ THROUGH SPEC-DISK-001
// Sec 3's THREE DOCUMENT HAZARDS -- the source tables have scrambled columns
// (match by capacity, never header position), a 3078/3708 digit
// transposition (3708 is correct), and a 1024-byte sector in an HP disktab
// context (512 is correct).  Do NOT re-extract without reading Sec 3.
//
// RZ29L geometry/count: CONFIRMED (three witnesses -- the document, Charon
// live DEVDEPEND 0x0E7C1471, EmulatR's own measured MAXBLOCK 0x007FDEB0).
// RZ28L/RZ40 geometry and ALL identity strings: _PROVISIONAL until a boot
// with the profile selected produces a matching DEVDEPEND (geometry) and
// Charon's UCB$L_DK_INQUIRY_DATA is captured (identity) -- Sec 8.
//
// ARCHITECT DECISIONS (2026-08-04): keys carry the L (RZ29L, per the source
// doc, diverging from Charon's plain RZ29 display); selection is manifest-
// only (storage row "model"); image smaller than profile = attach REFUSED,
// larger = loud warn; all three platforms default RZ29L for system disks.
//
// CHANGE HISTORY
//   2026-08-04  Created (SPEC-DISK-001).  Three DEC profiles + the
//               EMULATR-512M synthetic (coherent identity+geometry for the
//               512 MB dka1..6 scratch images -- without it they would
//               hard-stop off the bus).
//
//   FILE 1: deviceLib/scsi/DriveProfile.h
//   FUNCTION: kDriveProfiles[] -- RZ28L / RZ29L / RZ40 rows, inquiry_byte7
//   CHANGE (2026-08-04): inquiry_byte7 0x12 -> 0x00 on all three DEC
//   profiles.  Implements JRN-AUD-004 DEC-1 (RANK 1) and honours
//   PLAN-SCSI-001 AM-5 / D-4, which already ruled byte 7 stays 00h until
//   the capability is real.  The 0x12 value was a SEMANTIC derivation from
//   the source document's FEATURE LIST ("Tagged command queuing and
//   multi-initiator support", sm2drpnb p.1) -- a faithful statement about
//   the REAL DRIVE, never a byte-level witness, and never true of this
//   target (JRN-AUD-004 PR-3).
//
//   BEHAVIOR ADDRESSED -- MEASURED, not predicted.  Crash dump of the
//   post-W-4 build (DKA0 UCB 81C67040) shows the full failure chain:
//     INQUIRY byte 7 = 0x12 on the wire (UCB$L_DK_INQUIRY_DATA +4 = 12);
//     UCB$L_DK_FLAGS 0x0030001A -> SDA decodes "cmdq, port_cmdq" SET, so
//     OpenVMS enabled TAGGED QUEUING on the mount path (DK_QDEPTH 8);
//     the HBA's MSG OUT parser silently swallows queue tag messages
//     20h-22h (Ncr53C810.h:1313-1345 = JRN-AUD-004 N-7) and the target
//     has no queue, no QUEUE FULL status;
//     result: UCB$L_DK_UNEXPLAINED 0x0000F134 = 61,748 of 61,750
//     operations, DK_READ_COUNT 0, STS 0x08000110 (never VALID), VCB 0.
//   SCSI-2 6.6.17.3 REQUIRES a non-queueing target to MESSAGE REJECT a
//   queue tag message; we neither reject it nor implement the queue, so
//   asserting CmdQue invites a protocol we cannot complete.  Byte 7 is a
//   CAPABILITY ASSERTION, not a label.
//
//   Re-assert ONLY with PLAN-SCSI-001 Phase C-3 (tagged command queuing:
//   SIMPLE/ORDERED/HEAD OF QUEUE tags, per-tag target state, out-of-order
//   completion) AND C-2 reselection landed.  See the guard comment at the
//   table.
// ============================================================================

#ifndef DEVICELIB_SCSI_DRIVEPROFILE_H
#define DEVICELIB_SCSI_DRIVEPROFILE_H

#include <cstdint>
#include <cstring>

namespace scsi {

// ---------------------------------------------------------------------------
// One atomic drive profile.  String fields are EXACT-WIDTH, space-padded:
// vendor 8, product 16, revision 4 (doctest T-6 pins the widths).
// ---------------------------------------------------------------------------
struct DriveProfile {
    char const* mnemonic;            // table key (and manifest "model" value)
    char const* inquiryVendor;       // 8 bytes, space-padded, exact
    char const* inquiryProduct;      // 16 bytes, space-padded, exact
    char const* inquiryRevision;     // 4 bytes, exact
    uint8_t     inquiryByte7;        // CmdQue/Sync/WBus16 flags
    bool        inquiryRmb;          // removable media bit
    uint8_t     inquiryAnsiVersion;  // 0x02 = SCSI-2
    uint32_t    blockSize;           // bytes (512)
    uint64_t    blocks;              // block COUNT (READ CAPACITY = blocks-1)
    uint32_t    sectorsPerTrack;     // nominal (ZBR flattened, deviation D-3)
    uint32_t    tracksPerCylinder;   // = heads
    uint32_t    cylinders;
    uint16_t    rotationRateRpm;
    bool        geometryConfirmed;   // false = _PROVISIONAL (Sec 8)
    bool        identityConfirmed;   // false = _PROVISIONAL (Sec 8)

    // VMS UCB$L_DEVDEPEND disk encoding: <0:7> sectors, <8:15> tracks,
    // <16:31> cylinders.  The Sec 8 confirmation loop compares this against
    // the dump's UCB$L_DEVDEPEND.
    uint32_t devdepend() const noexcept
    {
        return (cylinders << 16)
             | ((tracksPerCylinder & 0xFFu) << 8)
             | (sectorsPerTrack & 0xFFu);
    }

    // Sec 7.1 geometry sanity band: +/-2% TOLERANCE, NOT EQUALITY -- these
    // are zoned-bit-recording drives; sectors*tracks*cylinders is nominal
    // (RZ28L overshoots +1.93%, RZ40 undershoots -0.32%; RZ29L is exact by
    // coincidence).  This catches scrambled columns and dropped digits (the
    // H-1/H-2 document failure modes), nothing more.
    bool geometrySane() const noexcept
    {
        double const prod  = double(sectorsPerTrack) * double(tracksPerCylinder)
                           * double(cylinders);
        double const delta = (prod - double(blocks)) / double(blocks);
        return delta >= -0.02 && delta <= 0.02;
    }
};

// ---------------------------------------------------------------------------
// Sec 7.2 image-size agreement.  Smaller than profile = REFUSE (a disk that
// answers READ CAPACITY with blocks that do not exist is the failure mode
// most likely to reach a user).  Larger = attach with a LOUD warn (the tail
// is unaddressable; more likely a config error than an intent).
// ---------------------------------------------------------------------------
enum class ProfileMediaCheck : uint8_t { Ok, MediaSmaller, MediaLarger };

inline ProfileMediaCheck checkProfileMedia(DriveProfile const& p,
                                           uint64_t mediaBlocks) noexcept
{
    if (mediaBlocks < p.blocks) return ProfileMediaCheck::MediaSmaller;
    if (mediaBlocks > p.blocks) return ProfileMediaCheck::MediaLarger;
    return ProfileMediaCheck::Ok;
}

// ---------------------------------------------------------------------------
// The table.  Values per SPEC-DISK-001 Sec 4 (already hazard-corrected --
// see the header comment).  Identity strings follow the Sec 8 Solaris LEAD
// ("DEC_RZ29L-AA(C)DEC-LYJ0" -> product "RZ29L-AA (C)DEC ", rev "LYJ0"),
// _PROVISIONAL until Charon's UCB$L_DK_INQUIRY_DATA is captured.
// inquiry_byte7 = 0x00 on ALL profiles (JRN-AUD-004 DEC-1, 2026-08-04).
// DO NOT set bit 1 (CmdQue) or bit 4 (Sync) here until the capability is
// implemented: bit 1 without Phase C-3 tagged queuing measurably wedges the
// OpenVMS mount path (61,748 unexplained operations, see the header block).
// Byte 7 is a capability assertion the initiator ACTS ON, not a label.
// ---------------------------------------------------------------------------
inline DriveProfile const kDriveProfiles[] = {
    // b7 = 0x00 on the three DEC rows: 2026-08-04, was 0x12 -- see the
    // header block "FILE 1 ... inquiry_byte7" (JRN-AUD-004 DEC-1 / DK-10 /
    // N-7; measured consumer = crash dump UCB 81C67040, cmdq set,
    // DK_UNEXPLAINED 61,748).  Re-assert only with Phase C-3.
    //  mnemonic   vendor      product              rev     b7    rmb  ansi  bs        blocks  s/t  t/c   cyl    rpm  geoOK  idOK
    { "RZ28L", "DEC     ", "RZ28L-AA (C)DEC ", "0000", 0x00, false, 0x02, 512,  4110480ull,  86, 16, 3045, 7200, false, false },
    { "RZ29L", "DEC     ", "RZ29L-AA (C)DEC ", "LYJ0", 0x00, false, 0x02, 512,  8380080ull, 113, 20, 3708, 7200, true,  false },
    { "RZ40",  "DEC     ", "RZ40-AA (C)DEC  ", "0000", 0x00, false, 0x02, 512, 17773524ull, 168, 20, 5273, 7200, false, false },
    // Synthetic scratch profile: EMULATR identity, geometry chosen so
    // 64 * 16 * 1024 == 1,048,576 blocks == the 512 MB dka1..6 images
    // EXACTLY (coherent by construction).  byte7 = 0 (no CmdQue/Sync
    // claimed): scratch disks keep the generic path deliberately.
    { "EMULATR-512M", "EMULATR ", "VIRTUAL DISK    ", "0001", 0x00, false, 0x02, 512, 1048576ull, 64, 16, 1024, 0, true, true },
};

inline DriveProfile const* findDriveProfile(char const* mnemonic) noexcept
{
    if (mnemonic == nullptr || mnemonic[0] == '\0') return nullptr;
    for (DriveProfile const& p : kDriveProfiles)
        if (std::strcmp(p.mnemonic, mnemonic) == 0) return &p;
    return nullptr;
}

} // namespace scsi

#endif // DEVICELIB_SCSI_DRIVEPROFILE_H
