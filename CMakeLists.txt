# ============================================================================
# CMakeLists.txt -- EmulatR V4 root build configuration
# ============================================================================
# Project: EmulatR -- Alpha AXP / EV6 Architecture Emulator (V4)
# Copyright (C) 2025, 2026 eNVy Systems, Inc.  All rights reserved.
# Licensed under eNVy Systems Non-Commercial License v1.1
#
# Project Architect: Timothy Peer
# AI Collaboration:  Claude (Anthropic)
#
# Commercial use prohibited without separate license.
# Contact:        peert@envysys.com  |  https://envysys.com
# Documentation:  https://timothypeer.github.io/ASA-EMulatR-Project/
# ============================================================================

cmake_minimum_required(VERSION 3.16)


# ============================================================================
# vcpkg Integration  (must appear before project())
# ============================================================================
# CMAKE_TOOLCHAIN_FILE wires vcpkg into CMake's find_package() mechanism.
# All installed packages (spdlog, fmt, ...) are discovered automatically,
# including correct debug/release library selection.
# Override at configure time: cmake -DVCPKG_ROOT=<path> ...
# ============================================================================
# The vcpkg toolchain + triplet defaults are Windows-specific (the dev
# host keeps vcpkg at C:/vcpkg).  Only seed them on Windows so non-Windows
# hosts (macOS/Linux, deps via Homebrew/system) configure without a bogus
# toolchain file.  Override anywhere with -DCMAKE_TOOLCHAIN_FILE=...
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE AND WIN32)
    set(CMAKE_TOOLCHAIN_FILE "C:/vcpkg/scripts/buildsystems/vcpkg.cmake"
        CACHE STRING "vcpkg toolchain file" FORCE)
endif()

if(NOT DEFINED VCPKG_TARGET_TRIPLET AND WIN32)
    set(VCPKG_TARGET_TRIPLET "x64-windows"
        CACHE STRING "vcpkg target triplet")
endif()

project(Emulatr VERSION 1.5.12 LANGUAGES C CXX)  # C enabled for the vendored SoftFloat (.c) static lib (task #26).  VERSION feeds CPACK_PACKAGE_VERSION.
# VERSION grammar (H&M topic "EmulatR Version Numbering and Release Stages",
# 2026-08-04): <stage><major>.<variant>-<build>.  MAJOR=release series,
# MINOR=appuni variant (5), PATCH=deliberate build number.  Synchronized with
# the H&M doc project (claudeRV4.hmxp versionmajor/minor/build).
# THIS LINE + THE STAGE LETTER BELOW ARE THE SINGLE SOURCE OF TRUTH.
#
# DEC release-stage letter (per the same topic): X=experimental (development),
# T=field test, V=released.  Flip this ONE letter at stage transitions; the
# badge renders "<stage>1.5-12" (e.g. "X1.5-12").
set(EMULATR_VERSION_STAGE "X")

# Render the version header from the one source of truth above.  Generated into
# the BINARY dir (== source root for the in-source MSVC build; out/build/<cfg>
# for Ninja hosts); both executable targets add <binary>/generated to their
# include path.  See config/emulatr_version.h.in.
configure_file(config/emulatr_version.h.in
               "${CMAKE_BINARY_DIR}/generated/emulatr_version.h" @ONLY)

# ----------------------------------------------------------------------------
# Compiler standard
# ----------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ----------------------------------------------------------------------------
# Qt (Core only by default; additional modules added with explicit rationale)
# ----------------------------------------------------------------------------
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# Qt is installed via the official Qt installer (not vcpkg).  Point
# CMake at the installer prefix so find_package(Qt6) resolves the
# config files at D:/Qt/.../msvc2022_64/lib/cmake/Qt6/Qt6Config.cmake.
# Override at configure time: cmake -DQT_INSTALL_ROOT=<path> ...
# Same pattern V1 uses (D:/EmulatR/EmulatRAppUni/CMakeLists.txt line 56).
# Windows seeds the official Qt-installer prefix; other hosts resolve Qt6
# from CMAKE_PREFIX_PATH (e.g. -DCMAKE_PREFIX_PATH=$(brew --prefix qt) or an
# aqtinstall prefix) or a -DQT_INSTALL_ROOT=... override.
if(NOT DEFINED QT_INSTALL_ROOT AND WIN32)
    set(QT_INSTALL_ROOT "D:/Qt/6.10.2/msvc2022_64"
        CACHE STRING "Qt installer prefix path")
endif()
if(DEFINED QT_INSTALL_ROOT)
    list(APPEND CMAKE_PREFIX_PATH "${QT_INSTALL_ROOT}")
endif()

find_package(QT NAMES Qt6 REQUIRED COMPONENTS Core Network Concurrent)
find_package(Qt6 REQUIRED COMPONENTS Core Network Concurrent)


# ----------------------------------------------------------------------------
# spdlog + fmt -- header-only mode (matches V1/V3 known-good wiring)
# ----------------------------------------------------------------------------
# Levels in use:
#   SPDLOG_LEVEL_TRACE     (compiled in only for Debug builds)
#   SPDLOG_LEVEL_DEBUG     (compiled in only for Debug builds)
#   SPDLOG_LEVEL_INFO      (always compiled)
#   SPDLOG_LEVEL_WARN      (always compiled)
#   SPDLOG_LEVEL_ERR       (always compiled)
#   SPDLOG_LEVEL_CRITICAL  (always compiled)
# SPDLOG_ACTIVE_LEVEL elides calls below the active level at the
# preprocessor; debug/trace are zero cost in Release builds.
#
# Header-only resolution:
#   spdlog and fmt are pulled in by include path from vcpkg's package
#   tree.  No find_package() and no link target are needed because the
#   SPDLOG_HEADER_ONLY + FMT_HEADER_ONLY compile definitions (set on
#   each target below) make every spdlog/fmt symbol inline.
#
#   This matches V3's known-good wiring.  Moving to the precompiled
#   library form would require (a) a completed `vcpkg install
#   spdlog:x64-windows` populating C:/vcpkg/installed/x64-windows/share/
#   and (b) reverting this block to find_package(spdlog REQUIRED) plus
#   spdlog::spdlog in target_link_libraries.
if(WIN32)
    include_directories(
        "C:/vcpkg/packages/spdlog_x64-windows/include"
        "C:/vcpkg/packages/fmt_x64-windows/include"
    )
else()
    # Non-Windows: locate the header-only spdlog/fmt via the platform
    # package manager (Homebrew: `brew install spdlog fmt`).  Only the
    # include path is needed -- SPDLOG_HEADER_ONLY + FMT_HEADER_ONLY (set
    # per-target below) keep every symbol inline, so no link target.
    find_path(SPDLOG_INCLUDE_DIR spdlog/spdlog.h)
    find_path(FMT_INCLUDE_DIR fmt/format.h)
    if(SPDLOG_INCLUDE_DIR)
        include_directories("${SPDLOG_INCLUDE_DIR}")
    endif()
    if(FMT_INCLUDE_DIR)
        include_directories("${FMT_INCLUDE_DIR}")
    endif()
endif()

# ----------------------------------------------------------------------------
# Project source files
# ----------------------------------------------------------------------------
# Source files land in the per-lib subdirectories (coreLib/, pipelineLib/,
# eBoxLib/, etc.) and are listed here as they are written.  For now, the
# project is documentation + scaffold only -- main.cpp is the entry point
# stub from Qt Creator.  As library code lands, append entries to
# EMULATR_SOURCES.
set(EMULATR_SOURCES
    main.cpp
    # config / settings: Emulatr.ini -> EmulatorSettings (wired 2026-06-08;
    # previously orphan TUs, never compiled).  IniLoader feeds the console port.
    config/EmulatorSettings.cpp
    config/IniLoader.cpp
    coreLib/alpha_int_byteops.h
    coreLib/alpha_int_helpers.h
    coreLib/axp_attributes_core.h
    coreLib/bitutils.h
    coreLib/BoxResult.h
    coreLib/CBoxState.h
    coreLib/CpuState.h
    coreLib/DispatchEntry.h
    coreLib/ExecCtx.h
    coreLib/HW_IPR.h
    coreLib/InstructionGrain.h
    coreLib/IprFields.h
    coreLib/LogSubsystem.h
    coreLib/LogSubsystem.cpp
    coreLib/PalShadow.h
    coreLib/OpcDec.cpp
    coreLib/PipelineSlot.h
    eBoxLib/grains/IntArith.cpp
    mBoxLib/grains/LoadStore.cpp
    cBoxLib/grains/CacheOps.cpp
    iBoxLib/grains/ControlFlow.cpp
    palBoxLib/grains/PalEntries.cpp
    fBoxLib/grains/Float.cpp
    fBoxLib/grains/FloatVariants.cpp
    deviceLib/global_firmwaredevicemanager.cpp
    memoryLib/GuestMemory.h
    memoryLib/GuestMemory.cpp
    pipelineLib/IFetchOverride.h
    pipelineLib/MemDrainer.h
    pipelineLib/PipelineDriver.h
    grainFactoryLib/DispatchAccess.h
    grainFactoryLib/generated/DispatchTables.cpp
    grainFactoryLib/generated/GrainStubs.cpp
    systemLib/StopReason.h
    systemLib/AppOptions.h
    systemLib/AppOptions.cpp
    systemLib/FirmwareLoader.h
    systemLib/FirmwareLoader.cpp
    systemLib/SrmLoader.h
    systemLib/SrmLoader.cpp
    systemLib/CpuStateDump.h
    systemLib/CpuStateDump.cpp
    systemLib/Machine.h
    systemLib/Machine.cpp
    systemLib/Snapshot.h
    systemLib/Snapshot.cpp
    systemLib/PlatformConfig.h
    systemLib/PlatformConfig.cpp
    schedLib/SmpHarness.h
    schedLib/SmpDrivers.h
    schedLib/AlphaCpuAgent.h
    schedLib/AlphaCpuAgent.cpp
    mmuLib/UnalignedEventLog.h
    mmuLib/UnalignedEventLog.cpp
    mmuLib/CboxEventLog.h
    mmuLib/CboxEventLog.cpp
    coreLib/FaultEventLog.h
    coreLib/FaultEventLog.cpp
    coreLib/LogArtifactPath.h
    coreLib/LogArtifactPath.cpp
    pteLib/AlphaPte.h
    pteLib/Ev6PteFormat.h
    pteLib/TlbEntry.h
    pteLib/TlbEpoch.h
    pteLib/SPAMBucket.h
    pteLib/SPAMBucket.cpp
    pteLib/SPAMShardManager.h
    pteLib/SPAMShardManager.cpp
    traceLib/CommitRecord.h
    traceLib/TraceSink.h
    traceLib/Disassembler.h
    traceLib/Disassembler.cpp
    traceLib/DecListingSink.h
    traceLib/DecListingSink.cpp
    traceLib/BreakpointSink.h
    traceLib/BreakpointSink.cpp
    traceLib/PaDump.h
    traceLib/PaDump.cpp
    # ...
)

# Documentation, spec, and reference files (visible in IDE, not compiled).
set(EMULATR_DOCS
    SPEC_execution_model.md
    SPEC_memory_realms_and_scaffold.md
    LICENSE.md
    .gitignore
    CMakeLists.txt
    grainFactoryLib/GrainMaster.tsv
    grainFactoryLib/GrainMasterV4.tsv
    grainFactoryLib/SemanticFlags.tsv
    docs/notes/ADR-0001-source-file-headers.md
    docs/notes/templates/header_cpp.txt
    cBoxLib/README.md
    coreLib/README.md
    cpuLib/README.md
    docs/README.md
    docs/generated/README.md
    docs/notes/README.md
    eBoxLib/README.md
    fBoxLib/README.md
    grainFactoryLib/codegen/README.md
    grainFactoryLib/codegen/genGrains.py
    grainFactoryLib/codegen/expandFpVariants.py
    grainFactoryLib/codegen/handwritten.tsv
    grainFactoryLib/generated/README.md
    iprLib/README.md
    journals/README.md
    mBoxLib/README.md
    memoryLib/README.md
    palBoxLib/README.md
    pipelineLib/README.md
    tests/README.md
    traceLib/README.md
)

# Tracking target so docs/spec files appear in the IDE without being
# compiled.  Not built; pure organization.
add_custom_target(emulatr_docs SOURCES ${EMULATR_DOCS})


# ----------------------------------------------------------------------------
# Main executable target
# ----------------------------------------------------------------------------
add_executable(Emulatr ${EMULATR_SOURCES}
    grainFactoryLib/GrainMasterV4.tsv grainFactoryLib/SemanticFlags.tsv
    grainFactoryLib/generated/DispatchKinds.h grainFactoryLib/generated/GrainsForward.h grainFactoryLib/generated/README.md grainFactoryLib/generated/SemanticFlagsEnum.h
    grainFactoryLib/codegen/genGrains.py grainFactoryLib/codegen/README.md
    coreLib/ProcessorVariant.h
    coreLib/alpha_int_byteops.h coreLib/alpha_int_helpers.h coreLib/axp_attributes_core.h coreLib/bitutils.h coreLib/BoxResult.h coreLib/CBoxState.h coreLib/CpuState.h coreLib/DispatchEntry.h coreLib/ExecCtx.h coreLib/HW_IPR.h coreLib/InstructionGrain.h coreLib/IprBank.h coreLib/OpcDec.cpp coreLib/PipelineSlot.h coreLib/ProcessorVariant.h coreLib/README.md coreLib/VA_types.h
    chipsetLib/chipset_adapter.h chipsetLib/IDeviceHandlers.h chipsetLib/Cypress_CY82C693ISABridge.h chipsetLib/AliM1543C.h
    chipsetLib/Tsunami21272_RegisterMap.h chipsetLib/Tsunami21272_CsrSpec.h chipsetLib/CsrDiag.h chipsetLib/CsrDiag.cpp 
    chipsetLib/TsunamiCchip.h 
    chipsetLib/Pic8259Pair.h
    chipsetLib/TsunamiChipset.cpp chipsetLib/TsunamiChipset.h chipsetLib/FlashRom.cpp chipsetLib/FlashRom.h chipsetLib/TsunamiDchip.h chipsetLib/TsunamiPchip.h chipsetLib/TsunamiVariant.h chipsetLib/Titan21274_CsrSpec.h chipsetLib/TitanPchip.h chipsetLib/TitanChipset.h deviceLib/Tsunami/IsaBridge.h deviceLib/Tsunami/Uart16550.h deviceLib/scsi/ScsiTypes.h deviceLib/scsi/ScsiSenseData.h deviceLib/scsi/ScsiCommand.h deviceLib/scsi/VirtualScsiDevice.h deviceLib/scsi/VirtualIsoDevice.h deviceLib/Tsunami/ITsunamiIde.h deviceLib/Tsunami/AtaTaskfileEngine.h deviceLib/Tsunami/PciConfigSpace.h deviceLib/Tsunami/AliM5229Ide.h deviceLib/Tsunami/Cy82C693Ide.h deviceLib/ConsoleManager.cpp deviceLib/ConsoleManager.h deviceLib/device_core_inl.h deviceLib/deviceLib_global.h deviceLib/global_ConsoleManager.cpp deviceLib/global_ConsoleManager.h deviceLib/global_SRMEnvStore.cpp deviceLib/global_SRMEnvStore.h deviceLib/IBootDevice.h deviceLib/IConsoleDevice.h deviceLib/OPA_Console_Config.h deviceLib/SRMConsole.cpp deviceLib/SRMConsole.h deviceLib/SRMConsoleDevice.cpp deviceLib/SRMConsoleDevice.h deviceLib/SRMEnvStore.cpp deviceLib/SRMEnvStore.h deviceLib/StdoutConsoleBackend.cpp deviceLib/StdoutConsoleBackend.h
    coreLib/LoggingMacros.h
    deviceLib/ControllerConfig.h)

target_include_directories(Emulatr PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_BINARY_DIR}/generated
)

target_link_libraries(Emulatr PRIVATE
    Qt6::Core
    Qt6::Network
)


target_compile_definitions(Emulatr PRIVATE
    SPDLOG_HEADER_ONLY
    FMT_HEADER_ONLY
    $<$<CONFIG:Debug>:SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE>
    $<$<CONFIG:Release>:SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_INFO>
    $<$<CONFIG:RelWithDebInfo>:SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_INFO>
    AXP_EXEC_TRACE
 
)
# Compiler definitions
# 2026-06-01: EMULATR_MEMDIAG disabled -- it emits an fprintf+fflush PER retired
# instruction (PipelineDriver.h:147 MEMDIAG-I, plus MEMDIAG-MTPR), which throttles
# throughput ~900x (to ~10K cyc/s) in dense PAL TB-fill regions. Was the DS10
# TB-fill debug scaffold; re-enable temporarily by uncommenting if that diagnostic
# is needed again.
# add_compile_definitions(EMULATR_MEMDIAG)

# ----------------------------------------------------------------------------
# EMULATR_CHIPSET_DIAG -- compile-time gate for chipset CSR access diagnostics.
# ----------------------------------------------------------------------------
# Wires the CSR_LOG_R / CSR_LOG_W macros in chipsetLib/CsrDiag.h so every
# chipset CSR access narrates itself (chip:reg, R/W, value, offset, cpu,
# cycle).  Off in Release -- zero-overhead in production builds.  Compiled IN for
# Debug and RelWithDebInfo, but DEFAULT SILENT at runtime (diagnostics-default-off
# rule, 2026-06-10): opt IN per run via env var EMULATR_CHIPSET_DIAG_ON=1 (no
# rebuild needed).  See chipsetLib/CsrDiag.h and journals/CchipPhaseA_Design_Notes.md.
option(EMULATR_CHIPSET_DIAG
       "Enable per-CSR-access diagnostic logging in the chipset"
       ON)

if(EMULATR_CHIPSET_DIAG)
    target_compile_definitions(Emulatr PRIVATE
        $<$<NOT:$<CONFIG:Release>>:EMULATR_CHIPSET_DIAG=1>
    )
endif()

# ----------------------------------------------------------------------------
# EMULATR_IRQDIAG -- one-shot interval-timer / HW_IER interrupt-delivery probe.
# ----------------------------------------------------------------------------
# TEMP diagnostic (2026-07-07) for the ES40 "lowering IPL" clock-interrupt halt
# (journals/20260707_es40_printf_deadlock_root.md sec.12).  Narrates two seams:
#   (1) palBoxLib execHwMtpr HW_IER / HW_IER_CM writes -- opB + resulting
#       cpu.ier, so we see whether the SRM ever sets ei[2] (bit 35).
#   (2) systemLib Machine interval-timer FIRE (b_irq<2> latch) + the
#       canAcceptInterrupt(22) DELIVER gate result (which condition is false).
# OFF by default; opt in per build with -DEMULATR_IRQDIAG=ON.  Delete this block
# and both #if EMULATR_IRQDIAG sites once the clock-interrupt path is proven.
option(EMULATR_IRQDIAG
       "Compile-in the interval-timer / HW_IER interrupt-delivery probe"
       ON)

if(EMULATR_IRQDIAG)
    target_compile_definitions(Emulatr PRIVATE EMULATR_IRQDIAG=1)
endif()

# ----------------------------------------------------------------------------
# EMULATR_USE_OS_PAGES -- platform page allocator for GuestMemory.
# ----------------------------------------------------------------------------
# Selects between OS-backed virtual memory (VirtualAlloc on Windows,
# mmap MAP_ANONYMOUS on Posix) and a calloc fallback.  OS pages are
# preferred -- they deliver demand-paged commits that keep the
# resident set proportional to touched memory rather than configured
# capacity.  calloc fallback exists for any host where the OS
# allocator is not desired.  See memoryLib/GuestMemory.cpp for the
# allocator switch and journals/MemoryV2_Integration_Notes.md for
# the rationale.
option(EMULATR_USE_OS_PAGES
       "Use mmap/VirtualAlloc for GuestMemory page allocation"
       ON)

if(EMULATR_USE_OS_PAGES)
    target_compile_definitions(Emulatr PRIVATE EMULATR_USE_OS_PAGES=1)
endif()

# ----------------------------------------------------------------------------
# EMULATR_DEBUG_STEP -- disable optimization on PipelineDriver::step.
# ----------------------------------------------------------------------------
# Leftover debug aid: when ON, step() compiles -O0 (#pragma optimize off) so the
# optimizer doesn't elide locals during single-step debugging.  DEFAULT OFF so
# the 82%-of-runtime hot loop ships at the build's optimization level (PERF #0,
# 2026-06-10 -- it was previously ALWAYS -O0).  Turn ON only to debug step().
option(EMULATR_DEBUG_STEP
       "Compile PipelineDriver::step at -O0 for single-step debugging (SLOW)"
       OFF)

if(EMULATR_DEBUG_STEP)
    target_compile_definitions(Emulatr PRIVATE EMULATR_DEBUG_STEP=1)
endif()

# ----------------------------------------------------------------------------
# EMULATR_TRACE_HOOKS -- compile-gate the per-commit trace-sink callbacks.
# ----------------------------------------------------------------------------
# PipelineDriver::step()'s per-retire trace block (sink->onCommit / onPalEntry /
# onPalExit, feeding BreakpointSink/DecListingSink + RetireProfiler checkpoints)
# runs EVERY step.  A runtime `if (sink)` still pays the branch+call on the
# 82%-of-runtime hot loop even when no trace is wanted (PERF #0/#15 lesson:
# hot-path diagnostics need a COMPILE guard, not just a runtime flag).  DEFAULT
# OFF per the diagnostics-default-off rule (2026-06-10): normal/perf runs ship
# the trace calls compiled OUT; build with -DEMULATR_TRACE_HOOKS=ON to capture a
# BreakpointSink / retire-compact trace (then --trace / the sink select what
# emits).  Not mirrored to the test target: no doctest drives step() with a live
# sink (they exercise the sinks directly), so the default-off build stays green.
option(EMULATR_TRACE_HOOKS
       "Compile in PipelineDriver per-commit trace-sink hooks (for trace capture)"
       OFF)

if(EMULATR_TRACE_HOOKS)
    target_compile_definitions(Emulatr PRIVATE EMULATR_TRACE_HOOKS=1)
endif()

# ----------------------------------------------------------------------------
# EMULATR_FP_SOFTFLOAT -- vendored Berkeley SoftFloat 3e as the FP oracle.
# ----------------------------------------------------------------------------
# Builds berkeley-softfloat-3-master/ (FAST_INT64 + 8086-SSE) as the static
# `softfloat` library and links it into EmulatR.  SoftFloat is the deterministic,
# bit-identical-across-hosts IEEE reference backend behind the IFpBackend seam
# (task #26; design journals/fp_backend_and_x87_guard_handoff.md).  Default ON --
# guest FP correctness depends on it; build -DEMULATR_FP_SOFTFLOAT=OFF only to
# compile EmulatR without the FP backend wired.
option(EMULATR_FP_SOFTFLOAT
       "Build + link vendored Berkeley SoftFloat as the FP reference oracle"
       ON)

if(EMULATR_FP_SOFTFLOAT)
    add_subdirectory(berkeley-softfloat-3-master)
    add_subdirectory(fpBoxLib)
    target_link_libraries(Emulatr PRIVATE softfloat fpBox)
    target_compile_definitions(Emulatr PRIVATE EMULATR_FP_SOFTFLOAT=1)
endif()

# ----------------------------------------------------------------------------
# EMULATR_DIAGNOSTIC_LOGGING -- centralized per-subsystem diagnostic logging.
# ----------------------------------------------------------------------------
# Compile-time gate for the coreLib/LogSubsystem.h facility.  Each diagnostic
# stream (Cbox, Unalign, IntervalTimer, Snapshot, PalRelocation, ChipsetCsr,
# StepD, Misc) becomes individually toggleable at runtime via CLI flags
# (--log-disable, --log-only, --log-verbose, --log-file).  When the macro
# expands to ((void)0) in Release builds, the call site evaluates none of
# its arguments and the compiler discards the call entirely.  See
# coreLib/LogSubsystem.h for the API and journals/LogSubsystem_Design_Notes.md
# for the design rationale.
option(EMULATR_DIAGNOSTIC_LOGGING
       "Compile-in centralized diagnostic log facility"
       ON)

if(EMULATR_DIAGNOSTIC_LOGGING)
    target_compile_definitions(Emulatr PRIVATE
        $<$<NOT:$<CONFIG:Release>>:EMULATR_DIAGNOSTIC_LOGGING=1>
    )
endif()

# ----------------------------------------------------------------------------
# EMULATR_PA_DUMP -- physical-address disassembly dump facility.
# ----------------------------------------------------------------------------
# Compile-time gate for the coreLib/PaDump.h facility.  Provides DUMP_PA
# and DUMP_DISASM macros plus the AppOptions --dump-disasm flag.  Used
# during bring-up to inspect firmware-decompressed PAL bytes without
# capturing a full retire trace.  Off in Release; on by default elsewhere.
option(EMULATR_PA_DUMP
       "Compile-in physical-address disassembly dump facility"
       ON)

if(EMULATR_PA_DUMP)
    target_compile_definitions(Emulatr PRIVATE
        $<$<NOT:$<CONFIG:Release>>:EMULATR_PA_DUMP=1>
    )
endif()

# ----------------------------------------------------------------------------
# EMULATR_BRINGUP_PROBES -- compile gate for ad-hoc bring-up scaffolding.
# ----------------------------------------------------------------------------
# Over bring-up, many one-off probes were left in the run path emitting raw
# fprintf(stderr) on EVERY run: per-step CPU samplers (PCSAMPLE/PCWIN/
# ITBMISS-PROBE), memory watchpoints (*-LOAD/STORE-WATCH, spurious-commit
# ASSERTs), chipset/UART breakpoint traces (UARTBP#n, HALTPROBE), the Pchip
# UNHANDLED OUTER READ/WRITE narration, the interrupt/timer divert + injection
# logs, the CSERVE/REI/DIVERT-REI PAL probes, and the end-of-run PROFILE /
# RetireProfiler histogram dumps.  None are needed for a normal run and several
# sit on the 82%-of-runtime hot loop.  DEFAULT OFF per the diagnostics-default-
# off rule (2026-06-15): a normal build emits only initialization messaging,
# snapshot-save notices, and genuine error messages.  Build
# -DEMULATR_BRINGUP_PROBES=ON to bring the scaffolding back for debugging.
# Compile gate (not a runtime flag) so the hot-path probes cost zero when off.
option(EMULATR_BRINGUP_PROBES
       "Compile in ad-hoc bring-up probe/trace prints (noisy; for debugging)"
       OFF)

if(EMULATR_BRINGUP_PROBES)
    target_compile_definitions(Emulatr PRIVATE EMULATR_BRINGUP_PROBES=1)
endif()

# ----------------------------------------------------------------------------
# Debug stepping flags (MSVC)
# ----------------------------------------------------------------------------
# Stepping through V4 under the Visual Studio / VS Code debugger needs
# two things turned on that are not default in CMake's Debug preset:
#
#   /Ob0  -- disable inline expansion.  Overrides AXP_ALWAYS_INLINE
#            (which expands to `inline __forceinline` on MSVC) so the
#            small helpers in leaves and ExecCtx builders show up as
#            real call frames in the debugger instead of being elided.
#
#   /JMC  -- Just My Code.  Lets F10/F11 step over stdlib internals
#            (basic_string operator+, ofstream::write, vector iter)
#            without dropping into <ostream> and friends.
#
#   /Oy-  -- keep frame pointers.  Reliable stack walking when the
#            debugger has to unwind from a leaf into PipelineDriver.
#
# Optimization stays at /Od (CMake default for Debug).
#
# RelWithDebInfo posture
# ----------------------
# Pure Debug is too slow to reach the multi-hundred-million-cycle boot
# points where the BreakpointSink / sys__cbox investigation lives -- a
# 5-hour Debug boot becomes a sub-minute RelWithDebInfo boot.  CMake's
# stock RelWithDebInfo on MSVC compiles with /MD /Zi /O2 /Ob1 /DNDEBUG
# and links with /DEBUG /INCREMENTAL:NO /OPT:REF /OPT:ICF, which gives
# us full debug symbols in a .pdb alongside an optimised .exe.  We add
# /Oy- (frame pointers) on top so the debugger can unwind the pipeline
# reliably -- the same rationale as the Debug build.  We do NOT add
# /Ob0 or /JMC here: keeping inline expansion preserves the
# optimisation win, and JMC is debug-only by design.
if(MSVC)
    target_compile_options(Emulatr PRIVATE
        $<$<CONFIG:Debug>:/Ob0>
        $<$<CONFIG:Debug>:/JMC>
        $<$<CONFIG:Debug>:/Oy->
        $<$<CONFIG:RelWithDebInfo>:/Oy->
    )
endif()

# ----------------------------------------------------------------------------
# Debugger run defaults (Visual Studio + VS Code)
# ----------------------------------------------------------------------------
# Pre-fill the command-line args + working directory the debugger uses
# when the user hits F5.  Override on the command line with
# `cmake -DEMULATR_DEBUG_ARGS=... -DEMULATR_DEBUG_WORKDIR=...` if the
# defaults need to change for a particular session.
#
# Default args mirror Tim's standing trace cycle: load the bundled
# ES45 v7.3 firmware (copied next to the executable by the POST_BUILD
# step below), enable the dual-channel trace, cap the run at 16 cycles
# for a quick smoke before bumping cycles to drive the decompressor.
# Working directory is the executable's output dir so relative paths
# resolve to the per-build runtime layout (firmware/, trace/, etc.).
set(EMULATR_DEBUG_ARGS
    "--firmware firmware/es45_v7_3.exe --start-pa 0x1 --trace dec.log,mach.log --max-cycles 16"
    CACHE STRING "Default command-line arguments when F5-running Emulatr in the debugger")

set_target_properties(Emulatr PROPERTIES
    VS_DEBUGGER_COMMAND_ARGUMENTS "${EMULATR_DEBUG_ARGS}"
    VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:Emulatr>"
)

# ----------------------------------------------------------------------------
# Test executable (doctest-based)
# ----------------------------------------------------------------------------
# Separate target for fast incremental test runs.  Links against the same
# project source files as the main target (minus main.cpp).  Per-test
# files are added to EMULATR_TEST_SOURCES as they are written.
set(EMULATR_TEST_SOURCES
    tests/main.cpp
    tests/grainFactoryLib/test_semanticflags.cpp
    tests/eBoxLib/test_intarith.cpp
    tests/mBoxLib/test_loadstore.cpp
    tests/iBoxLib/test_controlflow.cpp
    tests/cBoxLib/test_cacheops.cpp
    tests/palBoxLib/test_palentries.cpp
    tests/coreLib/test_iprfields.cpp
    tests/coreLib/test_byteops.cpp
    tests/fBoxLib/test_float.cpp
    tests/memoryLib/test_guestmemory.cpp
    tests/pipelineLib/test_memdrainer.cpp
    tests/pipelineLib/test_pipelinedriver.cpp
    tests/chipsetLib/test_mmio_csc_roundtrip.cpp
    tests/chipsetLib/test_csr_spec.cpp
    tests/chipsetLib/test_fixtures_sanity.cpp
    tests/chipsetLib/test_ticket01_dispatch.cpp
    tests/chipsetLib/test_ticket01_5_variant_binding.cpp
    tests/chipsetLib/test_ticket02_aar_encoding.cpp
    tests/chipsetLib/test_ticket03_interrupts.cpp
    tests/chipsetLib/test_ticket04_timer.cpp
    tests/chipsetLib/test_ticket05_pci_enum.cpp
    tests/chipsetLib/test_ticket06_isa_uart.cpp
    # 2026-06-08: S4 CY82C693 IDE func1 chipset wiring / routing
    tests/chipsetLib/test_ide_wiring.cpp
    tests/chipsetLib/test_flashrom.cpp
    # 2026-06-21: TIG per-CPU CPU-START latch (secondary-CPU bring-up)
    tests/chipsetLib/test_tsunami_tig.cpp
    # 2026-07-24: S1 seam (JRN-SCSI-002 G-B/G-A) -- BAR rebind + bulk DMA
    tests/chipsetLib/test_pci_bar_rebind.cpp
    # 2026-07-24: JRN-SCSI-001 P2 -- NCR 53C810 SCRIPTS engine + disk target
    tests/deviceLib/test_ncr53c810.cpp
    tests/systemLib/test_firmwareloader.cpp
    tests/systemLib/test_srmloader.cpp
    tests/systemLib/test_machine.cpp
    tests/systemLib/test_snapshot_roundtrip.cpp
    tests/systemLib/test_platform_config.cpp
    # 2026-05-30: first Qt-using doctest -- OPA0 / SRMConsoleDevice end-to-end
    tests/deviceLib/test_srm_console_device.cpp
    # 2026-06-03: MC146818 TOY clock + CMOS (krn$_reset_toy / fclose-halt work)
    tests/deviceLib/test_toyrtc.cpp
    # 2026-06-03: PCF8584 IIC controller stub (powerup iic_init stall work)
    tests/deviceLib/test_iicpcf8584.cpp
    # 2026-06-07: S1 scsiCoreLib port -- no-media ATAPI CD command layer
    tests/deviceLib/test_scsi_iso.cpp
    tests/deviceLib/test_version_badge.cpp
    # 2026-07-27: SPEC-SWPCTX-001 C2 -- HWPCB guest I/O + FEN-quad packing
    tests/deviceLib/test_hwpcb_context.cpp
    # 2026-07-27: SPEC-SWPCTX-001 C3 -- faithful VMS SWPCTX leaf (T1-T6)
    tests/palBoxLib/test_swpctx.cpp
    # 2026-06-12: IBlockMedia seam -- FileBlockMedia + MockBlockMedia (Phase A)
    tests/deviceLib/test_block_media.cpp
    # 2026-06-07: S2 CY82C693 IDE taskfile + ATA/ATAPI detection
    tests/deviceLib/test_cy82c693ide.cpp
    # 2026-06-11: 82077 floppy IRQ6 poll bit (0x536) -- dva0 fast-fail
    tests/deviceLib/test_floppy82077.cpp
    # 2026-06-09: Step 1 -- IoSeam value-out command/completion seam (C-VALUE)
    tests/deviceLib/test_io_seam.cpp
    tests/traceLib/test_disassembler.cpp
    tests/traceLib/test_declistingsink.cpp
    # 2026-06-05: boot profiler (retire-PC histogram)
    tests/traceLib/test_retireprofiler.cpp
    tests/pteLib/test_spam.cpp
    coreLib/OpcDec.cpp
    coreLib/LogSubsystem.cpp
    eBoxLib/grains/IntArith.cpp
    mBoxLib/grains/LoadStore.cpp
    iBoxLib/grains/ControlFlow.cpp
    cBoxLib/grains/CacheOps.cpp
    palBoxLib/grains/PalEntries.cpp
    fBoxLib/grains/Float.cpp
    fBoxLib/grains/FloatVariants.cpp
    chipsetLib/TsunamiChipset.cpp
    chipsetLib/FlashRom.cpp
    chipsetLib/CsrDiag.cpp
    systemLib/AppOptions.cpp
    systemLib/FirmwareLoader.cpp
    systemLib/SrmLoader.cpp
    systemLib/CpuStateDump.cpp
    systemLib/Machine.cpp
    # Machine.cpp now drives the SMP harness on the EMULATR_DISPATCH path, so the
    # AlphaCpuAgent vtable / out-of-line virtuals must be compiled into this
    # target to resolve at link (same reason StdoutConsoleBackend.cpp is here).
    schedLib/AlphaCpuAgent.cpp
    deviceLib/StdoutConsoleBackend.cpp
    # 2026-05-29: Machine now owns SRMConsoleDevice for its COM1 sink
    # (Qt-based TCP listen server with PuTTY auto-launch).  Tests don't
    # exercise the TCP path but Machine.cpp's vtable / ctor reference
    # SRMConsoleDevice's symbols, so the .cpp must be compiled into
    # this target to resolve them at link.
    deviceLib/SRMConsoleDevice.cpp
    systemLib/Snapshot.cpp
    systemLib/PlatformConfig.cpp
    mmuLib/UnalignedEventLog.cpp
    mmuLib/CboxEventLog.cpp
    coreLib/FaultEventLog.cpp
    coreLib/LogArtifactPath.cpp
    pteLib/SPAMBucket.cpp
    pteLib/SPAMShardManager.cpp
    memoryLib/GuestMemory.cpp
    traceLib/Disassembler.cpp
    traceLib/DecListingSink.cpp
    traceLib/BreakpointSink.cpp
    traceLib/PaDump.cpp
    grainFactoryLib/generated/DispatchTables.cpp
    grainFactoryLib/generated/GrainStubs.cpp
    # tests/coreLib/test_pipelineslot.cpp
    # tests/coreLib/test_instructiongrain.cpp
    # tests/coreLib/test_boxresult_merge.cpp
     # 2026-06-04: 16550 interrupt line Increment 1 (serial-console unblock)
    tests/deviceLib/test_uart16550_interrupts.cpp
    # 2026-06-04: Cypress 8259 pair + device-IRQ chain, Increment 2
    tests/chipsetLib/test_pic8259.cpp
    #2026-06-19 - Added SMP Determinism Harness - tests
    tests/schedLib/smp_harness_tests.cpp
    # ...
)

# The SoftFloat oracle-agreement + cycles/op bench test (#26) includes
# softfloat.h and links the vendored softfloat target, so it is only buildable
# when EMULATR_FP_SOFTFLOAT is ON. Its CHECKs assert host double == SoftFloat
# BIT-FOR-BIT, which only holds under strict FP -- pin it /fp:strict (MSVC) /
# -ffp-contract=off (GCC/Clang) so it cannot false-fail under a fast/contracting
# default. Seeds the EMULATR_FP_STRICT_SOURCES pinning Phase A formalizes.
if(EMULATR_FP_SOFTFLOAT)
    list(APPEND EMULATR_TEST_SOURCES
        tests/fBoxLib/fp_softfloat_vs_host_bench.cpp
        tests/fpBoxLib/test_fp_host_guard.cpp
        tests/fpBoxLib/test_fp_backend.cpp)
    if(MSVC)
        set_source_files_properties(tests/fBoxLib/fp_softfloat_vs_host_bench.cpp
            PROPERTIES COMPILE_OPTIONS "/fp:strict")
    else()
        set_source_files_properties(tests/fBoxLib/fp_softfloat_vs_host_bench.cpp
            PROPERTIES COMPILE_OPTIONS "-ffp-contract=off")
    endif()
endif()

add_executable(Emulatr_tests ${EMULATR_TEST_SOURCES})

# Tests use #include "doctest.h" with tests/ on the include path,
# which resolves to tests/doctest.h (the single-header doctest
# distribution lives at tests/doctest.h directly, no subdirectory).
target_include_directories(Emulatr_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/tests
    ${CMAKE_BINARY_DIR}/generated
)

target_link_libraries(Emulatr_tests PRIVATE
    Qt6::Core
    # 2026-05-29: SRMConsoleDevice (now linked in via Machine.cpp's COM1
    # backend) pulls in QTcpServer / QTcpSocket from QtNetwork.
    Qt6::Network
)

target_compile_definitions(Emulatr_tests PRIVATE
    SPDLOG_HEADER_ONLY
    FMT_HEADER_ONLY
    $<$<CONFIG:Debug>:SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE>
    $<$<CONFIG:Release>:SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_INFO>
    $<$<CONFIG:RelWithDebInfo>:SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_INFO>
)

# Mirror the EMULATR_CHIPSET_DIAG gate onto the test target so the
# Phase A doctest exercises see the same compile-time state as the
# main executable.  Same not-in-Release predicate.
if(EMULATR_CHIPSET_DIAG)
    target_compile_definitions(Emulatr_tests PRIVATE
        $<$<NOT:$<CONFIG:Release>>:EMULATR_CHIPSET_DIAG=1>
    )
endif()

# Mirror EMULATR_FP_SOFTFLOAT onto the test target so FP-backend doctests can
# link + call the SoftFloat oracle. The softfloat target is created by the
# add_subdirectory(berkeley-softfloat-3-master) in the main-target section above.
if(EMULATR_FP_SOFTFLOAT)
    target_link_libraries(Emulatr_tests PRIVATE softfloat fpBox)
    target_compile_definitions(Emulatr_tests PRIVATE EMULATR_FP_SOFTFLOAT=1)
endif()

# Mirror EMULATR_USE_OS_PAGES onto the test target so test_guestmemory
# and friends construct GuestMemory under the same allocator as the
# main exe.
if(EMULATR_USE_OS_PAGES)
    target_compile_definitions(Emulatr_tests PRIVATE EMULATR_USE_OS_PAGES=1)
endif()

# Mirror EMULATR_DIAGNOSTIC_LOGGING and EMULATR_PA_DUMP onto the test
# target so coreLib/LogSubsystem.cpp and coreLib/PaDump.cpp compile under
# the same gate state as the main executable.  Same Release-off predicate.
if(EMULATR_DIAGNOSTIC_LOGGING)
    target_compile_definitions(Emulatr_tests PRIVATE
        $<$<NOT:$<CONFIG:Release>>:EMULATR_DIAGNOSTIC_LOGGING=1>
    )
endif()
if(EMULATR_PA_DUMP)
    target_compile_definitions(Emulatr_tests PRIVATE
        $<$<NOT:$<CONFIG:Release>>:EMULATR_PA_DUMP=1>
    )
endif()

# Mirror the main target's debug stepping flags on the test executable
# so doctest cases can be stepped through with the same fidelity.
# RelWithDebInfo posture mirrored for the same reason -- so a test that
# reproduces a long-boot scenario can run in the same fast-but-steppable
# configuration the main binary uses for the sys__cbox investigation.
if(MSVC)
    target_compile_options(Emulatr_tests PRIVATE
        $<$<CONFIG:Debug>:/Ob0>
        $<$<CONFIG:Debug>:/JMC>
        $<$<CONFIG:Debug>:/Oy->
        $<$<CONFIG:Debug>:/EHsc>
        $<$<CONFIG:Release>:/EHsc>
        $<$<CONFIG:RelWithDebInfo>:/EHsc>
    )
endif()

# ----------------------------------------------------------------------------
# Per-build runtime layout
# ----------------------------------------------------------------------------
# After the main executable builds, create the runtime subdirectory
# layout and copy the firmware image(s) and configuration file(s) into
# the output directory so the binary can locate them at runtime.
#
# Subdirectories created next to the executable in the build output:
#   trace/      -- per-run cpu_trace.log files (rotated by timestamp)
#   logs/       -- spdlog output, separate from CPU trace
#   config/     -- runtime INI / configuration files
#   snapshots/  -- saved-state files (.axpsnap; post-v1 implementation)
#   firmware/   -- firmware blobs the emulator loads at runtime
set(EMULATR_RUNTIME_SUBDIRS
    trace
    logs
    config
    snapshots
    firmware
    tools
)

foreach(_subdir ${EMULATR_RUNTIME_SUBDIRS})
    add_custom_command(TARGET Emulatr POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_FILE_DIR:Emulatr>/${_subdir}"
        COMMENT "[Emulatr] mkdir runtime: ${_subdir}"
        VERBATIM
    )
endforeach()

# ----------------------------------------------------------------------------
# Firmware images consumed at runtime
# ----------------------------------------------------------------------------
# Only firmware images the project actually consumes are copied.  Add
# additional rows here as more images become consumed.
set(EMULATR_FIRMWARE_FILES
    "firmware/Es45/es45_v7_3.exe"
    # 2026-06-03: DS10 (webbrick) SRM -- the primary cold-boot target
    "firmware/ds10_v7_3.exe"
    # 2026-06-16: multi-platform SRM console images for the halt-button
    # generalization + firmware test matrix.  ES40/DS20 are Tsunami (runnable
    # now); DS25/ES45 are Titan (boot pending the TitanChipset path -- see
    # journals/20260616_titan_21274_interface.md).  2026-06-17: es40_v7_3.exe
    # copied into firmware/ and set as firmwareImage in Emulatr.ini; the
    # es40cv_v7_3.exe "cv" variant is also present (add a row to deploy it).
    "firmware/es40_v7_3.exe"
    "firmware/ds20_v7_3.exe"
    "firmware/ds25_v7_3.exe"
    "firmware/es45_v7_3.exe"
)

foreach(_fwfile ${EMULATR_FIRMWARE_FILES})
    get_filename_component(_fwname "${_fwfile}" NAME)
    add_custom_command(TARGET Emulatr POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/${_fwfile}"
                "$<TARGET_FILE_DIR:Emulatr>/firmware/${_fwname}"
        COMMENT "[Emulatr] copy firmware: ${_fwname}"
        VERBATIM
    )
endforeach()

# ----------------------------------------------------------------------------
# Configuration file(s) consumed at runtime
# ----------------------------------------------------------------------------
set(EMULATR_CONFIG_FILES
    "config/Emulatr.ini"
    # 2026-06-12: storage reference data consumed by tools (and, later, the
    # drive_model resolver in the C++ IDENTIFY path).
    "config/disk_types.json"
    "config/dec_disk_media_types.tsv"
)

foreach(_cfgfile ${EMULATR_CONFIG_FILES})
    get_filename_component(_cfgname "${_cfgfile}" NAME)
    add_custom_command(TARGET Emulatr POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/${_cfgfile}"
                "$<TARGET_FILE_DIR:Emulatr>/config/${_cfgname}"
        COMMENT "[Emulatr] copy config: ${_cfgname}"
        VERBATIM
    )
endforeach()

# ----------------------------------------------------------------------------
# Support tools deployed next to the executable (run dir == root).
# ----------------------------------------------------------------------------
# The run directory ($<TARGET_FILE_DIR:Emulatr>) is the runtime root: relative
# paths, [Storage] diskDir, the manifest, and any helper tool resolve from here.
# Tool/data files are copied next to the executable on EVERY build.  Previously
# this was a hand-maintained list, which meant a new helper script silently went
# undeployed (and had to be copied by hand) until someone remembered to add a
# row.  It is now GLOB-driven so new tools deploy automatically.
#
# run_fw.sh is SPECIAL: it must sit at the run-dir ROOT (beside Emulatr.exe)
# because it anchors to its own directory via BASH_SOURCE and launches
# ./Emulatr.exe.  Every other tool lands in run-dir/tools/ and locates config/
# as a sibling (../config from tools/).
add_custom_command(TARGET Emulatr POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/run_fw.sh"
            "$<TARGET_FILE_DIR:Emulatr>/run_fw.sh"
    COMMENT "[Emulatr] copy run helper: run_fw.sh (run-dir root)"
    VERBATIM
)

# Everything else under tools/ (top-level *.sh and *.py) -> run-dir/tools/.
# GLOB with CONFIGURE_DEPENDS so a NEWLY ADDED tool script is deployed on the
# next build WITHOUT hand-editing a list.  Deliberate, scoped exception to the
# house "no file(GLOB)" rule: that rule guards COMPILED SOURCES (CMake won't
# reconfigure when a new .cpp appears); these are runtime DATA files, and
# CONFIGURE_DEPENDS makes the generator re-glob at build time, so new scripts
# are caught.  Top-level glob only -- the tools/host_decompressor/ and
# tools/ghidra_scripts/ subtrees are NOT runtime tools and stay excluded.
file(GLOB EMULATR_TOOL_FILES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/*.sh"
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/*.py"
)
list(REMOVE_ITEM EMULATR_TOOL_FILES "${CMAKE_CURRENT_SOURCE_DIR}/tools/run_fw.sh")
foreach(_toolfile ${EMULATR_TOOL_FILES})
    get_filename_component(_toolname "${_toolfile}" NAME)
    add_custom_command(TARGET Emulatr POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${_toolfile}"
                "$<TARGET_FILE_DIR:Emulatr>/tools/${_toolname}"
        COMMENT "[Emulatr] copy tool: ${_toolname}"
        VERBATIM
    )
endforeach()

# ----------------------------------------------------------------------------
# Platform device manifests consumed at runtime
# ----------------------------------------------------------------------------
# Each manifest is named after its FIRMWARE-IMAGE STEM (<name>_v7_3_platform.json)
# and is HOST-AGNOSTIC: it describes the GUEST device tree (IIC + PCI devices,
# storage media), which is identical on every host -- so there is NO .win/.linux
# split.  At runtime Machine derives the leaf as stem(firmwareImage)+"_platform.json"
# (e.g. ds20_v7_3.exe -> ds20_v7_3_platform.json) and resolves it NEXT TO the
# executable; copy each one there.  A missing/invalid file is non-fatal (Machine
# falls back to the compiled-in default DS10 bus), but shipping them lets operators
# re-populate the emulated board with a JSON edit instead of a rebuild.
# EMULATR_PLATFORM_CONFIG overrides the path entirely.
set(EMULATR_PLATFORM_MANIFESTS
    "ds10_v7_3_platform.json"
    "ds20_v7_3_platform.json"
    "ds25_v7_3_platform.json"
    "es40_v7_3_platform.json"
    "es45_v7_3_platform.json"
)
foreach(_pm ${EMULATR_PLATFORM_MANIFESTS})
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${_pm}")
        add_custom_command(TARGET Emulatr POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${CMAKE_CURRENT_SOURCE_DIR}/${_pm}"
                    "$<TARGET_FILE_DIR:Emulatr>/${_pm}"
            COMMENT "[Emulatr] copy platform manifest: ${_pm}"
            VERBATIM
        )
    endif()
endforeach()

# ----------------------------------------------------------------------------
# Retire-trace output directory (diagnostics)
# ----------------------------------------------------------------------------
# DecListingSink writes the retire / breakpoint / profiler .trc files into the
# directory named by EMULATR_RETIRE_TRACE_DIR; if that directory does not exist
# the sink resolves an unreachable root and DISABLES itself.  Pre-create a
# "traces" subdir next to the executable so the default capture path is always
# reachable.  Run with: EMULATR_RETIRE_TRACE_DIR=<exe-dir>/traces (absolute).
add_custom_command(TARGET Emulatr POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:Emulatr>/traces"
    COMMENT "[Emulatr] create retire-trace dir: traces/"
    VERBATIM
)

# ----------------------------------------------------------------------------
# Auxiliary authoring tools -- PlatformEditor (manifest editor)  (2026-07-18,
# relocated 2026-07-25)
# ----------------------------------------------------------------------------
# SSOT for tool development is the SOURCE dir editors/PlatformEditor (moved
# from tools/Editors/PlatformEditor 2026-07-25: editors/ is a top-level
# sibling of tests/, matching the Emulatr_tests placement).  The
# run-directory copy is a staged, disposable mirror on the same footing as
# config/, firmware/, and the manifests -- never authored by hand.  The tool
# stages to <run-dir>/editors/ (2026-07-25, user direction: the run layout is
# <config>/editors, NOT <config>/tools/...), so the whole editor payload --
# platedit_qt[.exe], platedit_tui[.exe], schema/, catalog/, webui/ and the Qt
# runtime -- sits in ONE self-contained directory whose name mirrors the
# source-side top-level editors/ (sibling of tests/).
#
# (1) webui staging is UNCONDITIONAL.  The mockup is static data that must sit
#     under <run-dir>/editors/webui at run time, mirroring the
#     firmware/config/manifest staging above.  copy_directory creates the
#     intermediate editors/ level and the destination tree; it
#     re-copies every build (cheap for a small mockup -- cmake_minimum_required
#     is 3.16, so copy_directory_if_different (3.26+) is not available here).
#
# (2) The C++ editor builds WITH the emulator by default (2026-07-25, user
#     direction): the sub-project is included unconditionally and its binaries
#     land BESIDE Emulatr[.exe] in <config>/editors -- <root>/<Config>/editors
#     on the MSVC in-source build (which build_emulatr.sh mirrors into
#     out/build/<config>/editors) and out/build/<config>/editors on Ninja.  The
#     Qt Widgets frontend (platedit_qt) is itself gated inside the sub-project
#     on find_package(Qt6 Widgets QUIET), so a Qt-less environment still builds
#     the core + TUI.  Disable entirely with -DEMULATR_SKIP_EDITORS=ON.
#
# NOTE: copy_directory carries whatever the source webui/ dir holds, including
#       any macOS AppleDouble (._*) sidecars -- strip those from source before
#       a release so they do not ride into the run layout.
# ----------------------------------------------------------------------------
add_custom_command(TARGET Emulatr POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/editors/PlatformEditor/webui"
            "$<TARGET_FILE_DIR:Emulatr>/editors/webui"
    COMMENT "[Emulatr] stage PlatformEditor webui -> editors/webui"
    VERBATIM
)

# ----------------------------------------------------------------------------
# User documentation -- ship the rendered Help & Manual guide (PDF)
# ----------------------------------------------------------------------------
# Stage the rendered guide into <run-dir>/documentation/ so it ships with the
# build and the redist package. Source is the H&M export (sibling of the tree).
# Non-fatal if absent (a clean checkout without the H&M build present).
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../H&M/HMDocs/EmulatRGuide.pdf")
    add_custom_command(TARGET Emulatr POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_FILE_DIR:Emulatr>/documentation"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/../H&M/HMDocs/EmulatRGuide.pdf"
                "$<TARGET_FILE_DIR:Emulatr>/documentation/EmulatRGuide.pdf"
        COMMENT "[Emulatr] stage user guide -> documentation/EmulatRGuide.pdf"
        VERBATIM
    )
endif()

# PlatformEditor builds WITH the emulator (2026-07-25, user direction).
# UNCONDITIONAL include (not an option): the 2026-07-18 EMULATR_BUILD_EDITORS
# option left OFF in existing caches, silently skipping the sub-project on
# reconfigure -- an option default can never override a stale cache value.
# Opt out (e.g. a minimal CI lane) with -DEMULATR_SKIP_EDITORS=ON; that fresh
# name carries no cache history.
if(NOT EMULATR_SKIP_EDITORS)
    # PlatformEditor ships its own self-contained CMakeLists (Qt-free core +
    # doctest gates + TUI + Qt Widgets) at editors/PlatformEditor/CMakeLists.txt.
    # That sub-file is SSOT-side development and is edited THERE, not here.
    # The 2026-07-18 enable-gates are satisfied (2026-07-25): (a) MSVC guard on
    # warning flags, (b) test targets namespaced platedit_*, (c)
    # PLATEDIT_MANIFEST_DIR defaults to this source root (the manifest corpus).
    # enable_testing() at root scope so `ctest -R platedit` works from the
    # build root (the sub-project's add_test registrations are included).
    enable_testing()
    add_subdirectory(editors/PlatformEditor)

    # build_emulatr.sh drives `--target Emulatr`; hang the editor targets off
    # Emulatr so the standard build produces them without a script change.
    if(TARGET platedit_tui)
        add_dependencies(Emulatr platedit_tui)
    endif()
    if(TARGET platedit_qt)
        add_dependencies(Emulatr platedit_qt)
    endif()

    # NOTE(2026-07-25): the editor BINARIES need no staging copy.  The
    # sub-project points RUNTIME_OUTPUT_DIRECTORY at
    # $<TARGET_FILE_DIR:Emulatr>/editors, so platedit_qt / platedit_tui LINK
    # straight into the run layout (<config>/editors) and the sub-project's own
    # POST_BUILD steps drop schema/, catalog/ and the Qt DLLs beside them.
    # Supersedes TODO(platedit-stage) (2026-07-18), which assumed a copy out of
    # a separate editor build dir.
endif()


# ----------------------------------------------------------------------------
# windeployqt -- copy Qt runtime DLLs alongside the executable
# ----------------------------------------------------------------------------
# After Emulatr.exe builds, run windeployqt to populate the output
# directory with Qt6Core.dll and friends so the binary launches
# without a manual DLL copy step.  Without this, the .exe immediately
# fails to start because Qt6Core.dll is not on PATH.
#
# Default path points at the Qt 6.8.0 installer prefix per Tim's
# environment.  Override at configure time with
#   cmake -DWINDEPLOYQT_EXECUTABLE=<path>
# if Qt lives elsewhere.  If the path does not resolve, the build
# still succeeds but a warning surfaces and the operator must copy
# DLLs manually (or use a different deploy strategy).
if(WIN32)
    if(NOT DEFINED WINDEPLOYQT_EXECUTABLE)
        set(WINDEPLOYQT_EXECUTABLE
            "C:/Qt/6.8.0/msvc2022_64/bin/windeployqt.exe"
            CACHE FILEPATH "Path to windeployqt.exe")
    endif()

    if(EXISTS "${WINDEPLOYQT_EXECUTABLE}")
        add_custom_command(TARGET Emulatr POST_BUILD
            COMMAND "${WINDEPLOYQT_EXECUTABLE}"
                    --verbose 1
                    --no-translations
                    --no-system-d3d-compiler
                    --no-opengl-sw
                    --no-compiler-runtime
                    "$<TARGET_FILE:Emulatr>"
            COMMENT "[Emulatr] windeployqt: copying Qt runtime DLLs"
            VERBATIM
        )
        message(STATUS "Qt deployment configured: ${WINDEPLOYQT_EXECUTABLE}")

        # NOTE(2026-07-25): the matching windeployqt step for the PlatformEditor
        # GUI does NOT live here.  add_custom_command(TARGET <t> POST_BUILD ...)
        # is only legal in the directory that created <t>, and platedit_qt is
        # created by editors/PlatformEditor/CMakeLists.txt -- attaching it from
        # this scope is a hard configure error ("TARGET 'platedit_qt' was not
        # created in this directory").  That sub-project owns its own deploy
        # step (it resolves windeployqt from the Qt6 package it already found,
        # honouring WINDEPLOYQT_EXECUTABLE when the operator overrides it), which
        # also keeps the editor deployable when built standalone.
    else()
        message(WARNING
            "windeployqt not found at '${WINDEPLOYQT_EXECUTABLE}' -- "
            "Qt DLLs will need manual deployment.  Override with "
            "-DWINDEPLOYQT_EXECUTABLE=<path>.")
    endif()
endif()


# ----------------------------------------------------------------------------
# Install (kept from initial scaffold)
# ----------------------------------------------------------------------------
include(GNUInstallDirs)
install(TARGETS Emulatr
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# ============================================================================
# Packaging -- staged runtime layout + Qt deploy + CPack  (2026-06-20)
# ============================================================================
# Three layers, in order:
#   (1) install() rules stage a runnable layout (exe + default config + license)
#   (2) a Qt deploy step drops the Qt runtime (Qt6Core/Network/Concurrent + deps)
#       into that layout so the package runs on a box without Qt installed
#   (3) CPack wraps the staged tree into a portable archive and a native installer
#
# Cross-platform by construction: Qt 6.10's qt_generate_deploy_app_script is the
# single deploy entry point on Windows, Mint, and ARM Linux.  The dev-time
# windeployqt POST_BUILD above is a separate convenience (populates the BUILD dir
# so the freshly-built exe runs in place); it is Windows-only and does not feed
# the package -- the deploy SCRIPT below is what the package consumes.

# ---- (1) staged runtime layout --------------------------------------------
# Ship the curated DS10 deploy default (NOT the in-tree ES40 dev ini), renamed
# to the name the app searches for next to the executable.
install(FILES   "${CMAKE_SOURCE_DIR}/config/Emulatr.ini.default"
        DESTINATION "${CMAKE_INSTALL_BINDIR}"
        RENAME Emulatr.ini)

# License at the package root (text + html form).
install(FILES   "${CMAKE_SOURCE_DIR}/LICENSE.md"        DESTINATION .)
install(FILES   "${CMAKE_SOURCE_DIR}/docs/license.html" DESTINATION .)

# ---- (2) Qt deploy (cross-platform; Qt 6.3+ CMake-native API) --------------
# qt_generate_deploy_app_script emits a per-config script that, at install/cpack
# time, copies the Qt runtime the target actually links (Core/Network/Concurrent
# here -- no GUI plugins, since this is a QCoreApplication) next to the exe.
if(APPLE)
    # On Apple platforms qt_generate_deploy_app_script requires the target to
    # be an .app bundle (Qt6CoreMacros).  EmulatR is a plain QCoreApplication
    # executable and the macOS host is a dev/build environment, not a package
    # producer -- skip deploy-script generation here (Windows/Linux unchanged).
    message(STATUS
        "Skipping Qt deploy-script generation on macOS (non-bundle executable).")
elseif(COMMAND qt_generate_deploy_app_script)
    qt_generate_deploy_app_script(
        TARGET Emulatr
        OUTPUT_SCRIPT deploy_script
        NO_TRANSLATIONS)
    install(SCRIPT ${deploy_script})
else()
    message(WARNING
        "qt_generate_deploy_app_script not available (Qt < 6.3?) -- packaged "
        "Qt runtime will not be staged; the dev-time windeployqt step covers "
        "Windows build dirs only.")
endif()

# ---- (3) CPack -------------------------------------------------------------
set(CPACK_PACKAGE_NAME               "EmulatR")
set(CPACK_PACKAGE_VENDOR             "eNVy Systems, Inc.")
set(CPACK_PACKAGE_VERSION            "${PROJECT_VERSION}")
set(CPACK_PACKAGE_INSTALL_DIRECTORY  "EmulatR")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "EmulatR V4 -- Alpha AXP / EV6 architecture emulator")
set(CPACK_RESOURCE_FILE_LICENSE      "${CMAKE_SOURCE_DIR}/LICENSE.md")
# (exe name ; Start-Menu label) -- exe name is the lowercase target.
set(CPACK_PACKAGE_EXECUTABLES        "Emulatr" "EmulatR V4")

if(WIN32)
    set(CPACK_GENERATOR "ZIP;NSIS")
    set(CPACK_NSIS_DISPLAY_NAME "EmulatR V4")
    set(CPACK_NSIS_PACKAGE_NAME "EmulatR V4")
    set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
    set(CPACK_CREATE_DESKTOP_LINKS "Emulatr")
elseif(UNIX)
    set(CPACK_GENERATOR "TGZ;DEB")
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "eNVy Systems, Inc. <peert@envysys.com>")
    set(CPACK_DEBIAN_PACKAGE_SECTION "misc")
    # Qt runtime is bundled by the deploy script, so do NOT auto-depend on
    # system Qt via shlibdeps (keeps the .deb self-contained on Mint/ARM).
endif()

include(CPack)