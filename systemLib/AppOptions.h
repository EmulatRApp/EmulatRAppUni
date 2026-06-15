// ============================================================================
// systemLib/AppOptions.h -- command-line shape for the Emulatr executable
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
// Hand-rolled parser for the small set of flags EmulatR's main needs.
// No third-party arg-parse dependency.  Unknown flags fail parse; the
// caller (main) prints helpText and exits non-zero.
//
// Flags (Phase 1 v1):
//
//   --firmware <path>      raw-binary firmware image to load
//   --load-pa  <hex|dec>   physical address to load the image at (default 0x0)
//   --start-pa <hex|dec>   physical address to start execution
//                          (low bit is the PAL bit, V1 convention;
//                           parser strips it and sets palMode)
//   --mem      <bytes>     guest memory size (default 64 MiB)
//   --max-cycles <N>       cap on the run loop (default ~uint64_t{0})
//   --pal-mode             force palMode = true at reset (independent of
//                          the start-pa low bit; convenience for tests)
//   --trace    <dec,machine>  reserved for Phase 2 traceLib; parsed now,
//                             stored on the struct, no effect in Phase 1
//
// ============================================================================

#ifndef SYSTEMLIB_APPOPTIONS_H
#define SYSTEMLIB_APPOPTIONS_H

#include <cstdint>
#include <vector>   // snapshotOnPcs list (multi-PC snapshot triggers, 2026-06-06)
#include <filesystem>
#include <string>

namespace systemLib {

// Firmware load contract.  Auto picks SRM if path ends in .exe and
// the signature scan succeeds; otherwise falls back to raw-binary.
// Raw and Srm force the respective loader regardless of extension.
enum class FirmwareFormat : uint8_t {
    Auto = 0,
    Raw  = 1,
    Srm  = 2,
};

struct AppOptions
{
    std::filesystem::path firmwarePath;
    uint64_t              loadPa     = 0x0ULL;
    uint64_t              startPa    = 0x0ULL;
    uint64_t              memSize    = 64ULL * 1024ULL * 1024ULL;
    bool                  memSizeSet = false;   // true iff --mem given on CLI (SSOT: CLI > [System] memorySize)
    uint64_t              maxCycles  = ~uint64_t{0};
    bool                  palMode    = false;
    FirmwareFormat        firmwareFormat = FirmwareFormat::Auto;

    // Reserved for Phase 2.  decTraceLog / machineTraceLog stay empty
    // until traceLib lands; parser populates them for forward CLI
    // compatibility but Phase 1 main does not consume them.
    std::filesystem::path decTraceLog;
    std::filesystem::path machineTraceLog;
    uint32_t              traceMask = 0;

    // Pre-diagnostic one-shot snapshot trigger.  Non-zero PC arms the
    // Machine::run check that fires once when the CPU retires an
    // instruction at this PC; the resulting `predig_*.axpsnap` file
    // is non-pruneable and becomes the canonical resume point on the
    // next cold start (autoloadLatest picks it up by mtime).  Zero
    // disarms.  Default: 0.
    //
    // snapshotNameTag is appended after `predig_` in the filename when
    // non-empty -- a human-readable disambiguation token like
    // "sys_cbox_entry".  Empty falls back to the hex PC value.
    // 2026-06-06: was a single uint64_t; now a list so one run can mint
    // several entry-PC snapshots (--snapshot-on-pc 0xA,0xB,0xC).  A single
    // value preserves the legacy one-shot+disable-autos behavior; 2+ values
    // arm multi-PC triggers (pair with --autosnapshot off).
    std::vector<uint64_t> snapshotOnPcs;
    std::string           snapshotNameTag;

    // 2026-06-05: mint-run controls (Spec_overnight_coldboot_mint_run).
    // autoSnapshot=false suppresses periodic auto_*.axpsnap saves (the
    // only capture is then the named --snapshot-on-pc file) -- avoids the
    // disk cliff on a trillion-cycle cold boot.  noAutoload suppresses
    // autoloadLatest at startup so the run is a genuine cold boot (proves
    // the yyreset hit count from cold origin, not a restore).  Env
    // equivalents: EMULATR_AUTOSNAP=off, EMULATR_NO_AUTOLOAD=1.
    bool                  autoSnapshot    = true;
    bool                  noAutoload      = false;

    // Experimental: one-shot synthetic INTERRUPT-class trap injection
    // at the specified absolute cycleCount.  When cpu.cycleCount first
    // reaches this value, Machine::run forces cpu.pc = palBase + 0x100
    // as if a chipset had asserted IRQ.  Default 0 = disabled.  See
    // memory note `project_idle_wait_interrupt_hypothesis.md` for the
    // experimental design.
    uint64_t              injectInterruptCycle = 0;

    // --dump-disasm <pa>:<count>  one-shot disassembly dump fired by
    // main() immediately after firmware load completes and before run
    // begins.  count is the number of 4-byte instructions to dump.
    // Default count = 0 means "no dump".  Used to inspect bytes that
    // would otherwise only be visible through a full retire trace.
    // See coreLib/PaDump.h for the rendering format.
    uint64_t              dumpDisasmPa    = 0;
    uint64_t              dumpDisasmCount = 0;

    // --log-disable / --log-only / --log-verbose / --log-file payloads.
    // Parsed verbatim and forwarded to coreLib::applyLogFlag*() before
    // Machine::run() begins.  See coreLib/LogSubsystem.h for the
    // subsystem-name vocabulary and per-flag semantics.  Empty strings
    // mean the flag was not supplied.
    std::string           logDisable;       // body of --log-disable <subsys>
    std::string           logOnly;          // body of --log-only <csv>
    std::string           logVerbose;       // body of --log-verbose <subsys>
    std::string           logFile;          // body of --log-file <subsys>=<path>

    // Set by parse on a flag the user mistyped; main prints helpText
    // and returns non-zero exit when this is non-empty.
    std::string parseError;

    // Result of `--help`; main prints helpText and exits 0.
    bool helpRequested = false;


    // Parse argc/argv into an AppOptions.  Never throws; sets
    // parseError for caller-handled error paths.
    static AppOptions parse(int argc, char* argv[]);

    // One-shot help text for main to print on `--help` or parse error.
    static char const* helpText() noexcept;
};

} // namespace systemLib

#endif // SYSTEMLIB_APPOPTIONS_H
