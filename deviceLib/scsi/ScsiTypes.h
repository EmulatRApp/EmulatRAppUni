// ============================================================================
// deviceLib/scsi/ScsiTypes.h -- SCSI status / sense-key / device-type enums + opcode subset
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

#ifndef DEVICELIB_SCSI_SCSITYPES_H
#define DEVICELIB_SCSI_SCSITYPES_H

#include <cstdint>

namespace scsi {

enum class ScsiStatus : uint8_t {
    Good           = 0x00,
    CheckCondition = 0x02,
    Busy           = 0x08
};

enum class ScsiSenseKey : uint8_t {
    NoSense        = 0x00,
    RecoveredError = 0x01,
    NotReady       = 0x02,
    MediumError    = 0x03,
    HardwareError  = 0x04,
    IllegalRequest = 0x05,
    UnitAttention  = 0x06,
    DataProtect    = 0x07,
    AbortedCommand = 0x0B
};

enum class ScsiPeripheralDeviceType : uint8_t {
    DirectAccessBlockDevice = 0x00,   // disk
    SequentialAccess        = 0x01,   // tape
    CdDvdDevice             = 0x05,   // CD-ROM / DVD  (the LFU type-5 unit)
    OpticalMemory           = 0x07
};

enum class ScsiDataDirection : uint8_t { None = 0, DeviceToHost = 1, HostToDevice = 2 };

// SCSI opcodes -- the subset an ATAPI CD exercises at SRM boot (dq_driver.c).
namespace ScsiOp {
    constexpr uint8_t TEST_UNIT_READY = 0x00;
    constexpr uint8_t REQUEST_SENSE   = 0x03;
    constexpr uint8_t READ6           = 0x08;
    constexpr uint8_t INQUIRY         = 0x12;
    constexpr uint8_t START_STOP_UNIT = 0x1B;
    constexpr uint8_t PREVENT_ALLOW   = 0x1E;
    constexpr uint8_t READ_CAPACITY10 = 0x25;
    constexpr uint8_t READ10          = 0x28;
    constexpr uint8_t READ_TOC        = 0x43;
    constexpr uint8_t MODE_SENSE10    = 0x5A;
    constexpr uint8_t READ12          = 0xA8;
}

} // namespace scsi

#endif // DEVICELIB_SCSI_SCSITYPES_H
