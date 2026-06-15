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

#include "global_SRMEnvStore.h"
#include "SRMEnvStore.h"
#include "coreLib/LoggingMacros.h"
#include <QMutex>
#include <QScopedPointer>
#include <QMutexLocker>
#include <QString>



// ============================================================================
// Initialization and Cleanup
// ============================================================================

void initializeGlobalSRMEnvStore(const QString& configPath) noexcept
{
	QMutexLocker lock(&g_mutex);

	if (g_initialized) {
		SPDLOG_WARN("SRMEnvStore already initialized, skipping");
		return;
	}

	try {
		g_configPath = configPath;
		g_srmEnvStore = std::make_unique<SRMEnvStore>(configPath);
		g_initialized = true;

		SPDLOG_INFO("SRMEnvStore initialized with config path: {}",(configPath.toStdString()));
		SPDLOG_INFO("Loaded {} environment variables", (g_srmEnvStore->count()));
	}
	catch (const std::exception& e) {
		SPDLOG_ERROR("Failed to initialize SRMEnvStore: {}",(e.what()));
		g_initialized = false;
	}
	catch (...) {
		SPDLOG_ERROR("Failed to initialize SRMEnvStore: unknown error");
		g_initialized = false;
	}
}

void shutdownGlobalSRMEnvStore() noexcept
{
	QMutexLocker lock(&g_mutex);

	if (!g_initialized) {
		return;
	}

	try {
		if (g_srmEnvStore) {
			// Ensure final save before shutdown
			g_srmEnvStore->save();
			SPDLOG_INFO("SRMEnvStore saved before shutdown");

			g_srmEnvStore.reset(); 
		}

		g_initialized = false;
		SPDLOG_INFO("SRMEnvStore shutdown complete");
	}
	catch (const std::exception& e) {
		SPDLOG_ERROR("Error during SRMEnvStore shutdown: {}", (e.what()));
	}
	catch (...) {
		SPDLOG_ERROR("Unknown error during SRMEnvStore shutdown");
	}
}

bool isGlobalSRMEnvStoreInitialized() noexcept
{
	QMutexLocker lock(&g_mutex);
	return g_initialized;
}

// ============================================================================
// Global Access
// ============================================================================

SRMEnvStore& global_SRMEnvStore() noexcept
{
	QMutexLocker lock(&g_mutex);

	// Lazy initialization if not already initialized
	if (!g_initialized) {
		SPDLOG_WARN("SRMEnvStore accessed before initialization, using default config path");

		try {
			g_srmEnvStore = std::make_unique<SRMEnvStore>(g_configPath);
			g_initialized = true;

			SPDLOG_INFO("SRMEnvStore lazy-initialized with default path: {}",(g_configPath.toStdString()));
		}
		catch (...) {
			SPDLOG_ERROR("CRITICAL: Failed to lazy-initialize SRMEnvStore");
			// This should never happen in production, but we need to return something
			// Create a static fallback instance
			static SRMEnvStore fallback(".");
			return fallback;
		}
	}

	return *g_srmEnvStore;
}

// ============================================================================
// Convenience Functions
// ============================================================================

QString getSRMEnv(const QString& name) noexcept
{
	try {
		return global_SRMEnvStore().get(name);
	}
	catch (...)
	{
		SPDLOG_ERROR("Error getting SRM environment variable: {}",(name.toStdString()));
		return QString();
	}
}

void setSRMEnv(const QString& name, const QString& value) noexcept
{
	try {
		global_SRMEnvStore().set(name, value);
	}
	catch (...) {
		SPDLOG_ERROR("Error setting SRM environment variable: {}",(name.toStdString()));
	}
}

bool hasSRMEnv(const QString& name) noexcept
{
	try {
		return global_SRMEnvStore().exists(name);
	}
	catch (...) {
		SPDLOG_ERROR("Error checking SRM environment variable: {}",(name.toStdString()));
		return false;
	}
}

