// ============================================================================
// deviceLib/Tsunami/ITsunamiIde.h -- south-bridge IDE controller interface
// ============================================================================
// Project: EmulatR -- Alpha AXP / EV6 Architecture Emulator (V4)
// Copyright (C) 2025, 2026 eNVy Systems, Inc.  All rights reserved.
// Licensed under eNVy Systems Non-Commercial License v1.1
//
// Project Architect: Timothy Peer
// AI Collaboration:  Claude (Anthropic)
// Contact:        peert@envysys.com  |  https://envysys.com
// ============================================================================
//
// FILE: deviceLib/Tsunami/ITsunamiIde.h  (NEW, Phase 2B)
// FUNCTION: ITsunamiIde (whole file)
// CHANGE: The registration + media-attach interface BOTH IDE controllers
//   (Cy82C693Ide, AliM5229Ide) expose, so the chipset can hold ONE active
//   pointer regardless of the executing model's south bridge.  It is an
//   IIoPortHandler (taskfile ports) AND an IPciDeviceHandler (func-1 config),
//   plus the media-attach seam the chipset's setDiskMedia / CD wiring use.
// ============================================================================

#ifndef DEVICELIB_TSUNAMI_ITSUNAMIIDE_H
#define DEVICELIB_TSUNAMI_ITSUNAMIIDE_H

#include <memory>

#include "chipsetLib/IDeviceHandlers.h"          // IIoPortHandler, IPciDeviceHandler
#include "deviceLib/scsi/VirtualScsiDevice.h"
#include "deviceLib/scsi/IBlockMedia.h"

class ITsunamiIde : public IIoPortHandler, public IPciDeviceHandler
{
public:
    ~ITsunamiIde() override = default;

    // ATAPI device (CD) attach + ATA fixed-disk media attach + reset.  ioRead/
    // ioWrite come from IIoPortHandler; pciConfigRead/Write from IPciDeviceHandler.
    virtual void attachDevice(int channel, int unit, scsi::VirtualScsiDevice* dev) noexcept = 0;
    virtual bool attachMedia(int channel, int unit, std::unique_ptr<scsi::IBlockMedia> media) noexcept = 0;
    virtual void reset() noexcept = 0;
};

#endif // DEVICELIB_TSUNAMI_ITSUNAMIIDE_H
