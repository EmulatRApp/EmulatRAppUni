# ============================================================================
# berkeley-softfloat-3-master/CMakeLists.txt
# ============================================================================
# Project: EmulatR -- Alpha AXP / EV6 Architecture Emulator (V4)
# Copyright (C) 2025, 2026 eNVy Systems, Inc.  All rights reserved.
# Licensed under eNVy Systems Non-Commercial License v1.1
#
# Project Architect: Timothy Peer
# AI Collaboration:  Claude (Anthropic)
# ============================================================================
#
# EmulatR build glue for the VENDORED Berkeley SoftFloat Release 3e
# (John R. Hauser / Regents of the University of California; BSD 3-clause,
# see COPYING.txt -- redistributed unmodified, headers retained).
#
# Builds the FAST_INT64 + 8086-SSE configuration as a static library `softfloat`
# that EmulatR links to use as the deterministic IEEE FP reference oracle behind
# the IFpBackend seam (task #26, design: journals/fp_backend_and_x87_guard_handoff.md).
#
# The exact source manifest (309 objects) is generated from the upstream
# build/Linux-x86_64-GCC/Makefile object list into softfloat_sources.cmake
# (regenerate, do not hand-edit). SoftFloat is pure integer arithmetic, so it
# is immune to /fp:fast and needs no /fp:strict.
# ============================================================================

set(SOFTFLOAT_ROOT ${CMAKE_CURRENT_SOURCE_DIR})

# Explicit, auditable FAST_INT64 + 8086-SSE source list (avoids the
# FAST_INT64 / not-FAST_INT64 duplicate-symbol trap of a blanket glob).
include(${CMAKE_CURRENT_SOURCE_DIR}/softfloat_sources.cmake)

add_library(softfloat STATIC ${SOFTFLOAT_FAST_INT64_SOURCES})

# Host parameter header (platform.h) -- PORTABLE config for ALL toolchains.
# The upstream Linux-x86_64-GCC platform.h pulls in opts-GCC.h, whose
# extern-inline __int128 / __builtin_clz 128-bit + count-leading-zeros
# primitives do not emit linkable symbols under a plain build (the .c versions
# are guarded out, the inlines are never instantiated) -> link failure. The
# portable config (build/EmulatR-MSVC-x64/platform.h: no __int128, no
# opts-GCC.h) compiles SoftFloat's software primitives instead -- validated to
# link clean and compute correctly, and deterministic/identical across
# compilers (gcc/clang accept the __inline keyword it uses). Same config on
# MSVC and on the Linux sandbox build.
set(SF_PLATFORM_DIR ${SOFTFLOAT_ROOT}/build/EmulatR-MSVC-x64)

target_include_directories(softfloat
    PUBLIC  ${SOFTFLOAT_ROOT}/source/include       # softfloat.h for consumers
    PRIVATE ${SF_PLATFORM_DIR}                     # platform.h (build-only)
            ${SOFTFLOAT_ROOT}/source/8086-SSE)     # specialize.h (build-only)

# Config defines matching the upstream Linux-x86_64-GCC build (portable; not
# GCC-specific). SOFTFLOAT_FAST_INT64 selects the 64-bit-host primitive set.
target_compile_definitions(softfloat PRIVATE
    SOFTFLOAT_FAST_INT64
    SOFTFLOAT_ROUND_ODD
    INLINE_LEVEL=5
    SOFTFLOAT_FAST_DIV32TO16
    SOFTFLOAT_FAST_DIV64TO32)

# Vendored third-party C: compile as C, silence its warnings (not our code).
set_target_properties(softfloat PROPERTIES LINKER_LANGUAGE C)
if(MSVC)
    target_compile_options(softfloat PRIVATE /W0)
else()
    target_compile_options(softfloat PRIVATE -w)
endif()
