// ============================================================================
// deviceLib/scsi/ScsiCommand.h -- controller<->target command contract
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

#ifndef DEVICELIB_SCSI_SCSICOMMAND_H
#define DEVICELIB_SCSI_SCSICOMMAND_H

#include <cstdint>

#include "deviceLib/scsi/ScsiTypes.h"
#include "deviceLib/scsi/ScsiSenseData.h"

namespace scsi {

// The contract a controller (the ATAPI taskfile) fills in and submits to a
// target (VirtualIsoDevice).  The target reads the CDB, moves data through the
// caller-provided buffer, and sets status + sense.
struct ScsiCommand {
    const uint8_t*    cdb              = nullptr;  // command descriptor block
    uint8_t           cdbLength        = 0;
    uint8_t           lun              = 0;
    ScsiDataDirection dataDirection    = ScsiDataDirection::None;

    uint8_t*          dataBuffer       = nullptr;  // caller-owned transfer buffer
    uint32_t          dataBufferLength = 0;        // capacity in bytes
    uint32_t          dataTransferred  = 0;        // target sets bytes moved

    ScsiStatus         status      = ScsiStatus::Good;  // what the guest sees
    ScsiFixedSenseData senseData{};                     // valid iff CheckCondition
    bool               senseValid  = false;

    void setGood() noexcept { status = ScsiStatus::Good; senseValid = false; }

    void setCheckCondition(const ScsiFixedSenseData& s) noexcept
    {
        status     = ScsiStatus::CheckCondition;
        senseData  = s;
        senseValid = true;
    }

    uint8_t opcode() const noexcept { return (cdb && cdbLength) ? cdb[0] : 0xFFu; }
};

} // namespace scsi

#endif // DEVICELIB_SCSI_SCSICOMMAND_H
