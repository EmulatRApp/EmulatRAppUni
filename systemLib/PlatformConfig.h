// ============================================================================
// systemLib/PlatformConfig.h -- declarative platform device manifest (P1)
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
// Spec: journals/Device_Enumeration_Scaffold_Spec_20260607.md.
//
// P1 SCOPE (this file + PlatformConfig.cpp): parse a JSON platform manifest
// via Qt's QJsonDocument (no-throw, error-return; see SRMEnvStore.cpp for the
// in-tree precedent), gate on manifest_version (D-VERSION), and run an explicit
// validate() pass (D-VALIDATE) BEFORE any consumer binds.  On any parse or
// validation failure the loader logs every issue and returns the compiled-in
// default DS10 manifest (usedDefault=true), so a bad/absent file degrades
// gracefully instead of half-configuring the machine.
//
// NOT in P1: the on-wire SYNTHESIS (high-level fields -> 256-byte JEDEC EEPROM
// images + checksums, 256-byte PCI config headers + BAR masks).  Those are P2
// and operate on the structs declared here; the structs intentionally carry
// only the high-level fields at this stage.
//
// This header is Qt-free by design -- all Qt usage lives in the .cpp so the
// manifest types can be included anywhere without pulling QtCore.
//
// PROVISIONAL discipline (spec 7.5): PCI identity fields (vendor, device,
// class_code, BAR sizes) are estimates until confirmed against a real hardware
// dump or firmware source; each such declaration carries a _PROVISIONAL comment
// that is removed in the same edit that lands the confirmed value.  Mirrors the
// standing IPR/SCBD _PROVISIONAL convention.
// ============================================================================

#ifndef SYSTEMLIB_PLATFORMCONFIG_H
#define SYSTEMLIB_PLATFORMCONFIG_H

#include <cstdint>
#include <array>
#include <string>
#include <vector>

namespace systemLib {

// ---------------------------------------------------------------------------
// IIC device class (maps to apisrm iic_node_list device "class").
// ---------------------------------------------------------------------------
enum class IicClass : uint8_t {
    FruEeprom,   // 256-byte JEDEC FRU EEPROM (synthesized in P2)
    Nvram,       // 256-byte zero-filled NVRAM, mutable at runtime
    Status,      // 1-byte status register (e.g. iic_system0/1 at 0x70/0x72)
    Led          // 1-byte LED/OCP register (read-mostly)
};

// ---------------------------------------------------------------------------
// One IIC bus device.  High-level fields only; P2 synthesizes the on-wire
// image from these.  `address' is the even 8-bit node address (0x70, 0xA2...).
// ---------------------------------------------------------------------------
struct IicDeviceEntry {
    std::string name;                 // informational; matches iic_node_list
    uint8_t     address     = 0;      // even node address, 0x00..0xFE
    IicClass    cls         = IicClass::Status;

    // class FruEeprom (high-level identity; synthesized to JEDEC in P2):
    std::string manufacturer;         // e.g. "DEC"
    std::string model;                // e.g. "DS10"
    std::string partClass;            // e.g. "54-"
    std::string serial;               // may be empty; guest may overwrite
    uint8_t     revisionRo  = 0;      // JEDEC revision_ro byte
    uint8_t     revisionRw  = 0;      // JEDEC revision_rw byte

    // class Nvram:
    uint32_t    size        = 256;    // bytes

    // class Status / Led:
    uint8_t     statusByte  = 0;      // value reads return
};

// ---------------------------------------------------------------------------
// One PCI Base Address Register request.
// ---------------------------------------------------------------------------
struct PciBarEntry {
    uint8_t  index    = 0;            // 0..5
    bool     isMem    = false;        // true = memory BAR, false = I/O BAR
    uint32_t size     = 0;            // _PROVISIONAL -- confirm against hw dump
    bool     prefetch = false;        // memory BARs only
};

// ---------------------------------------------------------------------------
// PCI device backing model (D-PCIMODEL): how the device answers config cycles.
// ---------------------------------------------------------------------------
enum class PciModel : uint8_t {
    Generic,     // synthesized config-only ManifestPciDevice (P2/P4)
    Passive,     // answers config reads, absorbs writes (pure presence stub)
    Named        // routes to a specific behavioral class (modelName)
};

// ---------------------------------------------------------------------------
// Storage logical-unit type behind a (named) storage controller.
// ---------------------------------------------------------------------------
enum class StorageType : uint8_t {
    AtapiCdrom,  // ATAPI CD/DVD logical unit (scsi::VirtualIsoDevice)
    AtaDisk      // ATA fixed disk (future disk model)
};

// ---------------------------------------------------------------------------
// One storage logical unit attached behind a storage controller (e.g. the IDE
// func).  High-level/identity only; the controller consumer binds it to a
// device model and (later phase) a media-file backend.  IDE addressing:
// channel 0/1 = primary/secondary, unit 0/1 = master/slave; lun 0 for ATA/ATAPI.
// `media' empty = no media loaded (today's state).
// ---------------------------------------------------------------------------
struct StorageTarget {
    uint8_t      channel = 0;         // 0 = primary, 1 = secondary
    uint8_t      unit    = 0;         // 0 = master, 1 = slave
    uint8_t      lun     = 0;         // 0 for ATA/ATAPI
    StorageType  type    = StorageType::AtapiCdrom;
    std::string  model;               // INQUIRY/identify model string (informational)
    std::string  media;               // path to ISO/disk image, or host selector; empty = no media
    std::string  media_kind;          // "image"|"iso"|absent -> file; "host" -> passthrough
    bool         enabled = true;      // absent -> enabled; false -> skipped (lets an
                                      // alternate target share a channel/unit, toggled off)
    bool         createIfMissing = false; // create a blank backing if absent (writable disk)
    uint64_t     sizeBytes = 0;       // capacity for create_if_missing (bytes; K/M/G in JSON)
};

// ---------------------------------------------------------------------------
// One PCI device.  BDF = (hose,bus,slot,func); slot == PCI device number.
// Identity fields are _PROVISIONAL until confirmed (spec 7.5).
// ---------------------------------------------------------------------------
struct PciDeviceEntry {
    std::string name;
    PciModel    model        = PciModel::Generic;
    std::string modelName;            // when model==Named, e.g. "cypress_isa"

    uint8_t     hose         = 0;
    uint8_t     bus          = 0;
    uint8_t     slot         = 0;     // PCI device number
    uint8_t     func         = 0;

    uint16_t    vendor       = 0;     // _PROVISIONAL -- confirm against hw dump
    uint16_t    device       = 0;     // _PROVISIONAL -- confirm against hw dump
    uint32_t    classCode    = 0;     // 24-bit; _PROVISIONAL -- confirm against hw dump
    uint8_t     revision     = 0;
    uint16_t    subsysVendor = 0;
    uint16_t    subsysId     = 0;

    std::vector<PciBarEntry> bars;
    bool        optionRom    = false; // false => expansion-ROM BAR reads 0
    uint8_t     interruptPin = 0;     // 0 = none, 1..4 = INTA..INTD

    // Storage logical units behind this controller (named storage models only;
    // empty for non-storage devices).  Consumed by the controller attach.
    std::vector<StorageTarget> storage;
};

// ---------------------------------------------------------------------------
// The parsed, validated platform manifest.
// ---------------------------------------------------------------------------
struct DeviceManifest {
    int                          manifestVersion = 0;
    std::string                  platform;
    std::vector<IicDeviceEntry>  iic;
    std::vector<PciDeviceEntry>  pci;
};

// ---------------------------------------------------------------------------
// Result of a load attempt.  `ok' is true whenever `manifest' is usable, which
// includes the graceful-fallback case (usedDefault=true).  `issues' carries
// every validation error and warning encountered (warnings prefixed "WARN:").
// ---------------------------------------------------------------------------
struct ManifestLoadResult {
    bool                     ok          = false;
    bool                     usedDefault = false;  // fell back to built-in DS10
    std::string              error;                // first hard error (parse/validate)
    std::vector<std::string> issues;               // all errors + warnings
    DeviceManifest           manifest;
};

// ---------------------------------------------------------------------------
// PlatformConfig -- stateless loader/validator (all methods static).
// ---------------------------------------------------------------------------
class PlatformConfig {
public:
    // The manifest_version this build understands.
    static constexpr int kSupportedVersion = 1;

    // Read the file at `path', parse + version-check + validate.  On any hard
    // failure (missing file, bad JSON, wrong version, validation error) logs
    // the issue(s) and returns the built-in default DS10 manifest with
    // usedDefault=true and ok=true.  Never throws.
    static ManifestLoadResult load(const std::string& path);

    // Same as load() but from an in-memory JSON string (no file I/O).  This is
    // the unit-test entry point and the core of load().
    static ManifestLoadResult loadFromString(const std::string& json);

    // The compiled-in DS10 manifest (used when the file is missing/invalid and
    // as the reference for what a minimal valid board looks like).
    static DeviceManifest defaultDs10Manifest();

    // Validate structural invariants of `m'.  Appends every problem to
    // `issues' (warnings prefixed "WARN:").  Returns true iff there are no
    // HARD errors (warnings do not fail).
    static bool validate(const DeviceManifest& m, std::vector<std::string>& issues);
};


// ===========================================================================
// P2 synthesis -- high-level manifest fields -> on-wire device images.
// These operate on the structs above; the IIC/PCI consumers (P3/P4) feed the
// results to the device models.  Pure functions, no Qt, fully unit-testable.
// ===========================================================================

// 256-byte JEDEC FRU EEPROM image for a FruEeprom entry (offsets per apisrm
// jedec_def.sdl; the three JEDEC-21C segment checksums are computed last).
// A non-FruEeprom entry yields an all-zero image (caller should not use it).
std::array<uint8_t, 256> synthesizeFruImage(const IicDeviceEntry& e);

// PCI configuration-space image plus BAR sizing data.  barMask[i] is the value
// the firmware reads back after writing 0xFFFFFFFF to BAR i (the size-probe
// handshake); 0 means BAR i is unused.  barIsMem[i] distinguishes mem vs I/O.
struct PciConfigImage {
    std::array<uint8_t, 256> cfg{};
    uint32_t                 barMask[6]  = { 0, 0, 0, 0, 0, 0 };
    bool                     barIsMem[6] = { false, false, false, false, false, false };
};

// 256-byte PCI config header synthesized from a PciDeviceEntry.  Standard
// type-0 header offsets; expansion-ROM BAR (0x30) is 0 when optionRom=false.
PciConfigImage synthesizePciConfig(const PciDeviceEntry& e);

} // namespace systemLib

#endif // SYSTEMLIB_PLATFORMCONFIG_H
