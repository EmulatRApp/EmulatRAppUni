// ============================================================================
// FirmwareDeviceManager.h - Phase 0: Firmware Context Initialization
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

// ============================================================================
// FirmwareDeviceManager.h (UPDATED - Uses Global Singleton Pattern)
// ============================================================================
// Purpose: SRM Device Tree Manager (as per DEVICE_TREE_INITIALIZATION_SPEC)
//
// This implements the 5-phase device tree initialization:
// - Phase 0: Firmware Context Initialization
// - Phase 1: Platform Root Creation
// - Phase 2: Bus Discovery and Attachment
// - Phase 3: Device Enumeration and Registration
// - Phase 4: Device Finalization and Console Exposure
//
// Integration with ASAEmulatR.ini:
// - Reads device configurations from EmulatorSettings
// - Creates SRM-style device tree (OPA0, DKA0, PKB0, EWA0, etc.)
// - Exposes devices to console (SHOW DEVICE command)
//
// Access Pattern:
// - Use global_FirmwareDeviceManager() to access singleton
// - Use initializeGlobalFirmwareDeviceManager() to initialize
// ============================================================================

#ifndef FIRMWAREDEVICEMANAGER_H
#define FIRMWAREDEVICEMANAGER_H

#include <QString>
#include <QMap>
#include <QVector>
#include <QSharedPointer>
#include <algorithm>
#include <cstdint>
#include <vector>
#include "config/EmulatorSettings.h"
#include "coreLib/LoggingMacros.h"
#include "deviceLib/Hwrpb.h"
#include "deviceLib/HwrpbBuilder.h"
#include "deviceLib/OPA_Console_Config.h"
#include "deviceLib/ControllerConfig.h"

// ============================================================================
// Device Tree Node Types
// ============================================================================

enum class DeviceNodeType : quint8
{
    PlatformRoot,       // Top-level platform node
    SystemBus,          // System bus
    PCIBus,             // PCI root bus
    ConsoleBus,         // Console I/O bus
    VirtualBus,         // Virtual/firmware bus
    SCSIController,     // SCSI HBA (PKB0, PKC0)
    SCSIDisk,           // SCSI disk (DKA0, DKA1, DKB1)
    SCSITape,           // SCSI tape (MKA600)
    IDEController,      // IDE controller (PQA0)
    IDEDisk,            // IDE disk (DQA0)
    NetworkInterface,   // Ethernet (EWA0)
    ConsoleTerminal,    // UART console (OPA0, OPA1)
    Unknown
};

// ============================================================================
// Device Tree Node
// ============================================================================

struct DeviceNode
{
    // Core properties
    QString name;                       // "OPA0", "PKB0", "DKA0"
    DeviceNodeType deviceNodeType;
    QString busName;                    // Parent bus
    quint32 unit;                       // Unit number
    bool enabled{ true };
    
    // Hardware resources
    uint64_t mmioBase{ 0 };
    uint64_t mmioSize{ 0 };
    QString irqStr;                     // "auto" or "0x300"
    quint32 irqIpl{ 20 };
    
    // SRM-specific
    QString location;                   // "cab0/drw0/io0/hose0/bus3/slot1"
    QString classType;                  // "SCSI_HBA", "SCSI_DISK", "NIC"
    
    // Configuration (from ASAEmulatR.ini)
    QMap<QString, QVariant> properties;
    
    // Relationships
    QVector<QString> children;          // Child device names
    QString parent;                     // Parent device name
};

// ============================================================================
// Firmware Device Manager
// ============================================================================
// Access via global_FirmwareDeviceManager() function (defined in global_*.h)
// ============================================================================

class FirmwareDeviceManager
{
public:
    // ========================================================================
    // 5-Phase Initialization (SRM Device Tree Spec)
    // ========================================================================
    
    /**
     * @brief Phase 0: Firmware Context Initialization
     * Initialize Firmware Device Manager, SystemOwner=SRM, memory maps
     */
    bool initializePhase0_FirmwareContext(const emulatr::config::EmulatorSettings& config)
    {
        EMULATR_LOG_INFO("Device Tree Phase 0: Firmware Context Initialization");

        m_config = config;
        m_initialized = false;
        m_nodes.clear();
        m_hwrpbBuffer.clear();
        m_hwrpbBufferSize = 0;

        // Build the HWRPB image (firmware-OS data contract) into an
        // internal buffer.  The owner of GuestMemory deploys it via
        //   memcpy(guestMem.basePtr() + fdm.hwrpbPa(),
        //          fdm.hwrpbImage(), fdm.hwrpbImageSize())
        // when guest memory is available -- typically right before CPU
        // reset.  See deviceLib/Hwrpb.h and HwrpbBuilder.h for the
        // structure layout (AARM Section 26.1).
        if (!buildHwrpbImage()) {
            EMULATR_LOG_ERROR("Phase 0: HWRPB image build failed");
            return false;
        }

        EMULATR_LOG_INFO("Firmware context initialized; HWRPB image: {} bytes",
                         m_hwrpbBufferSize);
        return true;
    }

    /**
     * @brief Get the populated HWRPB image (Phase 0 output).
     * @return Pointer to the byte buffer; size given by hwrpbImageSize().
     *         Returns nullptr if Phase 0 has not produced an image yet.
     *
     * The caller is expected to copy these bytes into guest physical
     * memory at the address given by hwrpbPa().  This indirection keeps
     * FirmwareDeviceManager free of any GuestMemory dependency.
     */
    [[nodiscard]] uint8_t const* hwrpbImage() const noexcept
    {
        return m_hwrpbBufferSize > 0 ? m_hwrpbBuffer.data() : nullptr;
    }

    /**
     * @brief Size of the populated HWRPB image in bytes.  Zero if none.
     */
    [[nodiscard]] std::size_t hwrpbImageSize() const noexcept
    {
        return m_hwrpbBufferSize;
    }

    /**
     * @brief Guest physical address where the HWRPB image should be
     *        deployed.  Currently fixed at PA 0 (Tsunami convention).
     */
    [[nodiscard]] std::uint64_t hwrpbPa() const noexcept
    {
        return m_hwrpbPa;
    }
    
    /**
     * @brief Phase 1: Platform Root Creation
     * Create immutable platform root node
     */
    bool initializePhase1_PlatformRoot()
    {
        EMULATR_LOG_INFO("Device Tree Phase 1: Platform Root Creation");
        
        DeviceNode root;
        root.name = "platform";
        root.deviceNodeType = DeviceNodeType::PlatformRoot;
        root.enabled = true;
        
        // Platform properties from config
        root.properties["platform.name"] = "AlphaServer";
        root.properties["platform.model"] = QVariant(QString(m_config.system.model.c_str()));
        root.properties["platform.cpu.count"] = m_config.system.cpuCount;
        root.properties["platform.memory.size"] = m_config.system.memorySizeBytes;
        root.properties["platform.firmware.version"] = "1.0.0";
        
        m_nodes["platform"] = root;
        
        SPDLOG_INFO("Platform root created: {}, {} CPUs, {} GB RAM",m_config.system.model,m_config.system.cpuCount,m_config.system.memorySizeBytes);
        
        return true;
    }
    
    /**
     * @brief Phase 2: Bus Discovery and Attachment
     * Create bus nodes (SystemBus, PCI, Console, Virtual)
     */
    bool initializePhase2_BusDiscovery()
    {
        EMULATR_LOG_INFO("Device Tree Phase 2: Bus Discovery");
        
        // System Bus
        createBusNode("systembus", DeviceNodeType::SystemBus, "platform");
        
        // PCI Root Bus
        createBusNode("pci0", DeviceNodeType::PCIBus, "systembus");
        
        // Console Bus
        createBusNode("consolebus", DeviceNodeType::ConsoleBus, "systembus");
        
        // Virtual Bus (for firmware services)
        createBusNode("virtualbus", DeviceNodeType::VirtualBus, "systembus");
        
        EMULATR_LOG_INFO("Bus discovery complete (4 buses)");
        return true;
    }
    
    /**
     * @brief Phase 3: Device Enumeration and Registration
     * Read ASAEmulatR.ini and create device nodes
     */
    /* 
     * // TODO DeviceEnumeration Not Hooked
    bool initializePhase3_DeviceEnumeration()
    {
        SPDLOG_INFO("Device Tree Phase 3: Device Enumeration");
        
        int deviceCount = 0;
        
        // Enumerate OPA consoles
        for (auto it = m_config.opaConsoles.begin(); 
             it != m_config.opaConsoles.end(); ++it)
        {
            registerConsoleDevice(it.key(), it.value());
            deviceCount++;
        }
        
        // Enumerate controllers (PKB0, PKC0, PQA0, EWA0)
        for (auto it = m_config.controllers.begin();
             it != m_config.controllers.end(); ++it)
        {
            registerController(it.key(), it.value());
            deviceCount++;
        }
        
        // Enumerate devices (DKA0, DKA1, DKB1, DQA0, MKA600)
        for (auto it = m_config.devices.begin();
             it != m_config.devices.end(); ++it)
        {
            registerDevice(it.key(), it.value());
            deviceCount++;
        }
        
        SPDLOG_INFO("Device enumeration complete: {} devices", (deviceCount));
        return true;
    }
	*/
    
    /**
     * @brief Phase 4: Device Finalization and Console Exposure
     * Validate, bind console services, mark firmware-ready
     */
    bool initializePhase4_Finalization()
    {
        SPDLOG_INFO("Device Tree Phase 4: Finalization");
        
        // Validate address space (no overlaps)
        if (!validateAddressSpace()) {
            SPDLOG_ERROR("Address space validation failed");
            return false;
        }
        
        // Bind console services to OPA devices
        bindConsoleServices();
        
        // Mark all devices firmware-ready
        for (auto& node : m_nodes) {
            node.properties["firmware.ready"] = true;
        }
        
        m_initialized = true;
        
        SPDLOG_INFO("Device tree finalized and exposed to console");
        dumpDeviceTree();
        
        return true;
    }

    // ========================================================================
    // Query Interface (for SHOW DEVICE, SHOW CONFIG commands)
    // ========================================================================
    
    /**
     * @brief Get device node by name
     */
    const DeviceNode* getDevice(const QString& name) const
    {
        auto it = m_nodes.find(name);
        return (it != m_nodes.end()) ? &it.value() : nullptr;
    }
    
    /**
     * @brief Get all devices of a specific type
     */
    QVector<QString> getDevicesByType(DeviceNodeType type) const
    {
        QVector<QString> result;
        for (auto it = m_nodes.begin(); it != m_nodes.end(); ++it) {
            if (it.value().deviceNodeType == type) {
                result.append(it.key());
            }
        }
        return result;
    }
    
    /**
     * @brief Get all device names (for SHOW DEVICE)
     */
    QVector<QString> getAllDeviceNames() const
    {
        QVector<QString> result;
        for (auto it = m_nodes.begin(); it != m_nodes.end(); ++it) {
            // Skip buses and platform root
            if (it.value().deviceNodeType != DeviceNodeType::PlatformRoot &&
                it.value().deviceNodeType != DeviceNodeType::SystemBus &&
                it.value().deviceNodeType != DeviceNodeType::PCIBus &&
                it.value().deviceNodeType != DeviceNodeType::ConsoleBus &&
                it.value().deviceNodeType != DeviceNodeType::VirtualBus)
            {
                result.append(it.key());
            }
        }
        return result;
    }
    
    /**
     * @brief Check if device tree is initialized
     */
    bool isInitialized() const noexcept
    {
        return m_initialized;
    }
    
    /**
     * @brief Dump device tree to log (for debugging)
     */
	void dumpDeviceTree() const
	{
		try {
            EMULATR_LOG_INFO("\n{}", std::string(70, '='));
            EMULATR_LOG_INFO("SRM DEVICE TREE");
            EMULATR_LOG_INFO("{}", std::string(70, '='));

			int deviceCount = 0;
			for (auto it = m_nodes.begin(); it != m_nodes.end(); ++it) {
				const auto& node = it.value();

				// Skip buses and platform
				if (node.deviceNodeType == DeviceNodeType::PlatformRoot ||
					node.deviceNodeType == DeviceNodeType::SystemBus ||
					node.deviceNodeType == DeviceNodeType::PCIBus ||
					node.deviceNodeType == DeviceNodeType::ConsoleBus ||
					node.deviceNodeType == DeviceNodeType::VirtualBus) {
					continue;
				}

				QString typeStr = node.classType.isEmpty()
					? deviceTypeToString(node.deviceNodeType)
					: node.classType;

                EMULATR_LOG_INFO("  {}: {} ({})", node.name.toStdString(), typeStr.toStdString(), node.enabled ? "Online" : "Offline");

				deviceCount++;

				// Safety limit
				if (deviceCount > 100) {
					SPDLOG_ERROR("Device tree dump exceeded 100 devices - stopping");
					break;
				}
			}

            EMULATR_LOG_INFO("70, '=' \n");
            EMULATR_LOG_INFO("Total devices displayed: {}",(deviceCount));

		}
		catch (const std::exception& e) {
            EMULATR_LOG_ERROR("Exception in dumpDeviceTree: {}",(e.what()));
		}
		catch (...) {
            EMULATR_LOG_ERROR("Unknown exception in dumpDeviceTree");
		}
	}

    // ========================================================================
    // Allow global accessor to construct singleton
    // ========================================================================
    friend FirmwareDeviceManager& global_FirmwareDeviceManager() noexcept;

private:
    // Private constructor for Meyer's singleton
    FirmwareDeviceManager() = default;
    ~FirmwareDeviceManager() = default;
    
    FirmwareDeviceManager(const FirmwareDeviceManager&) = delete;
    FirmwareDeviceManager& operator=(const FirmwareDeviceManager&) = delete;

    // ========================================================================
    // Device Registration Helpers
    // ========================================================================
    
    void createBusNode(const QString& name, DeviceNodeType type, const QString& parent)
    {
        DeviceNode bus;
        bus.name = name;
        bus.deviceNodeType = type;
        bus.parent = parent;
        bus.enabled = true;
        
        m_nodes[name] = bus;
        
        // Add to parent's children
        if (m_nodes.contains(parent)) {
            m_nodes[parent].children.append(name);
        }
    }
    
	void registerConsoleDevice(const QString& name, const OPAConfig& config)
	{
		DeviceNode node;
		node.name = name;
		node.deviceNodeType = DeviceNodeType::ConsoleTerminal;
		node.busName = "consolebus";
		node.unit = name.mid(3).toUInt();  // "OPA0" -> 0
		node.enabled = true;
		node.location = config.location;

		// Set classType explicitly
		node.classType = "UART";

		// Copy config properties
		node.properties["iface"] = config.iface;
		node.properties["iface_port"] = config.ifacePort;
		node.properties["application"] = config.application;

		m_nodes[name] = node;
	}
    
	void registerController(const QString& name, const emulatr::config::ControllerConfig& config)
	{
		DeviceNode node;
		node.name = name;
		node.deviceNodeType = classTypeToDeviceType(config.classType);
		node.busName = "pci0";
		node.enabled = true;

		// Set classType from config
		node.classType = config.classType;  // "SCSI_HBA", "NIC", etc.

		// Copy all fields
		for (auto it = config.fields.begin(); it != config.fields.end(); ++it) {
			node.properties[it.key()] = it.value();
		}

		m_nodes[name] = node;
	}
    
	void registerDevice(const QString& name, const emulatr::config::DeviceConfig& config)
	{
		DeviceNode node;
		node.name = name;
		node.deviceNodeType = deviceTypeFromString(config.classType);
		node.parent = config.parent;
		node.unit = config.fields.value("unit", 0).toUInt();
		node.enabled = true;

		// Set classType from config (this is what shows in SHOW DEVICE)
		node.classType = config.classType;  // "SCSI_DISK", "SCSI_TAPE", etc.

		// Copy all fields
		for (auto it = config.fields.begin(); it != config.fields.end(); ++it) {
			node.properties[it.key()] = it.value();
		}

		m_nodes[name] = node;
	}
    
    // ========================================================================
    // HWRPB image builder (Phase 0)
    // ========================================================================
    //
    // Lays down a complete HWRPB into m_hwrpbBuffer using deviceLib::
    // hwrpb::populateHwrpb().  Driven by m_config (EmulatorSettings) for
    // CPU count and memory size; uses Tsunami-class defaults for fields
    // EmulatorSettings doesn't yet carry (interval-clock rate, cycle
    // frequency, PA width, ASN max).  Returns false on overflow or spec
    // validation failure.
    //
    // The CRB / CTB callback fields are zeroed here -- the OS-callable
    // firmware entry points (dispatch_pa, putchar_callback_pa) get filled
    // in by Phase 4 (bindConsoleServices) and the HWRPB is rebuilt or
    // patched at that point.  v1: rebuild; later: in-place patch.
    // ========================================================================

    bool buildHwrpbImage()
    {
        using namespace deviceLib::hwrpb;

        // 16 KB buffer comfortably accommodates Tsunami-class HWRPBs
        // (header 320B + 4 SLOTs at ~1KB + CTB + CRB + MEMDSC ~= 5-6 KB).
        // AARM Section 26.1 says "8K to 16K bytes" is the typical range.
        constexpr std::size_t kHwrpbBufferSize = 16384;
        m_hwrpbBuffer.assign(kHwrpbBufferSize, 0);

        // Per-CPU configs -- one entry per emulated CPU, seeded from the
        // EV6 default and overridden with CPU-specific identity.
        std::uint64_t const cpuCount =
            std::max<std::uint64_t>(1, m_config.system.cpuCount);
        std::vector<PerCpuConfig> cpus(static_cast<std::size_t>(cpuCount),
                                        defaultEv6PrimaryCpu());
        for (std::uint64_t i = 0; i < cpuCount; ++i) {
            cpus[i].whami      = i;
            cpus[i].present    = true;
            cpus[i].available  = true;
            cpus[i].primary    = (i == 0);   // CPU 0 is the boot CPU
            // pal_mem_pa / pal_rev are populated later when the firmware
            // image (SRM .exe) is loaded and decompressed; see SrmLoader.
        }

        // Single memory cluster covering all configured RAM.  Multi-node
        // (Wildfire QBB) systems would extend this with per-QBB clusters.
        MemoryClusterSpec cluster{};
        cluster.start_pfn = 0;
        cluster.pfn_count = m_config.system.memorySizeBytes / kAlphaPageSize;
        cluster.usage     = 0;     // OS-usable

        HwrpbBuildSpec spec{};
        spec.hwrpb_pa             = m_hwrpbPa;                  // PA 0 (Tsunami default)
        spec.system_type          = SystemType::DEC_TSUNAMI;
        spec.system_variation     = 0;
        spec.system_revision      = 0;
        spec.serial_number[0]     = 0;
        spec.serial_number[1]     = 0;
        spec.intrclock_freq_x4096 = 1024 * 4096;                // 1024 Hz interval clock
        spec.cycle_count_freq_hz  = 500'000'000;                // 500 MHz EV6 (typical)
        spec.primary_cpu_id       = 0;
        spec.cpus                 = cpus.data();
        spec.cpu_count            = cpuCount;
        spec.clusters             = &cluster;
        spec.cluster_count        = 1;
        spec.console_type         = ConsoleType::SerialLine;
        spec.console_unit         = 0;
        spec.console_dev_ipl      = 20;
        spec.console_putchar_callback_pa = 0;                    // Phase 4 wires
        spec.crb_dispatch_va      = 0;
        spec.crb_dispatch_pa      = 0;
        spec.crb_fixup_va         = 0;
        spec.crb_fixup_pa         = 0;
        spec.crb_io_pages_to_map  = 0;
        spec.crb_io_entries       = nullptr;
        spec.crb_io_entry_count   = 0;
        spec.pa_size_bits         = 40;                          // Tsunami EV6 PA width
        spec.max_valid_asn        = 127;                         // EV6 default
        spec.vptb_va              = 0;                           // PALcode sets at boot
        spec.hwrpb_revision       = 0;                           // 0 -> kHwrpbRevisionCurrent

        std::size_t const written = populateHwrpb(m_hwrpbBuffer.data(),
                                                  m_hwrpbBuffer.size(),
                                                  spec);
        if (written == 0) {
            EMULATR_LOG_ERROR("populateHwrpb returned 0 (validation or overflow)");
            m_hwrpbBuffer.clear();
            m_hwrpbBufferSize = 0;
            return false;
        }

        m_hwrpbBufferSize = written;

        EMULATR_LOG_INFO("HWRPB built: {} bytes, {} CPUs, {} pages of memory at PA 0x{:x}",
                         written,
                         cpuCount,
                         cluster.pfn_count,
                         m_hwrpbPa);
        return true;
    }

    // ========================================================================
    // Validation and Binding
    // ========================================================================

    bool validateAddressSpace()
    {
        // Check for MMIO overlaps
        QMap<uint64_t, QString> mmioMap;
        
        for (auto it = m_nodes.begin(); it != m_nodes.end(); ++it) {
            const auto& node = it.value();
            if (node.mmioBase != 0 && node.mmioSize != 0) {
                // Check overlap
                for (uint64_t addr = node.mmioBase; 
                     addr < node.mmioBase + node.mmioSize; 
                     addr += 4096)  // Page granularity
                {
                    if (mmioMap.contains(addr)) {
                        SPDLOG_ERROR("MMIO overlap: {} and {} at 0x{:016x}",
                            node.name.toStdString(), mmioMap[addr].toStdString(), addr);
                        return false;
                    }
                    mmioMap[addr] = node.name;
                }
            }
        }
        
        return true;
    }
    
    void bindConsoleServices()
    {
        // Bind OPA devices to console manager
        // This is done in EmulatR_init Phase 13
        SPDLOG_INFO("Console services binding deferred to Phase 13");
    }
    
    // ========================================================================
    // Type Conversion Helpers
    // ========================================================================
    
    static DeviceNodeType classTypeToDeviceType(const QString& classType)
    {
        if (classType == "SCSI_HBA") return DeviceNodeType::SCSIController;
        if (classType == "IDE_CONTROLLER") return DeviceNodeType::IDEController;
        if (classType == "NIC") return DeviceNodeType::NetworkInterface;
        return DeviceNodeType::Unknown;
    }
    
    static DeviceNodeType deviceTypeFromString(const QString& typeStr)
    {
        if (typeStr == "SCSI_DISK") return DeviceNodeType::SCSIDisk;
        if (typeStr == "SCSI_TAPE") return DeviceNodeType::SCSITape;
        if (typeStr == "IDE_DISK") return DeviceNodeType::IDEDisk;
        if (typeStr == "UART") return DeviceNodeType::ConsoleTerminal;
        return DeviceNodeType::Unknown;
    }
    
    static QString deviceTypeToString(DeviceNodeType type)
    {
        switch (type) {
        case DeviceNodeType::PlatformRoot: return "Platform";
        case DeviceNodeType::SystemBus: return "SystemBus";
        case DeviceNodeType::PCIBus: return "PCIBus";
        case DeviceNodeType::ConsoleBus: return "ConsoleBus";
        case DeviceNodeType::VirtualBus: return "VirtualBus";
        case DeviceNodeType::SCSIController: return "SCSI_HBA";
        case DeviceNodeType::SCSIDisk: return "SCSI_DISK";
        case DeviceNodeType::SCSITape: return "SCSI_TAPE";
        case DeviceNodeType::IDEController: return "IDE_CONTROLLER";
        case DeviceNodeType::IDEDisk: return "IDE_DISK";
        case DeviceNodeType::NetworkInterface: return "NIC";
        case DeviceNodeType::ConsoleTerminal: return "UART";
        default: return "Unknown";
        }
    }

    // ========================================================================
    // Member Variables
    // ========================================================================

	emulatr::config::EmulatorSettings m_config;
    QMap<QString, DeviceNode> m_nodes;
    bool m_initialized{ false };

    // HWRPB image populated by Phase 0; deployed into guest physical
    // memory by the Machine orchestrator at boot time.
    std::vector<std::uint8_t> m_hwrpbBuffer;
    std::size_t               m_hwrpbBufferSize{ 0 };
    std::uint64_t             m_hwrpbPa{ 0 };   // Tsunami convention: HWRPB at PFN 0
};

#endif // FIRMWAREDEVICEMANAGER_H
