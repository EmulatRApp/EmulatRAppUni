// ============================================================================
// traceLib/CommitRecord.h -- per-instruction commit packet for TraceSink
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
// CommitRecord is the small descriptor PipelineDriver hands to a
// TraceSink at WB after retire.  Holds everything a trace formatter
// needs to emit one listing line: cycle, PC, raw encoding, mnemonic
// pointer (codegen-emitted literal), and a pointer to the BoxResult
// that the leaf produced.  The BoxResult carries regWriteIdx /
// regWriteValue / regWriteIsFp -- the "result" half of the listing
// shape -- without copying.
//
// Pointer fields, not values, because the call site has the live
// objects in scope and the sink consumes them within the same call;
// no lifetime extension is needed.  Sinks must not store these
// pointers across calls.
//
// ============================================================================

#ifndef TRACELIB_COMMITRECORD_H
#define TRACELIB_COMMITRECORD_H

#include <cstdint>

namespace coreLib {
struct BoxResult;
}

namespace traceLib {

struct CommitRecord
{
    uint64_t                  cycle;     // CPU cycle this instruction retired in
    uint64_t                  pc;        // architectural PC of the retired instruction
    uint32_t                  encoded;   // raw 32-bit instruction word
    char const*               mnemonic;  // codegen-emitted literal (e.g. "ADDQ")
    coreLib::BoxResult const* result;    // result of the leaf -- regWrite* fields read here
};

} // namespace traceLib

#endif // TRACELIB_COMMITRECORD_H
