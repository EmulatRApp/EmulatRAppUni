// ============================================================================
// global_SRMEnvStore.cpp - ============================================================================
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
#ifndef GLOBAL_SRMENVSTORE_H
#define GLOBAL_SRMENVSTORE_H

#include "global_SRMEnvStore.h"
#include "SRMEnvStore.h"
#include "coreLib/LoggingMacros.h"
#include <spdlog/spdlog.h>
#include <QMutex>
#include <QScopedPointer>
#include <QMutexLocker>
#include <QString>

// ============================================================================
// Singleton State
// ============================================================================

namespace {
	// Singleton instance (pointer for controlled lifetime)
	std::unique_ptr<SRMEnvStore> g_srmEnvStore;

	// Thread safety
	QMutex g_mutex;

	// Initialization state
	bool g_initialized = false;
	QString g_configPath = ".";  // Default config path
}


// ============================================================================
// Initialization and Cleanup
// ============================================================================

void initializeGlobalSRMEnvStore(const QString& configPath) noexcept;


void shutdownGlobalSRMEnvStore() noexcept;


bool isGlobalSRMEnvStoreInitialized() noexcept;


// ============================================================================
// Global Access
// ============================================================================

SRMEnvStore& global_SRMEnvStore() noexcept;


// ============================================================================
// Convenience Functions
// ============================================================================

QString getSRMEnv(const QString& name) noexcept;


void setSRMEnv(const QString& name, const QString& value) noexcept;


bool hasSRMEnv(const QString& name) noexcept;


#endif