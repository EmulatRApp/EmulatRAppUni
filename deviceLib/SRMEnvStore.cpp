// ============================================================================
// SRMEnvStore.cpp - ============================================================================
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

#include "SRMEnvStore.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QFile>
#include <QDir>
#include <QStandardPaths>
#include <QDateTime>
#include <QDebug>
#include "../coreLib/LoggingMacros.h"

#define COMPONENT_NAME "SRMEnvStore"
// ============================================================================
// SRMEnvStore Implementation - Toy Persistence
// ============================================================================

SRMEnvStore::SRMEnvStore(const QString& configPath)
	: m_configPath(configPath)
{
	// Initialize default SRM environment variables
	initializeDefaults();

	// Load existing variables from JSON file
	load();
}

SRMEnvStore::~SRMEnvStore()
{
	// Auto-save on destruction
	save();
}

void SRMEnvStore::initializeDefaults() noexcept
{
	// Standard SRM environment variables with sensible defaults
	m_variables["bootdef_dev"] = "dka0";
	m_variables["boot_osflags"] = "0,0";
	m_variables["console"] = "serial";
	m_variables["os_type"] = "openvms";
	m_variables["auto_action"] = "boot";

	// System information variables
	m_variables["sys_serial"] = "EMU0000000001";   //  *** system serial number not set. use set sys_serial_num command.
	m_variables["sys_type"] = "AlphaStation_Emulator";
	m_variables["pal_version"] = "1.70-0";

	// Console and I/O
	m_variables["terminal_type"] = "vt100";
	m_variables["com1_baud"] = "9600";
	m_variables["com1_parity"] = "none";

	// Boot behavior
	m_variables["halt_action"] = "halt";
	m_variables["mopv3_boot"] = "on";

	// Time offset for toy persistence (default: no offset)
	m_timeOffsetSeconds = 0;
	m_lastSetTimestamp = QDateTime::currentDateTime();
}

QString SRMEnvStore::get(const QString& name) const noexcept
{
	return m_variables.value(name.toLower(), QString());
}

void SRMEnvStore::set(const QString& name, const QString& value) noexcept
{
	QString key = name.toLower();
	QString oldValue = m_variables.value(key);

	m_variables[key] = value;

	qDebug() << "SRM ENV:" << name << "=" << value;

	// Auto-save on modification (toy persistence behavior)
	save();
}

bool SRMEnvStore::exists(const QString& name) const noexcept
{
	return m_variables.contains(name.toLower());
}

QStringList SRMEnvStore::getAllNames() const noexcept
{
	return m_variables.keys();
}

void SRMEnvStore::remove(const QString& name) noexcept
{
	QString key = name.toLower();
	if (m_variables.remove(key) > 0) {
		qDebug() << "SRM ENV: Removed" << name;
		save();
	}
}

void SRMEnvStore::clear() noexcept
{
	m_variables.clear();
	initializeDefaults();
	save();
}

int SRMEnvStore::count() const noexcept
{
	return m_variables.size();
}

void SRMEnvStore::setTimeOffset(qint64 offsetSeconds) noexcept
{
	m_timeOffsetSeconds = offsetSeconds;
	m_lastSetTimestamp = QDateTime::currentDateTime();
	save();
}

qint64 SRMEnvStore::getTimeOffset() const noexcept
{
	return m_timeOffsetSeconds;
}

QDateTime SRMEnvStore::getAdjustedTime() const noexcept
{
	return QDateTime::currentDateTime().addSecs(m_timeOffsetSeconds);
}

QString SRMEnvStore::getStoragePath() const noexcept
{
	QDir configDir(m_configPath);
	if (!configDir.exists()) {
		configDir.mkpath(".");
	}

	return configDir.filePath("srm_env.json");
}

void SRMEnvStore::save() noexcept
{
	QString filePath = getStoragePath();

	QJsonObject root;

	// Environment variables
	QJsonObject envVars;
	for (auto it = m_variables.begin(); it != m_variables.end(); ++it) {
		envVars[it.key()] = it.value();
	}
	root["environment_variables"] = envVars;

	// Time information
	QJsonObject timeObj;
	timeObj["offset_seconds"] = m_timeOffsetSeconds;
	timeObj["last_set_timestamp"] = m_lastSetTimestamp.toString(Qt::ISODate);
	root["time"] = timeObj;

	// Metadata
	QJsonObject meta;
	meta["format_version"] = "1.0";
	meta["created_by"] = "ASA EmulatR";
	meta["last_modified"] = QDateTime::currentDateTime().toString(Qt::ISODate);
	root["metadata"] = meta;

	// Write to file
	QJsonDocument doc(root);
	QFile file(filePath);
	if (file.open(QIODevice::WriteOnly)) {
		file.write(doc.toJson());
		file.close();
		qDebug() << "SRM environment saved to" << filePath;
	}
	else {
		qWarning() << "Failed to save SRM environment to" << filePath << ":" << file.errorString();
	}
}


void SRMEnvStore::load() noexcept
{
	QString filePath = getStoragePath();

	QFile file(filePath);
	if (!file.exists()) {
		qDebug() << "No existing SRM environment file, using defaults";
		return;
	}

	if (!file.open(QIODevice::ReadOnly)) {
		qWarning() << "Failed to read SRM environment from" << filePath << ":" << file.errorString();
		return;
	}

	QByteArray data = file.readAll();
	file.close();

	QJsonParseError error;
	QJsonDocument doc = QJsonDocument::fromJson(data, &error);

	if (error.error != QJsonParseError::NoError) {
		qWarning() << "Failed to parse SRM environment JSON:" << error.errorString();
		return;
	}

	QJsonObject root = doc.object();

	// Load environment variables
	if (root.contains("environment_variables") && root["environment_variables"].isObject()) {
		QJsonObject envVars = root["environment_variables"].toObject();

		for (auto it = envVars.begin(); it != envVars.end(); ++it) {
			m_variables[it.key()] = it.value().toString();
		}
	}

	// Load time information
	if (root.contains("time") && root["time"].isObject()) {
		QJsonObject timeObj = root["time"].toObject();

		if (timeObj.contains("offset_seconds")) {
			m_timeOffsetSeconds = timeObj["offset_seconds"].toInt();
		}

		if (timeObj.contains("last_set_timestamp")) {
			m_lastSetTimestamp = QDateTime::fromString(
				timeObj["last_set_timestamp"].toString(), Qt::ISODate);
		}
	}

	qDebug() << "SRM environment loaded from" << filePath
		<< "with" << m_variables.size() << "variables";
}