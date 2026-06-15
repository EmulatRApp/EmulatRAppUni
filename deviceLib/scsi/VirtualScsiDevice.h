// ============================================================================
// deviceLib/scsi/VirtualScsiDevice.h -- SCSI logical-unit target base
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
// Abstract SCSI/ATAPI target: a controller hands it a ScsiCommand; it performs
// the operation and sets status/sense.  S1 ports the minimal surface the ATAPI
// CD needs from V1 scsiCoreLib (VirtualScsiDevice.h), std-ized (no Qt).
// ============================================================================

#ifndef DEVICELIB_SCSI_VIRTUALSCSIDEVICE_H
#define DEVICELIB_SCSI_VIRTUALSCSIDEVICE_H

#include "deviceLib/scsi/ScsiTypes.h"
#include "deviceLib/scsi/ScsiCommand.h"

namespace scsi {

class VirtualScsiDevice {
public:
    virtual ~VirtualScsiDevice() = default;

    // SPC-3 peripheral device type (e.g. 0x05 = CD/DVD) -- the byte the SRM's
    // LFU keys on (ub->inq[0] & 0x1f).
    virtual ScsiPeripheralDeviceType deviceType() const noexcept = 0;

    // Execute one CDB; sets cmd.status / cmd.senseData / cmd.dataTransferred.
    virtual void handleCommand(ScsiCommand& cmd) noexcept = 0;
};

} // namespace scsi

#endif // DEVICELIB_SCSI_VIRTUALSCSIDEVICE_H
