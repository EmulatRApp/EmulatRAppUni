// ============================================================================
// traceLib/PaDump.cpp -- physical-address disassembly dump facility
// ============================================================================
// Project: EmulatR -- Alpha AXP / EV6 Architecture Emulator (V4)
// Copyright (C) 2025, 2026 eNVy Systems, Inc.  All rights reserved.
// Licensed under eNVy Systems Non-Commercial License v1.1
// ============================================================================

#include "traceLib/PaDump.h"

#include <cstdint>
#include <cstdio>
#include <iomanip>
#include <ios>
#include <ostream>

#include "coreLib/VA_types.h"
#include "memoryLib/GuestMemory.h"
#include "grainFactoryLib/DispatchAccess.h"
#include "coreLib/DispatchEntry.h"
#include "traceLib/Disassembler.h"

namespace traceLib {

namespace {

// Print one 16-byte hexdump row.  PA column on the left, hex middle,
// ASCII gutter on the right; non-printable bytes show as '.'.  Bytes
// that could not be read are rendered as "??" in the hex column and
// as '.' in the gutter.
void writeHexRow(uint64_t      rowPa,
                 uint8_t const bytes[16],
                 bool    const valid[16],
                 std::ostream& out)
{
    char line[128];
    int  n = std::snprintf(line, sizeof line,
                           "PA=0x%016llx  ",
                           static_cast<unsigned long long>(rowPa));
    out.write(line, n);

    for (int i = 0; i < 16; ++i) {
        if (valid[i]) {
            n = std::snprintf(line, sizeof line, "%02x ",
                              static_cast<unsigned>(bytes[i]));
        } else {
            n = std::snprintf(line, sizeof line, "?? ");
        }
        out.write(line, n);
        if (i == 7) out.write(" ", 1);   // mid-row separator
    }
    out.write(" |", 2);
    for (int i = 0; i < 16; ++i) {
        char c = '.';
        if (valid[i]) {
            char const b = static_cast<char>(bytes[i]);
            if (b >= 0x20 && b < 0x7f) c = b;
        }
        out.write(&c, 1);
    }
    out.write("|\n", 2);
}

} // anonymous namespace


// ----------------------------------------------------------------------------
// Public entry points
// ----------------------------------------------------------------------------
void dumpPaRange(memoryLib::GuestMemory const& mem,
                 uint64_t                      pa,
                 std::size_t                   bytes,
                 std::ostream&                 out) noexcept
{
    if (bytes == 0) return;

    // Round start down to 16-byte alignment so the address column reads
    // naturally.  Bytes before the requested start are rendered "??".
    uint64_t const rowStart = pa & ~uint64_t{0xF};
    uint64_t const rowEnd   = (pa + bytes + 0xF) & ~uint64_t{0xF};

    out << "# PaDump bytes: pa=0x" << std::hex << pa
        << " count=0x" << bytes
        << " rowStart=0x" << rowStart
        << " rowEnd=0x"   << rowEnd
        << std::dec << "\n";

    for (uint64_t row = rowStart; row < rowEnd; row += 16) {
        uint8_t  buf [16] = {0};
        bool     valid[16] = {false};
        for (int i = 0; i < 16; ++i) {
            uint64_t const cursor = row + i;
            if (cursor < pa || cursor >= pa + bytes) {
                // before/after the requested window
                valid[i] = false;
                continue;
            }
            uint8_t v = 0;
            auto const st = mem.read1(static_cast<coreLib::PAType>(cursor), v);
            if (st == memoryLib::MemStatus::Ok) {
                buf[i]   = v;
                valid[i] = true;
            }
        }
        writeHexRow(row, buf, valid, out);
    }
}


void dumpDisasmAt(memoryLib::GuestMemory const& mem,
                  uint64_t                      pa,
                  std::size_t                   instructions,
                  std::ostream&                 out) noexcept
{
    if (instructions == 0) return;

    out << "# PaDump disasm: pa=0x" << std::hex << pa
        << " count=" << std::dec << instructions << "\n";
    out << "# format: PA=<hex16>  encoded=<hex8>  <primary-mnem>  <operands>\n";

    for (std::size_t i = 0; i < instructions; ++i) {
        uint64_t const cursor = pa + (i * 4);
        uint32_t encoded = 0;
        auto const st = mem.read4(static_cast<coreLib::PAType>(cursor), encoded);
        if (st != memoryLib::MemStatus::Ok) {
            char buf[96];
            int  n = std::snprintf(buf, sizeof buf,
                "PA=0x%016llx  encoded=??????????  <oor>\n",
                static_cast<unsigned long long>(cursor));
            out.write(buf, n);
            return;
        }

        uint8_t  const primaryOp = static_cast<uint8_t>((encoded >> 26) & 0x3Fu);
        auto const& entry        = grainFactory::primaryEntry(primaryOp);
        char const* const mnem   = entry.mnemonic ? entry.mnemonic : "?";

        // The leaf-level mnemonic requires walking the sub-table the same
        // way the dispatcher does; we deliberately do not.  The primary
        // mnemonic plus the encoded hex is enough for a human reader to
        // disambiguate (e.g., HW_MTPR's IPR index is in encoded[15:0]).
        std::string const operands =
            traceLib::disassembleOperands(encoded, primaryOp, entry.kind);

        char buf[160];
        int  n = std::snprintf(buf, sizeof buf,
                               "PA=0x%016llx  encoded=0x%08x  %-9s %s\n",
                               static_cast<unsigned long long>(cursor),
                               encoded,
                               mnem,
                               operands.c_str());
        out.write(buf, n);
    }
}

} // namespace traceLib
