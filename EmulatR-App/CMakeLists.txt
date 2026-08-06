# ============================================================================
# CMakeLists.txt -- EmulatrLaunch top-level build
# ============================================================================
# Project: EmulatR -- EmulatrLaunch (SPEC-LAUNCH-001 Rev D.2)
# Copyright (C) 2026 eNVy Systems, Inc.  All rights reserved.
# Licensed under eNVy Systems Non-Commercial License v1.1
#
# Project Architect: Timothy Peer
# AI Collaboration:  Claude (Anthropic)
#
# Version: 1.0.0-alpha   Date: 2026-07-29
# Spec:    Section 3 (directory root and layout), G5 (skeleton gate)
#
# Qt6 Widgets, C++20, MSVC.  The emulator core is NEVER linked here -- this is
# a PERIPHERAL application (Section 2, Qt-free-core mandate).  The only
# coupling to Emulatr.exe is the public invocation contract: command line, cwd,
# Emulatr.ini, the stop sentinel, and the exit code.
# ============================================================================

cmake_minimum_required(VERSION 3.21)

project(EmulatrLaunch
        VERSION     1.0.0
        DESCRIPTION "EmulatR Launcher -- named-system front door for Emulatr.exe"
        LANGUAGES   CXX)

set(CMAKE_CXX_STANDARD          20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS        OFF)

if (MSVC)
    add_compile_options(/W4 /permissive- /utf-8 /EHsc)
    add_compile_definitions(UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets)
qt_standard_project_setup()

# Resources are compiled in via AUTORCC by listing the .qrc as a target source.
# NOT via qt_add_resources(<target> <file>.qrc): that form takes the second
# argument as a RESOURCE NAME, not a .qrc path, so it silently compiles nothing
# and every ":/..." lookup fails at runtime with no build-time complaint.
set(CMAKE_AUTORCC ON)

qt_add_executable(EmulatrLaunch WIN32
    src/main.cpp

    src/Version.h
    src/SystemRecord.h
    src/SystemModel.h              src/SystemModel.cpp
    src/RunDirSkeleton.h           src/RunDirSkeleton.cpp
    src/IniOverlay.h               src/IniOverlay.cpp
    src/FirmwareCheck.h            src/FirmwareCheck.cpp
    src/ExeDiscovery.h             src/ExeDiscovery.cpp
    src/EmulatorProcess.h          src/EmulatorProcess.cpp
    src/EnvVarModel.h              src/EnvVarModel.cpp
    src/EnvVarPanel.h              src/EnvVarPanel.cpp
    src/DiskImageFactory.h         src/DiskImageFactory.cpp
    src/StorageCreateDialog.h      src/StorageCreateDialog.cpp
    src/NewSystemDialog.h          src/NewSystemDialog.cpp
    src/PlatEdBridge.h             src/PlatEdBridge.cpp
    src/TerminalBridge.h           src/TerminalBridge.cpp
    src/LauncherWindow.h           src/LauncherWindow.cpp

    resources/emulatr_launch.qrc
)

target_include_directories(EmulatrLaunch PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(EmulatrLaunch PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets)

set_target_properties(EmulatrLaunch PROPERTIES
    OUTPUT_NAME "EmulatrLaunch"
    WIN32_EXECUTABLE ON)

# ----------------------------------------------------------------------------
# Tests.  Gate G6 names the IniOverlay round-trip byte-preservation test, the
# denylist stripping test, and DiskImageFactory output against the G3 format
# as REVIEW EVIDENCE, so they build by default rather than behind an option.
# Console subsystem (no WIN32 flag) so the results land on stdout.
# Only the UI-free translation units are linked -- the point is to test the
# logic without standing up a window.
# ----------------------------------------------------------------------------
qt_add_executable(EmulatrLaunchTests
    tests/test_launcher.cpp
    src/IniOverlay.h        src/IniOverlay.cpp
    src/RunDirSkeleton.h    src/RunDirSkeleton.cpp
    src/FirmwareCheck.h     src/FirmwareCheck.cpp
    src/DiskImageFactory.h  src/DiskImageFactory.cpp
    src/EnvVarModel.h       src/EnvVarModel.cpp

    resources/emulatr_launch.qrc
)
target_include_directories(EmulatrLaunchTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(EmulatrLaunchTests PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets)
set_target_properties(EmulatrLaunchTests PROPERTIES WIN32_EXECUTABLE OFF)

enable_testing()
add_test(NAME launcher_units COMMAND EmulatrLaunchTests)

# ----------------------------------------------------------------------------
# Run targets.  `cmake --build --preset debug --target run` builds and then
# executes out/build/debug/EmulatrLaunch.exe.
#
# The whole reason these exist is the Qt DLLs: a freshly linked EmulatrLaunch.exe
# does NOT start from Explorer or a bare shell, because Qt6Widgets.dll and
# friends live in the Qt kit's bin directory and nothing puts them on PATH.
# The failure is a silent "the application was unable to start correctly"
# dialog, which reads like a broken build rather than a missing search path.
#
# `run` therefore does BOTH of the things that can go wrong:
#
#   1. It depends on `deploy`, which puts a MATCHING Qt runtime beside the exe.
#      This is not belt-and-braces -- it is the load-bearing half.  Windows
#      searches the APPLICATION DIRECTORY before PATH, so a stale or foreign
#      Qt DLL sitting in the build output wins over any PATH we set and the
#      process dies with 0xC0000139 (entry point not found).  That is not
#      hypothetical: this directory was found holding Qt 6.9.1 debug DLLs
#      mixed with 6.10.2 release ones, left by other tooling pointed at the
#      same tree.  Re-deploying overwrites them with the kit we linked against.
#   2. It injects the kit's bin directory ahead of the inherited PATH, which
#      covers anything windeployqt did not copy.
#
# PATH is captured at CONFIGURE time.  Its semicolons must be escaped or CMake
# splits the single "PATH=..." argument into a dozen arguments and cmake -E env
# treats the tail as the command to run.
# ----------------------------------------------------------------------------
set(_launch_host_path "$ENV{PATH}")
if (WIN32)
    string(REPLACE ";" "\\;" _launch_host_path "${_launch_host_path}")
    set(_launch_path_sep "\\;")
else()
    set(_launch_path_sep ":")
endif()

# windeployqt and the Qt runtime DLLs share the kit's bin directory, so one
# generator expression locates both.
set(_launch_qt_bin "$<TARGET_FILE_DIR:Qt6::Core>")

add_custom_target(run
    COMMAND ${CMAKE_COMMAND} -E env
            "PATH=${_launch_qt_bin}${_launch_path_sep}${_launch_host_path}"
            "$<TARGET_FILE:EmulatrLaunch>"
    DEPENDS EmulatrLaunch
    WORKING_DIRECTORY "$<TARGET_FILE_DIR:EmulatrLaunch>"
    USES_TERMINAL
    VERBATIM
    COMMENT "Running EmulatrLaunch (close the window to return)")

add_custom_target(run-tests
    COMMAND ${CMAKE_COMMAND} -E env
            "PATH=${_launch_qt_bin}${_launch_path_sep}${_launch_host_path}"
            "$<TARGET_FILE:EmulatrLaunchTests>"
    DEPENDS EmulatrLaunchTests
    WORKING_DIRECTORY "$<TARGET_FILE_DIR:EmulatrLaunchTests>"
    USES_TERMINAL
    VERBATIM
    COMMENT "Running the EmulatrLaunch unit tests")

# `deploy` copies the Qt runtime NEXT TO the built exe, so it starts by
# double-click from Explorer with no PATH at all -- which is how it will be
# handed to a tester, and how you will want to reproduce a tester's report.
# It also REPAIRS the output directory when other tooling has dropped a
# different Qt version into it (see the note above).  Kept off the default
# build path because windeployqt costs seconds on every build.
add_custom_target(deploy
    COMMAND "${_launch_qt_bin}/windeployqt.exe"
            --no-translations --no-system-d3d-compiler --no-opengl-sw
            "$<TARGET_FILE:EmulatrLaunch>"
    DEPENDS EmulatrLaunch
    USES_TERMINAL
    VERBATIM
    COMMENT "Copying the Qt runtime beside EmulatrLaunch.exe")

# The tests link the same Qt modules into the same directory, so one deploy
# serves both executables.
add_dependencies(run deploy)
add_dependencies(run-tests deploy)

# ----------------------------------------------------------------------------
# Deployment.  windeployqt via Qt's generated script so the Setup Factory
# payload can be assembled from `cmake --install`.  Harmless on a dev box.
# ----------------------------------------------------------------------------
install(TARGETS EmulatrLaunch
        RUNTIME DESTINATION .
        BUNDLE  DESTINATION .)

qt_generate_deploy_app_script(
    TARGET EmulatrLaunch
    OUTPUT_SCRIPT deploy_script
    NO_UNSUPPORTED_PLATFORM_ERROR)
install(SCRIPT ${deploy_script})
