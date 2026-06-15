// ============================================================================
// systemLib/Snapshot.cpp -- Level 1 snapshot save/load implementation
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

#include "systemLib/Snapshot.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>   // 2026-06-02 (Task #3): std::getenv for prune gate
#include <cstring>
#include <vector>

#include <QByteArray>
#include <QDataStream>
#include <QFile>
#include <QIODevice>
#include <QString>

#include <spdlog/spdlog.h>

#include "chipsetLib/TsunamiChipset.h"
#include "chipsetLib/TsunamiVariant.h"
#include "coreLib/CpuState.h"
#include "memoryLib/GuestMemory.h"
#include "systemLib/Machine.h"
#include "systemLib/SrmLoader.h"


namespace systemLib {

// ---------------------------------------------------------------------------
// Local helpers.
// ---------------------------------------------------------------------------
namespace {

// Lightweight 64-bit FNV-1a over a byte range.  Adequate for snapshot
// integrity (we are detecting truncation / corruption, not adversarial
// tampering).  Replace with a real cryptographic digest if integrity
// threats evolve.
uint64_t fnv1a64(uint8_t const* data, size_t len) noexcept
{
    uint64_t h = 0xcbf29ce484222325ULL;
    for (size_t i = 0; i < len; ++i) {
        h ^= static_cast<uint64_t>(data[i]);
        h *= 0x100000001b3ULL;
    }
    return h;
}

// Comment buffer is zero-padded to a fixed 256-byte field so the
// header has a stable layout.
constexpr size_t kCommentSize = 256;


// Convert std::filesystem::path -> QString (UTF-8 round-trip safe on
// Windows and POSIX).
QString toQString(std::filesystem::path const& p)
{
    auto u8 = p.u8string();
    return QString::fromUtf8(reinterpret_cast<char const*>(u8.data()),
                             static_cast<int>(u8.size()));
}

} // namespace


// ---------------------------------------------------------------------------
// save
// ---------------------------------------------------------------------------
SnapshotResult save(Machine&                       machine,
                    std::filesystem::path const&   path,
                    char const*                    comment) noexcept
{
    SnapshotResult r;
    r.path = path.string();

    QFile file(toQString(path));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        r.errorMessage = "Snapshot::save: cannot open file for writing: ";
        r.errorMessage += r.path;
        SPDLOG_ERROR(r.errorMessage);
        return r;
    }

    QDataStream ds(&file);
    ds.setByteOrder(QDataStream::LittleEndian);
    ds.setVersion(QDataStream::Qt_6_0);

    // -- Header ----------------------------------------------------------
    ds.writeRawData(kSnapshotMagic, 8);
    ds << kFormatVersion;
    ds << kCpuStateVersion;
    ds << kChipsetVersion;

    uint64_t const ts =
        static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
    ds << ts;

    auto const& cpu = machine.cpu();
    ds << cpu.cycleCount;
    ds << cpu.ccOffset;

    char commentBuf[kCommentSize] = {};
    if (comment) {
        std::strncpy(commentBuf, comment, kCommentSize - 1);
    }
    ds.writeRawData(commentBuf, kCommentSize);

    // -- CpuState (raw POD bytes) ----------------------------------------
    ds.writeRawData(reinterpret_cast<char const*>(&cpu), sizeof(cpu));

    // -- GuestMemory ------------------------------------------------------
    // Walk the sparse page table and emit page-by-page bytes so the
    // total output is exactly `memSize` bytes -- preserves the existing
    // snapshot file format (no version bump).  Absent (untouched) pages
    // are emitted as zero runs from a single shared zero buffer.
    //
    // A future sparse-format upgrade can switch to page-indexed records
    // and bump the snapshot version; for now, keeping byte-equivalence
    // means existing predig_*.axpsnap files remain loadable.  See
    // journals/Snapshots_Design_Notes.md "Sparse-aware save" follow-on
    // section for the planned format change.
    //
    auto const& mem = machine.memory();
    uint64_t const memSize = mem.sizeBytes();
    ds << memSize;

    // Page-indexed walk over the full logical span [0, memSize).  This is
    // deliberately NOT forEachPage: forEachPage visits only *allocated*
    // pages (it skips null slots), which would emit fewer than memSize
    // bytes for a sparsely-touched machine and desync the stream against
    // the page-indexed load loop below.  readBlock() zero-fills absent
    // (sparse) pages, so the output is exactly memSize bytes regardless of
    // how many pages are backed -- preserving byte-equivalence with the
    // existing file format (no version bump).
    uint64_t remaining = memSize;
    uint64_t savePa    = 0;
    uint8_t  pageBuf[memoryLib::GuestMemory::kPageSize];
    while (remaining > 0) {
        uint64_t const pageBytes =
            (remaining < memoryLib::GuestMemory::kPageSize)
              ? remaining
              : memoryLib::GuestMemory::kPageSize;
        mem.readBlock(savePa, pageBuf, pageBytes);
        ds.writeRawData(reinterpret_cast<char const*>(pageBuf),
                        static_cast<int>(pageBytes));
        savePa    += pageBytes;
        remaining -= pageBytes;
    }

    // -- Chipset wrapper --------------------------------------------------
    auto const& chip = machine.chipset();
    ds << static_cast<uint32_t>(chip.variant());
    ds << QString::fromStdString(chip.model());
    // cpuCount is captured from Cchip (the construction parameter lives
    // there) for cross-check on restore.
    ds << static_cast<int32_t>(chip.cchip().cpuCount());
    // memSize was already captured above as part of GuestMemory; capture
    // again here so the chipset block is self-checkable.
    ds << memSize;

    chip.cchip().serialize(ds);
    chip.dchip().serialize(ds);
    chip.pchip().serialize(ds);
    // kChipsetVersion 2 (2026-06-05): interrupt-chain devices --
    // UARTs + 8259 pair + DRIR<55> mirror cache.  See Snapshot.h
    // version history.
    chip.serializeDevices(ds);

    // TIG-bus device state (TsunamiTig: smir/halt/ipcr/arbiter) is
    // intentionally NOT serialized yet (decision 3, 2026-06-13): the cold
    // path through capture should leave every TIG R/W reg at reset, so a
    // defer-and-zero-init restore loses nothing.  ASSERT-GUARDED DEFER --
    // prove that here.  If this fires, a TIG reg (e.g. arb_ctrl via
    // pc264_init outtig(0xE00004), or an ipcr) is non-zero at capture and
    // WOULD be silently lost on restore: add TsunamiTig to the serialize/
    // deserialize path (bump kChipsetVersion) instead of chasing a mysterious
    // post-restore divergence.
    if (!chip.tig().isAtResetState()) {
        spdlog::warn(
            "Snapshot: TIG-bus state is non-reset at capture -- deferred "
            "serialization will LOSE it on restore; wire TsunamiTig into "
            "Snapshot (see TsunamiTig::isAtResetState).");
    }

    // -- SRM firmware staging --------------------------------------------
    // Wire format preserved post-2026-05-19 SrmDescriptor split: the
    // single-word at the palBase position now carries targetPalBase
    // (the firmware-embedded constant, == old palBase semantically).
    // initialPalBase is not on the wire -- on load it is re-derived as
    // srmLoadPa, matching how loadSrmFirmware initializes it on cold
    // boot.
    SrmDescriptor const& sd = machine.srmDescriptor();
    ds << sd.valid;
    ds << static_cast<uint64_t>(sd.sigOffset);
    ds << static_cast<uint64_t>(sd.payloadSize);
    ds << sd.targetPalBase;
    ds << sd.finalPC;
    ds << static_cast<uint64_t>(sd.jsrOffset);
    ds << machine.srmLoadPa();
    ds << machine.palImageRelocated();
    ds << machine.loadedStartPc();
    ds << machine.loadedPalMode();

    std::vector<uint8_t> const& payload = machine.srmPayload();
    uint64_t const payloadBytes = static_cast<uint64_t>(payload.size());
    ds << payloadBytes;
    if (payloadBytes > 0) {
        ds.writeRawData(reinterpret_cast<char const*>(payload.data()),
                        static_cast<int>(payloadBytes));
    }

    // -- Flush and compute checksum over all bytes written so far -------
    ds.setDevice(nullptr);
    file.flush();
    file.close();

    if (!file.open(QIODevice::ReadOnly)) {
        r.errorMessage = "Snapshot::save: cannot reopen for checksum";
        SPDLOG_ERROR(r.errorMessage);
        return r;
    }
    QByteArray const body = file.readAll();
    file.close();

    uint64_t const cksum = fnv1a64(reinterpret_cast<uint8_t const*>(body.constData()),
                                   static_cast<size_t>(body.size()));

    if (!file.open(QIODevice::Append)) {
        r.errorMessage = "Snapshot::save: cannot append checksum";
        SPDLOG_ERROR(r.errorMessage);
        return r;
    }
    {
        QDataStream df(&file);
        df.setByteOrder(QDataStream::LittleEndian);
        df.setVersion(QDataStream::Qt_6_0);
        df << cksum;
    }
    r.bytesWritten = static_cast<uint64_t>(file.size());
    file.close();

    r.success        = true;
    r.cycleAtCapture = cpu.cycleCount;

    SPDLOG_INFO("Snapshot::save: wrote {} bytes to {} (cycle={}, comment='{}')",
        r.bytesWritten, r.path,
        static_cast<unsigned long long>(cpu.cycleCount),
        comment ? comment : "");
    return r;
}


// ---------------------------------------------------------------------------
// load
// ---------------------------------------------------------------------------
SnapshotResult load(Machine&                      machine,
                    std::filesystem::path const&  path) noexcept
{
    SnapshotResult r;
    r.path = path.string();

    QFile file(toQString(path));
    if (!file.open(QIODevice::ReadOnly)) {
        r.errorMessage = "Snapshot::load: cannot open file: ";
        r.errorMessage += r.path;
        SPDLOG_ERROR(r.errorMessage);
        return r;
    }

    qint64 const totalSize = file.size();
    if (totalSize < static_cast<qint64>(sizeof(uint64_t))) {
        r.errorMessage = "Snapshot::load: file too small to contain footer";
        SPDLOG_ERROR(r.errorMessage);
        return r;
    }

    // -- Verify checksum first (read all but trailing 8 bytes) ------------
    QByteArray const body = file.read(totalSize - static_cast<qint64>(sizeof(uint64_t)));
    uint64_t storedCksum = 0;
    {
        QDataStream df(&file);
        df.setByteOrder(QDataStream::LittleEndian);
        df.setVersion(QDataStream::Qt_6_0);
        df >> storedCksum;
    }
    file.seek(0);

    uint64_t const computedCksum =
        fnv1a64(reinterpret_cast<uint8_t const*>(body.constData()),
                static_cast<size_t>(body.size()));
    if (computedCksum != storedCksum) {
        r.errorMessage = "Snapshot::load: checksum mismatch -- file corrupt";
        SPDLOG_ERROR(r.errorMessage);
        return r;
    }

    QDataStream ds(&file);
    ds.setByteOrder(QDataStream::LittleEndian);
    ds.setVersion(QDataStream::Qt_6_0);

    // -- Header ----------------------------------------------------------
    char magic[8] = {};
    ds.readRawData(magic, 8);
    if (std::memcmp(magic, kSnapshotMagic, 8) != 0) {
        r.errorMessage = "Snapshot::load: bad magic -- not a EMULATR1 file";
        SPDLOG_ERROR(r.errorMessage);
        return r;
    }

    uint32_t formatVersion   = 0;
    uint32_t cpuStateVersion = 0;
    uint32_t chipsetVersion  = 0;
    ds >> formatVersion >> cpuStateVersion >> chipsetVersion;

    if (formatVersion != kFormatVersion
        || cpuStateVersion != kCpuStateVersion
        || chipsetVersion != kChipsetVersion)
    {
        r.errorMessage = "Snapshot::load: version mismatch (file=";
        r.errorMessage += std::to_string(formatVersion);
        r.errorMessage += "/";
        r.errorMessage += std::to_string(cpuStateVersion);
        r.errorMessage += "/";
        r.errorMessage += std::to_string(chipsetVersion);
        r.errorMessage += ", expected=";
        r.errorMessage += std::to_string(kFormatVersion);
        r.errorMessage += "/";
        r.errorMessage += std::to_string(kCpuStateVersion);
        r.errorMessage += "/";
        r.errorMessage += std::to_string(kChipsetVersion);
        r.errorMessage += ")";
        SPDLOG_ERROR(r.errorMessage);
        return r;
    }

    uint64_t timestampUnix    = 0;
    uint64_t cycleAtCapture   = 0;
    uint64_t ccOffsetAtCapture = 0;
    ds >> timestampUnix >> cycleAtCapture >> ccOffsetAtCapture;

    char commentBuf[kCommentSize] = {};
    ds.readRawData(commentBuf, kCommentSize);

    // -- CpuState --------------------------------------------------------
    coreLib::CpuState cpuTmp{};
    ds.readRawData(reinterpret_cast<char*>(&cpuTmp), sizeof(cpuTmp));

    // -- GuestMemory -----------------------------------------------------
    uint64_t memSize = 0;
    ds >> memSize;
    if (memSize != machine.memory().sizeBytes()) {
        r.errorMessage = "Snapshot::load: memory size mismatch (file=";
        r.errorMessage += std::to_string(memSize);
        r.errorMessage += ", machine=";
        r.errorMessage += std::to_string(machine.memory().sizeBytes());
        r.errorMessage += ")";
        SPDLOG_ERROR(r.errorMessage);
        return r;
    }
    // Walk the sparse page table on the load side -- materialise each
    // page in turn via ensurePage() and read its bytes from the file.
    // Format-compatible with the wholesale-byte save above.  When
    // sparse-format lands, the load path will read a page-presence
    // map first and skip ensurePage for absent pages.
    {
        uint64_t loadRemaining = memSize;
        uint32_t pidx          = 0;
        while (loadRemaining > 0) {
            uint64_t const pageBytes =
                (loadRemaining < memoryLib::GuestMemory::kPageSize)
                  ? loadRemaining
                  : memoryLib::GuestMemory::kPageSize;
            uint8_t* page = machine.memory().ensurePage(pidx);
            if (!page) {
                r.errorMessage = "Snapshot::load: ensurePage failed";
                SPDLOG_ERROR(r.errorMessage);
                return r;
            }
            ds.readRawData(reinterpret_cast<char*>(page),
                           static_cast<int>(pageBytes));
            ++pidx;
            loadRemaining -= pageBytes;
        }
    }

    // -- Chipset wrapper -------------------------------------------------
    uint32_t variantU32 = 0;
    QString  modelQ;
    int32_t  cpuCountI = 0;
    uint64_t chipMemSize = 0;
    ds >> variantU32 >> modelQ >> cpuCountI >> chipMemSize;

    if (static_cast<ChipsetVariant>(variantU32) != machine.chipset().variant()) {
        r.errorMessage = "Snapshot::load: chipset variant mismatch";
        SPDLOG_ERROR(r.errorMessage);
        return r;
    }
    if (chipMemSize != memSize) {
        r.errorMessage = "Snapshot::load: chipset block memSize disagrees with memory block";
        SPDLOG_ERROR(r.errorMessage);
        return r;
    }

    machine.chipset().cchip().deserialize(ds);
    machine.chipset().dchip().deserialize(ds);
    machine.chipset().pchip().deserialize(ds);
    // kChipsetVersion 2 (2026-06-05): interrupt-chain devices.
    machine.chipset().deserializeDevices(ds);

    // -- SRM firmware staging --------------------------------------------
    SrmDescriptor sd{};
    bool sdValid = false;
    uint64_t sigOff = 0, payloadSz = 0, palBase = 0, finalPC = 0, jsrOff = 0;
    uint64_t srmLoadPa = 0;
    bool palImageRelocated = false;
    uint64_t loadedStartPc = 0;
    bool loadedPalMode = false;
    uint64_t payloadBytes = 0;

    ds >> sdValid >> sigOff >> payloadSz >> palBase >> finalPC >> jsrOff
       >> srmLoadPa >> palImageRelocated >> loadedStartPc >> loadedPalMode
       >> payloadBytes;

    std::vector<uint8_t> payload;
    if (payloadBytes > 0) {
        payload.resize(static_cast<size_t>(payloadBytes));
        ds.readRawData(reinterpret_cast<char*>(payload.data()),
                       static_cast<int>(payloadBytes));
    }

    sd.valid          = sdValid;
    sd.sigOffset      = static_cast<size_t>(sigOff);
    sd.payloadSize    = static_cast<size_t>(payloadSz);
    // Wire palBase (post-2026-05-19 split) carries targetPalBase.
    // initialPalBase is re-derived from srmLoadPa, matching how
    // loadSrmFirmware initializes it on cold boot.
    sd.targetPalBase  = palBase;
    sd.initialPalBase = srmLoadPa;
    sd.finalPC        = finalPC;
    sd.jsrOffset      = static_cast<size_t>(jsrOff);

    // -- Commit CpuState last; restore order avoids leaving the Machine
    //    half-restored on early-return error paths above. -----------------
    machine.cpu() = cpuTmp;

    // -- Push SRM staging back into Machine through its restore hook. ----
    machine.restoreSrmStaging(sd, std::move(payload), srmLoadPa,
                              palImageRelocated,
                              loadedStartPc, loadedPalMode);

    file.close();

    r.success        = true;
    r.bytesRead      = static_cast<uint64_t>(totalSize);
    r.cycleAtCapture = cycleAtCapture;

    SPDLOG_INFO("Snapshot::load: restored {} bytes from {} (cycle={}, comment='{}')",
        r.bytesRead, r.path,
        static_cast<unsigned long long>(cycleAtCapture),
        commentBuf);
    return r;
}


// ---------------------------------------------------------------------------
// autoloadLatest
// ---------------------------------------------------------------------------
SnapshotResult autoloadLatest(Machine&                      machine,
                              std::filesystem::path const&  dir) noexcept
{
    namespace fs = std::filesystem;
    SnapshotResult r;

    std::error_code ec;
    if (!fs::exists(dir, ec) || !fs::is_directory(dir, ec)) {
        // Cold boot is the expected case when no snapshots have been
        // written yet; leave success=false and an empty path so the
        // caller can distinguish "no file found" from "found but bad."
        r.errorMessage = "Snapshot::autoloadLatest: directory does not exist";
        SPDLOG_INFO("Snapshot::autoloadLatest: no '{}' directory; cold boot",
            dir.string());
        return r;
    }

    fs::path newest;
    fs::file_time_type newestMtime{};
    bool found = false;
    for (auto const& entry : fs::directory_iterator(dir, ec)) {
        if (ec) break;
        if (!entry.is_regular_file(ec)) continue;
        if (entry.path().extension() != kSnapshotExtension) continue;

        auto const mt = entry.last_write_time(ec);
        if (ec) continue;
        if (!found || mt > newestMtime) {
            newest = entry.path();
            newestMtime = mt;
            found = true;
        }
    }

    if (!found) {
        SPDLOG_INFO("Snapshot::autoloadLatest: no *.axpsnap in '{}'; cold boot",
            dir.string());
        return r;
    }

    SPDLOG_INFO("Snapshot::autoloadLatest: selected '{}' as newest",
        newest.string());
    return load(machine, newest);
}


// ---------------------------------------------------------------------------
// pruneOldSnapshots
// ---------------------------------------------------------------------------
void pruneOldSnapshots(std::filesystem::path const& dir,
                       int                          keepCount) noexcept
{
    namespace fs = std::filesystem;
    // 2026-06-02 (Task #3): RETAIN ALL SNAPSHOTS by default -- no auto-deletion.
    // The full set is our menu of predig/restart anchors (boot is time-bound; choosing
    // which to keep is a later telemetry-driven step). Prune ONLY if explicitly enabled
    // via EMULATR_SNAPSHOT_PRUNE; until then this function is a no-op.
    if (std::getenv("EMULATR_SNAPSHOT_PRUNE") == nullptr) return;
    if (keepCount < 1) keepCount = 1;

    std::error_code ec;
    if (!fs::exists(dir, ec) || !fs::is_directory(dir, ec)) return;

    struct Entry {
        fs::path           path;
        fs::file_time_type mtime;
    };
    std::vector<Entry> autoFiles;

    for (auto const& e : fs::directory_iterator(dir, ec)) {
        if (ec) break;
        if (!e.is_regular_file(ec)) continue;
        auto const& p = e.path();
        if (p.extension() != kSnapshotExtension) continue;
        // Only prune files that look auto-generated (prefix "auto_").
        // Manual / named captures are never touched.
        auto const stem = p.stem().string();
        if (stem.size() < 5 || stem.compare(0, 5, "auto_") != 0) continue;
        autoFiles.push_back({p, e.last_write_time(ec)});
    }

    if (static_cast<int>(autoFiles.size()) <= keepCount) return;

    std::sort(autoFiles.begin(), autoFiles.end(),
        [](Entry const& a, Entry const& b) { return a.mtime > b.mtime; });

    for (size_t i = static_cast<size_t>(keepCount); i < autoFiles.size(); ++i) {
        fs::remove(autoFiles[i].path, ec);
        if (!ec) {
            SPDLOG_INFO("Snapshot::pruneOldSnapshots: removed '{}'",
                autoFiles[i].path.string());
        }
    }
}

} // namespace systemLib
