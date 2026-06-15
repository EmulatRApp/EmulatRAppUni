// ============================================================================
// device_core_inl.h - Official ASA EmulatR SRM Device Naming Standard:
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

#ifndef DEVICE_CORE_INL_H
#define DEVICE_CORE_INL_H
#include <QtGlobal>

// Official ASA EmulatR SRM Device Naming Standard:
enum class SRMNamingPolicy_enum {
    dka = 0, // SCSI disk           // dka0, dka1, dka2...
    mka = 1, // SCSI tape          // mka0, mka1...
    dga = 2, // Fibre Channel disk  // dga0, dga1...
    ewa = 3, // Ethernet           // ewa0, ewa1...
    fwa = 4, // FDDI               // fwa0, fwa1...
    opa = 5, // Console            // opa0, opa1...
    pka = 6, // SCSI controller    // pka0, pka1...
    gga = 7 // FC controller      // gga0, gga1...
};

struct SRMNamingPolicy {
    SRMNamingPolicy_enum namingPolicyType;
};

#endif // DEVICE_CORE_INL_H
