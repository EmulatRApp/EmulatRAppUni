// ============================================================================
// deviceLib_global.h - device Lib global
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

#ifndef DEVICELIB_GLOBAL_H
#define DEVICELIB_GLOBAL_H

#include <QtCore/qglobal.h>

#if defined(DEVICELIB_LIBRARY)
#define DEVICELIB_EXPORT Q_DECL_EXPORT
#else
#define DEVICELIB_EXPORT Q_DECL_IMPORT
#endif

#endif // DEVICELIB_GLOBAL_H
