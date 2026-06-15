// ============================================================================
// tests/systemLib/test_platform_config.cpp -- platform manifest loader tests
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
// ============================================================================
//
// Exercises systemLib/PlatformConfig (P1): QJsonDocument parse, manifest_version
// gate, validate() invariants, and graceful fallback to the built-in DS10
// default.  Per house rule, doctest CHECK only -- never REQUIRE (V4 builds
// disable exceptions; REQUIRE expands to a static_assert that fails compile).
// ============================================================================

#include "doctest.h"

#include <array>
#include <string>
#include <vector>

#include "systemLib/PlatformConfig.h"

using systemLib::PlatformConfig;
using systemLib::DeviceManifest;
using systemLib::ManifestLoadResult;
using systemLib::IicDeviceEntry;
using systemLib::IicClass;
using systemLib::PciDeviceEntry;
using systemLib::PciBarEntry;
using systemLib::PciConfigImage;
using systemLib::synthesizeFruImage;
using systemLib::synthesizePciConfig;

namespace {

const char* kGoodManifest = R"({
  "manifest_version": 1,
  "platform": "DS10",
  "iic_devices": [
    { "name":"iic_system0", "address":"0x70", "class":"status", "byte":"0x00" },
    { "name":"iic_system1", "address":"0x72", "class":"status", "byte":"0x00" },
    { "name":"iic_smb0", "address":"0xA2", "class":"fru_eeprom",
      "manufacturer":"DEC", "model":"DS10" }
  ],
  "pci_devices": []
})";

} // namespace

TEST_CASE("PlatformConfig: built-in DS10 default validates clean")
{
    DeviceManifest m = PlatformConfig::defaultDs10Manifest();
    std::vector<std::string> issues;
    bool ok = PlatformConfig::validate(m, issues);
    CHECK(ok);
    CHECK(m.manifestVersion == PlatformConfig::kSupportedVersion);
    CHECK(m.platform == "DS10");
    CHECK(m.iic.size() >= 3);   // at least 0x70, 0x72, 0xA2
    CHECK(m.pci.size() >= 1);
    // No hard errors; any issue present must be a WARN line.
    for (const std::string& s : issues)
        CHECK(s.rfind("WARN:", 0) == 0);
}

TEST_CASE("PlatformConfig: good manifest parses, no fallback")
{
    ManifestLoadResult r = PlatformConfig::loadFromString(kGoodManifest);
    CHECK(r.ok);
    CHECK_FALSE(r.usedDefault);
    CHECK(r.manifest.platform == "DS10");
    CHECK(r.manifest.iic.size() == 3);
    // hex address parsed correctly
    bool saw70 = false;
    for (const IicDeviceEntry& e : r.manifest.iic)
        if (e.address == 0x70 && e.cls == IicClass::Status) saw70 = true;
    CHECK(saw70);
}

TEST_CASE("PlatformConfig: malformed JSON falls back to default")
{
    ManifestLoadResult r = PlatformConfig::loadFromString("{ this is not json");
    CHECK(r.ok);              // ok==true because the default is usable
    CHECK(r.usedDefault);
    CHECK(r.manifest.platform == "DS10");
}

TEST_CASE("PlatformConfig: unsupported manifest_version falls back")
{
    ManifestLoadResult r = PlatformConfig::loadFromString(
        R"({ "manifest_version": 999, "platform":"DS10",
             "iic_devices":[], "pci_devices":[] })");
    CHECK(r.ok);
    CHECK(r.usedDefault);
}

TEST_CASE("PlatformConfig: duplicate IIC address is a hard error")
{
    DeviceManifest m;
    m.manifestVersion = PlatformConfig::kSupportedVersion;
    m.platform = "other";
    IicDeviceEntry a; a.name = "a"; a.address = 0x70; a.cls = IicClass::Status;
    IicDeviceEntry b; b.name = "b"; b.address = 0x70; b.cls = IicClass::Status;
    m.iic.push_back(a);
    m.iic.push_back(b);
    std::vector<std::string> issues;
    bool ok = PlatformConfig::validate(m, issues);
    CHECK_FALSE(ok);
}

TEST_CASE("PlatformConfig: odd IIC address is a hard error")
{
    DeviceManifest m;
    m.manifestVersion = PlatformConfig::kSupportedVersion;
    m.platform = "other";
    IicDeviceEntry a; a.name = "odd"; a.address = 0x71; a.cls = IicClass::Status;
    m.iic.push_back(a);
    std::vector<std::string> issues;
    CHECK_FALSE(PlatformConfig::validate(m, issues));
}

TEST_CASE("PlatformConfig: fru_eeprom missing required fields is a hard error")
{
    DeviceManifest m;
    m.manifestVersion = PlatformConfig::kSupportedVersion;
    m.platform = "other";
    IicDeviceEntry e; e.name = "smb"; e.address = 0xA2; e.cls = IicClass::FruEeprom;
    // manufacturer + model deliberately empty
    m.iic.push_back(e);
    std::vector<std::string> issues;
    CHECK_FALSE(PlatformConfig::validate(m, issues));
}


// ---- P2 synthesis -------------------------------------------------------

TEST_CASE("synthesizeFruImage: JEDEC fields + checksums")
{
    IicDeviceEntry e;
    e.name = "iic_smb0"; e.address = 0xA2; e.cls = IicClass::FruEeprom;
    e.manufacturer = "DEC"; e.model = "DS10"; e.partClass = "54-";
    e.revisionRo = 0x20; e.revisionRw = 0x21;
    std::array<uint8_t, 256> d = synthesizeFruImage(e);

    CHECK(d[0x48] == 0x02);            // manuf_location = AY
    CHECK(d[0x7D] == 0xB5);            // manuf_spec_dec_JEDEC_ID = DEC
    CHECK(d[0xFD] == 0xB5);            // dec_flag_id = DEC
    CHECK(d[0x7E] == 0x20);            // revision_ro
    CHECK(d[0xFE] == 0x21);            // rev_rw
    CHECK(d[0x63] == 'D');             // manuf_spec_alias = "DEC"
    CHECK(d[0x64] == 'E');
    CHECK(d[0x65] == 'C');
    CHECK(d[0x73] == 'D');             // manuf_spec_model = "DS10"
    CHECK(d[0x74] == 'S');
    CHECK(d[0x49] == '5');             // manuf_part_class = "54-"
    CHECK(d[0x4A] == '4');

    // checksums self-consistent (low byte of segment sums)
    unsigned s0 = 0; for (int i = 0x00; i <= 0x3E; ++i) s0 += d[i];
    unsigned s1 = 0; for (int i = 0x40; i <= 0x7E; ++i) s1 += d[i];
    unsigned s2 = 0; for (int i = 0x80; i <= 0xFE; ++i) s2 += d[i];
    CHECK(d[0x3F] == static_cast<uint8_t>(s0 & 0xFF));
    CHECK(d[0x7F] == static_cast<uint8_t>(s1 & 0xFF));
    CHECK(d[0xFF] == static_cast<uint8_t>(s2 & 0xFF));
}

TEST_CASE("synthesizeFruImage: non-FRU entry is all zero")
{
    IicDeviceEntry e;
    e.name = "iic_system0"; e.address = 0x70; e.cls = IicClass::Status;
    e.statusByte = 0x5A;
    std::array<uint8_t, 256> d = synthesizeFruImage(e);
    bool allZero = true;
    for (uint8_t b : d) if (b != 0) { allZero = false; break; }
    CHECK(allZero);
}

TEST_CASE("synthesizePciConfig: header fields + BAR masks")
{
    PciDeviceEntry e;
    e.name = "de500_tulip"; e.vendor = 0x1011; e.device = 0x0019;
    e.classCode = 0x020000; e.revision = 0x00; e.interruptPin = 1;
    PciBarEntry b0; b0.index = 0; b0.isMem = false; b0.size = 0x80;  // I/O
    PciBarEntry b1; b1.index = 1; b1.isMem = true;  b1.size = 0x80;  // mem
    e.bars.push_back(b0); e.bars.push_back(b1);

    PciConfigImage img = synthesizePciConfig(e);
    const std::array<uint8_t, 256>& d = img.cfg;

    CHECK(d[0x00] == 0x11);            // vendor LE
    CHECK(d[0x01] == 0x10);
    CHECK(d[0x02] == 0x19);            // device LE
    CHECK(d[0x03] == 0x00);
    CHECK(d[0x09] == 0x00);            // prog-if
    CHECK(d[0x0A] == 0x00);            // subclass
    CHECK(d[0x0B] == 0x02);            // base class = network
    CHECK(d[0x0E] == 0x00);            // header type 0
    CHECK(d[0x3D] == 0x01);            // interrupt pin = INTA

    // expansion-ROM BAR (0x30..0x33) stays 0 (optionRom defaulted false)
    CHECK(d[0x30] == 0x00);
    CHECK(d[0x33] == 0x00);

    // I/O BAR0 size 0x80 -> mask ~(0x7F) with bit0=1 = 0xFFFFFF81
    CHECK(img.barIsMem[0] == false);
    CHECK(img.barMask[0] == 0xFFFFFF81u);
    // mem BAR1 size 0x80 -> mask 0xFFFFFF80, type bits 0
    CHECK(img.barIsMem[1] == true);
    CHECK(img.barMask[1] == 0xFFFFFF80u);
    // unused BARs report 0
    CHECK(img.barMask[2] == 0u);
}

TEST_CASE("PlatformConfig: storage 'enabled' flag -- implicit true; disabled alt shares a slot")
{
    const char* manifest = R"({
      "manifest_version": 1,
      "platform": "DS10",
      "iic_devices": [],
      "pci_devices": [
        { "name":"cypress_ide", "model":"cypress_ide", "hose":0, "bus":0, "slot":5, "func":1,
          "vendor":"0x1080", "device":"0xc693", "class_code":"0x010100",
          "storage": [
            { "channel":0, "unit":0, "type":"ata_disk",    "media":"" },
            { "channel":0, "unit":1, "type":"atapi_cdrom", "media":"a.iso",     "media_kind":"iso"  },
            { "channel":0, "unit":1, "type":"atapi_cdrom", "media":"host:0", "media_kind":"host", "enabled": false }
          ] }
      ]
    })";

    ManifestLoadResult r = PlatformConfig::loadFromString(manifest);
    REQUIRE(r.ok);                       // disabled duplicate on (0,1) is NOT a hard error
    CHECK_FALSE(r.usedDefault);
    REQUIRE(r.manifest.pci.size() == 1);
    const std::vector<systemLib::StorageTarget>& st = r.manifest.pci[0].storage;
    REQUIRE(st.size() == 3);

    CHECK(st[0].enabled);                // absent -> implicitly enabled
    CHECK(st[1].enabled);                // absent -> implicitly enabled
    CHECK_FALSE(st[2].enabled);          // explicit "enabled": false
    CHECK(st[1].media_kind == "iso");
    CHECK(st[2].media_kind == "host");

    // The disabled alternate may share channel/unit with the enabled one.
    std::vector<std::string> issues;
    CHECK(PlatformConfig::validate(r.manifest, issues));

    // Positive control: enabling it makes (0,1) a real duplicate -> hard error.
    r.manifest.pci[0].storage[2].enabled = true;
    std::vector<std::string> issues2;
    CHECK_FALSE(PlatformConfig::validate(r.manifest, issues2));
}

TEST_CASE("PlatformConfig: create_if_missing parses size + validates (disk only, 512-aligned)")
{
    const char* manifest = R"({
      "manifest_version": 1,
      "platform": "DS10",
      "iic_devices": [],
      "pci_devices": [
        { "name":"cypress_ide", "model":"cypress_ide", "hose":0, "bus":0, "slot":5, "func":1,
          "vendor":"0x1080", "device":"0xc693", "class_code":"0x010100",
          "storage": [
            { "channel":0, "unit":0, "type":"ata_disk", "media":"dqa0.img",
              "create_if_missing": true, "size":"4G" }
          ] }
      ]
    })";
    ManifestLoadResult r = PlatformConfig::loadFromString(manifest);
    REQUIRE(r.ok);
    REQUIRE(r.manifest.pci.size() == 1);
    const systemLib::StorageTarget& d = r.manifest.pci[0].storage[0];
    CHECK(d.createIfMissing);
    CHECK(d.sizeBytes == 4ull * 1024 * 1024 * 1024);     // "4G"
    std::vector<std::string> issues;
    CHECK(PlatformConfig::validate(r.manifest, issues));

    // create_if_missing on a CD -> hard error
    r.manifest.pci[0].storage[0].type = systemLib::StorageType::AtapiCdrom;
    std::vector<std::string> i2;
    CHECK_FALSE(PlatformConfig::validate(r.manifest, i2));

    // back to disk but zero size -> hard error
    r.manifest.pci[0].storage[0].type      = systemLib::StorageType::AtaDisk;
    r.manifest.pci[0].storage[0].sizeBytes = 0;
    std::vector<std::string> i3;
    CHECK_FALSE(PlatformConfig::validate(r.manifest, i3));

    // non-512-aligned size -> hard error
    r.manifest.pci[0].storage[0].sizeBytes = 1000;
    std::vector<std::string> i4;
    CHECK_FALSE(PlatformConfig::validate(r.manifest, i4));
}
