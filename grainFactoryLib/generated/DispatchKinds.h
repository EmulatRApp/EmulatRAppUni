// ============================================================================
// DispatchKinds.h -- generated for EmulatR V4
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
// AUTO-GENERATED -- DO NOT EDIT.
// Source:    grainFactoryLib/codegen/genGrains.py (canonical list)
// Generator: grainFactoryLib/codegen/genGrains.py
//
// Edit the source TSV and re-run the generator.  Hand-edits to this
// file will be lost on the next codegen pass.
// ============================================================================


#pragma once

#include <cstdint>

namespace grainFactory {

// Dispatch-kind discriminator for the primary opcode table.
// Each value names a sub-table layout that the dispatcher uses
// to extract the secondary decode field from the 32-bit
// instruction word.  See GrainMasterV4.tsv schema docs for the
// per-kind sub-decode width and bit position.
enum class DispatchKind : uint8_t {
    Direct,
    IntArith,
    IntLogical,
    IntShift,
    IntMul,
    ItFp,
    FltVax,
    FltIeee,
    FltLogical,
    Misc,
    JmpClass,
    FpTiExt,
    Pal,
    HwMfpr,
    HwLd,
    HwMtpr,
    HwRei,
    HwSt,
    Reserved,
};

// Returns the canonical name of a DispatchKind value.
constexpr char const* dispatchKindName(DispatchKind k) {
    switch (k) {
        case DispatchKind::Direct: return "Direct";
        case DispatchKind::IntArith: return "IntArith";
        case DispatchKind::IntLogical: return "IntLogical";
        case DispatchKind::IntShift: return "IntShift";
        case DispatchKind::IntMul: return "IntMul";
        case DispatchKind::ItFp: return "ItFp";
        case DispatchKind::FltVax: return "FltVax";
        case DispatchKind::FltIeee: return "FltIeee";
        case DispatchKind::FltLogical: return "FltLogical";
        case DispatchKind::Misc: return "Misc";
        case DispatchKind::JmpClass: return "JmpClass";
        case DispatchKind::FpTiExt: return "FpTiExt";
        case DispatchKind::Pal: return "Pal";
        case DispatchKind::HwMfpr: return "HwMfpr";
        case DispatchKind::HwLd: return "HwLd";
        case DispatchKind::HwMtpr: return "HwMtpr";
        case DispatchKind::HwRei: return "HwRei";
        case DispatchKind::HwSt: return "HwSt";
        case DispatchKind::Reserved: return "Reserved";
    }
    return "<invalid>";
}

} // namespace grainFactory
