// ============================================================================
// coreLib/IprBank.h -- DEPRECATED; merged into CpuState
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
// Documentation:  https://timothypeer.github.io/ASA-EMulatR-Project/
// ============================================================================
//
// IprBank was the v1-interim per-CPU IPR carrier.  Its fields have
// been absorbed into CpuState verbatim (same names, same types) so
// that callers reaching for ptbr / asn / va_ctl / i_ctl / m_ctl /
// i_spe / m_spe / mode / palMode / cycleCount / intrFlag / mm_stat
// find them as direct members of CpuState rather than nested under
// an iprs sub-struct.
//
// This header now redirects to coreLib/CpuState.h for the few
// translation units that still reach for the old name.  Once those
// are updated, this file can be deleted outright.
//
// ============================================================================

#ifndef CORELIB_IPRBANK_H
#define CORELIB_IPRBANK_H

#include "coreLib/CpuState.h"

#endif // CORELIB_IPRBANK_H
