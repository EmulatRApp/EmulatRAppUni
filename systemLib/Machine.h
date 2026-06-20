// ============================================================================
// systemLib/Machine.h -- top-level orchestration over CpuState + memory
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
// Machine is the V4 fabric's "system under emulation" aggregate.  One
// instance per emulated CPU; owns the CpuState, the GuestMemory, and
// the Ev6Translator (empty struct today, future home for TLB caches /
// page walker state).  Provides reset / step / run / loadFirmware
// facades over the lower-level pipelineLib + memoryLib layers so
// `main` holds a single object instead of juggling five.
//
// Phase 1 v1 contract:
//
//   Machine(memSize)              -- construct; default 64 MiB
//   loadFirmware(path, loadPa, startPa) -> bool
//                                 -- delegate to FirmwareLoader; on
//                                    success, capture startPc + palMode
//                                    for the next reset() call
//   reset(pc, palMode)            -- clear regfiles + IPRs + halt flags;
//                                    set pc + palMode
//   step()                        -- one PipelineDriver tick
//   run(maxCycles)  -> StopReason -- loop until halted / fault / max;
//                                    classify and return the reason
//   cpu()                         -- const + mutable accessors
//   memory()                      -- const + mutable accessors
//
// Phase 2 will add setTraceSink(TraceSink*) and forward it into the
// PipelineDriver call.  Phase 3 will add palVectors() once
// PalVectorTable lives.  Both extensions go on this same class.
//
// Memory ownership:
//   GuestMemory uses unique_ptr internally; Machine holds the
//   GuestMemory by value.  Move-only, non-copyable.  Construct fresh
//   Machine for each independent run.
//
// ============================================================================

#ifndef SYSTEMLIB_MACHINE_H
#define SYSTEMLIB_MACHINE_H

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "chipsetLib/TsunamiChipset.h"
#include "chipsetLib/TsunamiVariant.h"
// 2026-05-29: switched the COM1 sink from StdoutConsoleBackend to
// SRMConsoleDevice (the Qt-based TCP listen server with PuTTY auto-
// launch).  StdoutConsoleBackend.h is no longer included; the file
// remains in the tree, unused, until a separate cleanup PR removes
// it.  Uart16550::writeTHR stderr-mirrors every TX byte regardless of
// backend state, so the host terminal still sees the banner / >>>
// prompt with or without a PuTTY client attached.
#include "deviceLib/SRMConsoleDevice.h"
#include "coreLib/CpuState.h"
#include "memoryLib/GuestMemory.h"
#include "mmuLib/Ev6Translator.h"
#include "pipelineLib/IFetchOverride.h"
#include "pipelineLib/MmioRegistry.h"
#include "systemLib/SrmLoader.h"
#include "systemLib/StopReason.h"
#include "config/EmulatorSettings.h"   // SSOT: config threaded into Machine (Slice A)

namespace traceLib {
class TraceSink;
}

namespace systemLib {

// Machine implements pipelineLib::IFetchOverride so it can serve
// instruction fetches from the immutable SRM payload buffer when the
// decompressor copy loop overwrites the running stub in guest memory.
// Inheritance is the cheapest path -- avoids a separate adapter class
// and a separate ownership question -- and the interface is a single
// virtual call on the IF hot path.
class Machine : public pipelineLib::IFetchOverride
{
public:
    explicit Machine(uint64_t memSize = 64ULL * 1024ULL * 1024ULL,
                     emulatr::config::EmulatorSettings settings = {});

    // Non-copyable.  Also non-movable because m_memory's MMIO hook is
    // attached with &m_mmio at construction (a self-referential
    // pointer that would dangle after a move).  If Machine ever needs
    // to be movable, override the move constructor to re-attach the
    // hook with the new m_mmio address; deleting move is simpler for
    // v1 since Machine is always held by value at the top level.
    Machine(Machine const&)            = delete;
    Machine& operator=(Machine const&) = delete;
    Machine(Machine&&)                 = delete;
    Machine& operator=(Machine&&)      = delete;

    // Clean-shutdown backstop: flushes any dirty TIG-bus NVRAM the debounce
    // poll has not yet persisted (D1).  m_chipset is still alive in the dtor
    // body, so flash().forceFlush() is safe.
    ~Machine();

    // Load a raw-binary firmware image.  On success the machine
    // remembers the (startPc, palMode) the loader returned so a
    // subsequent reset() can be called without arguments.  Returns
    // false on any FirmwareLoader failure; caller queries
    // lastLoadError() for a printable description.
    bool loadFirmware(std::filesystem::path const& path,
                      uint64_t                     loadPa,
                      uint64_t                     startPa);

    // Load a vendor SRM .exe firmware image (V1 path A).  Performs
    // signature scan + descriptor population + dual-load (mirror PA
    // 0x0 plus stub PA loadPa).  On success captures the descriptor
    // and the immutable payload buffer for later phases (fetch
    // override, done detection, PAL relocation).  Sets cpu.palBase
    // from the descriptor so HW_MFPR HW_PAL_BASE returns the correct
    // value before the decompressor runs.
    bool loadSrmFirmware(std::filesystem::path const& path,
                         uint64_t                     loadPa = kDefaultLoadPa);

    // Load a pre-decompressed AXPBox console image (.rom: [PC u64][PAL_BASE
    // u64][0x200000 console @ PA 0]).  No decompressor / Step D; the console
    // runs from its low base, palBase seeded from the image header.
    bool loadDecompressedRom(std::filesystem::path const& path);

    // SRM descriptor from the most recent loadSrmFirmware (valid =
    // true on success).  Empty / valid = false otherwise.
    SrmDescriptor const& srmDescriptor() const noexcept { return m_srmDescriptor; }

    // Frozen SRM payload (Step C fetch override reads from this).
    std::vector<uint8_t> const& srmPayload() const noexcept { return m_srmPayload; }

    // pipelineLib::IFetchOverride implementation.  Returns true with
    // out filled when pa falls inside [m_srmLoadPa, m_srmLoadPa +
    // payloadSize) and the SRM descriptor is valid; false otherwise.
    // PipelineDriver consults this at the IF stage before reading
    // the encoded instruction word from GuestMemory, so the IBox
    // sees the original SRM stub bytes even after the decompressor
    // has overwritten them in guest memory.
    bool tryFetch(uint64_t pa, uint32_t& out) const noexcept override;

    // pipelineLib::IFetchOverride pre-fetch hook.  Implements V4's
    // Step D: when the CPU first reaches descriptor.entryPa() (the
    // post-decompression entry PC the .exe declared via its embedded
    // LDA/JSR pair), copy the freshly-decompressed PALcode from the
    // mirror PA window [0, kPalRelocSize) up to the architectural
    // [palBase, palBase + kPalRelocSize) so the impending fetch and
    // every subsequent CALL_PAL dispatch lands in real PALcode bytes.
    // One-shot; m_palImageRelocated gates re-firing.  After the
    // trigger, the hot-path cost is one boolean branch per fetch.
    void onBeforeFetch(uint64_t pa) noexcept override;

    std::string const& lastLoadError() const noexcept { return m_lastLoadError; }

    // Clear the architectural state and seed pc + palMode.  Memory
    // contents are NOT cleared -- firmware loaded before reset stays
    // intact.
    void reset(uint64_t pc, bool palMode) noexcept;

    // Convenience: reset using the (startPc, palMode) captured by the
    // most recent successful loadFirmware.  Calling without a prior
    // load is a no-op other than clearing CpuState fields and sets pc
    // = 0, palMode = false.
    void resetToLoadedEntry() noexcept;

    // One pipeline tick.  Returns true if the CPU is still running
    // afterwards (halted = false), false if it halted on this tick.
    bool step() noexcept;

    // One per-cycle iteration of the run loop: the verbatim body of
    // Machine::run's for-loop (sentinel poll + step() + snapshot/predig
    // bookkeeping + IDLEWARP + interval-timer FIRE/DELIVER + the b_irq
    // diverts + synthetic INTERRUPT injection).  Returns false to BREAK the
    // loop (stop sentinel or CPU halt), true to continue.  `i` is the loop
    // ordinal (== legacy run()'s counter), used only for the coarse stop-
    // sentinel poll cadence.  Shared by the legacy Machine::run loop and the
    // dispatcher-driven AlphaCpuAgent so both run the IDENTICAL body -- the
    // Phase-1 byte-identical-boot acceptance gate
    // (journals/20260619_alphacpuagent_phase1_design.md).
    bool stepCycle(uint64_t i) noexcept;

    // Run until halted or maxCycles reached.  Classifies the stop and
    // returns it; cpu() and memory() are observable post-run for the
    // post-mortem dump.
    StopReason run(uint64_t maxCycles = ~uint64_t{0}) noexcept;

    coreLib::CpuState&       cpu()       noexcept { return m_cpu; }
    coreLib::CpuState const& cpu() const noexcept { return m_cpu; }

    // ---- System timebase (Phase 2, STEP 1a) --------------------------------
    // The SYSTEM clock, conceptually distinct from any one CPU's architectural
    // PCC.  Today it is exactly the (single) CPU's cycleCount, so this is a PURE
    // indirection with ZERO behavior change.  Every SYSTEM consumer -- the RTC
    // time source, the Cchip interval-timer fire edge, the flash-flush debounce,
    // the snapshot cadence + filenames, the IDLEWARP -- must read THIS, never a
    // CPU's cycleCount directly.  STEP 3 decouples it into its own counter that
    // advances by the per-step retire cycle delta (design D-1a); under policy
    // P-A the running CPU's PCC tracks it, so the byte-identical gate still holds.
    // See journals/20260619_alphacpuagent_phase2_ownership_lift_design.md.
    [[nodiscard]] uint64_t systemNow() const noexcept { return m_cpu.cycleCount; }

    memoryLib::GuestMemory&       memory()       noexcept { return m_chipset.guestMemory(); }
    memoryLib::GuestMemory const& memory() const noexcept { return m_chipset.guestMemory(); }

    // Tsunami chipset and PA dispatch registry.  Construct-time
    // wired so that any MMIO PA load/store from the pipeline is
    // routed through the registry into the chipset (instead of
    // returning OutOfRange from GuestMemory's flat-array path).
    TsunamiChipset&        chipset()       noexcept { return m_chipset; }
    TsunamiChipset const&  chipset() const noexcept { return m_chipset; }
    pipelineLib::MmioRegistry&       mmio()       noexcept { return m_mmio; }
    pipelineLib::MmioRegistry const& mmio() const noexcept { return m_mmio; }

    // Install a TraceSink (or clear it with nullptr).  Forwarded into
    // PipelineDriver on each step()/run() call.  Caller owns the sink
    // and must keep it alive for the lifetime of any subsequent run().
    void setTraceSink(traceLib::TraceSink* sink) noexcept { m_traceSink = sink; }
    traceLib::TraceSink* traceSink() const noexcept       { return m_traceSink; }

    // ------------------------------------------------------------------
    // Snapshot accessors / hooks.
    // ------------------------------------------------------------------
    // Read-only accessors are consumed by systemLib::Snapshot::save to
    // capture SRM staging state without coupling Snapshot to Machine's
    // private members.  restoreSrmStaging is the matching push-back on
    // the load path; it overwrites the SRM staging fields wholesale
    // (the SRM payload buffer move-in is intentional -- callers should
    // hand off a freshly-deserialised vector).

    uint64_t srmLoadPa()          const noexcept { return m_srmLoadPa; }
    bool     palImageRelocated()  const noexcept { return m_palImageRelocated; }
    uint64_t loadedStartPc()      const noexcept { return m_loadedStartPc; }
    bool     loadedPalMode()      const noexcept { return m_loadedPalMode; }

    void restoreSrmStaging(SrmDescriptor const&    descriptor,
                           std::vector<uint8_t>&&  payload,
                           uint64_t                loadPa,
                           bool                    relocated,
                           uint64_t                startPc,
                           bool                    palMode) noexcept;

    // Configure the directory used for auto-saves (relative paths
    // resolve against the current working directory).  Defaults to
    // systemLib::kSnapshotDirDefault if never called.
    void setSnapshotDir(std::filesystem::path const& dir) noexcept
    {
        m_snapshotDir = dir;
    }
    std::filesystem::path const& snapshotDir() const noexcept
    {
        return m_snapshotDir;
    }

    // Disable / re-enable the automatic save-on-halt and periodic-save
    // hooks inside run().  Tests that exercise step() in tight loops
    // and do not want the disk traffic should disable.  Default: on.
    void setAutoSnapshotEnabled(bool on) noexcept { m_autoSnapshotEnabled = on; }
    bool autoSnapshotEnabled() const noexcept     { return m_autoSnapshotEnabled; }

    // ------------------------------------------------------------------
    // One-shot pre-diagnostic snapshot trigger.
    // ------------------------------------------------------------------
    // Arms a per-retire PC check inside run().  The first time the CPU
    // retires an instruction whose architectural PC equals `pc`, the run
    // loop writes a snapshot named `predig_<hex16>_cyc<cycle>.axpsnap`
    // into the configured snapshotDir and immediately calls
    // setAutoSnapshotEnabled(false).  The trigger then disarms itself
    // (one-shot) and the run continues.
    //
    // The predig_ prefix is deliberately distinct from auto_ so the
    // periodic prune in run() never touches the file.  Disabling
    // auto-save after the capture keeps the predig snapshot
    // mtime-newest, so the next cold start's autoloadLatest call
    // picks it up and resumes from exactly the saved PC -- the
    // operator-visible effect is "skip the multi-hour boot replay,
    // land at the diag boundary, immediately enter the diag region."
    //
    // pc == 0 disarms (PC 0 is the reset vector and is never retired
    // in steady state; safe sentinel).  Calling armSnapshotOnPc after
    // the trigger has already fired re-arms for a second capture.
    //
    // nameTag is appended after `predig_` in the filename when non-
    // empty -- "0x12040" or "sys_cbox_entry" letting an operator
    // disambiguate multiple captures from the same boot.  Empty tag
    // falls back to a hex PC encoding.
    void armSnapshotOnPc(uint64_t pc, std::string nameTag = {})
    {
        // Legacy single-PC arm: clear the trigger list, add one, and
        // (back-compat) disable auto-save on fire so the predig stays
        // mtime-newest for autoload.
        m_snapTriggers.clear();
        m_anySnapFired           = false;
        m_snapDisableAutosOnFire = true;
        if (pc != 0) m_snapTriggers.push_back(SnapTrigger{pc, std::move(nameTag), false});
        m_snapTriggersRemaining  = static_cast<int>(m_snapTriggers.size());
    }

    // 2026-06-06: multi-PC one-shot snapshot triggers.  Append a PC to
    // capture in the SAME run; each fires once and writes its own
    // predig_<tag>_cyc<N>.axpsnap.  Unlike armSnapshotOnPc, this does NOT
    // disable auto-save (pair with --autosnapshot off for mint runs so
    // there is no periodic-save disk cliff).  Lets one cold boot mint
    // several entry-PC snapshots (pre-init / post-GCT-build / UPD> ...).
    void addSnapshotOnPc(uint64_t pc, std::string nameTag = {})
    {
        if (pc == 0) return;
        m_snapTriggers.push_back(SnapTrigger{pc, std::move(nameTag), false});
        ++m_snapTriggersRemaining;
    }

    // True after at least one armed-PC trigger has fired in the current
    // run.  Cleared by armSnapshotOnPc.  Used by tests / post-run diags.
    bool armSnapshotFired() const noexcept { return m_anySnapFired; }

    // ------------------------------------------------------------------
    // One-shot synthetic INTERRUPT-class trap injection.
    // ------------------------------------------------------------------
    // Arms a per-cycle check inside run().  When cpu.cycleCount first
    // reaches or exceeds `cycle`, the run loop forces a divert to
    // palBase + 0x100 (the Alpha INTERRUPT entry vector) as if a chipset
    // had asserted IRQ.  Saves the current cpu.pc into excAddr with the
    // PALmode bit, sets cpu.pc = palBase + 0x100, sets palMode = true,
    // and disarms (one-shot).  The trigger then never fires again in
    // this run unless armInterruptInjection is called a second time.
    //
    // Purpose: test the hypothesis that the 2026-05-13 SRM sys__cbox
    // outer loop is a deliberate MCHK-as-yield idle wait that progresses
    // only on receipt of an external INTERRUPT-class trap.  See memory
    // note `project_idle_wait_interrupt_hypothesis.md` for the
    // experimental design and the three outcome categories.
    //
    // cycle == 0 disarms.  Calling armInterruptInjection after the
    // trigger has already fired re-arms for a second injection.
    //
    // Requires palBase != 0 at fire time; if palBase is zero (PALcode
    // not yet loaded) the injection is skipped with a loud diagnostic
    // -- there is no architectural target for the divert and silently
    // halting the run would mask the misuse.
    void armInterruptInjection(uint64_t cycle) noexcept
    {
        m_injectInterruptCycle = cycle;
        m_injectInterruptFired = false;
    }

    // True after the interrupt injection has fired in the current run.
    bool injectInterruptFired() const noexcept { return m_injectInterruptFired; }

    // ------------------------------------------------------------------
    // Phase C+: interrupt-arbitration policy.
    // ------------------------------------------------------------------
    // Returns true when an interrupt of priority `irqLevel` may be
    // delivered to the CPU at this instant.  Consulted by the per-retire
    // poll in `run()` before staging an interval-timer divert (see
    // chipsetLib/CchipIntervalTimer.h and the Phase C block in
    // Machine::run).
    //
    // Today's predicate (Phase C+ MVP):
    //   m_palImageRelocated && m_cpu.palBase != 0
    //
    // The relocation gate captures the safety invariant "PAL bytes are
    // resident at palBase + 0x100, so a divert lands on valid trap
    // code."  Before Step D PAL relocation fires (cyc ~4.2M on cold
    // boot), `palBase` points at a destination region not yet populated
    // -- diverting there executes uninitialized memory and leaves the
    // firmware in an unrecoverable partial-PAL state (see Phase C+
    // post-mortem in journals/CchipPhaseA_Design_Notes.md addendum).
    //
    // TODO(unwired, Phase D):  extend to
    //   m_palImageRelocated && m_cpu.palBase != 0 && irqLevel > m_cpu.ipl
    // once CpuState carries a tracked IPL field updated by HW_MTPR IPL
    // dispatch.  The relocation gate then transitions from operational
    // policy to defensive safety assertion.  Real Alpha hardware gates
    // exclusively on (irqLevel > cpu.ipl); the relocation predicate is
    // an emulator-side approximation of the firmware-managed IPL high
    // window that masks early-init interrupts on real hardware.
    //
    // Layering note: this method lives on Machine because PAL
    // relocation lifecycle is system-level state, not CPU-architectural
    // state.  When V4 grows a Cpu/AlphaCpu execution object above
    // CpuState, the method migrates down to it; Machine forwards the
    // relocation signal in via a constructor argument or setter.  Call
    // sites already read `machine.canAcceptInterrupt(N)` which becomes
    // `cpu.canAcceptInterrupt(N)` with no semantic change.
    bool canAcceptInterrupt(uint8_t irqLevel) const noexcept;


private:
    // 2026-06-08 (SSOT Slice A): authoritative config, loaded once in main and
    // threaded in by value.  Declared FIRST so the m_consoleCfg in-class
    // initializer (makeCom1Cfg(m_settings)) sees a fully-constructed object.
    emulatr::config::EmulatorSettings m_settings;
    coreLib::CpuState        m_cpu;
    // m_memory removed -- m_chipset owns the single GuestMemory backing.
    mmuLib::Ev6Translator    m_translator;   // owned for future TLB state

    // COM1 host console sink: SRM firmware UART output (THR writes to
    // port 0x3F8) is forwarded here.  Declared before m_chipset so it
    // outlives the UART that holds a pointer to it.
    //
    // 2026-05-29: m_consoleCfg must be declared BEFORE m_com1Backend
    // because SRMConsoleDevice's constructor takes Config& AND copies
    // it by value into its own m_config member.  In-class initializer
    // calls makeCom1Cfg() to populate the Config WITH the desired
    // port=10023 / autoLaunchPutty=true / puttyPath BEFORE m_com1Backend
    // runs its constructor -- the construction order is:
    //
    //   1. m_consoleCfg{makeCom1Cfg()}     <-- our values written here
    //   2. m_com1Backend(m_consoleCfg)     <-- ctor copies our values
    //
    // Without the factory, body-level assignments to m_consoleCfg are
    // dead writes -- they mutate the visible m_consoleCfg but never
    // reach SRMConsoleDevice's already-captured m_config.  This bug
    // was diagnosed 2026-05-29 from the boot log showing "Listening
    // on TCP port 23" (the Config struct's default) instead of 10023.
    //
    // SRMConsoleDevice::start() (called from Machine ctor body, see
    // Machine.cpp) opens the TCP listen socket, launches PuTTY via
    // QProcess::startDetached, and migrates the backend object to its
    // own QThread for Qt event-loop affinity.  PuTTY is detached and
    // persists past Machine destruction -- the user sees a frozen
    // record of the session even after the emulator exits.
    static SRMConsoleDevice::Config makeCom1Cfg(
        emulatr::config::EmulatorSettings const& settings) noexcept;
    SRMConsoleDevice::Config m_consoleCfg{makeCom1Cfg(m_settings)};
    SRMConsoleDevice         m_com1Backend;

    // Tsunami chipset (CSRs, PCI bridges, sparse mem/IO windows) and
    // the PA dispatch registry that the GuestMemory MMIO hooks consult
    // before falling through to flat-array DRAM.  Construction order
    // is intentional: m_memory must outlive the hook attachment, and
    // m_chipset/m_mmio must be alive whenever m_memory is read or
    // written (which is always, post-construction).  Default chipset
    // variant is Tsunami (ES40 / PC264 -- the platform the cl67 SRM
    // firmware targets); future Machine constructor overload can
    // parameterise this.
    TsunamiChipset             m_chipset;
    pipelineLib::MmioRegistry  m_mmio;

    // Captured by loadFirmware for resetToLoadedEntry's convenience.
    uint64_t                 m_loadedStartPc = 0;
    bool                     m_loadedPalMode = false;
    uint64_t                 m_loadedPalBase = 0;

    // Last load error message (empty when no failure).
    std::string              m_lastLoadError;

    // SRM-specific state captured by loadSrmFirmware.  Empty descriptor
    // (valid = false) when the most recent load was raw-binary or
    // there has been no load at all.
    SrmDescriptor            m_srmDescriptor;
    std::vector<uint8_t>     m_srmPayload;
    uint64_t                 m_srmLoadPa = 0;   // PA the stub was loaded at; tryFetch's range base

    // Step D one-shot gate.  Set by onBeforeFetch the first time the
    // CPU reaches descriptor.entryPa(), after the relocation copy has
    // run.  Steady-state hot-path cost after the trigger is a single
    // boolean check, predicted-taken (no-op early-out).
    bool                     m_palImageRelocated = false;

    // Optional non-owning trace sink, forwarded into PipelineDriver.
    traceLib::TraceSink*     m_traceSink = nullptr;

    // Graceful-stop sentinel path, resolved once in run()'s setup and read by
    // stepCycle()'s coarse poll.  Was a run()-local; promoted to a member so
    // the per-cycle body (stepCycle) reads it without re-resolving the path
    // each cycle.  (2026-06-19, AlphaCpuAgent Phase-1 extraction.)
    std::filesystem::path    m_stopSentinel;

    // ------------------------------------------------------------------
    // Snapshot auto-save state.
    // ------------------------------------------------------------------
    // m_snapshotDir defaults to "snapshots" (resolved against CWD); a
    // call to setSnapshotDir overrides.  m_nextAutoSaveCycle is the
    // absolute cycleCount the next periodic save will fire at; updated
    // after each successful save.  m_autoSnapshotEnabled defaults to
    // false so the test suite stays silent; main.cpp explicitly opts
    // in for the production binary via setAutoSnapshotEnabled(true).
    std::filesystem::path    m_snapshotDir       = "snapshots";
    uint64_t                 m_nextAutoSaveCycle = 0;
    bool                     m_autoSnapshotEnabled = false;

    // ------------------------------------------------------------------
    // Pre-diagnostic snapshot trigger state.
    // ------------------------------------------------------------------
    // 2026-06-06: generalized from a single armed PC to a LIST of
    // one-shot triggers, so one cold-boot pass can mint several entry-PC
    // snapshots (pre-init / post-GCT-build / UPD> ...).  Each fires once
    // at the first retire whose PC matches, writing predig_<tag>_cyc<N>.
    // m_snapTriggersRemaining gates the per-retire cost: when 0, a single
    // compare; when >0, a short loop over the (few) triggers.
    struct SnapTrigger { uint64_t pc; std::string tag; bool fired; };
    std::vector<SnapTrigger> m_snapTriggers;
    int                      m_snapTriggersRemaining  = 0;
    bool                     m_anySnapFired           = false;
    bool                     m_snapDisableAutosOnFire = false;

    // ------------------------------------------------------------------
    // One-shot interrupt injection trigger state.
    // ------------------------------------------------------------------
    // Armed via armInterruptInjection.  Run loop fires the divert when
    // cycleCount first reaches m_injectInterruptCycle and palBase is
    // non-zero.  Same posture as the snapshot trigger: cycle == 0 means
    // disarmed; once-fired sets m_injectInterruptFired so subsequent
    // cycles do not re-fire.  Steady-state cost when disarmed: one
    // uint64_t load + branch-predicted-NOT-taken compare per retire.
    uint64_t                 m_injectInterruptCycle = 0;
    bool                     m_injectInterruptFired = false;
};

} // namespace systemLib

#endif // SYSTEMLIB_MACHINE_H
