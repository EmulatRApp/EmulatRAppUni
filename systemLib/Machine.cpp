// ============================================================================
// systemLib/Machine.cpp -- top-level orchestration implementation
// ============================================================================
// Project: EmulatR -- Alpha AXP / EV6 Architecture Emulator (V4)
// Copyright (C) 2025, 2026 eNVy Systems, Inc.  All rights reserved.
// Licensed under eNVy Systems Non-Commercial License v1.1
// ============================================================================

#include "systemLib/Machine.h"

#include "chipsetLib/CchipIntervalTimer.h"      // Phase C: cycle-driven fire predicate
#include "coreLib/BoxResult.h"
#include "coreLib/Ev6EntryVectors.h"            // kEntry_INTERRUPT = 0x680 etc.
#include "coreLib/FaultEventLog.h"
#include "coreLib/PalShadow.h"
#include "mmuLib/CboxEventLog.h"
#include "mmuLib/UnalignedEventLog.h"
#include "pipelineLib/PipelineDriver.h"
#include "schedLib/SmpHarness.h"
#include "schedLib/SmpDrivers.h"
#include "schedLib/AlphaCpuAgent.h"
#include "systemLib/FirmwareLoader.h"
#include "systemLib/Snapshot.h"
#include "systemLib/SrmLoader.h"
#include "systemLib/PlatformConfig.h"
#include "deviceLib/scsi/BlockMediaFactory.h"     // media_kind -> IBlockMedia (seam)
#include "traceLib/RetireProfiler.h"            // 2026-06-05: boot profiler dump


#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>
#include <iomanip>
#include <sstream>
#include <system_error>
#include <QCoreApplication>
#include <QString>
#include <spdlog/spdlog.h>


// ----------------------------------------------------------------------------
// TEMP DIAGNOSTIC (DIVERT-REI register ledger, fill side) -- REMOVE AFTER
// the nvram_get fclose(&spl_kernel) corruption is root-caused.
// Storage lives in palBoxLib/grains/PalEntries.cpp (the compare side,
// execHwRei) inside namespace palBox; declared here at global scope so
// the interval-timer divert site can record the native conserved
// registers (R2-R7, R20-R23) it is about to hand to the PAL INTERRUPT
// flow.  See the PalEntries.cpp block comment for the full rationale.
// NOTE: the register-number map (R2-R7, R20-R23) is duplicated at the
// fill site rather than extern'd -- constexpr namespace-scope arrays
// have internal linkage.
// ----------------------------------------------------------------------------
namespace palBox { namespace palDiag {
extern uint64_t g_divertPendingPc[2];
extern uint64_t g_divertPendingCyc[2];
extern uint64_t g_divertPendingReg[2][10];
extern bool     g_divertPendingLive[2];
} } // namespace palBox::palDiag
// ---- END TEMP DIVERT-REI ledger extern declarations ----

namespace systemLib {

namespace {
    

// ----------------------------------------------------------------------------
// Tsunami MMIO window: 0x800.0000.0000 .. 0xFFF.FFFF.FFFF (8 TB I/O).
// Per Tsunami HRM and chipsetLib/Tsunami21272_RegisterMap.h.
// ----------------------------------------------------------------------------
// Defined here (ahead of the hook adapters) so machineMmioRead /
// machineMmioWrite can use kTsunamiMmioBase as their forensic gate
// floor as well as the constructor using it for range registration.
constexpr uint64_t kTsunamiMmioBase = 0x80000000000ULL;
constexpr uint64_t kTsunamiMmioSize = 0x80000000000ULL;
constexpr uint64_t kTsunamiMmioEnd  = kTsunamiMmioBase + kTsunamiMmioSize;

// ----------------------------------------------------------------------------
// GuestMemory MMIO hook adapters.
// ----------------------------------------------------------------------------
// Direct chipset attach -- the MmioRegistry indirection layer is no
// longer used by Machine (deprecated 2026-05-14 per the sparse-paged
// GuestMemory rewrite; see journals/MemoryV2_Integration_Notes.md).
// Adapter shape matches the new bool-returning GuestMemory::MmioReadHook
// / MmioWriteHook signatures.
//
// Service contract:
//   - PA inside the Tsunami window -> translate to base-relative
//     offset, forward to TsunamiChipset::mmioRead/Write, return true.
//   - PA outside the window -> return false.  GuestMemory then
//     reports MemStatus::OutOfRange and the pipeline produces
//     kFaultBusError (-> MCHK trap delivery).  This is the load-
//     bearing path the OSF/1 PAL MCHK-as-yield idle loop depends on;
//     returning true here for OOR PAs corrupts firmware execution
//     (the 2026-05-17 regression).
//
// No forensic stderr in this adapter today: OOR PAs are now expected
// behavior for the firmware's intentional bus-error pattern, and
// would flood the log.  A throttled audit can be added later if we
// need to surface truly-unexpected OOR addresses; that's a Phase-B
// chipset-coverage concern, not a memory-routing concern.
// ----------------------------------------------------------------------------
bool machineMmioRead(void*     ctx,
                     uint64_t  pa,
                     uint8_t   width,
                     uint64_t& valueOut) noexcept
{
    if (pa >= kTsunamiMmioBase && pa < kTsunamiMmioEnd) {
        auto* chipset = static_cast<TsunamiChipset*>(ctx);
        valueOut = TsunamiChipset::mmioRead(chipset, pa - kTsunamiMmioBase,
                                            width);
        return true;
    }
    return false;
}

bool machineMmioWrite(void*    ctx,
                      uint64_t pa,
                      uint64_t value,
                      uint8_t  width) noexcept
{
    if (pa >= kTsunamiMmioBase && pa < kTsunamiMmioEnd) {
        auto* chipset = static_cast<TsunamiChipset*>(ctx);
        TsunamiChipset::mmioWrite(chipset, pa - kTsunamiMmioBase, value,
                                  width);
        return true;
    }
    return false;
}

// ----------------------------------------------------------------------------
// stageInterruptDivert -- shared staging recipe for the EV6 PAL
// INTERRUPT vector at `palBase + 0x680`.
// ----------------------------------------------------------------------------
//
// Two call sites in Machine::run share this state mutation:
//
//   1. The Phase C interval-timer divert (timer fired -> Cchip
//      pendingIrq2 latch -> Machine run-loop polls -> divert).
//   2. The 2026-05-13 one-shot synthetic INTERRUPT injection
//      (debugging path that armed via --inject-interrupt-at-cycle).
//
// Both stage the same CPU state transition:
//
//     excAddr  <- pc | palMode-bit
//     pc       <- palBase + 0x680
//     palMode  <- true
//     isum     <- isumMask   (caller-selected EI[] cause bit;
//                            EV6__ISUM__EI__S = ^x21 = 33, so
//                            EI[n] = 1 << (33 + n).  Timer = EI[2]
//                            = 1<<35 = IRQ_CLK; error = EI[0] = 1<<33
//                            = IRQ_ERR.  ev6_defs.mar)
//
// The savedPc carries the PALmode bit in bit 0 so HW_REI in the
// PAL handler returns the CPU to its original mode (caller may
// have been in PAL or kernel).  isum is SET (not OR'd) -- matches
// the synthetic injection's prior behavior and mirrors real
// hardware's IPL-driven re-vector where the vector entry sees a
// clean isum state for that interrupt source.
//
// Caller MUST verify cpu.palBase != 0 before invoking; the helper
// does not gate so the precondition stays explicit at each call
// site (lets the caller log a distinct skip message when palBase
// hasn't been set yet by firmware).
//
// CHANGE 2026-05-28 (vector-offset fix): target changed from
// palBase + 0x100 to palBase + 0x680.  The previous 0x100 was wrong
// for EV6 -- per the silicon-determined vector layout in
// ev6_defs.mar (TRAP__START + EV6__INTERRUPT_ENTRY = ^x680) and the
// START_HW_VECTOR macro expansion in ev6_pal_macros.mar, the
// INTERRUPT entry lives at offset 0x680.  Offset 0x100 is
// DTBM_DOUBLE_3 -- the data-TB double-miss-on-granularity-hint-3
// handler -- which silently consumed every interval-timer divert
// (and every synthetic injection) without ever ACK'ing the
// underlying interrupt latch.  Symptom: post Phase B + kPfnWidth
// fix, firmware enabled interval-timer interrupts via DIM0
// (`CSR Cchip:DIM0 W val=0xc080000000000000`), the Cchip timer fired
// at its 2^kCchipTimerBit period, the divert ran into DTBM_DOUBLE_3
// which "succeeded" (PTs are real by that point) and HW_REI'd back
// without ever invoking sys__int_clk -> MISC<ITINTR<0>> W1C -- so
// the level-pinned timer kept re-firing every 266K cycles, parking
// the firmware in a steady ASSERT-every-266K-cycles holding pattern
// at native PC 0x1c69c8 (the CALL_PAL #0x3f stall in the timer-wait
// outer loop).  See [[phase-b-verified-and-os-pal-takeover]] memory
// and task #37.  Affected all three call sites: timer divert,
// b_irq<0> NXM divert (latent post Phase B), --inject-interrupt
// synthetic path.
// ----------------------------------------------------------------------------
inline void stageInterruptDivert(coreLib::CpuState& cpu, uint64_t isumMask) noexcept
{
    // CHANGE 2026-05-21: cpu.pc already carries the PALmode bit (PC<0>),
    // so the saved PC is just cpu.pc -- no separate flag to OR in.
    uint64_t const savedPc = cpu.pc;
    cpu.excAddr = savedPc;
    // CHANGE 2026-05-28: target is INTERRUPT vector (palBase + 0x680),
    // NOT DTBM_DOUBLE_3 (palBase + 0x100).  See block comment above.
    // 2026-05-29: replaced magic 0x680 with coreLib::ev6::kEntry_INTERRUPT
    // (Ev6EntryVectors.h) so the offset stays in one HRM-cited place.
    cpu.pc      = cpu.palBase + coreLib::ev6::kEntry_INTERRUPT;
    // Use palModeEnter (not direct assignment) so the EV6 R4-R7 / R20-R23
    // shadow swap fires when I_CTL[SDE] is set.  PAL handlers expect
    // their private scratch view of those 8 registers on entry; without
    // the swap, native-context values leak in and handlers misbehave.
    coreLib::palModeEnter(cpu);
    // 2026-05-30: caller selects the EV6 ISUM external-interrupt cause bit.
    // EV6__ISUM__EI__S = 33, so EI[n] = bit (33 + n): EI[0]=IRQ_ERR (1<<33),
    // EI[2]=IRQ_CLK / interval timer (1<<35).  The interval-timer divert site
    // MUST pass EI[2]; staging EI[0] mis-dispatches every tick to sys__int_err
    // instead of sys__int_clk and the SRM clock self-test reports
    // "*** no timer interrupts on CPU 0 ***".  See journals/
    // RCA_no_timer_interrupts_20260530.txt.
    cpu.isum    = isumMask;            // caller-selected EI[] cause bit
}

// Map cpu.lastFaultCode + cpu.halted into a StopReason.  Pure
// derivation; no side effects.
StopReason classifyStop(coreLib::CpuState const& cpu) noexcept
{
    if (!cpu.halted) {
        return StopReason::MaxCyclesExceeded;
    }
    switch (cpu.lastFaultCode) {
        case coreLib::kFaultHalt:         return StopReason::HaltedClean;
        case coreLib::kFaultOpcDec:       return StopReason::OpcDecFault;
        case coreLib::kFaultUnaligned:    return StopReason::UnalignedFault;
        case coreLib::kFaultAcv:          return StopReason::AcvFault;
        case coreLib::kFaultDtbMiss:
        case coreLib::kFaultItbMiss:
        case coreLib::kFaultFor:
        case coreLib::kFaultFow:
        case coreLib::kFaultFoe:
        case coreLib::kFaultBusError:
        case coreLib::kFaultNonCanonical:
        case coreLib::kFaultPrivileged:
        case coreLib::kFaultUnimplemented:
        default:
            return StopReason::MemFault;
    }
}

} // anonymous namespace


// ============================================================================
// Construction
// ============================================================================

// ============================================================================
// makeCom1Cfg -- COM1 SRMConsoleDevice configuration                2026-05-29
// ============================================================================
// Returns the Config the in-class initializer (Machine.h) feeds into
// m_consoleCfg.  Declared static in Machine.h so it can run during
// member initialisation -- this is the ONLY place port/autoLaunchPutty/
// puttyPath are set, because SRMConsoleDevice's constructor captures
// Config by value (m_config(config)) and any later mutation to
// m_consoleCfg is a dead write that doesn't propagate.
//
// Values:
//   port            = 10023  -- unprivileged, mnemonic for "telnet
//                              on Alpha".  Default Config port 23 is
//                              privileged and was the bug we fixed.
//   autoLaunchPutty = true   -- spawn a PuTTY window automatically on
//                              Machine construction via QProcess::
//                              startDetached.  Detached, so it outlives
//                              the emulator and keeps the session
//                              record visible after exit.
//   puttyPath       = "PuTTY.exe" -- PATH-resolved.  If PuTTY is not on
//                              PATH, launchPutty logs a non-fatal warning
//                              and the emulator continues with the
//                              stderr-mirror diagnostic only.
// ============================================================================
SRMConsoleDevice::Config Machine::makeCom1Cfg(
    emulatr::config::EmulatorSettings const& settings) noexcept
{
    SRMConsoleDevice::Config cfg;
    // SSOT (Slice A): console config comes from the threaded EmulatorSettings,
    // loaded once in main from EmulatrV4.ini.  Struct defaults live in
    // SrmConsoleSettings (port 10023, never the privileged 23).
    cfg.port            = settings.srmConsole.port != 0
                              ? settings.srmConsole.port
                              : static_cast<quint16>(10023);
    cfg.autoLaunchPutty = true;
    cfg.puttyPath       = QString::fromStdString(
                              settings.srmConsole.puttyPath.empty()
                                  ? std::string("PuTTY.exe")
                                  : settings.srmConsole.puttyPath);
    // EMULATR_CONSOLE_PORT overrides the ini at runtime -- e.g. when a prior
    // run's PuTTY/listener still holds the port, or to run instances side by
    // side.  The PuTTY auto-launch (SRMConsoleDevice) reads m_config.port, so it
    // follows the override automatically.  Out-of-range values are ignored.
    if (char const* portEnv = std::getenv("EMULATR_CONSOLE_PORT")) {
        int const v = std::atoi(portEnv);
        // v == 0 is honored as EPHEMERAL (OS-assigned): SRMConsoleDevice binds
        // any free port and reports it via boundPort().  The test runner sets
        // EMULATR_CONSOLE_PORT=0 so coexisting Machines (e.g. snapshot round-trip
        // builds source + restored at once) never collide on the fixed 10023.
        if (v >= 0 && v < 65536) {
            cfg.port = static_cast<quint16>(v);
        }
    }
    // 2026-06-06: EMULATR_NO_PUTTY suppresses the auto-launched PuTTY so a
    // scripted/headless plink can be the sole console client (the single-
    // client console otherwise rejects a second connection).  Checked here
    // because makeCom1Cfg runs at construction, before main parses argv.
    if (std::getenv("EMULATR_NO_PUTTY") != nullptr) {
        cfg.autoLaunchPutty = false;
    }
    return cfg;
}

Machine::Machine(uint64_t memSize, emulatr::config::EmulatorSettings settings)
    : m_settings(std::move(settings))
    // Phase-2 T4: agent0 owns the CpuState; m_cpu aliases it.  m_agent0 must be
    // initialized BEFORE m_cpu (declaration + init-list order both honor this).
    , m_agent0(*this, /*cpuId/slot*/ 0)
    , m_cpu(m_agent0.cpu())
    , m_translator()
    // 2026-05-29: COM1 backend wiring.  m_consoleCfg is fully populated
    // by the in-class initializer (Machine.h) which calls
    // makeCom1Cfg() above; m_com1Backend's ctor then snapshots that
    // populated value into its own m_config member.
    , m_com1Backend(m_consoleCfg)
    // 2026-06-17: construct the chipset from the ini model string (the existing
    // model-string ctor) instead of a hardcoded Tsunami/1.  This populates the
    // chipset's m_model so wireDevices() can select the correct south bridge
    // (Cypress for DS10/DS20, ALi M1543C for ES40/ES45) and uses the ini
    // cpuCount.  DS10 -> Tsunami, cpuCount 1: byte-identical to the prior line.
    // (m_settings is initialized above, so .system is valid here.)
    , m_chipset(m_settings.system.model, m_settings.system.cpuCount, memSize)
    , m_mmio()
{
    // Bring up the TCP listen socket, launch PuTTY, migrate the backend
    // to its own QThread.  Failure to listen is logged but non-fatal --
    // Uart16550::writeTHR stderr-mirrors every byte regardless.
    if (!m_com1Backend.start()) {
        std::fprintf(stderr,
                     "Machine: SRMConsoleDevice failed to start on port %u "
                     "(stderr mirror still active for COM1 output).\n",
                     static_cast<unsigned>(m_consoleCfg.port));
        std::fflush(stderr);
    }

    // 2026-06-03: deterministic TOY clock.  Bind the CPU cycle counter as
    // the RTC time source (time = fixed epoch + cycleCount / cyclesPer-
    // Second); the RTC samples the pointer lazily at index-write time on
    // the CPU thread, so no synchronization is required.  No host clock
    // anywhere -- identical boots read identical TOY bytes.  See
    // deviceLib/Tsunami/ToyRtc.h DETERMINISM INVARIANT.
    // L1 SEAM (Phase 2): the RTC time source is the SYSTEM clock, not a CPU's
    // PCC.  Today systemNow() == m_cpu.cycleCount so binding the pointer here is
    // behavior-identical; STEP 3/4 re-homes this to dedicated system-clock
    // storage (the RTC samples a uint64_t* lazily, so the re-home is a pointer
    // swap).  Do NOT bind this to a second CPU's cycleCount when CPU1 lands.
    m_chipset.rtc().bindCycleSource(&m_cpu.cycleCount);

    // CpuState default-init is sufficient for a freshly constructed
    // machine: regfiles all zero, pc = 0, palMode = false, halted =
    // false, lastFaultCode = 0.  Memory is zero-initialised by
    // GuestMemory's constructor.

    // ----------------------------------------------------------------
    // Wire MMIO routing: GuestMemory hooks point directly at the
    // chipset via a thin adapter that translates absolute PA to the
    // Tsunami-base-relative offset the chipset expects.  The
    // MmioRegistry layer (m_mmio member, retained for backwards-
    // compatibility with test_mmio_csc_roundtrip.cpp) is no longer
    // wired into the GuestMemory hook path -- see
    // journals/MemoryV2_Integration_Notes.md, the "MmioRegistry
    // retirement" section.
    //
    // After this attach, any PA load/store from the pipeline that
    // falls outside RAM and PAL scratch is delivered to
    // machineMmioRead/Write here, range-checked against the Tsunami
    // window, and forwarded to TsunamiChipset::mmioRead/Write with
    // the relative offset.  Out-of-window accesses log a throttled
    // diagnostic and return 0 / drop the write.
    // ----------------------------------------------------------------
    // m_memory.attachMmioHooks(&m_chipset,
    //                          &machineMmioRead,
    //                          &machineMmioWrite);

    // TODO(deprecated): m_mmio is retained as a member only because
    // test_mmio_csc_roundtrip.cpp still exercises the MmioRegistry
    // class in isolation.  Once that test migrates to the direct
    // chipset-attach path used here, the member and the class can
    // be removed.  No production code path consults m_mmio anymore.
    (void) m_mmio;

    // Wire COM1's host sink so the SRM firmware's UART output (writes to
    // the 16550 THR at port 0x3F8, routed through the chipset sparse-I/O
    // path) is forwarded to stdout.  This is the only thing the firmware
    // needs to print its console output; input is not yet modelled.
    m_chipset.com1().setBackend(&m_com1Backend);

    // ----------------------------------------------------------------
    // Bind the TIG-bus flash / NVRAM backing file (host-file NVRAM, D2).
    // Default 'ds10_flash.rom' in CWD (sibling to the snapshots dir),
    // overridable via EMULATR_FLASH_ROM.  A missing or wrong-size image
    // leaves the device at factory 0xFF -- the correct first-boot state:
    // the firmware initializes its own NVRAM defaults and the debounce
    // poll persists them.  Flash holds configuration only, never firmware
    // (C1); the SRM image stays on the decompressor load path.
    // ----------------------------------------------------------------
    {
        char const* const envPath = std::getenv("EMULATR_FLASH_ROM");
        std::string const flashPath =
            envPath ? std::string(envPath) : std::string("ds10_flash.rom");
        m_chipset.flash().loadRaw(flashPath);
    }

    // ----------------------------------------------------------------
    // Populate the IIC bus from the platform manifest (P3, device-enum
    // scaffold).  Machine is the only layer high enough to call
    // PlatformConfig (systemLib): it synthesizes each device's on-wire image
    // and pushes a neutral IicDevice list down into the chipset controller.
    // Runs in the ctor, BEFORE any snapshot autoload, so restored content
    // lands on an already-configured (identity-correct) bus.  Path:
    // EMULATR_PLATFORM_CONFIG, default ds10_platform.json; a missing/invalid
    // file falls back to the compiled-in default DS10 manifest.
    // ----------------------------------------------------------------
    {
        char const* const mp = std::getenv("EMULATR_PLATFORM_CONFIG");
        std::string manifestPath;
        if (mp) {
            manifestPath = std::string(mp);
        } else {
            // OS-suffixed manifest so one tree carries both host variants:
            // ds10_platform.win on Windows, ds10_platform.linux elsewhere (the
            // user renamed the file per host).  Content is plain JSON regardless
            // of extension; PlatformConfig::load parses by content, not suffix.
#ifdef _WIN32
            char const* const kManifestLeaf = "ds10_platform.win";
#else
            char const* const kManifestLeaf = "ds10_platform.linux";
#endif
            if (QCoreApplication::instance() != nullptr) {
                // Resolve NEXT TO the executable (build/install dir), not the
                // launch CWD, so the POST_BUILD-copied manifest is found wherever
                // Emulatr is started from.  Mirrors IniLoader's applicationDirPath().
                manifestPath = (QCoreApplication::applicationDirPath()
                                + "/" + kManifestLeaf).toStdString();
            } else {
                manifestPath = kManifestLeaf;          // no Qt app (unit tests)
            }
        }
        ManifestLoadResult const mr = PlatformConfig::load(manifestPath);

        std::vector<IicPcf8584::IicDevice> devs;
        devs.reserve(mr.manifest.iic.size());
        for (IicDeviceEntry const& e : mr.manifest.iic) {
            IicPcf8584::IicDevice d;
            d.address = e.address;
            switch (e.cls) {
            case IicClass::FruEeprom:
                d.kind  = IicPcf8584::Kind::FruEeprom;
                d.image = synthesizeFruImage(e);
                break;
            case IicClass::Nvram:
                d.kind = IicPcf8584::Kind::Nvram;       // image stays zero
                break;
            case IicClass::Status:
            case IicClass::Led:
                d.kind     = IicPcf8584::Kind::Status;
                d.image[0] = e.statusByte;
                break;
            }
            devs.push_back(d);
        }
        m_chipset.iic().configureDevices(devs);

        if (mr.usedDefault) {
            spdlog::warn("PlatformConfig: manifest '{}' unusable ({}); "
                         "using built-in default DS10 bus",
                         manifestPath, mr.error);
        }

        // ------------------------------------------------------------
        // Storage attach (2026-06-11, dqa0 boot): for each AtaDisk target
        // behind a named storage controller, resolve the manifest media
        // FILENAME against [Storage] diskDir (absolute media used as-is;
        // empty media = no disk) and attach the raw image to the CY82C693
        // IDE.  The ATAPI CD is wired in the chipset (no-media) and is not
        // path-driven here.  Runs in the ctor, before any snapshot autoload.
        // ------------------------------------------------------------
        for (PciDeviceEntry const& dev : mr.manifest.pci) {
            for (StorageTarget const& st : dev.storage) {
                if (!st.enabled) continue;        // disabled target: not wired
                if (st.type != StorageType::AtaDisk &&
                    st.type != StorageType::AtapiCdrom) continue;
                bool const isDisk = (st.type == StorageType::AtaDisk);
                char const* const what = isDisk ? "ATA disk" : "ATAPI CD";
                bool const isHost  = (st.media_kind == "host");

                // Resolve a file media path against [Storage] diskDir.  A host
                // selector (media_kind "host", e.g. "host:0") is passed through
                // untouched for the platform resolver.
                std::string mediaSpec = st.media;
                if (!isHost && !mediaSpec.empty()) {
                    std::filesystem::path p(mediaSpec);
                    if (p.is_relative() && !m_settings.storage.diskDir.empty())
                        p = std::filesystem::path(m_settings.storage.diskDir) / p;
                    mediaSpec = p.string();
                }
                if (!isHost && mediaSpec.empty()) continue;   // no image -> empty drive

                // media_kind factory: image/iso -> FileBlockMedia; host -> Phase B;
                // unknown -> fail closed (no silent fallback).  blockSize/readOnly
                // are the drive's: 512 RW disk, 2048 RO CD.  createBytes>0 (writable
                // disk + create_if_missing) auto-provisions a blank image if absent.
                uint32_t const blockSize = isDisk ? 512u : 2048u;
                bool     const readOnly  = !isDisk;
                uint64_t const createBytes =
                    (isDisk && st.createIfMissing) ? st.sizeBytes : 0ull;
                bool const willCreate = createBytes > 0 &&
                    !std::filesystem::exists(std::filesystem::path(mediaSpec));
                std::string err;
                auto media = scsi::makeBlockMedia(st.media_kind, mediaSpec,
                                                  blockSize, readOnly, createBytes, err);
                if (media && willCreate)
                    spdlog::info("Storage: created blank disk image '{}' ({} bytes)",
                                 mediaSpec, static_cast<unsigned long long>(createBytes));
                if (!media) {
                    spdlog::warn("Storage: {} '{}' (kind '{}') not attached: {}; "
                                 "IDE ch{} unit{} left empty",
                                 what, mediaSpec,
                                 st.media_kind.empty() ? "image" : st.media_kind,
                                 err, int(st.channel), int(st.unit));
                    continue;
                }
                bool const ok = isDisk
                    ? m_chipset.setDiskMedia(st.channel, st.unit, std::move(media))
                    : m_chipset.setCdMedia(std::move(media));
                if (ok)
                    spdlog::info("Storage: attached {} '{}' to IDE ch{} unit{}",
                                 what, mediaSpec, int(st.channel), int(st.unit));
                else
                    spdlog::warn("Storage: {} '{}' attach rejected; "
                                 "IDE ch{} unit{} left empty",
                                 what, mediaSpec, int(st.channel), int(st.unit));
            }
        }
    }
}

// ============================================================================
// ~Machine -- clean-shutdown NVRAM backstop (D1)
// ============================================================================
Machine::~Machine()
{
    // Persist any dirty flash the debounce poll has not yet flushed.  Safe
    // here: member destructors run after this body, so m_chipset is alive.
    m_chipset.flash().forceFlush();
}


// ============================================================================
// loadFirmware
// ============================================================================

bool Machine::loadFirmware(std::filesystem::path const& path,
                           uint64_t                     loadPa,
                           uint64_t                     startPa)
{
    LoadResult const r = loadRawBinary(m_chipset.guestMemory(), path, loadPa, startPa);
    if (!r.ok) {
        m_lastLoadError = r.error;
        return false;
    }
    m_loadedStartPc = r.startPc;
    m_loadedPalMode = r.palMode;
    m_srmDescriptor = SrmDescriptor{};   // raw load -> no SRM descriptor
    m_srmPayload.clear();
    m_srmLoadPa     = 0;
    m_lastLoadError.clear();
    return true;
}


// ============================================================================
// IFetchOverride: SRM payload buffer fallback for the IF stage.
// ============================================================================

bool Machine::tryFetch(uint64_t pa, uint32_t& out) const noexcept
{
    // Only active when an SRM image has been loaded.  Raw-binary loads
    // leave m_srmDescriptor.valid = false, so tryFetch always returns
    // false for non-SRM scenarios -- IF reads pass straight through to
    // GuestMemory.read4 in the normal pipeline path.
    if (!m_srmDescriptor.valid) {
        return false;
    }

    // Range check: pa must be inside the loaded stub region and have
    // four bytes available before the payload buffer ends.
    uint64_t const payloadSize = m_srmPayload.size();
    if (pa < m_srmLoadPa) {
        return false;
    }
    uint64_t const offset = pa - m_srmLoadPa;
    if (offset + 4 > payloadSize) {
        return false;
    }

    // Aligned 32-bit read out of the immutable buffer.  memcpy avoids
    // the strict-aliasing question and works for any host alignment;
    // the cost of the copy is one instruction at this size.
    std::memcpy(&out, m_srmPayload.data() + offset, sizeof(out));
    return true;
}


// ============================================================================
// IFetchOverride: Step D pre-fetch hook -- one-shot PAL image relocation.
// ============================================================================
//
// Fires the moment the CPU is about to fetch from the post-decompression
// entry PC the .exe declared (descriptor.entryPa() = palBase + finalPC).
// At that point the decompressor has finished writing decompressed
// PALcode into the mirror PA window [0, kPalRelocSize); we mirror that
// window up to [palBase, palBase + kPalRelocSize) so the impending IF
// read and every subsequent CALL_PAL dispatch lands in real PALcode.
//
// Single-shot: m_palImageRelocated gates re-firing so steady-state hot-
// path cost is one boolean check.  Compiler can hoist the check across
// loop bodies; branch is predicted-taken (the common case) once relocation
// has happened.
// ============================================================================

// ============================================================================
// canAcceptInterrupt
// ============================================================================
//
// Phase C+ interrupt-arbitration policy.  See Machine.h for the full
// architectural rationale, the Phase D extension TODO, and the layering
// note.  This MVP body gates exclusively on PAL relocation completion;
// `irqLevel` is currently unused and present in the signature solely so
// call sites pre-document the priority being arbitrated and the Phase D
// drop-in is a one-line body change with no call-site touch.
//
// Hot-path note: invoked once per retire from Machine::run.  Two field
// loads + one logical AND + one compare-to-zero -- branch-predicted-
// false during the cold-boot window (cyc < 4.2M) and branch-predicted-
// true for the rest of the run.  No allocation, no atomics.
// ============================================================================
bool Machine::canAcceptInterrupt(uint8_t irqLevel) const noexcept
{
    // First gate: PAL must be at its post-relocation address.  Before
    // Step D fires, the chipset's irqs would route through a stub
    // handler at palBase=0x900000 which the firmware doesn't expect
    // to take while it's still decompressing.
    if (!m_palImageRelocated || m_cpu.palBase == 0) return false;

    // PALmode gate (#70 / interrupt correctness): never take an interrupt
    // while the CPU is executing PALcode.  PAL flows run to completion (or
    // HW_REI back to native); the latched request stays pending and is
    // delivered on return to non-PAL code.  Mirrors AXPBox check_int's
    // `!(state.pc & 1)` gate.  Without this, a divert staged mid-PAL
    // overwrites excAddr/PC and corrupts the PAL flow -- proven by the
    // in-PALmode synthetic-injection halt (cl67 1 GiB run, cyc 178M).
    if (m_cpu.inPalMode()) return false;

    // Phase D gate: HW_IER bit for the requesting IRQ source must be
    // set.  Alpha 21264 HRM Section 5.4: HW_IER and HW_ISUM share a
    // bit layout where bits 33..38 cover the EI[0..5] external IRQs.
    // Map irqLevel (architectural priority) onto the EI bit:
    //
    //   irq_h[0] -> IPL 30 -> IER bit 33
    //   irq_h[1] -> IPL 23 -> IER bit 34
    //   irq_h[2] -> IPL 22 -> IER bit 35   (interval timer)
    //   irq_h[3] -> IPL 21 -> IER bit 36   (interprocessor interrupt)
    //   irq_h[4] -> IPL 20 -> IER bit 37   (performance counter 0)
    //   irq_h[5] -> IPL 19 -> IER bit 38   (performance counter 1)
    //
    // Cold-boot reset value of cpu.ier is 0 (all sources masked).
    // Firmware writes via HW_MTPR HW_IER enable specific sources as
    // it brings each handler online -- mirroring the AXPBox
    // (state.eien & state.eir) gate but expressed in 21264 IPR terms.
    //
    // irqLevel mapping: irqLevel = 30 .. 19 corresponds to IRQs 0..5;
    // shift to IER-bit by (35 - (irqLevel - 22)) = (57 - irqLevel),
    // valid for irqLevel in [19, 30].
    if (irqLevel >= 19 && irqLevel <= 30) {
        uint8_t  const ierBit = static_cast<uint8_t>(57u - irqLevel);
        uint64_t const mask   = uint64_t{1} << ierBit;
        return (m_cpu.ier & mask) != 0;
    }

    // IRQ levels outside the chipset EI range (software interrupts at
    // IPL 1..14, AST at IPL 2..3, etc.) fall through to "accepted"
    // for now; their own per-source gates will land alongside SIRR /
    // ASTRR wiring in a follow-up phase.
    return true;
}


void Machine::onBeforeFetch(uint64_t pa) noexcept
{
    // Hot-path early-out.  After this fires once, this is the only
    // condition evaluated per fetch.
    if (m_palImageRelocated) {
        return;
    }
    if (!m_srmDescriptor.valid) {
        return;
    }
    if (pa != m_srmDescriptor.entryPa()) {
        return;
    }

    // ------------------------------------------------------------------
    // Step D PAL relocation: detection trigger ONLY (copy DISABLED).
    // ------------------------------------------------------------------
    // V1-era logic: copy PA [0, kPalRelocSize) -> [targetPalBase, +N) to
    // shim the PA 0 mirror's file bytes into the PAL_BASE region.  The
    // 2026-05-18 forensic trace (D:\EmulatR\traces\20260518-225111_srm.trc)
    // proved this copy was actively DESTRUCTIVE under the AXPBox-aligned
    // load model: by the time the firmware JSRs to entryPa, it has
    // ALREADY decompressed valid PAL code into the [targetPalBase, ...)
    // region itself (5,120 confirmed HW_ST writes into 0x600000..0x60ffff
    // in the cyc 0..4.19M window).  Step D was then reading from empty
    // PA 0 (no mirror in new model) and zeroing the firmware's just-
    // written PAL code at PA 0x6005c0, causing CALL_PAL HALT on
    // execution of the resulting 0x00000000 opcode.
    //
    // The TRIGGER detection itself is still load-bearing: this is the
    // first cycle when m_palImageRelocated transitions true, which
    // gates Phase C+ interrupt arbitration (`canAcceptInterrupt`) and
    // flips the trace-emit gate.  Keep that part; delete the copy.
    //
    // If a future use case needs the relocation copy (e.g., a different
    // firmware load model that DOES depend on a mirror), it can be
    // restored under a conditional flag.  For the AXPBox-aligned model
    // it must stay disabled.
    //
    //                       *** DELETED ***
    // uint64_t const palBase = m_srmDescriptor.palBase();
    // for (uint64_t off = 0; off + 4 <= kPalRelocSize; off += 4) { ... }

    m_palImageRelocated = true;

    // Phase C+: PAL is now resident at palBase; the trace stream becomes
    // meaningful.  Flip the sink's emit-gate true so per-commit listing
    // I/O and the retire-compact stream begin emitting from this cycle
    // onward.  Pre-relocation cycles are intentionally not traced -- the
    // SRM init sequence is well-characterised and tracing it doubles
    // overnight wall-clock for no diagnostic value.  See TraceSink::
    // setEmitEnabled for the architectural rationale.
    if (m_traceSink) {
        m_traceSink->setEmitEnabled(true);
    }

    // DIAGNOSTIC: surface the trigger event with cycle context so the
    // operator can correlate against the trace.  Message rewritten 2026-
    // 05-18 to reflect detect-only semantics: V1's PAL byte-copy was
    // disabled after the forensic trace proved it was destroying
    // firmware-written PAL bytes at trigger PC.
#if EMULATR_BRINGUP_PROBES
    std::fprintf(stderr,
                 "DEBUG: Step D PAL relocation TRIGGER (copy DISABLED) -- "
                 "trigger pa=0x%016llx  targetPalBase=0x%016llx  cycle=%llu\n",
                 static_cast<unsigned long long>(pa),
                 static_cast<unsigned long long>(m_srmDescriptor.palBase()),
                 static_cast<unsigned long long>(m_cpu.cycleCount));
#endif
}


bool Machine::loadSrmFirmware(std::filesystem::path const& path,
                              uint64_t                     loadPa)
{
    // systemLib:: qualifier is required: an unqualified call resolves
    // to the member function being defined here and recurses.
    SrmLoadResult r = systemLib::loadSrmFirmware(m_chipset.guestMemory(), path, loadPa);
    if (!r.ok) {
        m_lastLoadError = r.error;
        return false;
    }

    // Capture descriptor + payload for later phases (fetch override,
    // done detection, PAL relocation).
    m_srmDescriptor     = r.descriptor;
    m_srmPayload        = std::move(r.payload);
    m_srmLoadPa         = loadPa;     // base address tryFetch uses for range check
    m_palImageRelocated = false;      // Step D arms on first entryPa fetch

    // ----------------------------------------------------------------
    // Flash seeding (2026-06-03): if loadRaw found no persisted backing
    // image, initialize the TIG flash from the RAW firmware file bytes.
    // Real hardware ships the SRM ROM (env blocks, DSRDB, SROM config)
    // in this flash; factory 0xFF sent build_dsrdb chasing an all-ones
    // header to flash byte 0xE00004 -- 14 MB past the 2 MB device --
    // into a fatal double TLB miss (the 2026-06-03 fread / build_hwrpb
    // SRM crash dump).  AXPBox seeds its CFlash from rom.srm the same
    // way.  A persisted ds10_flash.rom always wins (backingLoaded), so
    // firmware env writes survive across boots once the debounce poll
    // flushes the seeded image.
    // ----------------------------------------------------------------
    if (!m_chipset.flash().backingLoaded()) {
        std::FILE* f = std::fopen(path.string().c_str(), "rb");
        if (f != nullptr) {
            std::vector<uint8_t> rom(FlashRom::kSize);
            size_t const got = std::fread(rom.data(), 1, rom.size(), f);
            std::fclose(f);
            m_chipset.flash().seedFrom(rom.data(), got);
        } else {
            std::fprintf(stderr,
                "Machine: flash seeding skipped -- cannot reopen '%s'\n",
                path.string().c_str());
        }
    }

    // Seed the CpuState's palBase so HW_MFPR HW_PAL_BASE returns the
    // correct value the moment the decompressor reads it.  Note 2026-
    // 05-19: we now use initialPalBase (= loadPa, e.g. 0x900000), NOT
    // targetPalBase (the firmware-embedded constant, e.g. 0x600000).
    // The AXPBox reference loader uses the load address as the initial
    // PAL_BASE; the firmware's decompressor issues HW_MTPR HW_PAL_BASE
    // during its run to transition to the target value.  Prior V4
    // design seeded with the target value, causing the decompressor's
    // destination computation (which is palBase-relative) to land in
    // a region that overlapped the running PAL text once Step D
    // relocation fired.
    //
    // resetToLoadedEntry below re-seeds palBase from the descriptor
    // after a clean CpuState wipe, also using initialPalBase.
    m_loadedStartPc = r.startPc;
    m_loadedPalMode = r.palMode;
    m_loadedPalBase = r.descriptor.initialPalBase;
    m_cpu.palBase   = r.descriptor.initialPalBase;
    m_lastLoadError.clear();
    return true;
}


// ============================================================================
// loadDecompressedRom -- load an AXPBox pre-decompressed console image.
// ============================================================================
// The console is already decompressed and relocated to its low base at PA 0x0.
// There is no decompressor stub, no I-stream fetch override, and no Step D
// relocation: the SRM descriptor stays invalid (so onBeforeFetch()/tryFetch()
// are no-ops), m_palImageRelocated starts true (the console is already at its
// final state, satisfying canAcceptInterrupt's post-relocation gate), and
// palBase is seeded directly from the image header.
bool Machine::loadDecompressedRom(std::filesystem::path const& path)
{
    SrmLoadResult r = systemLib::loadDecompressedRom(m_chipset.guestMemory(), path);
    if (!r.ok) {
        m_lastLoadError = r.error;
        return false;
    }

    m_srmDescriptor     = SrmDescriptor{};   // valid = false: no Step D / no tryFetch
    m_srmPayload.clear();
    m_srmLoadPa         = 0;
    m_palImageRelocated = true;              // console already at final state

    m_loadedStartPc = r.startPc;                   // image entry (e.g. 0x8000)
    m_loadedPalMode = r.palMode;                   // PAL bit from the image header
    m_loadedPalBase = r.descriptor.targetPalBase;  // PAL_BASE from header (e.g. 0x600000)
    m_cpu.palBase   = m_loadedPalBase;
    m_lastLoadError.clear();
    return true;
}


// ============================================================================
// reset / resetToLoadedEntry
// ============================================================================

void Machine::reset(uint64_t pc, bool palMode) noexcept
{
    // Clear architectural state.  Re-default-construct in place so we
    // do not have to enumerate every field on CpuState by name -- new
    // fields added later are picked up automatically.
    m_cpu = coreLib::CpuState{};
    // CHANGE 2026-05-21 (PALmode == PC<0>): fold the entry mode into the
    // PC's low bit.  palMode == true seeds PC<0> = 1 (PAL entry), matching
    // AXPBox's set_pc(loadPa | 1).  This is the SRM decompressor entry
    // seed -- firmware PC round-trips then preserve PAL the whole way.
    m_cpu.pc = palMode ? (pc | uint64_t{1}) : (pc & ~uint64_t{1});

    // Mode follows palMode at reset: PALcode runs in Kernel.  PALmode
    // false leaves it at the CpuState default (Kernel) too; firmware
    // is expected to drop privilege via MTPR_CM after init.
    m_cpu.mode = coreLib::Mode_Privilege::Kernel;

    // HARDCODED 2026-05-26: the DS10/SRM firmware is OpenVMS PAL, so the
    // CALL_PAL decoder must consult lookupPalVms (PipelineDriver::decode's
    // DispatchKind::Pal arm keys on this).  Tru64 isn't ready, so pin the
    // personality to VMS here rather than wiring a CLI flag / autodetect.
    m_cpu.palPersonality = 1;   // 1 = OpenVMS

}


void Machine::resetToLoadedEntry() noexcept
{
    reset(m_loadedStartPc, m_loadedPalMode);
    // reset() re-default-constructs m_cpu, wiping palBase.  Re-seed
    // it from the captured SRM descriptor when one is present so
    // HW_MFPR HW_PAL_BASE returns the correct value the moment the
    // decompressor reads it.  Use initialPalBase (= loadPa) to match
    // the AXPBox reference loader's starting state; the firmware's
    // own HW_MTPR HW_PAL_BASE during decompression transitions this
    // to targetPalBase.  See loadSrmFirmware() above for the
    // architectural rationale.
    // Re-seed palBase from the captured load value -- set by BOTH
    // loadSrmFirmware (= descriptor.initialPalBase) and
    // loadDecompressedRom (= the decompressed image's PAL_BASE).
    // reset() re-default-constructed m_cpu and wiped palBase.
    m_cpu.palBase = m_loadedPalBase;
    // P2-T3a: reset() re-default-constructed m_cpu (cycleCount = 0); resync the
    // decoupled system clock so systemNow() == m_cpu.cycleCount post-reset.
    m_systemClock = m_cpu.cycleCount;
}


// ============================================================================
// step / run
// ============================================================================

bool Machine::step() noexcept
{
    // Pass `this` as the IFetchOverride so the IF stage can serve
    // SRM-stub fetches from the immutable payload buffer.  When no
    // SRM image has been loaded, tryFetch returns false unconditionally
    // and the pipeline reads from GuestMemory normally.

 
    return pipelineLib::PipelineDriver::step(m_cpu, m_chipset, m_chipset.guestMemory(), m_traceSink, this);
}


// ============================================================================
// run -- step-loop with periodic auto-save and save-on-halt.
// ============================================================================
//
// Previously a single PipelineDriver::run() call.  Now a step()-loop so
// the snapshot subsystem can observe each retirement boundary and:
//
//   1. Periodically write a rolling auto-save every kAutoSavePeriodCycles
//      retired cycles.  Files are named auto_<unix_ts>_<cycle>.axpsnap
//      under the configured snapshotDir.  pruneOldSnapshots bounds the
//      directory at kAutoSaveKeepCount auto_*.axpsnap files (newest kept).
//
//   2. On halt (any kFault* including kFaultHalt), write one final
//      capture as auto_halt_<unix_ts>_<cycle>.axpsnap so the post-mortem
//      state is preserved for the next run's autoload.
//
// Tests that exercise step() in tight loops without disk traffic should
// call setAutoSnapshotEnabled(false) before run().  The default is on.
//
// Cost: one boolean check and one uint64 compare per cycle in the
// no-save path.  The save path itself is rare (every 10M cycles by
// default) and dominated by file I/O.

StopReason Machine::run(uint64_t maxCycles) noexcept
{
    // Lazy-schedule the first periodic save.  Reset between runs so a
    // post-snapshot resume does not race a save fired in the previous
    // run's tail.
    if (m_autoSnapshotEnabled) {
        m_nextAutoSaveCycle = systemNow() + kAutoSavePeriodCycles;
    }

    // Clear the forensic exception logs so each run() starts with
    // empty files.  Mid-run accumulation is what we want for the
    // morning review; carrying over a prior run's entries would muddy
    // the diagnostic.
    mmuLib::resetUnalignedEventLog();
    mmuLib::resetCboxEventLog();
    coreLib::resetFaultEventLog();

    // Phase C+: sync the trace sink's emit-gate at run-entry.
    //
    // 2026-05-18 evening FORENSIC OVERRIDE: only force-flip the gate
    // when m_palImageRelocated is TRUE (snapshot-resume case).  For
    // cold-boot (m_palImageRelocated == false), leave the gate at
    // whatever the sink constructor set it to.  This lets a debug
    // build set DecListingSink::m_emitEnabled = true in its header
    // default and get full retire trace from cyc 0 -- useful for
    // diagnosing the SrmLoader Step D source-starvation issue
    // documented in project_srmloader_axpbox_model.md.
    //
    // Revert to unconditional sync once Step D is fixed.
    if (m_traceSink && m_palImageRelocated) {
        m_traceSink->setEmitEnabled(true);
    }

    // Graceful-stop sentinel (2026-06-06, task #9).  Polling a file on a
    // coarse cadence lets a background (&) run be stopped cleanly from bash
    // -- `touch <sentinel>' -- so run() returns and ~Machine's forceFlush()
    // persists the flash NVRAM (an `update srm' heal or env `set').  A hard
    // taskkill /F skips the destructor and loses the heal.  Path:
    // $EMULATR_STOP_FILE if set, else "EMULATR_STOP" in the cwd; the resolved
    // absolute path is logged so the touch target is unambiguous regardless
    // of launch cwd (cf. task #3 cwd-pinning).  Pre-cleared at entry so a
    // stale sentinel from a prior run cannot stop this one.
    // m_stopSentinel is a member (was a run()-local) so the per-cycle body in
    // stepCycle() can read it.  Resolved once here, per run().
    {
        char const* envp = std::getenv("EMULATR_STOP_FILE");
        m_stopSentinel = (envp && *envp) ? std::filesystem::path(envp)
                                         : std::filesystem::path("EMULATR_STOP");
        std::error_code ec;
        std::filesystem::path const abs = std::filesystem::absolute(m_stopSentinel, ec);
        if (!ec) m_stopSentinel = abs;
        std::error_code rmec;
        std::filesystem::remove(m_stopSentinel, rmec);
        SPDLOG_INFO("Machine::run: graceful-stop sentinel = {} "
                    "(touch it to stop cleanly and flush flash NVRAM)",
                    m_stopSentinel.string());
    }
    // Match PipelineDriver::run() semantics: maxCycles caps the number of
    // step() iterations, not the cycleCount delta.  Each iteration runs the
    // shared per-cycle body stepCycle(i), which returns false to BREAK (stop
    // sentinel or CPU halt).  The legacy loop here and the dispatcher-driven
    // AlphaCpuAgent call the IDENTICAL stepCycle -- the Phase-1 byte-identical
    // boot acceptance gate.
    // PHASE-1 DISPATCHER PATH (gated; legacy is the default).  EMULATR_DISPATCH
    // routes the per-cycle stepCycle() through the SMP harness: one
    // AlphaCpuAgent under SequentialDriver, which calls the IDENTICAL
    // stepCycle(i).  A clean boot here is the step-3 byte-identical gate for the
    // dispatcher path; until it passes, the unset default keeps the legacy
    // direct loop.  After it passes, this becomes the default and the legacy
    // loop is deleted in a separate commit.  (See
    // journals/20260619_alphacpuagent_phase1_design.md.)
    static bool const s_useDispatcher = (std::getenv("EMULATR_DISPATCH") != nullptr);
    if (s_useDispatcher) {
        emulatr::smp::Dispatcher disp(/*quantum*/ 1);
        // Phase-2 T4: reuse the Machine-owned agent0 (it OWNS the CpuState that
        // m_cpu aliases) instead of a transient local agent; reset only its
        // per-run scheduling state (cycle index + stop flag).
        m_agent0.resetForRun();
        disp.addAgent(&m_agent0);
        disp.setDriver(std::make_unique<emulatr::smp::SequentialDriver>());
        disp.run(maxCycles);
    } else {
        for (uint64_t i = 0; i < maxCycles; ++i)
            if (!stepCycle(i)) break;
    }

    // Save-on-halt.  Independent of the periodic counter so it always
    // fires when execution stops (kFaultHalt, kFault*, or any other
    // halted state).  Distinct filename prefix so prune does not evict
    // halt captures alongside periodic auto-saves.
    if (m_autoSnapshotEnabled && m_cpu.halted) {
        std::ostringstream name;
        uint64_t const ts =
            static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count());
        name << "auto_halt_" << ts << "_" << systemNow() << kSnapshotExtension;

        std::error_code ec;
        std::filesystem::create_directories(m_snapshotDir, ec);
        (void) systemLib::save(*this, m_snapshotDir / name.str(), "halt");
    }

    // Notify the trace sink that the run has ended, so it can dump
    // its lookback ring and write a RUN END marker.  Unconditional --
    // works for clean halts, faults, and max-cycles stops.
    if (m_traceSink) {
        m_traceSink->onRunEnd(m_cpu);
    }

    // Boot profiler (2026-06-05): end-of-run histogram dump.  Cheap
    // (one ASCII file), and the data is exactly the "where did the
    // silent boot cycles go" answer.  Threshold guards against doctest
    // litter: every Machine-running test case would otherwise drop a
    // 35-retire profile into the traces dir (observed 2026-06-05).
    // Real runs retire billions; 100K is comfortably below any run
    // worth profiling and above any test.
#if EMULATR_BRINGUP_PROBES
    if (traceLib::RetireProfiler::totalRetires() >= 100000) {
        traceLib::RetireProfiler::dump("run_end");
    }
#endif

    return classifyStop(m_cpu);
}


// ============================================================================
// stepCycle -- one per-cycle iteration of Machine::run's loop, relocated
// verbatim 2026-06-19 (AlphaCpuAgent Phase 1).  Returns false to BREAK the
// loop (stop sentinel or CPU halt), true to continue.  Shared by the legacy
// run() loop above and the dispatcher-driven AlphaCpuAgent so both execute
// the IDENTICAL body -- the Phase-1 byte-identical-boot acceptance gate.  `i`
// is the loop ordinal, used only for the coarse stop-sentinel poll cadence.
// ============================================================================
bool Machine::cpuKernel([[maybe_unused]] coreLib::CpuState& cpu) noexcept
{
    // P2-T2 split (2026-06-20): the ONLY per-CPU action -- advance this CPU one
    // tick via PipelineDriver::step(), which retires zero or more instructions
    // and updates the CPU's cycleCount.  Returns false when the CPU halted on
    // this tick (the caller BREAKs the loop).  No global side effects -- all
    // system-level bookkeeping lives in systemTick().
    //
    // STEP-2 seam: step() still drives m_cpu internally; `cpu` is the forward-
    // compat handle so STEP 4 (CpuState ownership lift) can drive a real per-
    // agent CpuState here.  Today cpu === m_cpu.
    return step();   // false => CPU halted this tick
}


bool Machine::systemTick(uint64_t i) noexcept
{
    // P2-T2 split (2026-06-20): once-per-quantum, dispatcher-level bookkeeping.
    // Runs ONCE per quantum regardless of CPU count.  Returns false to BREAK the
    // run loop (stop sentinel); all cycle-based predicates read systemNow().
    //
    // STEP-4 / SMP SEAMS (the Phase-1 gate proves single-agent equivalence only,
    // NOT split-correctness -- these placements are reasoned, not gate-checked):
    //   * interval-timer DELIVER reads pendingIrq2(0) (hardcoded CPU0); it becomes
    //     a per-CPU divert in cpuKernel once ownership gives it a real cpuId.
    //   * predig snapshot-on-PC matches m_cpu.pc -- a latent per-CPU read; the
    //     "which CPU's PC?" policy is an SMP-era decision.
    static constexpr uint64_t kStopPollMask = 0xFFFFFULL;  // poll ~every 1M steps
        // Graceful-stop poll (task #9): cheap existence check on a coarse
        // cadence; a clean break lets ~Machine's forceFlush persist NVRAM.
        if ((i & kStopPollMask) == 0) {
            std::error_code sec;
            if (std::filesystem::exists(m_stopSentinel, sec)) {
                SPDLOG_INFO("Machine::run: stop sentinel seen -- clean exit at cycle {}",
                            static_cast<unsigned long long>(m_cpu.cycleCount));
                std::error_code rmec;
                std::filesystem::remove(m_stopSentinel, rmec);  // consume it
                return false;   // BREAK the run loop (was `break;` pre-extraction)
            }
        }

        // ---- Option-A console-triggered snapshot --------------------------------
        // Off unless EMULATR_CONSOLE_SNAPSHOT is set; fires when the operator types
        // the marker line (default "set oem_string snapshot") at >>>.  Reuse the same
        // predig path-builder + save the --snapshot-on-pc fire path already uses.
        if (m_chipset.com1().takeSnapshotRequest()) {
            std::string const path = (snapshotDir() /
                ("predig_oemsnap_cyc" + std::to_string(systemNow()) + ".axpsnap")).string();
            save(*this, path);
            spdlog::info("console-snapshot: marker matched -> {}", path);
        }
        if (m_autoSnapshotEnabled && systemNow() >= m_nextAutoSaveCycle) {
            // Rolling auto-save.  Filename embeds wall-clock seconds
            // and cycle for chronological sorting under most file
            // systems.  Errors are logged inside save() and do not
            // halt the run -- snapshot failure is a recoverable event,
            // not a fault.
            std::ostringstream name;
            uint64_t const ts =
                static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count());
            name << "auto_" << ts << "_" << systemNow() << kSnapshotExtension;

            std::error_code ec;
            std::filesystem::create_directories(m_snapshotDir, ec);
            (void) systemLib::save(*this, m_snapshotDir / name.str(), "periodic");
            systemLib::pruneOldSnapshots(m_snapshotDir, kAutoSaveKeepCount);
            m_nextAutoSaveCycle = systemNow() + kAutoSavePeriodCycles;
        }

        // ------------------------------------------------------------------
        // Pre-diagnostic snapshot trigger.  One-shot.
        // ------------------------------------------------------------------
        // Fires the first retire whose architectural PC equals the armed
        // value.  Writes a `predig_<tag>_cyc<cycle>.axpsnap` file (no
        // `auto_` prefix, so pruneOldSnapshots never touches it) and
        // immediately disables auto-save so this file stays mtime-newest
        // across the rest of the run.  Next cold start's autoloadLatest
        // resumes from this exact CPU state -- collapsing a multi-hour
        // boot replay to a single load.
        //
        // Gate ordering: check (pc != 0 && !fired) cheaply first; the
        // PC compare is only done when those two are true.  Steady-state
        // cost when disarmed: one uint64_t load + branch-predicted-NOT-
        // taken comparison.  After firing, the same two loads still gate
        // but `fired` stays true so the PC compare never runs again.
        // 2026-06-06: list of one-shot triggers (multi-PC).  Cheap gate:
        // when none remain, a single compare; otherwise a short loop over
        // the (few) triggers, each matched against the retired PC.
        if (m_snapTriggersRemaining > 0) {
            for (SnapTrigger& trig : m_snapTriggers) {
                if (trig.fired || trig.pc != m_cpu.pc) continue;

                std::ostringstream pname;
                pname << "predig_";
                if (!trig.tag.empty()) {
                    pname << trig.tag;
                } else {
                    pname << std::hex
                          << std::setw(16) << std::setfill('0')
                          << trig.pc
                          << std::dec << std::setfill(' ');
                }
                pname << "_cyc" << systemNow() << kSnapshotExtension;

                std::error_code ec;
                std::filesystem::create_directories(m_snapshotDir, ec);
                SnapshotResult const sr =
                    systemLib::save(*this, m_snapshotDir / pname.str(), "predig");

                std::fprintf(stderr,
                             "Machine: predig snapshot fired at pc=0x%016llx "
                             "cyc=%llu -> '%s' (success=%d bytes=%llu)\n",
                             static_cast<unsigned long long>(trig.pc),
                             static_cast<unsigned long long>(m_cpu.cycleCount),
                             sr.path.c_str(),
                             sr.success ? 1 : 0,
                             static_cast<unsigned long long>(sr.bytesWritten));
                std::fflush(stderr);

                trig.fired = true;
                --m_snapTriggersRemaining;
                m_anySnapFired = true;

                // Legacy single-PC arm disables auto-save so the predig
                // stays mtime-newest; multi-PC arms (addSnapshotOnPc) do
                // not (they pair with --autosnapshot off).
                if (m_snapDisableAutosOnFire) m_autoSnapshotEnabled = false;

                break;  // at most one capture per retire
            }
        }

        // ------------------------------------------------------------------
        // Phase C: cycle-driven interval timer + b_irq<2> divert poll.
        // ------------------------------------------------------------------
        // The Cchip interval timer is modelled as a power-of-two cycle-
        // mask predicate (chipsetLib/CchipIntervalTimer.h); when the
        // predicate fires, MISC<ITINTR<4+n>> is asserted for each enabled
        // CPU and the in-Cchip per-CPU b_irq<2> latch is set.  We then
        // poll the latch for the current CPU and, if asserted, run the
        // same stageInterruptDivert recipe used by the synthetic
        // injection below.  Edge acknowledgment via clearPendingIrq2()
        // prevents re-divert while the PAL handler is in flight; the
        // firmware's eventual W1C of MISC<ITINTR> is mirrored back into
        // the latch by miscWriteW1C() as a defensive idempotent clear.
        //
        // Phase C+: both fire and divert are gated on
        // `canAcceptInterrupt(22)` (interval-timer IPL).  Today the gate
        // collapses to "PAL relocation has completed AND palBase != 0";
        // Phase D extends it to include the architectural IPL compare.
        // The old palBase-only gate let ticks fire before Step D PAL
        // relocation copied valid trap-vector bytes to palBase+0x100,
        // diverting the CPU into uninitialized memory and leaving the
        // firmware in an unrecoverable partial-PAL state (see Phase C+
        // post-mortem in journals/CchipPhaseA_Design_Notes.md addendum).
        //
        // Suppression observability: when the cycle-mask predicate
        // fires but the arbitration gate blocks delivery, log a
        // throttled "SUPPRESSED" message.  Expected volume during cold
        // boot is small (one entry per 2^20 cycles between cyc 0 and
        // PAL relocation at ~cyc 4.2M -- so ~4 entries on ES45 1 GHz).
        // If the log volume grows large in a future run, that signals
        // a longer than expected pre-relocation window.
        //
        // The `divertedThisCycle` flag suppresses the synthetic INTERRUPT
        // injection block below when a timer divert just staged the
        // CPU state -- they would otherwise both write excAddr/pc/etc.,
        // and the second writer's savedPc would point at the wrong
        // origin PC (palBase+0x100 instead of the original retire PC).
        // ------------------------------------------------------------------
        // ------------------------------------------------------------------
        // Interval timer (#70): FIRE and DELIVER are SEPARATE concerns.
        //
        // FIRE -- the timer is a hardware event driven by the cycle counter.
        // It latches MISC<ITINTR> + the per-CPU b_irq<2> latch UNCONDITIONALLY
        // on the cycle edge, independent of whether the CPU can take the
        // interrupt right now.  HRM 6.3.2: b_irq<2> stays asserted until the
        // CPU W1C-clears MISC<ITINTR>; AXPBox interrupt(-1) sets it with no
        // eien/IPL/PALmode check.  Gating the *latch* on acceptance was the
        // bug -- firmware that POLLS MISC<ITINTR> never saw it set while the
        // CPU was masked and waited forever (the timer-poll spin).
        //
        // DELIVER -- the divert to the INTERRUPT vector happens ONLY when the
        // CPU can accept it: canAcceptInterrupt(22) = relocation done +
        // palBase != 0 + IER<EIEN2> set + NOT in PALmode.  This preserves the
        // original no-premature-divert protection (it was the divert, not the
        // latch, that needed gating).  The latch persists across masked cycles
        // until it is either delivered here or W1C-cleared by firmware.
        // ------------------------------------------------------------------
        bool divertedThisCycle = false;

        // ------------------------------------------------------------------
        // Device-interrupt chain eval (Increment 2, 2026-06-04).
        // ------------------------------------------------------------------
        // Step-boundary mirror: uart_int_pending -> Cypress 8259 IRQ4 ->
        // [IMR/in-service/priority] -> DRIR<55>.  Level-combinational at
        // every layer; one eval per boundary IS the design's storm guard
        // (journals/20260604_serial_console_interrupt_design.md Sec. 5-6).
        // Cheap: two computed bools + compare; DRIR atomics touched only
        // on level CHANGE.
        m_chipset.evalDeviceIrqs();

        // ---- EMULATR_IDLEWARP: interval-timer idle fast-forward (#25) ----------
        // 2026-06-11/12.  The console's post-GCT/FRU and pre/at-dva0 sleeps idle in
        // a tick loop near PC 0x7bafc that, between ticks, spins ~one interval-
        // timer period of cycles per tick -- the ~30-min "initializing GCT/FRU"
        // and ~20-min dva0 host stalls.  The C970DUMP regs proved 0x7bef0 is only
        // the once-per-tick counter increment (counter @0x3c970), NOT the spin;
        // the host cost is the idle wait BETWEEN ticks.  When the CPU is in that
        // idle wait AND can accept the interval timer (IPL<22, not PAL, post-
        // relocation), jump cycleCount straight to the next timer-fire edge so
        // the FIRE/DELIVER below runs the tick ISR and the loop costs O(ticks)
        // host iterations instead of O(cycles).  FAITHFUL: emulated time still
        // advances by exactly the skipped amount and the timer fires at the same
        // emulated cycle it otherwise would; only the host re-execution of the
        // idle spin is skipped.  Self-terminating -- it jumps at most one tick
        // per visit and stops the instant the sleep's exit condition is met (PC
        // leaves the idle window).  Gated OFF by default: the faithful cycle-
        // accurate path is unchanged; arm EMULATR_IDLEWARP to measure.  SEPARATE
        // gate from the QUARANTINED RSCC warps (EMULATR_RSCCWARP) -- those jumped
        // many ticks at once AND rewrote 0x3c970 out-of-band, the confirmed cause
        // of the overnight 0x7f4xx boot corruption.  THIS warp advances cycleCount
        // exactly ONE tick edge and lets the real interval-timer ISR increment
        // 0x3c970 -- no out-of-band rewrite, so counter-polling loops stay coherent.
        // PC window covers the krn$_idle loop (PCSAMPLE showed ra=0x7bad8 / 0x7bafc /
        // 0x7bb04); narrow it against the >>> RetireProfiler idle bucket once known.
        // process-global static (one instance per process); revisit under the
        // threaded driver, where agent threads would share this getenv result.
        static bool const s_idleTickWarp = (std::getenv("EMULATR_IDLEWARP") != nullptr);
        if (s_idleTickWarp) {
            uint64_t const idlePc = m_cpu.pcAddr();
            if (idlePc >= 0x000000000007bad0ull && idlePc < 0x000000000007bb10ull
                && canAcceptInterrupt(22)
                && !chipsetLib::intervalTimerShouldFire(systemNow())) {
                // IDLEWARP SEAM (Phase 2): warps the SYSTEM clock past an idle
                // spin to the next timer edge.  Today systemNow() == this CPU's
                // PCC so warping m_cpu.cycleCount IS warping the system clock
                // (behavior-identical).  STEP 3 advances systemNow() by the warp
                // delta; under policy P-A the running CPU's PCC tracks the warp
                // (design D-1a) -- keep them moving together.
                // P2-T3a: the warp targets the SYSTEM clock (skip system time to
                // the next timer edge); the running CPU's PCC tracks it under P-A.
                // Base on systemNow() (== this CPU's PCC today) so the multi-CPU-
                // correct system-clock-primary shape needs no STEP-4 revisit.
                uint64_t const c0 = systemNow();
                m_systemClock     = (c0 | Tsunami21272::Spec::kCchipTimerMask) + 1;
                m_cpu.cycleCount  = m_systemClock;
                // process-global static; revisit under the threaded driver.
                static uint64_t s_warpLog = 0;
                if ((s_warpLog++ & 0x3FFull) == 0) {     // throttle 1/1024
                    std::fprintf(stderr,
                        "IDLETICKWARP cyc=%llu->%llu pc=0x%llx (skip idle spin to next tick)\n",
                        static_cast<unsigned long long>(c0),
                        static_cast<unsigned long long>(m_cpu.cycleCount),
                        static_cast<unsigned long long>(idlePc));
                    std::fflush(stderr);
                }
            }
        }
        // ---- END EMULATR_IDLEWARP idle fast-forward ----------------------------

        // FIRE: latch unconditionally on the cycle edge.
        if (chipsetLib::intervalTimerShouldFire(systemNow())) {
            m_chipset.cchip().fireIntervalTimer();

            // 2026-06-02: NVRAM flash debounce poll rides the interval-timer
            // tick (the live timer machinery).  tryFlush coalesces a write
            // burst (one SRM `set' = sector erase + N byte programs) into a
            // single atomic flush once the flash has been quiescent for W
            // cycles (D1).  Cheap; polled at the ~2^18-cycle timer cadence.
            m_chipset.flash().tryFlush(systemNow());
        }

        // DELIVER: divert only when the CPU can accept the latched request.
        // Single-CPU build today; cpuId=0.  Phase C+ extends to the per-CPU
        // loop when SMP arrives.
        if (canAcceptInterrupt(22) && m_chipset.cchip().pendingIrq2(0)) {
            // Throttled stderr -- timer divert at ~954 Hz on a long run would
            // otherwise drown other diagnostics.  First 32 fires loud, then a
            // summary every 64K.  Matches the CBOX/UNALIGN throttle policy.
#if EMULATR_BRINGUP_PROBES
            // process-global static; revisit under the threaded driver
            // (shared across agent threads -> contention/correctness).
            static std::atomic<uint64_t> s_cnt{ 0 };
            uint64_t const n = s_cnt.fetch_add(1, std::memory_order_relaxed);
            if (n < 32) {
                uint64_t const savedPc =
                    m_cpu.pc;   // pc already carries the PALmode bit (PC<0>)
                // 2026-05-29: target offset is coreLib::ev6::kEntry_INTERRUPT
                // (0x680), the EV6 external-interrupt vector; was a hardcoded
                // 0x680 magic number, now sourced from Ev6EntryVectors.h.
                uint64_t const target = m_cpu.palBase + coreLib::ev6::kEntry_INTERRUPT;
                std::fprintf(stderr,
                             "Machine: interval-timer divert[%llu] at "
                             "cyc=%llu savedPc=0x%016llx target=0x%016llx "
                             "(palBase + kEntry_INTERRUPT = 0x680)\n",
                             static_cast<unsigned long long>(n),
                             static_cast<unsigned long long>(m_cpu.cycleCount),
                             static_cast<unsigned long long>(savedPc),
                             static_cast<unsigned long long>(target));
                std::fflush(stderr);
            } else if ((n & 0xFFFFu) == 0) {
                std::fprintf(stderr,
                             "Machine: %llu interval-timer diverts "
                             "(loud-stderr muted past 32)\n",
                             static_cast<unsigned long long>(n + 1));
                std::fflush(stderr);
            }
#endif

            // TEMP DIAGNOSTIC (DIVERT-REI register ledger, fill) -- REMOVE
            // AFTER the fclose(&spl_kernel) corruption is root-caused.
            // Record the native conserved registers (R2-R7, R20-R23) the
            // interrupted context holds RIGHT NOW; execHwRei compares them
            // when the PAL returns to this savedPc.  Diverts fire only
            // outside PAL mode (canAcceptInterrupt), so pc bit 0 is clear
            // and the regfile view here is the native bank.
            {
                constexpr int kRegMap[10] = { 2, 3, 4, 5, 6, 7,
                                              20, 21, 22, 23 };
                namespace pd = ::palBox::palDiag;
                int const slot = pd::g_divertPendingLive[0] ? 1 : 0;
                pd::g_divertPendingPc[slot]  = m_cpu.pc & ~uint64_t{3};
                pd::g_divertPendingCyc[slot] = m_cpu.cycleCount;
                for (int i = 0; i < 10; ++i) {
                    pd::g_divertPendingReg[slot][i] =
                        m_cpu.intReg[kRegMap[i]];
                }
                pd::g_divertPendingLive[slot] = true;
            }
            // ---- END TEMP DIVERT-REI ledger fill ----

            // EI[2] = IRQ_CLK (interval timer); ISUM bit 35.  Routes SRM to
            // sys__int_clk (counts the tick) -- NOT EI[0]/sys__int_err.
            stageInterruptDivert(m_cpu, uint64_t{1} << 35);
            m_chipset.cchip().clearPendingIrq2(0);
            divertedThisCycle = true;
        }

        // ------------------------------------------------------------------
        // b_irq<3> -- Interprocessor interrupt (IPI).  Wired 2026-06-18.
        // ------------------------------------------------------------------
        // Mirrors the b_irq<2> interval-timer divert above.  Authoritative
        // mapping from the PC264 OSF PAL (apisrm ref ev6_osf_pc264_pal.mar
        // IPL table): IRQ<3>=interprocessor sits at the SAME IPL as the clock
        // (IPL 5) and maps to ISUM/IER EI[3] -- IRQ_IP=8 (bit 3 of the IE
        // field) and EV6__IER__EIEN__S=33, so bit 33+3 = 36.
        // canAcceptInterrupt(21) selects IER bit 36 (57-21), i.e. it delivers
        // IFF the guest has the interprocessor EIEN bit enabled at its current
        // IPL -- the faithful per-source gate (NOT 20, which would select bit
        // 37 = performance counter 0).  Single-CPU (cpuId 0) like the timer;
        // mutex via divertedThisCycle.  Source for the b_irq<3> latch is the
        // Cchip IPREQ->IPINTR path wired in TsunamiCchip.h the same day.
        if (!divertedThisCycle
            && canAcceptInterrupt(21)
            && m_chipset.cchip().pendingIrq3(0)) {
            // EI[3] = IRQ_IP (interprocessor); ISUM bit 36.  Routes the SRM
            // PAL INTERRUPT vector (palBase + kEntry_INTERRUPT) to its
            // interprocessor-interrupt handler.
            stageInterruptDivert(m_cpu, uint64_t{1} << 36);
            m_chipset.cchip().clearPendingIrq3(0);
            divertedThisCycle = true;
        }

        // ------------------------------------------------------------------
        // b_irq<0> -- System Error class (Phase B-NXMA, 2026-05-28)
        // ------------------------------------------------------------------
        // Per HRM 6.3.1 mapping: b_irq<0> = error class, gated at IPL 23.
        // Sources: DRIR<63:58> & DIM[cpuId]<63:58>.  Bit 63 = NXM (wired);
        // 62:58 reserved for future error sources (ECC double-bit etc.).
        //
        // Asymmetric to the b_irq<2> block above:
        //   - NO FIRE step.  b_irq<0> is asserted event-driven from
        //     TsunamiCchip::latchNxm (which fetch_or's DRIR<63>), not
        //     cycle-driven.  This poll only checks pendingIrq0(cpuId).
        //   - NO clearPendingIrq0() ack.  b_irq<0> is level-pinned by
        //     the source latch (MISC<NXM>).  Firmware's W1C of MISC<NXM>
        //     mirrors clear to DRIR<63> via miscWriteW1C(), de-asserting
        //     the per-CPU view on the next pendingIrq0 poll.  If the OS
        //     drops IPL below 23 without clearing the source, the divert
        //     re-fires immediately -- HRM-correct sticky semantics for
        //     catastrophic errors.
        //
        // Mutex with the b_irq<2> divert above: `divertedThisCycle` is set
        // when the timer fires, suppressing this block on the same cycle
        // to avoid double-staging excAddr/pc/palMode.  Same exclusion as
        // the synthetic INTERRUPT injection further down.
        if (!divertedThisCycle
            && m_chipset.cchip().pendingIrq0(0)
            && canAcceptInterrupt(23))
        {
            // Throttled stderr.  Bursty MCHK reports during a probe sweep
            // would otherwise drown other diagnostics.  First 32 fires
            // loud, then a summary every 64K -- matches the timer-divert
            // throttle policy directly above.
#if EMULATR_BRINGUP_PROBES
            // process-global static; revisit under the threaded driver
            // (shared across agent threads -> contention/correctness).
            static std::atomic<uint64_t> s_cnt{ 0 };
            uint64_t const n = s_cnt.fetch_add(1, std::memory_order_relaxed);
            if (n < 32) {
                uint64_t const savedPc = m_cpu.pc;
                // 2026-05-29: target offset is coreLib::ev6::kEntry_INTERRUPT
                // (0x680), shared with the b_irq<2> divert above.
                uint64_t const target = m_cpu.palBase + coreLib::ev6::kEntry_INTERRUPT;
                std::fprintf(stderr,
                             "Machine: b_irq<0> divert[%llu] (NXM/error) at "
                             "cyc=%llu savedPc=0x%016llx target=0x%016llx "
                             "(palBase + kEntry_INTERRUPT = 0x680)\n",
                             static_cast<unsigned long long>(n),
                             static_cast<unsigned long long>(m_cpu.cycleCount),
                             static_cast<unsigned long long>(savedPc),
                             static_cast<unsigned long long>(target));
                std::fflush(stderr);
            } else if ((n & 0xFFFFu) == 0) {
                std::fprintf(stderr,
                             "Machine: %llu b_irq<0> diverts "
                             "(loud-stderr muted past 32)\n",
                             static_cast<unsigned long long>(n + 1));
                std::fflush(stderr);
            }
#endif

            // EI[0] = IRQ_ERR (system error / NXM class); ISUM bit 33.
            stageInterruptDivert(m_cpu, uint64_t{1} << 33);
            // Intentionally NO clearPendingIrq0 -- level-pinned per HRM.
            divertedThisCycle = true;
        }

        // ------------------------------------------------------------------
        // b_irq<1> -- Device class (Increment 2, 2026-06-04).
        // ------------------------------------------------------------------
        // Serial-console interrupt design Seam 3
        // (journals/20260604_serial_console_interrupt_design.md).
        //
        // Level pattern like b_irq<0>: pendingIrq1 computes
        // (DRIR & DIM)<55:0> per poll; the level is held by the Cypress
        // 8259 output (mirrored into DRIR<55> by evalDeviceIrqs above)
        // and falls at acknowledge below.  No latch, no clear method.
        //
        // 4.1 DEFERRAL INVARIANT (verified 2026-06-04): this poll runs
        // every step iteration; canAcceptInterrupt returns false during
        // PAL mode WITHOUT consuming the request, so a request masked by
        // a PAL window redelivers on the first post-HW_REI boundary.
        // Latch-and-redeliver comes free from the poll-per-step shape --
        // do not add an execHwRei seam.
        //
        // NAMING NOTE: the 23 below is V4's canAcceptInterrupt
        // bit-selector convention (57-23 = ISUM/IER bit 34 = EIEN<1> =
        // EI[1] = b_irq<1>, matching AXPBox irq_h(1)).  It is NOT the
        // HRM IPL of the device class (HRM 6.3.1: device = IPL 21).  Do
        // not conflate -- the error path's gate/stage mismatch (gate 23/
        // bit 34 vs staged bit 33) is exactly this confusion; see the
        // sidebar ticket in the design doc Seam 3.
        //
        // NO INTA AT DIVERT (corrected 2026-06-04, first live run): Alpha
        // has no automatic interrupt-acknowledge cycle.  The Tsunami
        // holds the b_irq<1> level into the handler; the PAL/SRM triage
        // reads Cchip DIR0 FIRST to find DRIR<55> (acking here zeroed
        // that read -- proven in run 144555), then resolves the source
        // at the ISA bridge.  The PIC ack (IRR->ISR, output falls,
        // DRIR<55> mirror drops) happens when the handler polls/IACKs
        // the PIC -- the Pic8259Pair OCW3-poll path performs it.  The
        // PAL-mode gate covers re-divert during triage; if a native-
        // window re-divert livelock is ever observed, add an EOI-keyed
        // in-flight latch here -- do not restore the divert-time ack.
        if (!divertedThisCycle
            && m_chipset.cchip().pendingIrq1(0)
            && canAcceptInterrupt(23))
        {
#if EMULATR_BRINGUP_PROBES
            // process-global static; revisit under the threaded driver
            // (shared across agent threads -> contention/correctness).
            static std::atomic<uint64_t> s_cnt{ 0 };
            uint64_t const n = s_cnt.fetch_add(1, std::memory_order_relaxed);
         //   int const vec = m_chipset.acknowledgeDeviceInterrupt();
            if (n < 32) {
                uint64_t const savedPc = m_cpu.pc;
                uint64_t const target =
                    m_cpu.palBase + coreLib::ev6::kEntry_INTERRUPT;
                std::fprintf(stderr,
                             "Machine: b_irq<1> divert[%llu] (device) at cyc=%llu savedPc=0x%016llx target=0x%016llx picVector=0x%02x",
                             static_cast<unsigned long long>(n),
                             static_cast<unsigned long long>(m_cpu.cycleCount),
                             static_cast<unsigned long long>(savedPc),
                             static_cast<unsigned long long>(target)
                             );
                std::fflush(stderr);
            } else if ((n & 0xFFFFu) == 0) {
                std::fprintf(stderr,
                             "Machine: %llu b_irq<1> diverts (loud-stderr muted past 32)", static_cast<unsigned long long>(n + 1));
                std::fflush(stderr);
            }
#endif

            // EI[1] = IRQ_DEV (device / PCI / ISA class); ISUM bit 34.
            stageInterruptDivert(m_cpu, uint64_t{1} << 34);
            divertedThisCycle = true;
        }

        // ------------------------------------------------------------------
        // One-shot synthetic INTERRUPT-class trap injection.  Experimental.
        // ------------------------------------------------------------------
        // Tests the "MCHK-as-yield idle wait" hypothesis for the 2026-05-13
        // SRM sys__cbox outer loop.  When cpu.cycleCount first reaches the
        // armed cycle and palBase is non-zero, force a divert to palBase +
        // 0x680 (the EV6 INTERRUPT entry vector) as if a chipset had
        // asserted IRQ.  Saves current cpu.pc to excAddr with the PALmode
        // bit and sets palMode = true, matching what the trap delivery
        // path in PipelineDriver::retire would do for a real fault.
        //
        // CHANGE 2026-05-28: vector target corrected from 0x100 to 0x680
        // (see stageInterruptDivert block comment for the full rationale).
        //
        // Gate ordering: cycle != 0 && !fired cheaply first; palBase check
        // and PC arithmetic only when the cycle threshold is reached.
        // Steady-state cost when disarmed: one load + branch-predicted-
        // NOT-taken compare per retire.
        //
        // Phase C interaction: gated on !divertedThisCycle so the Phase C
        // timer-driven divert above does not get clobbered by the synthetic
        // injection writing excAddr/pc/etc. a second time in the same
        // retire boundary.  If both want to fire on the same cycle, the
        // timer wins; the synthetic stays armed for the next cycle (its
        // !fired guard re-checks cpu.cycleCount >= m_injectInterruptCycle
        // which will still be true).
        // L2 (Phase 2): armInterruptInjection arms a one-shot at a target SYSTEM
        // cycle, so the predicate reads systemNow(), not a CPU's PCC.  Today they
        // are equal (pure indirection).  The compare is level-triggered (>=) and
        // one-shot, so it is IDLEWARP-safe -- a warp past the target is still
        // caught on the next retire (same property the interval-timer FIRE has).
        if (!divertedThisCycle
            && m_injectInterruptCycle != 0 && !m_injectInterruptFired
            && systemNow() >= m_injectInterruptCycle)
        {
            if (m_cpu.palBase != 0) {
                // Snapshot the staging arguments for logging BEFORE
                // stageInterruptDivert() mutates cpu state.
                uint64_t const savedPc =
                    m_cpu.pc;   // pc already carries the PALmode bit (PC<0>)
                // 2026-05-29: target offset is coreLib::ev6::kEntry_INTERRUPT
                // (0x680), shared with the timer / NXM divert paths above.
                uint64_t const target = m_cpu.palBase + coreLib::ev6::kEntry_INTERRUPT;

                // Stage ISUM cause bits so the OSF/1 PAL INTERRUPT vector
                // (ev6_osf_pal.mar START_HW_VECTOR <INTERRUPT>) dispatches
                // out of the trap__interrupt_dismiss leg.  Decode order
                // there is CR -> PC -> EI -> SL -> dismiss; setting only
                // EI[0] (bit 33) lets PC/CR fall through and lands the
                // handler at sys__interrupt_ei, which is the most likely
                // pathway a real chipset IRQ would have taken.  Bit
                // numbers per ev6_defs.mar EV6__ISUM__EI__S = ^x21 = 33.
                uint64_t const isumBits = uint64_t{1} << 33;

                std::fprintf(stderr,
                             "Machine: INTERRUPT injection fired at cyc=%llu "
                             "savedPc(excAddr)=0x%016llx target=0x%016llx "
                             "(palBase=0x%016llx + kEntry_INTERRUPT=0x680)  "
                             "isum=0x%016llx (EI[0])\n",
                             static_cast<unsigned long long>(m_cpu.cycleCount),
                             static_cast<unsigned long long>(savedPc),
                             static_cast<unsigned long long>(target),
                             static_cast<unsigned long long>(m_cpu.palBase),
                             static_cast<unsigned long long>(isumBits));
                std::fflush(stderr);

                // Debug injection keeps EI[0] (isumBits, 1<<33); see comment
                // above for the dismiss-leg rationale.
                stageInterruptDivert(m_cpu, isumBits);
            } else {
                std::fprintf(stderr,
                             "Machine: INTERRUPT injection requested at cyc=%llu "
                             "but palBase=0; injection SKIPPED (PALcode not "
                             "loaded yet, no architectural target for the divert)\n",
                             static_cast<unsigned long long>(m_cpu.cycleCount));
                std::fflush(stderr);
            }

            // One-shot: disarm regardless of whether the divert actually
            // happened.  A caller that needs a second injection re-arms.
            m_injectInterruptFired = true;
        }

    return true;
}


// ============================================================================
// stepCycle -- one quantum tick = per-CPU kernel + once-per-quantum system tick
// ============================================================================
// P2-T2 transitional shape (2026-06-20): drive the (single) CPU, then run the
// once-per-quantum system bookkeeping.  cpuKernel() false (halt) BREAKs BEFORE
// systemTick, matching the pre-split order where a CPU halt returned immediately
// after step() (the only behavioral reorder is the stop-sentinel poll, now after
// step instead of before -- a no-op whenever the sentinel is absent, so the
// byte-identical boot gate is unaffected).  Dispatcher and legacy paths call this
// IDENTICAL body, so phase1_dispatch_gate.sh still governs.
// ============================================================================
bool Machine::stepCycle(uint64_t i) noexcept
{
    // P2-T3a: advance the SYSTEM clock by the RAW retire-cycle delta this CPU
    // produced (INVARIANT D-1a: by newPCC - oldPCC, NOT +1 per iteration and NOT
    // +1 per quantum), whether or not the CPU halted on this tick.  For the
    // single running agent m_systemClock stays == m_cpu.cycleCount, so
    // systemNow() reads identically -> the dispatch gate is byte-identical.
    uint64_t const pccBefore = m_cpu.cycleCount;
    bool const     alive     = cpuKernel(m_cpu);
    m_systemClock += (m_cpu.cycleCount - pccBefore);
    if (!alive) {
        return false;   // CPU halted -> BREAK the run loop
    }
    return systemTick(i);
}


// ============================================================================
// restoreSrmStaging -- snapshot load-path hook
// ============================================================================
//
// Snapshot::load() pulls SRM staging fields back out of the snapshot
// file and pushes them in here.  Wholesale overwrite of every staging
// field; the previous payload buffer is replaced.  Caller is expected
// to have already overwritten m_cpu (CpuState carries palBase which is
// the authoritative live value; the SRM descriptor's palBase is the
// post-load seed and may or may not still match after PALcode has done
// HW_MTPR HW_PAL_BASE in the captured run).
//
// Resets m_lastLoadError because a successful restore implies a known-
// good staging state.

void Machine::restoreSrmStaging(SrmDescriptor const& descriptor,
    std::vector<uint8_t>&& payload,
    uint64_t                loadPa,
    bool                    relocated,
    uint64_t                startPc,
    bool                    palMode) noexcept
{
    m_srmDescriptor = descriptor;
    m_srmPayload = std::move(payload);
    m_srmLoadPa = loadPa;
    m_palImageRelocated = relocated;
    m_loadedStartPc = startPc;
    m_loadedPalMode = palMode;
    m_lastLoadError.clear();
    // P2-T3a: Snapshot::load has already restored m_cpu (incl. cycleCount) via
    // `machine.cpu() = cpuTmp` before calling this; resync the decoupled system
    // clock so a resumed run reads systemNow() == the restored PCC.  (The boot
    // gate never reaches this -- it is the dormant-arm autoload/restore path.)
    m_systemClock = m_cpu.cycleCount;
}
} // namespace systemLib