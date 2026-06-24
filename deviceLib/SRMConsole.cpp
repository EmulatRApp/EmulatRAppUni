// ============================================================================
// SRMConsole.cpp - Write a QString to console (no newline)
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

#include "SRMConsole.h"


#include "coreLib/LoggingMacros.h"
#include <spdlog/spdlog.h>
#include <QRegularExpression>
#include <QDateTime>
#include <QDebug>
#include <QThread>
#include <QString>
#include <QStringList>
#include "deviceLib/global_ConsoleManager.h"
#include "ConsoleManager.h"
#include "FirmwareDeviceManager.h"
#include "SRMConsoleDevice.h"
#include "config/EmulatorSettings.h"
#include "deviceLib/global_firmwaredevicemanager.h"


#define COMPONENT_NAME "SRMConsole"
// ============================================================================
// SRMConsole Implementation
// ============================================================================

SRMConsole::SRMConsole(const emulatr::config::EmulatorSettings& settings, SRMEnvStore& envStore)
	: m_settings(settings)
	  , m_envStore(envStore)
{
	// Initialize command dispatch table
	m_commands["help"] = [this](const QStringList& args) { cmdHelp(args); };
	m_commands["?"]    = [this](const QStringList& args) { cmdHelp(args); };
	m_commands["show"] = [this](const QStringList& args) {
		if (args.size() < 2) {
			printError("SHOW requires an argument");
			return;
		}

		QString subCmd = args[1].toLower();
		if (subCmd == "device") {
			cmdShowDevice(args);
		}
		else if (subCmd == "config") {
			cmdShowConfig(args);
		}
		else if (subCmd == "*") {
			cmdShowAll(args);
		}
		else {
			cmdShowVar(subCmd);
		}
	};
	m_commands["set"]      = [this](const QStringList& args) { cmdSet(args); };
	m_commands["boot"]     = [this](const QStringList& args) { cmdBoot(args); };
	m_commands["halt"]     = [this](const QStringList& args) { cmdHalt(args); };
	m_commands["continue"] = [this](const QStringList& args) { cmdContinue(args); };
	m_commands["reset"]    = [this](const QStringList& args) { cmdReset(args); };
	m_commands["reload"]   = [this](const QStringList& args) {
		if (args.size() > 1 && args[1].toLower() == "config") {
			cmdReloadConfig(args);
		}
		else {
			printError("Usage: RELOAD CONFIG");
		}
	};
}

void SRMConsole::initialize(uint16_t cpuId) noexcept
{
	m_cpuId = cpuId;

	// Generate device listings from configuration
	generateDeviceListing();

	m_initialized = true;

	SPDLOG_DEBUG("SRMConsole: Initialized");
}

void SRMConsole::start() noexcept
{
	if (!m_initialized || !m_cpuId) {
		SPDLOG_ERROR("SRMConsole: Not properly initialized");
		return;
	}

	m_running = true;

	// Display boot banner
	showBanner();

	SPDLOG_INFO("SRMConsole: Started");
}

bool SRMConsole::step() noexcept
{
	if (!m_running) {
		return false;
	}

	// Show prompt and read command
	showPrompt();
	QString command = readLine();

	if (!command.isEmpty()) {
		processCommand(command);
	}

	return m_running;
}

void SRMConsole::stop() noexcept
{
	m_running = false;
	putLine(std::string(""));
	putLine(std::string("Console stopped."));

	SPDLOG_INFO("SRMConsole: Stopped");
}

bool SRMConsole::isRunning() const noexcept
{
	return m_running;
}

void SRMConsole::showBanner() noexcept
{
	putLine(std::string("AlphaStation Emulator Console V1.0-0"));
	putLine(std::string("ASA EmulatR (c) 2026 Timothy Peer / eNVy Systems, Inc."));
	putLine(std::string(""));
	putLine(std::string("                   OpenVMS PALcode V1.70-0, built on 04-JAN-2026 17:30:27"));
	putLine(std::string("                   Firmware build date: 04-JAN-2026"));
	putLine(std::string(""));
	putString("                   Processor                : ");
	putLine(formatCPUInfo());
	putLine(std::string("                   System Serial Number     : EMU0000000001"));
	putString("                   Memory Testing           : ");
	putLine(formatMemoryInfo());
	putLine(std::string("                   Bcache                   : 2 MB"));
	putLine(std::string(""));
	putLine(std::string("                   Testing memory from 200000 to 20000000"));
	putString("                   Memory: ");
	putString(formatMemoryInfo());
	putLine(std::string(", cache: 2048 KB"));
	putLine(std::string("                   Loading System Software"));
	putLine(std::string(""));
	putLine(std::string("                   Primary bootstrap loaded from (boot device)"));
	putLine(std::string("                   Secondary bootstrap loaded from (boot device)"));
	putLine(std::string(""));
	putLine(std::string("                   Console is running on CPU 0"));
	putLine(std::string("                   Entering SRM Console Mode"));
	putLine(std::string(""));
	putLine(std::string("                   For system information, type SHOW CONFIG"));
	putLine(std::string("                   For help, type HELP"));
	putLine(std::string(""));
}

void SRMConsole::showPrompt() noexcept
{
	putString(">>>");
}

void SRMConsole::processCommand(const QString& commandLine) noexcept
{
	QStringList tokens = parseCommand(commandLine);

	if (tokens.isEmpty()) {
		return;
	}

	QString command = tokens[0].toLower();

	auto it = m_commands.find(command);
	if (it != m_commands.end()) {
		it.value()(tokens);
	}
	else {
		printError(QString("%%SRM-E-UNKNOWNCMD, Unknown command: %1").arg(command));
		putLine(std::string("Type HELP for a list of available commands."));
	}
}

QStringList SRMConsole::parseCommand(const QString& commandLine) noexcept
{
	QString trimmed = commandLine.trimmed();
	if (trimmed.isEmpty()) {
		return {};
	}

	// Split on whitespace, preserving quoted strings
	QRegularExpression rx(R"(\s*([^\s"]+|"[^"]*")\s*)");
	QStringList        tokens;

	auto iterator = rx.globalMatch(trimmed);
	while (iterator.hasNext()) {
		auto    match = iterator.next();
		QString token = match.captured(1);

		// Remove quotes if present
		if (token.startsWith('"') && token.endsWith('"')) {
			token = token.mid(1, token.length() - 2);
		}

		tokens.append(token);
	}

	return tokens;
}

QString SRMConsole::readLine() noexcept
{
	m_currentLine.clear();

	while (true) {
		int ch = getChar();

		if (ch == -1) {
			// No data available - this is expected in polling mode
			QThread::yieldCurrentThread();
			continue;
		}

		char c = static_cast<char>(ch);

		switch (c) {
		case CR:
		case LF:
			// End of line
			putChar(CR);
			putChar(LF);
			return m_currentLine;

		case BACKSPACE:
		case DELETE_:   // 0x7F; NOT the Windows <winnt.h> DELETE macro (0x00010000)
			handleBackspace();
			break;

		case CTRL_C:
			// Ctrl+C - cancel line
			putLine(std::string("^C"));
			m_currentLine.clear();
			return {};

		case CTRL_U:
			// Ctrl+U - clear line
			while (!m_currentLine.isEmpty()) {
				handleBackspace();
			}
			break;

		default:
			// Regular character
			if (c >= ' ' && c <= '~' && m_currentLine.length() < MAX_LINE_LENGTH) {
				m_currentLine.append(c);
				echoChar(c);
			}
			break;
		}
	}
}

void SRMConsole::handleBackspace() noexcept
{
	if (!m_currentLine.isEmpty()) {
		m_currentLine.chop(1);
		putChar(BACKSPACE);
		putChar(' ');
		putChar(BACKSPACE);
	}
}

void SRMConsole::handleShowConfig(SRMConsoleDevice* console)
{

	auto& fdm = global_FirmwareDeviceManager();

	if (!fdm.isInitialized()) {
		console->putLine("?? Device tree not initialized");
		return;
	}

	// Get platform node
	const DeviceNode* platform = fdm.getDevice("platform");
	if (!platform) {
		console->putLine("?? Platform information not available");
		return;
	}

	console->putLine();
	console->putLine("System Configuration:");
	console->putLine("========================================");

	console->putLine(QString("Platform:      %1")
		.arg(platform->properties.value("platform.model", "Unknown").toString()));

	console->putLine(QString("CPU Count:     %1")
		.arg(platform->properties.value("platform.cpu.count", 0).toInt()));

	console->putLine(QString("Memory:        %1 GB")
		.arg(platform->properties.value("platform.memory.size", 0).toInt()));

	console->putLine(QString("Firmware:      %1")
		.arg(platform->properties.value("platform.firmware.version", "Unknown").toString()));

	console->putLine();

}

void SRMConsole::handleShowDevice()
{
	//TODO SHOW DEVICE
}

void SRMConsole::handleBoot(SRMConsoleDevice* console, const QString& args)
{

	QString deviceName = args.trimmed().toUpper();

	if (deviceName.isEmpty()) {
		console->putLine("Usage: boot <device>");
		return;
	}

	auto& fdm = global_FirmwareDeviceManager();

	// Lookup device
	const DeviceNode* device = fdm.getDevice(deviceName);
	if (!device) {
		console->putLine(QString("?? %1 - device not found").arg(deviceName));
		return;
	}

	// Check if enabled
	if (!device->enabled) {
		console->putLine(QString("?? %1 - device offline").arg(deviceName));
		return;
	}

	// Check if bootable
	if (device->deviceNodeType != DeviceNodeType::SCSIDisk &&
		device->deviceNodeType != DeviceNodeType::IDEDisk) {
		console->putLine(QString("?? %1 - not a bootable device").arg(deviceName));
		return;
	}

	// Boot message
	console->putLine(QString("(boot %1)").arg(deviceName));
	console->putLine();

	// Get image path
	QString image = device->properties.value("image", "").toString();
	if (image.isEmpty()) {
		console->putLine("?? No disk image configured");
		return;
	}

	console->putLine(QString("Loading from %1...").arg(image));

	// Actual boot process would go here...
}

void SRMConsole::handleBootCommand(const QString& deviceName)
{

	auto& fdm = global_FirmwareDeviceManager();

	// Lookup device in tree
	const DeviceNode* device = fdm.getDevice(deviceName.toUpper());

	if (!device) {
		putLine(QString("Error: Device %1 not found").arg(deviceName));
		return;
	}

	if (!device->enabled) {
		putLine(QString("Error: Device %1 is offline").arg(deviceName));
		return;
	}

	// Check if bootable
	if (device->deviceNodeType != DeviceNodeType::SCSIDisk &&
		device->deviceNodeType != DeviceNodeType::IDEDisk) {
		putLine(QString("Error: Device %1 is not bootable").arg(deviceName));
		return;
	}

	putLine(QString("Booting from %1...").arg(deviceName));
}

void SRMConsole::echoChar(char ch) const noexcept
{
	putChar(ch);
}

// ============================================================================
// Console I/O (CSERVE Integration)
// ============================================================================

int SRMConsole::getChar() const noexcept
{
	return static_cast<int>(executeCSERVE(0x01));
}

void SRMConsole::putChar(char ch) const noexcept
{
	executeCSERVE(0x02, static_cast<quint8>(ch));
}

void SRMConsole::putString(const QString& str) const noexcept
{
	for (QChar qc : str) {
		if (qc.unicode() < 256) {
			putChar(static_cast<char>(qc.unicode()));
		}
	}
}

void SRMConsole::putLine(const QString& str) noexcept
{
	putString(str);
	putChar(CR);
	putChar(LF);
}
void SRMConsole::putLine(const std::string& str) noexcept
{
	QString str_q(str.c_str());
	putString(str_q);
	putChar(CR);
	putChar(LF);
}
/**
 * @brief Write a QString to console (no newline)
 * @param console Console device
 * @param str String to write
 */
void SRMConsole::putText(SRMConsoleDevice* console, const QString& str)
{
	if (!console) return;

	QByteArray utf8 = str.toUtf8();
	console->putString(reinterpret_cast<const quint8*>(utf8.constData()),
		utf8.size());
}

/**
 * @brief Write a QString to console with newline (CRLF)
 * @param console Console device
 * @param str String to write
 */
void SRMConsole::putLine(SRMConsoleDevice* console, const QString& str)
{
	if (!console) return;

	putText(console, str);
	console->putChar('\r');  // CR
	console->putChar('\n');  // LF
}

/**
 * @brief Write a blank line (CRLF only)
 * @param console Console device
 */
void SRMConsole::putLine(SRMConsoleDevice* console)
{
	if (!console) return;

	console->putChar('\r');
	console->putChar('\n');
}

/**
 * @brief Write a C-string to console with newline
 * @param console Console device
 * @param str Null-terminated C string
 */
void putLine(SRMConsoleDevice* console, const char* str)
{
	if (!console || !str) return;
	QString xx(str);
	putLine(console, xx.toUtf8());
}
/*
SRMConsole's CSERVE is a logical console service, not a literal CALL_PAL ABI.
*/
quint64 SRMConsole::executeCSERVE(quint8 function, quint64 a0, quint64 a1, quint64 a2, quint64 a3) const noexcept
{
	/*
	 *
	 *TODO  executeCSERVE may be superfluous to PalBox
	auto& regs = globalFloatRegs(m_cpuId);
	// Set up registers for CSERVE call
	regs.write(16, a0);  // A0
	regs.write(17, a1);  // A1  
	regs.write(18, a2);  // A2
	regs.write(19, a3);  // A3

	// Create a minimal pipeline slot for PAL call
	// Note: This is a simplified approach for SRM console use
	// Real implementation would go through proper pipeline

	// For now, directly call the console manager functions
	// This bypasses the full PAL machinery but provides SRM console functionality

	switch (function) {
	case 0x01:  // GETC
	{
		return global_ConsoleManager().getCharFromOPA(0);
	}


	case 0x02:  // PUTC
	{
		global_ConsoleManager().putCharToOPA(0, static_cast<quint8>(a0));
		return 0;
	}
	case 0x09:  // PUTS (simplified - just put individual characters)
		// For SRM console, we mainly use putChar in loops
		return 0;

	default:
		return static_cast<quint64>(-1);
	}
	*/
	return 0 ;
}

// ============================================================================
// Command Implementations
// ============================================================================

void SRMConsole::cmdHelp(const QStringList& args) noexcept
{
	Q_UNUSED(args);

	putLine(std::string(""));
	putLine(std::string("Available SRM Console Commands:"));
	putLine(std::string(""));
	putLine(std::string("BOOT [device] [flags]     - Boot from device"));
	putLine(std::string("CONTINUE                  - Resume execution"));
	putLine(std::string("HALT                      - Halt system"));
	putLine(std::string("HELP                      - Display this help"));
	putLine(std::string("RESET                     - Reset system"));
	putLine(std::string("SET <var> <value>         - Set environment variable"));
	putLine(std::string("SHOW CONFIG               - Display system configuration"));
	putLine(std::string("SHOW DEVICE               - Display device information"));
	putLine(std::string("SHOW *                    - Display all environment variables"));
	putLine(std::string("SHOW <variable>           - Display specific environment variable"));
	putLine(std::string(""));
	putLine(std::string("Examples:"));
	putLine(std::string("  >>> SHOW DEVICE"));
	putLine(std::string("  >>> SET bootdef_dev dka0"));
	putLine(std::string("  >>> BOOT dka0"));
	putLine(std::string(""));
}



void SRMConsole::cmdShowConfig(const QStringList& args) noexcept
{
	Q_UNUSED(args);

	putLine(std::string(""));
	putLine(std::string("System Configuration:"));
	putLine(std::string(""));

	// CPU Information
	putString("  Processor: ");
	putLine(formatCPUInfo());
	putString("  CPU Count: ");
	putLine(QString::number(m_settings.system.cpuCount));
	// TODO //putString("  CPU Frequency: ");
	//putLine(QString("%1 MHz").arg(m_settings.system.cpuFrequencyHz / 1000000));

	// Memory Information
	putString("  Memory Size: ");
	putLine(formatMemoryInfo());
	// TODO //putString("  Page Size: ");
	//putLine(QString("%1 bytes").arg(m_settings.system.ptePageSize));

	// System Information
	putString("  Hardware Model: ");
	putLine(QString(m_settings.system.model.c_str()).isEmpty() ? "AlphaStation Emulator" : m_settings.system.model);
	// TODO //putString("  Serial Number: ");
	//putLine(m_settings.system.hwSerialNumber.isEmpty() ? "EMU0000000001" : m_settings.podData.system.hwSerialNumber);

	// Boot Configuration
	putString("  Default Boot Device: ");
	putLine(m_envStore.get("bootdef_dev"));
	putString("  Boot OS Flags: ");
	putLine(m_envStore.get("boot_osflags"));
	putString("  Console Type: ");
	putLine(m_envStore.get("console"));

	putLine(std::string(""));
}

void SRMConsole::cmdShowAll(const QStringList& args) noexcept
{
	Q_UNUSED(args);

	putLine(std::string(""));
	putLine(std::string("Environment Variables:"));
	putLine(std::string(""));

	QStringList names = m_envStore.getAllNames();
	names.sort();

	for (const QString& name : names) {
		QString value = m_envStore.get(name);
		putLine(QString("  %1 = %2").arg(name, -20).arg(value));
	}

	putLine(std::string(""));
}

void SRMConsole::cmdShowVar(const QString& varName) noexcept
{
	QString value = m_envStore.get(varName);

	if (value.isEmpty() && !m_envStore.exists(varName)) {
		printError(QString("%%SRM-E-NOVAR, Variable not found: %1").arg(varName));
	}
	else {
		putLine(QString("%1 = %2").arg(varName).arg(value));
	}
}

void SRMConsole::cmdSet(const QStringList& args) noexcept
{
	if (args.size() < 3) {
		printError("SET requires variable name and value");
		putLine(std::string("Usage: SET <variable> <value>"));
		return;
	}

	QString varName = args[1].toLower();
	QString value = args[2];

	// Join remaining arguments for multi-word values
	for (int i = 3; i < args.size(); ++i) {
		value += " " + args[i];
	}

	m_envStore.set(varName, value);

	putLine(QString("%1 = %2").arg(varName).arg(value));
}

void SRMConsole::cmdBoot(const QStringList& args) noexcept
{

	QString deviceName;
	QString flags;

	if (args.size() > 1) {
		deviceName = args[1];
	}
	else {
		deviceName = m_envStore.get("bootdef_dev");
	}

	if (args.size() > 2) {
		flags = args[2];
	}
	else {
		flags = m_envStore.get("boot_osflags");
	}

	if (deviceName.isEmpty()) {
		printError("No boot device specified and bootdef_dev not set");
		return;
	}

	//TODO DeviceConfig not yet implemented

	const emulatr::config::DeviceConfig* device = resolveBootDevice(deviceName);
	if (!device) {
		printError(QString("%%SRM-E-NODEV, Boot device not found: %1").arg(deviceName));
		return;
	}

	putLine(QString("Booting from %1...").arg(deviceName));
	initiateBootSequence(*device, flags);

}

void SRMConsole::cmdHalt(const QStringList& args) noexcept
{
	Q_UNUSED(args);

	putLine(std::string("System halted."));
	m_running = false;
}

void SRMConsole::cmdContinue(const QStringList& args) noexcept
{
	Q_UNUSED(args)

	putLine(std::string("Continuing execution..."));
	m_running = false;  // Exit console to resume normal execution
}

void SRMConsole::cmdReset(const QStringList& args) noexcept
{
	Q_UNUSED(args);

	putLine(std::string("System reset requested."));
	// Implementation would trigger system reset
	m_running = false;
}

// ============================================================================
// Device Enumeration
// ============================================================================

void SRMConsole::generateDeviceListing() noexcept
{

	m_deviceList.clear();
	m_deviceAliases.clear();

	int diskIndex = 0;
	int netIndex = 0;

	// Process regular devices
	for (auto it = m_settings.devices.begin(); it != m_settings.devices.end(); ++it) {
		const QString&                       deviceName   = it.key();
		const emulatr::config::DeviceConfig& deviceConfig = it.value();

		QString srmName = mapToSRMName(deviceName, deviceConfig);
		QString description = formatDeviceDescription(deviceConfig);
		QString path = formatDevicePath(deviceConfig);

		// Store alias mapping
		m_deviceAliases[srmName] = deviceName;

		// Format display line
		QString line = QString("%-19s %-30s %1")
			.arg(srmName)
			.arg(path)
			.arg(description);

		m_deviceList.append(line);
	}

	// Process controllers
	for (auto it = m_settings.controllers.begin(); it != m_settings.controllers.end(); ++it) {
		const QString&                           controllerName   = it.key();
		const emulatr::config::ControllerConfig& controllerConfig = it.value();

		QString srmName = controllerName.toLower();
		QString description = QString("%1 Controller").arg(controllerConfig.classType);
		QString path = QString("pci/bus0/slot%1").arg(controllerName.right(1));

		QString line = QString("%-19s %-30s %1")
			.arg(srmName)
			.arg(path)
			.arg(description);

		m_deviceList.append(line);
	}

}





QString SRMConsole::formatDeviceDescription(const emulatr::config::DeviceConfig& deviceConfig) noexcept
{
	
	QString type = deviceConfig.fields.value("class", "Unknown").toString();
	QString model = deviceConfig.fields.value("model", "").toString();

	if (!model.isEmpty()) {
		return QString("%1 - %2").arg(type).arg(model);
	}
	else {
		return type;
	}

}

QString SRMConsole::formatDevicePath(const emulatr::config::DeviceConfig& deviceConfig) noexcept
{
	QString parent = deviceConfig.fields.value("parent", "").toString();
	QString location = deviceConfig.fields.value("location", "").toString();

	if (!location.isEmpty()) {
		return location;
	}
	else if (!parent.isEmpty()) {
		return QString("pci/%1").arg(parent);
	}
	else {
		return "unknown";
	}
}


// ============================================================================
// System Information
// ============================================================================
// TODO not implemented formatCPUInfo
/* QString SRMConsole::formatCPUInfo() noexcept
{
	quint64 frequency = m_settings.system.cpuFrequencyHz;
	return QString("EV6 21264 at %1 MHz").arg(frequency / 1000000);
}
*/

QString SRMConsole::formatMemoryInfo() const noexcept
{
	const uint64_t memorySizeBytes = m_settings.system.memorySizeBytes;
	if (memorySizeBytes > 0) {
		if (memorySizeBytes >= 1) {
			return QString("%1 MB").arg(memorySizeBytes/1024);
		}
	}
	return  QString("Not Implemented");
}

QString SRMConsole::formatSystemInfo() const noexcept
{
	return QString("EmulatR V4 - %1-CPU System")
		.arg(m_settings.system.cpuCount);
}

// ============================================================================
// Boot Processing
// ============================================================================

const emulatr::config::DeviceConfig* SRMConsole::resolveBootDevice(const QString& deviceName) const noexcept
{
	// Check device aliases first
	QString actualName = m_deviceAliases.value(deviceName, deviceName);

	// Look up in device configuration
	auto it = m_settings.devices.find(actualName);
	if (it != m_settings.devices.end()) {
		return &it.value();
	}

	return nullptr;
}


void SRMConsole::initiateBootSequence(const emulatr::config::DeviceConfig& device, const QString& flags) noexcept
{
	Q_UNUSED(flags);

	putLine(QString("Loading bootstrap from %1...").arg(device.name));
	putLine(std::string("Bootstrap load complete."));
	putLine(std::string("Jumping to bootstrap code..."));
	putLine(std::string(""));
	putLine(std::string("(Boot sequence implementation in progress)"));

	// For now, just print status and return to prompt
	// Real implementation would transfer control to boot loader
}


// ============================================================================
// Utility Methods
// ============================================================================

void SRMConsole::printError(const QString& message) noexcept
{
	putLine(message);
}

bool SRMConsole::matchesPattern(const QString& str, const QString& pattern) noexcept
{
	return str.contains(pattern, Qt::CaseInsensitive);
}

/**
 * @brief Get next available device index for given prefix
 * @param prefix Device prefix (dka, ewa, mka, etc.)
 * @return Next available index number
 */
int SRMConsole::getNextIndex(const QString& prefix) const noexcept
{
	// Check existing aliases to find highest used index
	int maxIndex = -1;

	QStringList allNames = m_envStore.getAllNames();
	QString searchKey = "device_alias_";

	for (const QString& name : allNames) {
		if (!name.startsWith(searchKey)) continue;

		QString alias = m_envStore.get(name);
		if (!alias.startsWith(prefix)) continue;

		// Extract index from alias (e.g., "dka2" -> 2)
		QString indexStr = alias.mid(prefix.length());
		bool ok;
		int index = indexStr.toInt(&ok);
		if (ok && index > maxIndex) {
			maxIndex = index;
		}
	}

	return maxIndex + 1;  // Next available index
}

/**
 * @brief Enhanced device name mapping with official naming policy
 * @param deviceName Original device name from configuration
 * @param deviceConfig Device configuration data
 * @return Stable SRM device name
 */
QString SRMConsole::mapToSRMName(const QString& deviceName, const emulatr::config::DeviceConfig& config) const noexcept {
	QString aliasKey = QString("device_alias_%1").arg(deviceName);
	QString existing = m_envStore.get(aliasKey);
	if (!existing.isEmpty()) return existing;

	// Official naming policy
	QString prefix;
	if (config.fields.value("transport") == "scsi") {
		prefix = config.fields.value("media") == "tape" ? "mka" : "dka";
	}
	else if (config.fields.value("transport") == "fc") {
		prefix = "dga";
	}
	else if (config.fields.value("class") == "ethernet") {
		prefix = "ewa";
	}
	else if (config.fields.value("class") == "fddi") {
		prefix = "fwa";
	}
	else if (config.fields.value("class") == "console") {
		prefix = "opa";
	}

	QString srmName = QString("%1%2").arg(prefix).arg(getNextIndex(prefix));
	m_envStore.set(aliasKey, srmName);  //  Persistent
	return srmName;
}

/**
 * @brief Determine SRM prefix based on device configuration
 * @param deviceConfig Device configuration
 * @return SRM prefix (dka, ewa, mka, etc.) or empty string if unknown
 */
QString SRMConsole::determineSRMPrefix(const emulatr::config::DeviceConfig& deviceConfig) noexcept
{
	QString transport = deviceConfig.fields.value("transport", "").toString().toLower();
	QString deviceClass = deviceConfig.fields.value("class", "").toString().toLower();
	QString media = deviceConfig.fields.value("media", "").toString().toLower();
	QString controllerType = deviceConfig.fields.value("controller_type", "").toString().toLower();

	// ========================================================================
	// OFFICIAL ASA EmulatR SRM Device Naming Policy
	// ========================================================================

	// SCSI Devices
	if (transport == "scsi" || controllerType == "scsi") {
		if (media == "tape" || deviceClass == "tape") {
			return "mka";  // SCSI tape
		}
		else if (deviceClass == "controller" || controllerType.contains("controller")) {
			return "pka";  // SCSI controller
		}
		else {
			return "dka";  // SCSI disk (default for SCSI)
		}
	}

	// Fibre Channel Devices  
	if (transport == "fc" || transport == "fibre_channel") {
		if (deviceClass == "controller" || controllerType.contains("controller")) {
			return "gga";  // FC controller
		}
		else {
			return "dga";  // Fibre Channel disk
		}
	}

	// Network Devices
	if (transport == "ethernet" || deviceClass == "ethernet" || deviceClass == "network") {
		return "ewa";  // Ethernet
	}

	if (transport == "fddi" || deviceClass == "fddi") {
		return "fwa";  // FDDI
	}

	// Console Devices
	if (deviceClass == "console" || deviceClass == "uart" || deviceClass == "terminal") {
		return "opa";  // Console
	}

	// IDE/ATA Devices (future expansion)
	if (transport == "ide" || transport == "ata" || transport == "pata" || transport == "sata") {
		return "dka";  // Map IDE to SCSI naming for compatibility
	}

	// USB Devices (future expansion)
	if (transport == "usb") {
		if (media == "tape") {
			return "mka";  // USB tape
		}
		else {
			return "dka";  // USB disk
		}
	}

	// Unknown device type
	return {};
}


/**
 * @brief Get device statistics for SHOW CONFIG
 * @return Formatted device statistics
 */
QString SRMConsole::getDeviceStatistics() const noexcept
{
	QMap<QString, int> prefixCounts;
	QStringList allNames = m_envStore.getAllNames();

	// Count devices by prefix
	for (const QString& name : allNames) {
		if (!name.startsWith("device_alias_")) continue;

		QString alias = m_envStore.get(name);
		if (alias.length() >= 3) {
			QString prefix = alias.left(3);
			prefixCounts[prefix]++;
		}
	}

	QStringList stats;
	if (prefixCounts["dka"] > 0) stats << QString("%1 SCSI disk(s)").arg(prefixCounts["dka"]);
	if (prefixCounts["mka"] > 0) stats << QString("%1 SCSI tape(s)").arg(prefixCounts["mka"]);
	if (prefixCounts["dga"] > 0) stats << QString("%1 FC disk(s)").arg(prefixCounts["dga"]);
	if (prefixCounts["ewa"] > 0) stats << QString("%1 Ethernet port(s)").arg(prefixCounts["ewa"]);
	if (prefixCounts["fwa"] > 0) stats << QString("%1 FDDI port(s)").arg(prefixCounts["fwa"]);
	if (prefixCounts["pka"] > 0) stats << QString("%1 SCSI controller(s)").arg(prefixCounts["pka"]);
	if (prefixCounts["gga"] > 0) stats << QString("%1 FC controller(s)").arg(prefixCounts["gga"]);
	if (prefixCounts["opa"] > 0) stats << QString("%1 console port(s)").arg(prefixCounts["opa"]);

	return stats.join(", ");
}

/**
 * @brief Validate SRM device name format
 * @param srmName SRM device name to validate
 * @return true if valid format
 */
bool SRMConsole::isValidSRMName(const QString& srmName) noexcept
{
	if (srmName.length() < 4) return false;

	QString prefix = srmName.left(3);
	QString indexStr = srmName.mid(3);

	// Check prefix validity
	QStringList validPrefixes = { "dka", "mka", "dga", "ewa", "fwa", "opa", "pka", "gga" };
	if (!validPrefixes.contains(prefix)) return false;

	// Check index validity
	bool ok;
	int index = indexStr.toInt(&ok);
	return ok && index >= 0 && index < 100;  // Reasonable range
}

// ============================================================================
// Enhanced SHOW DEVICE command with device statistics
// ============================================================================







/**
 * @brief RELOAD CONFIG - Runtime configuration reload
 * @param args Command arguments (should be ["reload", "config"])
 // TODO cmdReloadConfig not yet implemented

void SRMConsole::cmdReloadConfig(const QStringList& args) noexcept
{
	Q_UNUSED(args);

	putLine("Reloading device configuration...");

	// ========================================================================
	// 1. Backup current state for comparison
	// ========================================================================
	QMap<QString, QString> oldDeviceAliases = m_deviceAliases;
	QStringList oldDeviceList = m_deviceList;

	// ========================================================================
	// 2. Re-read configuration file
	// ========================================================================
	EmulatorSettings newSettings;
	QString configPath = getConfigurationPath();
	QString configFile = configPath + "/ASAEmulatr.ini";

	if (!QFile::exists(configFile)) {
		printError("Configuration file not found: " + configFile);
		return;
	}

	if (!newSettings.loadFromIni(configFile)) {
		printError("Failed to parse configuration file");
		return;
	}

	putLine(QString("Configuration loaded from: %1").arg(configFile));

	// ========================================================================
	// 3. Preserve existing aliases for stability
	// ========================================================================
	QSet<QString> existingDevices;
	QSet<QString> newDevices;
	QSet<QString> removedDevices;

	// Identify what changed
	for (auto it = newSettings.podData.devices.begin(); it != newSettings.podData.devices.end(); ++it) {
		QString deviceName = it.key();
		if (m_settings.podData.devices.contains(deviceName)) {
			existingDevices.insert(deviceName);
		}
		else {
			newDevices.insert(deviceName);
		}
	}

	for (auto it = m_settings.podData.devices.begin(); it != m_settings.podData.devices.end(); ++it) {
		QString deviceName = it.key();
		if (!newSettings.podData.devices.contains(deviceName)) {
			removedDevices.insert(deviceName);
		}
	}

	// ========================================================================
	// 4. Update settings reference (careful: we're modifying the reference)
	// ========================================================================

	// Note: In real implementation, you'd need to update m_settings safely
	// For now, we'll work with the new settings data

	// ========================================================================
	// 5. Process device changes with alias preservation
	// ========================================================================

	processDeviceChanges(newSettings.podData, newDevices, removedDevices, existingDevices);

	// ========================================================================
	// 6. Regenerate device listing with updated configuration
	// ========================================================================

	regenerateDeviceListingWithNewConfig(newSettings.podData);

	// ========================================================================
	// 7. Report results
	// ========================================================================

	reportConfigurationChanges(newDevices, removedDevices, existingDevices);

	putLine("Device configuration reload complete.");
}

 */

/**
 * @brief Process device changes while preserving existing aliases

// TODO processDeviceChanges not yet implemented
void SRMConsole::processDeviceChanges(
	const emulatr::config::EmulatorSettings& newSettings,
	const QSet<QString>& newDevices,
	const QSet<QString>& removedDevices,
	const QSet<QString>& existingDevices) noexcept
{
	// ========================================================================
	// Handle new devices: assign fresh SRM names
	// ========================================================================

	for (const QString& deviceName : newDevices) {
		const DeviceConfig& deviceConfig = newSettings.devices[deviceName];

		// Generate new SRM name (will persist automatically)
		QString srmName = mapToSRMName(deviceName, deviceConfig);

		// Update local alias mapping
		m_deviceAliases[srmName] = deviceName;

		SPDLOG_TRACE("New device added: {} -> %2", deviceName, srmName.toStdString());
	}

	// ========================================================================
	// Handle removed devices: mark as offline but preserve aliases
	// ========================================================================

	for (const QString& deviceName : removedDevices) {
		// Find the SRM name for this device
		QString srmName;
		for (auto it = m_deviceAliases.begin(); it != m_deviceAliases.end(); ++it) {
			if (it.value() == deviceName) {
				srmName = it.key();
				break;
			}
		}

		if (!srmName.isEmpty()) {
			// Mark as offline in environment store for future reference
			QString offlineKey = QString("device_offline_%1").arg(srmName);
			m_envStore.set(offlineKey, "true");

			SPDLOG_TRACE("Device marked offline: {} (%2)", deviceName, srmName.toStdString());
		}
	}

	// ========================================================================
	// Existing devices: no alias changes (stability preserved)
	// ========================================================================

	SPDLOG_TRACE("Existing devices unchanged: {}", existingDevices.size());
}
	 */
/**
 * @brief Regenerate device listing with new configuration
 // TODO regenerateDeviceListingWithNewConfig not yet implemented
void SRMConsole::regenerateDeviceListingWithNewConfig(const emulatr::config::EmulatorSettings& newSettings) noexcept
{
	m_deviceList.clear();

	// Process active devices only
	for (auto it = newSettings.devices.begin(); it != newSettings.devices.end(); ++it) {
		const QString& deviceName = it.key();
		const DeviceConfig& deviceConfig = it.value();

		// Get SRM name (should exist from previous processing or existing alias)
		QString srmName;
		for (auto aliasIt = m_deviceAliases.begin(); aliasIt != m_deviceAliases.end(); ++aliasIt) {
			if (aliasIt.value() == deviceName) {
				srmName = aliasIt.key();
				break;
			}
		}

		if (srmName.isEmpty()) {
			// This shouldn't happen, but handle gracefully
			srmName = mapToSRMName(deviceName, deviceConfig);
			m_deviceAliases[srmName] = deviceName;
		}

		// Generate device listing line
		QString description = formatDeviceDescription(deviceConfig);
		QString path = formatDevicePath(deviceConfig);

		QString line = QString("%-19s %-30s %1")
			.arg(srmName)
			.arg(path)
			.arg(description);

		m_deviceList.append(line);
	}

	// Process controllers
	for (auto it = newSettings.controllers.begin(); it != newSettings.controllers.end(); ++it) {
		const QString& controllerName = it.key();
		const ControllerConfig& controllerConfig = it.value();

		QString srmName = controllerName.toLower();
		QString description = QString("%1 Controller").arg(controllerConfig.classType);
		QString path = QString("pci/bus0/slot%1").arg(controllerName.right(1));

		QString line = QString("%-19s %-30s %1")
			.arg(srmName)
			.arg(path)
			.arg(description);

		m_deviceList.append(line);
	}
}
 */
/**
 * @brief Report configuration changes to user
 */
void SRMConsole::reportConfigurationChanges(
	const QSet<QString>& newDevices,
	const QSet<QString>& removedDevices,
	const QSet<QString>& existingDevices) noexcept
{
	putLine(std::string(""));

	if (!newDevices.isEmpty()) {
		putString("New devices found: ");
		putLine(QString::number(newDevices.size()));  // 
		for (const QString& device : newDevices) {
			// Find SRM name for this device
			QString srmName;
			for (auto it = m_deviceAliases.begin(); it != m_deviceAliases.end(); ++it) {
				if (it.value() == device) {
					srmName = it.key();
					break;
				}
			}
			putLine(QString("  %1 -> %2").arg(device, srmName));
		}
		putLine(std::string(""));
	}

	if (!removedDevices.isEmpty()) {
		putString("Devices removed: ");
		putLine(QString::number(removedDevices.size()));  // 
	}



	putLine(std::string("Use SHOW DEVICE to see updated device listing."));
}

/**
 * @brief Get configuration file path
 * @return Path to configuration directory
 */
QString SRMConsole::getConfigurationPath() const noexcept
{
	// Try to get from environment store first
	QString configPath = m_envStore.get("config_path");

	if (configPath.isEmpty()) {
		// Fallback to default or derive from current settings
		configPath = ".";  // Current directory fallback

		// You might want to store the config path when SRM console is initialized
		// m_envStore.set("config_path", actualConfigPath);
	}

	return configPath;
}

/**
 * @brief Enhanced SHOW DEVICE with offline device indication
 */
void SRMConsole::cmdShowDeviceWithOffline(const QStringList& args) noexcept
{
	// Call normal SHOW DEVICE first
	cmdShowDevice(args);

	// Check for offline devices
	QStringList offlineDevices;
	QStringList allNames = m_envStore.getAllNames();

	for (const QString& name : allNames) {
		if (name.startsWith("device_offline_")) {
			QString value = m_envStore.get(name);
			if (value == "true") {
				QString deviceName = name.mid(QString("device_offline_").length());
				offlineDevices.append(deviceName);
			}
		}
	}

	if (!offlineDevices.isEmpty()) {
		putLine(std::string("Offline Devices (removed from configuration):"));
		putLine(std::string("-------------------------------------------"));
		for (const QString& device : offlineDevices) {
			putLine(QString("  %1 (offline)").arg(device));
		}
		putLine(std::string(""));
		putLine(std::string("std::string(Use RELOAD CONFIG to refresh device configuration."));
	}
}

void SRMConsole::cmdShowDevice(const QStringList& args) noexcept
{
	Q_UNUSED(args);

	putLine(std::string(""));
	putLine(std::string("Device Name         Device Path                    Description"));
	putLine(std::string("------------------- ------------------------------ ------------------------------"));

	// Show devices in SRM order (controllers first, then devices)
	QStringList controllers, disks, tapes, network, consoles;

	for (const QString& deviceLine : m_deviceList) {
		QString prefix = deviceLine.left(3);
		if (prefix == "pka" || prefix == "gga") {
			controllers.append(deviceLine);
		}
		else if (prefix == "dka" || prefix == "dga") {
			disks.append(deviceLine);
		}
		else if (prefix == "mka") {
			tapes.append(deviceLine);
		}
		else if (prefix == "ewa" || prefix == "fwa") {
			network.append(deviceLine);
		}
		else if (prefix == "opa") {
			consoles.append(deviceLine);
		}
	}

	// Output in logical order
	for (const QString& line : controllers) putLine(line);
	for (const QString& line : disks) putLine(line);
	for (const QString& line : tapes) putLine(line);
	for (const QString& line : network) putLine(line);
	for (const QString& line : consoles) putLine(line);
	
	putLine(std::string(""));
	putString("Device Summary: ");
	putLine(getDeviceStatistics());
	putLine(std::string(""));
}