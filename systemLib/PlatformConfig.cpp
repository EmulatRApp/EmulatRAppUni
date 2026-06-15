// ============================================================================
// systemLib/PlatformConfig.cpp -- platform device manifest loader (P1)
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
// Qt's QJsonDocument does the parse (no-throw, QJsonParseError error-return --
// the same idiom as deviceLib/SRMEnvStore.cpp), so this compiles cleanly in the
// exceptions-disabled build.  Parsing runs once at/near init, off any hot path.
//
// NOTE: Qt JSON is strict -- no // or /* */ comments, no trailing commas.  A
// manifest may carry a "comment" string field for documentation; the loader
// ignores unknown fields.
// ============================================================================

#include "PlatformConfig.h"

#include <QByteArray>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QString>
#include <QDebug>

#include <cstdio>
#include <set>

namespace systemLib {

namespace {

// ---- scalar helpers -------------------------------------------------------

// Parse a hex ("0x..") or decimal QString into out.  Returns false on garbage.
bool parseHexOrDec(const QString& s, uint64_t& out)
{
    QString t = s.trimmed();
    if (t.isEmpty()) return false;
    bool ok = false;
    if (t.startsWith("0x", Qt::CaseInsensitive)) {
        out = t.mid(2).toULongLong(&ok, 16);
    } else {
        out = t.toULongLong(&ok, 10);
    }
    return ok;
}

// Read a manifest scalar that may be a hex-string OR a JSON number.
bool jsonHex(const QJsonValue& v, uint64_t& out)
{
    if (v.isString()) return parseHexOrDec(v.toString(), out);
    if (v.isDouble()) { out = static_cast<uint64_t>(v.toDouble()); return true; }
    return false;
}

bool iicClassFromString(const QString& s, IicClass& out)
{
    if (s == "fru_eeprom") { out = IicClass::FruEeprom; return true; }
    if (s == "nvram")      { out = IicClass::Nvram;     return true; }
    if (s == "status")     { out = IicClass::Status;    return true; }
    if (s == "led")        { out = IicClass::Led;       return true; }
    return false;
}

bool pciModelFromString(const QString& s, PciModel& out)
{
    if (s == "generic") { out = PciModel::Generic; return true; }
    if (s == "passive") { out = PciModel::Passive; return true; }
    // any other non-empty string is a named behavioral model
    if (!s.isEmpty())   { out = PciModel::Named;   return true; }
    return false;
}

bool storageTypeFromString(const QString& s, StorageType& out)
{
    if (s == "atapi_cdrom") { out = StorageType::AtapiCdrom; return true; }
    if (s == "ata_disk")    { out = StorageType::AtaDisk;    return true; }
    return false;
}

// Parse a manifest "size" value into bytes.  Accepts a JSON number (bytes) or a
// string with an optional K/M/G/T suffix (1024-based), e.g. "4G", "64M", "512".
// Returns 0 on absent/invalid (the caller treats 0 as "no size given").
uint64_t parseSizeBytes(const QJsonValue& v)
{
    if (v.isDouble()) {
        double const d = v.toDouble();
        return d > 0 ? static_cast<uint64_t>(d) : 0;
    }
    if (!v.isString()) return 0;
    QString s = v.toString().trimmed();
    if (s.isEmpty()) return 0;
    uint64_t mult = 1;
    QChar const last = s.at(s.size() - 1).toUpper();
    if      (last == 'K') { mult = 1024ull;                         s.chop(1); }
    else if (last == 'M') { mult = 1024ull*1024;                    s.chop(1); }
    else if (last == 'G') { mult = 1024ull*1024*1024;               s.chop(1); }
    else if (last == 'T') { mult = 1024ull*1024*1024*1024;          s.chop(1); }
    bool ok = false;
    unsigned long long const n = s.trimmed().toULongLong(&ok);
    return ok ? static_cast<uint64_t>(n) * mult : 0;
}

std::string stdstr(const QJsonValue& v) { return v.toString().toStdString(); }

// Pack a BDF into a comparable key for duplicate detection.
uint32_t bdfKey(const PciDeviceEntry& d)
{
    return (uint32_t(d.hose) << 24) | (uint32_t(d.bus) << 16)
         | (uint32_t(d.slot) << 8)  |  uint32_t(d.func);
}

// ---- per-device parse (best-effort; structural validation is in validate) -

void parseIicDevice(const QJsonObject& o, IicDeviceEntry& e,
                    std::vector<std::string>& issues, int idx)
{
    e.name = stdstr(o.value("name"));

    uint64_t addr = 0;
    if (jsonHex(o.value("address"), addr)) {
        e.address = static_cast<uint8_t>(addr & 0xFFu);
    } else {
        issues.push_back("iic_devices[" + std::to_string(idx)
                         + "]: missing or malformed 'address'");
    }

    QString cls = o.value("class").toString();
    if (!iicClassFromString(cls, e.cls)) {
        issues.push_back("iic_devices[" + std::to_string(idx)
                         + "]: missing or unknown 'class' (" + cls.toStdString() + ")");
    }

    e.manufacturer = stdstr(o.value("manufacturer"));
    e.model        = stdstr(o.value("model"));
    e.partClass    = stdstr(o.value("part_class"));
    e.serial       = stdstr(o.value("serial"));

    uint64_t tmp = 0;
    if (jsonHex(o.value("revision_ro"), tmp)) e.revisionRo = static_cast<uint8_t>(tmp);
    if (jsonHex(o.value("revision_rw"), tmp)) e.revisionRw = static_cast<uint8_t>(tmp);
    if (o.contains("size"))  e.size       = static_cast<uint32_t>(o.value("size").toInt(256));
    if (jsonHex(o.value("byte"), tmp))     e.statusByte = static_cast<uint8_t>(tmp);
}

void parsePciDevice(const QJsonObject& o, PciDeviceEntry& e,
                    std::vector<std::string>& issues, int idx)
{
    e.name = stdstr(o.value("name"));

    QString model = o.value("model").toString();
    if (!pciModelFromString(model, e.model)) {
        issues.push_back("pci_devices[" + std::to_string(idx)
                         + "]: missing 'model'");
    } else if (e.model == PciModel::Named) {
        e.modelName = model.toStdString();
    }

    e.hose = static_cast<uint8_t>(o.value("hose").toInt(0));
    e.bus  = static_cast<uint8_t>(o.value("bus").toInt(0));
    e.slot = static_cast<uint8_t>(o.value("slot").toInt(0));
    e.func = static_cast<uint8_t>(o.value("func").toInt(0));

    uint64_t tmp = 0;
    if (jsonHex(o.value("vendor"), tmp))     e.vendor    = static_cast<uint16_t>(tmp);
    else issues.push_back("pci_devices[" + std::to_string(idx) + "]: missing 'vendor'");
    if (jsonHex(o.value("device"), tmp))     e.device    = static_cast<uint16_t>(tmp);
    else issues.push_back("pci_devices[" + std::to_string(idx) + "]: missing 'device'");
    if (jsonHex(o.value("class_code"), tmp)) e.classCode = static_cast<uint32_t>(tmp);
    if (jsonHex(o.value("revision"), tmp))   e.revision  = static_cast<uint8_t>(tmp);
    if (jsonHex(o.value("subsys_vendor"), tmp)) e.subsysVendor = static_cast<uint16_t>(tmp);
    if (jsonHex(o.value("subsys_id"), tmp))     e.subsysId     = static_cast<uint16_t>(tmp);

    e.optionRom    = o.value("option_rom").toBool(false);
    e.interruptPin = static_cast<uint8_t>(o.value("interrupt_pin").toInt(0));

    const QJsonArray bars = o.value("bars").toArray();
    for (const QJsonValue& bv : bars) {
        const QJsonObject bo = bv.toObject();
        PciBarEntry bar;
        bar.index    = static_cast<uint8_t>(bo.value("index").toInt(0));
        bar.isMem    = (bo.value("kind").toString() == "mem");
        uint64_t sz = 0;
        if (jsonHex(bo.value("size"), sz)) bar.size = static_cast<uint32_t>(sz);
        bar.prefetch = bo.value("prefetch").toBool(false);
        e.bars.push_back(bar);
    }

    // Storage logical units behind a storage controller (optional; structural
    // validation -- channel/unit range + duplicates -- is in validate()).
    const QJsonArray stor = o.value("storage").toArray();
    for (const QJsonValue& sv : stor) {
        const QJsonObject so = sv.toObject();
        StorageTarget t;
        t.channel = static_cast<uint8_t>(so.value("channel").toInt(0));
        t.unit    = static_cast<uint8_t>(so.value("unit").toInt(0));
        t.lun     = static_cast<uint8_t>(so.value("lun").toInt(0));
        const QString ty = so.value("type").toString();
        if (!storageTypeFromString(ty, t.type)) {
            issues.push_back("pci_devices[" + std::to_string(idx)
                             + "]: storage entry missing/unknown 'type' ("
                             + ty.toStdString() + ")");
        }
        t.model = stdstr(so.value("model"));
        t.media = stdstr(so.value("media"));
        t.media_kind = stdstr(so.value("media_kind"));   // "image"|"iso"|"host"; absent=image
        t.enabled = so.value("enabled").toBool(true);    // absent -> enabled (implicit)
        t.createIfMissing = so.value("create_if_missing").toBool(false);
        t.sizeBytes = parseSizeBytes(so.value("size"));  // "4G"/"64M"/bytes; 0 if absent
        e.storage.push_back(t);
    }
}

// Build a graceful-fallback result around the compiled-in default.
ManifestLoadResult fallback(const std::string& why, std::vector<std::string> issues)
{
    qWarning() << "PlatformConfig: using built-in default DS10 manifest --"
               << QString::fromStdString(why);
    ManifestLoadResult r;
    r.ok          = true;
    r.usedDefault = true;
    r.error       = why;
    r.issues      = std::move(issues);
    r.manifest    = PlatformConfig::defaultDs10Manifest();
    return r;
}

} // namespace

// ---------------------------------------------------------------------------
// loadFromString
// ---------------------------------------------------------------------------
ManifestLoadResult PlatformConfig::loadFromString(const std::string& json)
{
    QJsonParseError perr;
    const QByteArray bytes(json.data(), static_cast<int>(json.size()));
    const QJsonDocument doc = QJsonDocument::fromJson(bytes, &perr);

    if (perr.error != QJsonParseError::NoError) {
        return fallback("JSON parse error at offset "
                        + std::to_string(perr.offset) + ": "
                        + perr.errorString().toStdString(), {});
    }
    if (!doc.isObject()) {
        return fallback("manifest root is not a JSON object", {});
    }

    const QJsonObject root = doc.object();

    DeviceManifest m;
    m.manifestVersion = root.value("manifest_version").toInt(-1);
    m.platform        = stdstr(root.value("platform"));

    if (m.manifestVersion != kSupportedVersion) {
        return fallback("unsupported manifest_version "
                        + std::to_string(m.manifestVersion) + " (expected "
                        + std::to_string(kSupportedVersion) + ")", {});
    }

    std::vector<std::string> issues;

    const QJsonArray iicArr = root.value("iic_devices").toArray();
    int i = 0;
    for (const QJsonValue& v : iicArr) {
        IicDeviceEntry e;
        parseIicDevice(v.toObject(), e, issues, i++);
        m.iic.push_back(e);
    }

    const QJsonArray pciArr = root.value("pci_devices").toArray();
    i = 0;
    for (const QJsonValue& v : pciArr) {
        PciDeviceEntry e;
        parsePciDevice(v.toObject(), e, issues, i++);
        m.pci.push_back(e);
    }

    const bool valid = validate(m, issues);
    if (!valid) {
        return fallback("manifest failed validation ("
                        + std::to_string(issues.size()) + " issue(s))",
                        std::move(issues));
    }

    ManifestLoadResult r;
    r.ok          = true;
    r.usedDefault = false;
    r.issues      = std::move(issues);   // may hold WARN: lines
    r.manifest    = std::move(m);
    return r;
}

// ---------------------------------------------------------------------------
// load (file)
// ---------------------------------------------------------------------------
ManifestLoadResult PlatformConfig::load(const std::string& path)
{
    QFile f(QString::fromStdString(path));
    if (!f.open(QIODevice::ReadOnly)) {
        return fallback("cannot open manifest '" + path + "': "
                        + f.errorString().toStdString(), {});
    }
    const QByteArray bytes = f.readAll();
    f.close();
    return loadFromString(std::string(bytes.constData(),
                                      static_cast<size_t>(bytes.size())));
}

// ---------------------------------------------------------------------------
// validate -- structural invariants.  Warnings prefixed "WARN:" do not fail.
// ---------------------------------------------------------------------------
bool PlatformConfig::validate(const DeviceManifest& m,
                              std::vector<std::string>& issues)
{
    bool hardError = false;
    auto err = [&](const std::string& s) { issues.push_back(s); hardError = true; };
    auto warn = [&](const std::string& s) { issues.push_back("WARN: " + s); };

    if (m.manifestVersion != kSupportedVersion) {
        err("manifest_version " + std::to_string(m.manifestVersion)
            + " != supported " + std::to_string(kSupportedVersion));
    }

    // ---- IIC ----
    std::set<uint8_t> iicAddrs;
    for (size_t k = 0; k < m.iic.size(); ++k) {
        const IicDeviceEntry& e = m.iic[k];
        const std::string tag = "iic[" + std::to_string(k) + "] (" + e.name + ")";
        if (e.address & 0x01u) err(tag + ": address 0x"
                                   + std::to_string(e.address) + " is not even");
        if (!iicAddrs.insert(e.address).second)
            err(tag + ": duplicate IIC address");
        switch (e.cls) {
        case IicClass::FruEeprom:
            if (e.manufacturer.empty()) err(tag + ": fru_eeprom needs 'manufacturer'");
            if (e.model.empty())        err(tag + ": fru_eeprom needs 'model'");
            break;
        case IicClass::Nvram:
            if (e.size == 0) err(tag + ": nvram 'size' must be > 0");
            break;
        case IicClass::Status:
        case IicClass::Led:
            break;
        }
    }

    // ---- PCI ----
    std::set<uint32_t> bdfs;
    for (size_t k = 0; k < m.pci.size(); ++k) {
        const PciDeviceEntry& e = m.pci[k];
        const std::string tag = "pci[" + std::to_string(k) + "] (" + e.name + ")";
        if (!bdfs.insert(bdfKey(e)).second)
            err(tag + ": duplicate BDF (hose/bus/slot/func)");
        if (e.vendor == 0x0000u || e.vendor == 0xFFFFu)
            err(tag + ": invalid vendor 0x" + std::to_string(e.vendor));
        if (e.model == PciModel::Named && e.modelName.empty())
            err(tag + ": model 'named' but no model name");
        std::set<uint8_t> barIdx;
        for (const PciBarEntry& b : e.bars) {
            if (b.index > 5) err(tag + ": BAR index > 5");
            if (!barIdx.insert(b.index).second)
                err(tag + ": duplicate BAR index "
                    + std::to_string(b.index));
        }
        if (!e.storage.empty() && e.model != PciModel::Named)
            warn(tag + ": has storage targets but model is not a named controller");
        std::set<uint16_t> chanUnit;
        for (const StorageTarget& s : e.storage) {
            if (!s.enabled) continue;          // disabled: not wired, may share a slot
            if (s.channel > 1) err(tag + ": storage channel > 1");
            if (s.unit > 1)    err(tag + ": storage unit > 1");
            const uint16_t cu = static_cast<uint16_t>((uint16_t(s.channel) << 8)
                                                       | uint16_t(s.unit));
            if (!chanUnit.insert(cu).second)
                err(tag + ": duplicate storage channel/unit (among enabled targets)");
            if (s.createIfMissing) {           // auto-provision a blank disk image
                if (s.type != StorageType::AtaDisk)
                    err(tag + ": create_if_missing only valid on a writable ata_disk");
                if (s.sizeBytes == 0)
                    err(tag + ": create_if_missing requires a non-zero 'size'");
                else if (s.sizeBytes % 512u != 0)
                    err(tag + ": create_if_missing 'size' must be a multiple of 512");
            }
        }
    }

    // ---- DS10 presence warnings (a partial board is legal but usually a slip) ----
    if (m.platform == "DS10") {
        auto haveIic = [&](uint8_t a) {
            for (const IicDeviceEntry& e : m.iic) if (e.address == a) return true;
            return false;
        };
        if (!haveIic(0x70)) warn("DS10 has no IIC 0x70 (iic_system0) -- build_power_hw will fail");
        if (!haveIic(0x72)) warn("DS10 has no IIC 0x72 (iic_system1) -- build_power_hw will fail");
        if (!haveIic(0xA2)) warn("DS10 has no IIC 0xA2 (iic_smb0) -- FRU descriptor will be empty");
    }

    return !hardError;
}

// ---------------------------------------------------------------------------
// defaultDs10Manifest -- the compiled-in fallback / reference board.
// PCI identity fields are _PROVISIONAL (spec 7.5) pending a real hw dump.
// ---------------------------------------------------------------------------
DeviceManifest PlatformConfig::defaultDs10Manifest()
{
    DeviceManifest m;
    m.manifestVersion = kSupportedVersion;
    m.platform        = "DS10";

    auto status = [](const char* name, uint8_t addr) {
        IicDeviceEntry e; e.name = name; e.address = addr;
        e.cls = IicClass::Status; e.statusByte = 0x00; return e;
    };
    auto fru = [](const char* name, uint8_t addr, const char* model) {
        IicDeviceEntry e; e.name = name; e.address = addr;
        e.cls = IicClass::FruEeprom; e.manufacturer = "DEC";
        e.model = model; e.partClass = "54-"; e.serial = "";
        e.revisionRo = 0x20; e.revisionRw = 0x21; return e;
    };
    auto nvram = [](const char* name, uint8_t addr) {
        IicDeviceEntry e; e.name = name; e.address = addr;
        e.cls = IicClass::Nvram; e.size = 256; return e;
    };

    m.iic.push_back(status("iic_system0", 0x70));
    m.iic.push_back(status("iic_system1", 0x72));
    m.iic.push_back(fru("iic_smb0", 0xA2, "DS10"));
    m.iic.push_back(fru("iic_cpu0", 0xA4, "EV6"));
    m.iic.push_back(nvram("iic_rcm_nvram0", 0xC0));

    // PCI: identity values _PROVISIONAL -- confirm against hw dump.
    {
        PciDeviceEntry e;
        e.name = "cypress_isa"; e.model = PciModel::Named; e.modelName = "cypress_isa";
        e.hose = 0; e.bus = 0; e.slot = 5; e.func = 0;
        e.vendor = 0x1080; e.device = 0xC693; e.classCode = 0x060100;  // _PROVISIONAL
        e.optionRom = false; e.interruptPin = 0;
        m.pci.push_back(e);
    }
    {
        PciDeviceEntry e;
        e.name = "cypress_ide"; e.model = PciModel::Named; e.modelName = "cypress_ide";
        e.hose = 0; e.bus = 0; e.slot = 5; e.func = 1;
        e.vendor = 0x1080; e.device = 0xC693; e.classCode = 0x010100;  // _PROVISIONAL prog-if
        e.optionRom = false; e.interruptPin = 0;       // legacy fixed ports; named model owns cfg
        StorageTarget disk;                            // primary master = dqa0 (bootable ATA disk)
        disk.channel = 0; disk.unit = 0; disk.lun = 0;
        disk.type = StorageType::AtaDisk; disk.model = "EMULATR VIRTUAL DISK";
        disk.media = "";                               // bare filename; resolves vs [Storage] diskDir
        e.storage.push_back(disk);
        StorageTarget cd;                              // primary slave = dqa1 (ATAPI CD)
        cd.channel = 0; cd.unit = 1; cd.lun = 0;
        cd.type = StorageType::AtapiCdrom; cd.model = "EMULATR VIRTUAL CDROM";
        e.storage.push_back(cd);                       // no media
        m.pci.push_back(e);
    }
    // symbios_scsi DROPPED 2026-06-09 -- the PC264 on-board SCSI is Adaptec (aic78xx),
    // not Symbios/NCR. d06 modeled at Tier-A (Adaptec identity) when the PCI consumer
    // lands; bootable SCSI = QLogic ISP/KZPBA (Tier-B, regime-3). See
    // journals/PCI_Fabric_Section7_Proposal_20260609.md.
    {
        PciDeviceEntry e;
        e.name = "de500_tulip"; e.model = PciModel::Generic;
        e.hose = 0; e.bus = 0; e.slot = 7; e.func = 0;
        e.vendor = 0x1011; e.device = 0x0019; e.classCode = 0x020000;  // _PROVISIONAL
        PciBarEntry b0; b0.index = 0; b0.isMem = false; b0.size = 0x80; // _PROVISIONAL
        PciBarEntry b1; b1.index = 1; b1.isMem = true;  b1.size = 0x80; // _PROVISIONAL
        e.bars.push_back(b0); e.bars.push_back(b1);
        e.optionRom = false; e.interruptPin = 1;
        m.pci.push_back(e);
    }

    return m;
}


// ===========================================================================
// P2 synthesis
// ===========================================================================
namespace {

// JEDEC-21C checksum: low byte of the sum of bytes [start, endInclusive].
uint8_t jedecSum(const std::array<uint8_t, 256>& d, int start, int endInclusive)
{
    unsigned sum = 0;
    for (int i = start; i <= endInclusive; ++i) sum += d[static_cast<size_t>(i)];
    return static_cast<uint8_t>(sum & 0xFFu);
}

// Copy up to `max' bytes of `src' into d[off..].  Stops at the NUL terminator.
void putStr(std::array<uint8_t, 256>& d, int off, const std::string& src, int max)
{
    int n = static_cast<int>(src.size());
    if (n > max) n = max;
    for (int i = 0; i < n; ++i) d[static_cast<size_t>(off + i)] = static_cast<uint8_t>(src[static_cast<size_t>(i)]);
}

// Little-endian field writers into a raw 256-byte config array.
void putLE16(std::array<uint8_t, 256>& d, int off, uint16_t v)
{
    d[static_cast<size_t>(off)]     = static_cast<uint8_t>(v & 0xFFu);
    d[static_cast<size_t>(off + 1)] = static_cast<uint8_t>((v >> 8) & 0xFFu);
}

// JEDEC field offsets (apisrm jedec_def.sdl, dimensions summed).
constexpr int kJManufLoc   = 0x48;   // manuf_location          (1)
constexpr int kJPartClass  = 0x49;   // manuf_part_class span   (18 read by fw)
constexpr int kJAssemSer   = 0x5F;   // assembly_serialnum      (4, 6-bit packed)
constexpr int kJDateY      = 0x5D;   // manuf_date_y            (1)
constexpr int kJDateM      = 0x5E;   // manuf_date_m            (1)
constexpr int kJAlias      = 0x63;   // manuf_spec_alias        (16; mfr TLV)
constexpr int kJModel      = 0x73;   // manuf_spec_model        (10; model TLV)
constexpr int kJDecId1     = 0x7D;   // manuf_spec_dec_JEDEC_ID (1) = 0xB5 DEC
constexpr int kJRevRo      = 0x7E;   // revision_ro_data        (1)
constexpr int kJDecId2     = 0xFD;   // dec_flag_id             (1) = 0xB5 DEC
constexpr int kJRevRw      = 0xFE;   // rev_rw_area             (1)
constexpr int kJCksum0     = 0x3F;   // checksum0to62   = sum(0x00..0x3E)
constexpr int kJCksum1     = 0x7F;   // checksum64to126 = sum(0x40..0x7E)
constexpr int kJCksum2     = 0xFF;   // checksum128to254= sum(0x80..0xFE)
constexpr uint8_t kDecJedecId = 0xB5;

} // namespace

std::array<uint8_t, 256> synthesizeFruImage(const IicDeviceEntry& e)
{
    std::array<uint8_t, 256> d{};        // zero-filled
    if (e.cls != IicClass::FruEeprom) return d;

    // manuf_location: 2 = "AY" prefix in JedecSerialNum (deterministic serial).
    d[kJManufLoc] = 0x02;

    putStr(d, kJPartClass, e.partClass, 18);
    putStr(d, kJAlias,     e.manufacturer, 16);
    putStr(d, kJModel,     e.model, 10);

    d[kJDecId1] = kDecJedecId;
    d[kJRevRo]  = e.revisionRo;
    d[kJDecId2] = kDecJedecId;
    d[kJRevRw]  = e.revisionRw;

    // assembly_serialnum: pack up to 5 chars of `serial' as 6-bit ASCII (the
    // inverse of JedecSerialNum), low date bytes fixed -> a stable `show fru'.
    if (!e.serial.empty()) {
        uint32_t packed = 0;
        int n = static_cast<int>(e.serial.size());
        if (n > 5) n = 5;
        for (int i = 0; i < n; ++i) {
            uint32_t c = static_cast<uint8_t>(e.serial[static_cast<size_t>(i)]);
            c = (c >= 32) ? (c - 32) & 0x3Fu : 0u;
            packed |= (c << (6 * i));
        }
        d[kJAssemSer]     = static_cast<uint8_t>(packed & 0xFFu);
        d[kJAssemSer + 1] = static_cast<uint8_t>((packed >> 8) & 0xFFu);
        d[kJAssemSer + 2] = static_cast<uint8_t>((packed >> 16) & 0xFFu);
        d[kJAssemSer + 3] = static_cast<uint8_t>((packed >> 24) & 0xFFu);
    }
    d[kJDateY] = 0x00;
    d[kJDateM] = 0x00;

    // Three JEDEC-21C segment checksums, computed last (cover the bytes set above).
    d[kJCksum0] = jedecSum(d, 0x00, 0x3E);
    d[kJCksum1] = jedecSum(d, 0x40, 0x7E);
    d[kJCksum2] = jedecSum(d, 0x80, 0xFE);
    return d;
}

PciConfigImage synthesizePciConfig(const PciDeviceEntry& e)
{
    PciConfigImage img;
    std::array<uint8_t, 256>& d = img.cfg;

    putLE16(d, 0x00, e.vendor);
    putLE16(d, 0x02, e.device);
    // 0x04 command / 0x06 status: left 0 (firmware programs as needed).
    d[0x08] = e.revision;
    d[0x09] = static_cast<uint8_t>( e.classCode        & 0xFFu);  // prog-if
    d[0x0A] = static_cast<uint8_t>((e.classCode >> 8)  & 0xFFu);  // subclass
    d[0x0B] = static_cast<uint8_t>((e.classCode >> 16) & 0xFFu);  // base class
    d[0x0E] = 0x00;                                               // header type 0
    putLE16(d, 0x2C, e.subsysVendor);
    putLE16(d, 0x2E, e.subsysId);
    // 0x30 expansion ROM BAR: 0 when optionRom=false (no option-ROM scan).
    d[0x3D] = e.interruptPin;                                     // interrupt pin

    for (const PciBarEntry& b : e.bars) {
        if (b.index > 5) continue;
        const int off = 0x10 + static_cast<int>(b.index) * 4;
        uint32_t typeBits;
        uint32_t typeMask;
        if (b.isMem) {
            typeMask = 0x0Fu;
            typeBits = b.prefetch ? 0x08u : 0x00u;               // 32-bit mem
        } else {
            typeMask = 0x03u;
            typeBits = 0x01u;                                    // I/O space
        }
        // Unprogrammed base: only the read-only type bits are set.
        d[static_cast<size_t>(off)]     = static_cast<uint8_t>(typeBits & 0xFFu);
        d[static_cast<size_t>(off + 1)] = 0;
        d[static_cast<size_t>(off + 2)] = 0;
        d[static_cast<size_t>(off + 3)] = 0;
        // Size-probe readback mask: ~(size-1) with type bits preserved.
        uint32_t mask = 0;
        if (b.size != 0) {
            mask = (~(b.size - 1u) & ~typeMask) | typeBits;
        }
        img.barMask[b.index]  = mask;
        img.barIsMem[b.index] = b.isMem;
    }
    return img;
}

} // namespace systemLib
