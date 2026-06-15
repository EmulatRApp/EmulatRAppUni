// ============================================================================
// deviceLib/scsi/ScsiSenseData.h -- fixed-format (18-byte) SCSI sense data + builders
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
// S1 of the 82C693 IDE + ATAPI CD scaffold (journals/IDE_ATAPI_CD_Scaffold_
// Spec_20260607.md).  Minimal SCSI command layer ported from V1 scsiCoreLib
// (D:\EmulatR\EmulatRAppUni\scsiCoreLib), std-ized (no Qt) per contract C4 --
// ATAPI packets ARE SCSI CDBs, so the CD's INQUIRY/sense handling is reuse.
// ============================================================================

#ifndef DEVICELIB_SCSI_SCSISENSEDATA_H
#define DEVICELIB_SCSI_SCSISENSEDATA_H

#include <cstdint>

#include "deviceLib/scsi/ScsiTypes.h"

namespace scsi {

// Fixed-format sense data (18 bytes), SPC-3 4.5.3.
//   byte 0  : response code (0x70 = current)
//   byte 2  : sense key (low nibble)
//   byte 7  : additional sense length (10)
//   byte 12 : additional sense code (ASC)
//   byte 13 : additional sense code qualifier (ASCQ)
struct ScsiFixedSenseData {
    uint8_t data[18] = { 0 };

    ScsiFixedSenseData() noexcept { data[0] = 0x70; data[7] = 0x0A; }

    static ScsiFixedSenseData make(ScsiSenseKey key, uint8_t asc, uint8_t ascq) noexcept
    {
        ScsiFixedSenseData s;
        s.data[0]  = 0x70;
        s.data[2]  = static_cast<uint8_t>(key) & 0x0Fu;
        s.data[7]  = 0x0A;
        s.data[12] = asc;
        s.data[13] = ascq;
        return s;
    }

    const uint8_t* bytes() const noexcept { return data; }
    static constexpr int size() noexcept { return 18; }
};

// NOT READY / MEDIUM NOT PRESENT (02/3A/00) -- the FAIL-FAST no-media sense
// (contract C2); the firmware must NOT retry this, unlike 02/04/xx (becoming
// ready).  This is the load-bearing sense for the empty-CD scaffold.
inline ScsiFixedSenseData senseNotReadyMediumNotPresent() noexcept
{
    return ScsiFixedSenseData::make(ScsiSenseKey::NotReady, 0x3A, 0x00);
}

inline ScsiFixedSenseData senseInvalidOpcode() noexcept
{
    return ScsiFixedSenseData::make(ScsiSenseKey::IllegalRequest, 0x20, 0x00);
}

inline ScsiFixedSenseData senseInvalidFieldInCdb() noexcept
{
    return ScsiFixedSenseData::make(ScsiSenseKey::IllegalRequest, 0x24, 0x00);
}

// LOGICAL BLOCK ADDRESS OUT OF RANGE (05/21/00) -- a READ whose LBA (or
// LBA+length) runs past the medium's last block.  The media-backed read path
// returns this rather than reading off the end of the backing file.
inline ScsiFixedSenseData senseLbaOutOfRange() noexcept
{
    return ScsiFixedSenseData::make(ScsiSenseKey::IllegalRequest, 0x21, 0x00);
}

// UNRECOVERED READ ERROR (03/11/00) -- the backing file could not be opened or
// the sector read short.  Distinct from no-media (02/3A/00): media IS present
// but the host-side read failed.
inline ScsiFixedSenseData senseUnrecoveredReadError() noexcept
{
    return ScsiFixedSenseData::make(ScsiSenseKey::MediumError, 0x11, 0x00);
}

} // namespace scsi

#endif // DEVICELIB_SCSI_SCSISENSEDATA_H
