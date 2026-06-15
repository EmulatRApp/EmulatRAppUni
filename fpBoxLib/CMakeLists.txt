# ============================================================================
# fpBoxLib/CMakeLists.txt -- EmulatR FP execute layer (host-FP surface)
# ============================================================================
# Project: EmulatR -- Alpha AXP / EV6 Architecture Emulator (V4)
# Copyright (C) 2025, 2026 eNVy Systems, Inc.  All rights reserved.
# Licensed under eNVy Systems Non-Commercial License v1.1
#
# Project Architect: Timothy Peer
# AI Collaboration:  Claude (Anthropic)
# ============================================================================
#
# fpBoxLib is the FP EXECUTE layer (task #26). It owns everything that touches
# the host-FP surface: the x87 guard (Phase A), and -- Phase B onward -- the
# IFpBackend seam, the SoftFloat reference backend, and the optional host
# SSE2/AArch64 fast-path backends.
#
# LAYERING (deliberate): fpBox depends on coreLib (HEADER-ONLY here: FPVariant /
# FPCR layout from fp_variant_core.h / alpha_fpcr_core.h, reached via the project
# root include path) and on the vendored `softfloat` target. coreLib depends on
# NEITHER and stays host-agnostic and dependency-free. Do not invert this edge.
#
# All sources in FPBOX_SOURCES are pinned strict-FP / no-FMA-contraction: the
# host `double` path must be single-rounded and x87-free or the oracle self-test
# (and the Phase E host fast-path) would diverge.
# ============================================================================

set(FPBOX_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/fp_host_guard.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/fp_backend_softfloat.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/fp_backend_active.cpp
)

add_library(fpBox STATIC ${FPBOX_SOURCES})

# coreLib headers are reached project-root-relative (#include "coreLib/...",
# "fpBoxLib/..."). The project does NOT put the root on a global include path --
# each target adds it PRIVATE (CMakeLists.txt line ~242), which does NOT
# propagate into a subdirectory library. So fpBox supplies the root itself.
# Use the PARENT of fpBoxLib/ (== the Emulatr root) rather than ${CMAKE_SOURCE_DIR}
# so it is correct even if Emulatr is ever built as a sub-project.
get_filename_component(EMULATR_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
target_include_directories(fpBox PUBLIC ${EMULATR_ROOT})
# softfloat is a real link dependency (PUBLIC so consumers that link fpBox also
# get softfloat.h -- source/include -- on their include path).
target_link_libraries(fpBox PUBLIC softfloat)

# Strict FP / no fused-multiply-add contraction on the whole FP execute layer.
if(MSVC)
    target_compile_options(fpBox PRIVATE /fp:strict)
else()
    target_compile_options(fpBox PRIVATE -ffp-contract=off)
endif()

# long-double lint: 80-bit x87 `long double` is never correct for guest FP, and
# /fp:fast / FMA are not always macro-detectable -- so refuse the token outright
# in any FP-layer source. (Cheap configure-time greppable lint.)
foreach(_fp_src ${FPBOX_SOURCES})
    file(READ ${_fp_src} _fp_txt)
    if(_fp_txt MATCHES "long[ \t]+double")
        message(FATAL_ERROR
            "fpBoxLib: 'long double' is forbidden in the FP path (80-bit x87 hazard): ${_fp_src}")
    endif()
endforeach()
