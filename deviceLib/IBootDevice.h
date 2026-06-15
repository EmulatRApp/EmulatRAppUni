// ============================================================================
// IBootDevice.h - Then boot becomes elegant:
// ============================================================================
// Project: ASA-EMulatR - Alpha AXP Architecture Emulator
// Copyright (C) 2025 eNVy Systems, Inc. All rights reserved.
// Licensed under eNVy Systems Non-Commercial License v1.1
//
// Project Architect: Timothy Peer
// AI Code Generation: Claude (Anthropic) / ChatGPT (OpenAI)
//
// Commercial use prohibited without separate license.
// Contact: peert@envysys.com | https://envysys.com
// Documentation: https://timothypeer.github.io/ASA-EMulatR-Project/
// ============================================================================

#ifndef IBOOTDEVICE_H
#define IBOOTDEVICE_H
#include <QString>
#include <QtGlobal>





class IBootDevice {
public:
    virtual bool hasBootstrapCapability() const = 0;
    virtual BootstrapImage readBootstrap() = 0;
    virtual QString getBootDevicePath() const = 0;
};

// Then boot becomes elegant:
void SRMConsole::cmdBoot(const QString& deviceName) {
    auto* device = deviceManager.getBootDevice(deviceName);
    if (!device || !device->hasBootstrapCapability()) {
        printError("Device not bootable");
        return;
    }

    auto bootstrap = device->readBootstrap();  //  Polymorphic
    transferControl(bootstrap);
}

#endif // IBOOTDEVICE_H
