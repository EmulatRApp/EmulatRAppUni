// ============================================================================
// systemLib/AppOptions.cpp -- argv parser for the Emulatr executable
// ============================================================================
// Project: EmulatR -- Alpha AXP / EV6 Architecture Emulator (V4)
// Copyright (C) 2025, 2026 eNVy Systems, Inc.  All rights reserved.
// Licensed under eNVy Systems Non-Commercial License v1.1
// ============================================================================

#include "systemLib/AppOptions.h"

#include <charconv>
#include <cstdlib>
#include <cstring>
#include <string_view>

namespace systemLib {

namespace {

// Parse an unsigned integer.  Accepts decimal or hex (0x prefix).
// Returns true on success and writes value; false on any malformed
// input.  Plain std::strtoull semantics with explicit base detection.
bool parseUnsigned(std::string_view text, uint64_t& value) noexcept
{
    if (text.empty()) return false;

    int  base   = 10;
    auto first  = text.data();
    auto last   = text.data() + text.size();

    if (text.size() >= 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        base   = 16;
        first += 2;
    }
    if (first == last) return false;

    uint64_t parsed = 0;
    auto [ptr, ec] = std::from_chars(first, last, parsed, base);
    if (ec != std::errc{} || ptr != last) return false;

    value = parsed;
    return true;
}


// Find the comma in --trace dec,machine and split into two paths.
// Empty string after the comma leaves machineTraceLog empty.
//
// path = std::string{...} is ambiguous on MSVC because std::string is
// convertible to both path::string_type (wstring on Windows) and path
// itself.  Construct path explicitly from the std::string and assign
// the path -- the assignment overload resolves cleanly.
void splitTracePaths(std::string_view spec,
                     std::filesystem::path& dec,
                     std::filesystem::path& machine)
{
    auto const comma = spec.find(',');
    if (comma == std::string_view::npos) {
        dec     = std::filesystem::path(std::string{spec});
        machine = std::filesystem::path{};
        return;
    }
    dec     = std::filesystem::path(std::string{spec.substr(0, comma)});
    machine = std::filesystem::path(std::string{spec.substr(comma + 1)});
}

} // anonymous namespace


AppOptions AppOptions::parse(int argc, char* argv[])
{
    AppOptions opts;

    for (int i = 1; i < argc; ++i) {
        std::string_view const flag{argv[i]};

        if (flag == "--help" || flag == "-h") {
            opts.helpRequested = true;
            return opts;
        }

        if (flag == "--pal-mode") {
            opts.palMode = true;
            continue;
        }

        // 2026-06-05: --no-autoload -- bare flag; suppress autoloadLatest
        // so the run is a genuine cold boot (mint-run / cold-origin count).
        if (flag == "--no-autoload") {
            opts.noAutoload = true;
            continue;
        }

        // Qt VS Tools auto-injects single-dash flags into argv when
        // debugging Qt-linked projects (e.g. -qmljsdebugger=file:...).
        // These are not ours; silently skip any single-dash arg that
        // is not -h.  Our project flags all use the -- prefix, so this
        // discriminates cleanly between "Qt passthrough" and "user
        // typed it wrong".
        if (flag.size() > 1 && flag[0] == '-' && flag[1] != '-') {
            continue;
        }

        // Validate the flag is known BEFORE consuming the next argv
        // entry as a value -- otherwise a malformed flag like --h
        // emits a misleading "flag missing value" message instead of
        // the real "unknown flag" diagnosis.
        bool const knownValueFlag =
               flag == "--firmware"
            || flag == "--firmware-format"
            || flag == "--load-pa"
            || flag == "--start-pa"
            || flag == "--mem"
            || flag == "--max-cycles"
            || flag == "--trace"
            || flag == "--snapshot-on-pc"
            || flag == "--snapshot-name-tag"
            || flag == "--autosnapshot"
            || flag == "--inject-interrupt-at-cycle"
            || flag == "--dump-disasm"
            || flag == "--log-disable"
            || flag == "--log-only"
            || flag == "--log-verbose"
            || flag == "--log-file";

        if (!knownValueFlag) {
            opts.parseError = std::string{"unknown flag: "} + std::string{flag};
            return opts;
        }

        if (i + 1 >= argc) {
            opts.parseError = std::string{"flag missing value: "} + std::string{flag};
            return opts;
        }
        std::string_view const value{argv[++i]};

        if (flag == "--firmware") {
            opts.firmwarePath = std::filesystem::path(std::string{value});
        }
        else if (flag == "--firmware-format") {
            if      (value == "auto") opts.firmwareFormat = FirmwareFormat::Auto;
            else if (value == "raw")  opts.firmwareFormat = FirmwareFormat::Raw;
            else if (value == "srm")  opts.firmwareFormat = FirmwareFormat::Srm;
            else {
                opts.parseError = std::string{"--firmware-format: must be auto|raw|srm, got "}
                                + std::string{value};
                return opts;
            }
        }
        else if (flag == "--load-pa") {
            if (!parseUnsigned(value, opts.loadPa)) {
                opts.parseError = std::string{"--load-pa: not a number ("}
                                + std::string{value} + ")";
                return opts;
            }
        }
        else if (flag == "--start-pa") {
            if (!parseUnsigned(value, opts.startPa)) {
                opts.parseError = std::string{"--start-pa: not a number ("}
                                + std::string{value} + ")";
                return opts;
            }
        }
        else if (flag == "--mem") {
            if (!parseUnsigned(value, opts.memSize)) {
                opts.parseError = std::string{"--mem: not a number ("}
                                + std::string{value} + ")";
                return opts;
            }
            opts.memSizeSet = true;   // CLI override present (SSOT: CLI > ini)
        }
        else if (flag == "--max-cycles") {
            if (!parseUnsigned(value, opts.maxCycles)) {
                opts.parseError = std::string{"--max-cycles: not a number ("}
                                + std::string{value} + ")";
                return opts;
            }
        }
        else if (flag == "--autosnapshot") {
            // 2026-06-05: master toggle for periodic auto_*.axpsnap saves.
            if      (value == "on")  opts.autoSnapshot = true;
            else if (value == "off") opts.autoSnapshot = false;
            else {
                opts.parseError = std::string{"--autosnapshot: must be on|off, got "}
                                + std::string{value};
                return opts;
            }
        }
        else if (flag == "--snapshot-on-pc") {
            // 2026-06-06: accept a comma-separated list of PCs so one run
            // can mint several entry-PC snapshots.  A single value keeps
            // the legacy one-shot+disable-autos behavior (see main.cpp).
            std::string_view rest = value;
            opts.snapshotOnPcs.clear();
            while (!rest.empty()) {
                size_t const comma = rest.find(',');
                std::string_view tok = (comma == std::string_view::npos)
                                      ? rest : rest.substr(0, comma);
                uint64_t pcv = 0;
                if (!parseUnsigned(tok, pcv)) {
                    opts.parseError = std::string{"--snapshot-on-pc: not a number ("}
                                    + std::string{tok} + ")";
                    return opts;
                }
                opts.snapshotOnPcs.push_back(pcv);
                if (comma == std::string_view::npos) break;
                rest = rest.substr(comma + 1);
            }
        }
        else if (flag == "--snapshot-name-tag") {
            opts.snapshotNameTag = std::string{value};
        }
        else if (flag == "--inject-interrupt-at-cycle") {
            if (!parseUnsigned(value, opts.injectInterruptCycle)) {
                opts.parseError = std::string{"--inject-interrupt-at-cycle: not a number ("}
                                + std::string{value} + ")";
                return opts;
            }
        }
        else if (flag == "--dump-disasm") {
            // Format: <pa>:<count>  e.g. 0x6003e0:32
            // count is number of 4-byte instructions to dump.
            auto const colon = value.find(':');
            if (colon == std::string_view::npos) {
                opts.parseError =
                    std::string{"--dump-disasm: expected pa:count, got "}
                  + std::string{value};
                return opts;
            }
            std::string_view const paText    = value.substr(0, colon);
            std::string_view const countText = value.substr(colon + 1);
            if (!parseUnsigned(paText, opts.dumpDisasmPa)) {
                opts.parseError = std::string{"--dump-disasm: bad pa ("}
                                + std::string{paText} + ")";
                return opts;
            }
            if (!parseUnsigned(countText, opts.dumpDisasmCount)) {
                opts.parseError = std::string{"--dump-disasm: bad count ("}
                                + std::string{countText} + ")";
                return opts;
            }
        }
        else if (flag == "--log-disable") {
            opts.logDisable = std::string{value};
        }
        else if (flag == "--log-only") {
            opts.logOnly = std::string{value};
        }
        else if (flag == "--log-verbose") {
            opts.logVerbose = std::string{value};
        }
        else if (flag == "--log-file") {
            opts.logFile = std::string{value};
        }
        else if (flag == "--trace") {
            splitTracePaths(value, opts.decTraceLog, opts.machineTraceLog);
            // Default trace mode: PAL_WINDOW (bit 0x40) plus
            // RETIRE_COMPACT (bit 0x80) -- both armed any time the
            // operator passes --trace.
            //
            // PAL_WINDOW is quiet during steady-state code (e.g. the
            // SRM decompressor copy loop) and loud only inside PAL
            // windows + their post-exit tail.  The per-instruction
            // REG/FRG dumps are NOT armed here; --trace-instr /
            // --trace-regfile CLI flags will land later when we want
            // full noise.
            //
            // RETIRE_COMPACT routes a third channel to X:\traces with
            // one elided line per retired instruction (cycle, PC, PAL
            // mode, exc_addr, non-zero integer registers).  This is
            // the hang-diagnosis stream: cheap to scan after the run,
            // self-describing header, debugger-pokeable peek counter.
            //
            // Heartbeat emission is independent of trace mask -- the
            // sink always emits a "still alive" line every N commits
            // so a long quiet run is observably progressing.
            opts.traceMask = 0x40u | 0x80u;   // TRACE_PAL_WINDOW | TRACE_RETIRE_COMPACT
        }
        // No final else needed: the knownValueFlag gate above already
        // rejected anything that does not match one of these branches.
    }

    // 2026-06-08 (SSOT Slice B): --firmware is no longer required on the CLI.
    // main falls back to [ROM] firmwareImage from EmulatrV4.ini and enforces
    // "firmware from CLI or ini" after the settings load.
    return opts;
}


char const* AppOptions::helpText() noexcept
{
    return
        "Emulatr -- Alpha AXP / EV6 Architecture Emulator (V4)\n"
        "\n"
        "Usage: Emulatr --firmware <path> [options]\n"
        "\n"
        "Required:\n"
        "  --firmware <path>      raw-binary firmware image to load\n"
        "\n"
        "Optional:\n"
        "  --firmware-format <auto|raw|srm>   loader contract (default auto;\n"
        "                                     auto picks srm on .exe and signature\n"
        "                                     match, raw otherwise)\n"
        "  --load-pa  <hex|dec>   physical address to load image at  (default 0x0\n"
        "                                                            for raw, 0x900000\n"
        "                                                            for srm)\n"
        "  --start-pa <hex|dec>   physical address to start at; low bit is PAL bit\n"
        "                         (default 0x0; ignored in srm format -- entry comes\n"
        "                         from the descriptor)\n"
        "  --mem      <bytes>     guest memory size                  (default 64 MiB)\n"
        "  --max-cycles <N>       cap on the run loop                (default unlimited)\n"
        "  --autosnapshot <on|off>  periodic auto_*.axpsnap saves      (default on;\n"
        "                              off = only the named --snapshot-on-pc file is\n"
        "                              written -- use for cold-boot mint runs to avoid\n"
        "                              the disk cliff.  Env: EMULATR_AUTOSNAP=off)\n"
        "  --no-autoload          skip autoload-newest at startup -- forces a genuine\n"
        "                              cold boot instead of restoring the newest\n"
        "                              snapshot.  Env: EMULATR_NO_AUTOLOAD=1\n"
        "  --pal-mode             force palMode = true regardless of start-pa low bit\n"
        "  --trace    <dec,machine>  DEC listing + machine-parsable trace channels\n"
        "  --snapshot-on-pc <hex|dec>[,<hex|dec>...]  one-shot snapshot trigger(s);\n"
        "                              first retire at each PC writes a predig_*.axpsnap.\n"
        "                              ONE PC: legacy behavior -- also disables auto-save\n"
        "                              so the file stays mtime-newest for autoload.\n"
        "                              MULTIPLE PCs (comma-list): each fires once; pair\n"
        "                              with --autosnapshot off (no auto-save disable) to\n"
        "                              mint several entry-PC snapshots in one boot.\n"
        "                              predig_ files are never pruned.\n"
        "  --snapshot-name-tag <s>  base tag inserted after `predig_` (e.g. gctbuild);\n"
        "                           multi-PC appends _<pchex> per file.  Default: hex PC\n"
        "  --inject-interrupt-at-cycle <hex|dec>  EXPERIMENTAL.  Fires a one-shot\n"
        "                                         synthetic INTERRUPT-class trap (divert\n"
        "                                         to palBase + 0x100) when cpu.cycleCount\n"
        "                                         first reaches this value.  Tests the\n"
        "                                         hypothesis that the sys__cbox MCHK loop\n"
        "                                         is a deliberate idle wait for an external\n"
        "                                         IRQ.  Zero = off (default).\n"
        "  --dump-disasm <pa>:<count>  one-shot disassembly dump fired after firmware\n"
        "                              load and before run; <count> 4-byte instructions\n"
        "                              starting at physical address <pa>.  Renders to\n"
        "                              stderr in the PaDump format (see coreLib/PaDump.h).\n"
        "                              Requires EMULATR_PA_DUMP=ON at compile time.\n"
        "  --log-disable <subsys>      silence one subsystem entirely\n"
        "  --log-only <csv>            silence every subsystem not in the list\n"
        "  --log-verbose <subsys>      unthrottled emit for one subsystem\n"
        "  --log-file <subsys>=<path>  redirect one subsystem's emit stream to a file\n"
        "                              <subsys> is one of: Cbox Unalign IntervalTimer\n"
        "                              Snapshot PalRelocation ChipsetCsr StepD Misc\n"
        "                              (case-insensitive).  Requires\n"
        "                              EMULATR_DIAGNOSTIC_LOGGING=ON at compile time.\n"
        "  --help                 print this help and exit\n"
        "\n"
        "On exit:\n"
        "  0  CPU halted cleanly (CALL_PAL HALT)\n"
        "  1  any other stop reason (fault, max cycles, parse error)\n"
        "  2  firmware load failure\n";
}

} // namespace systemLib
