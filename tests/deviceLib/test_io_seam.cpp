// ============================================================================
// tests/deviceLib/test_io_seam.cpp -- Step 1: IoSeam value-out command/completion
// ============================================================================
// Project: EmulatR -- Alpha AXP / EV6 Architecture Emulator (V4)
// Copyright (C) 2025, 2026 eNVy Systems, Inc.  All rights reserved.
// Licensed under eNVy Systems Non-Commercial License v1.1
//
// Project Architect: Timothy Peer
// AI Collaboration:  Claude (Anthropic)
// ============================================================================
//
// Proves the Stratum-3<->4 seam contract (journals/PCI_Fabric_Strata_and_
// BuildOrder_20260609.md, C-VALUE): submit()->completion delivers an IoResult BY
// VALUE, and the result OWNS its payload (a copy, not a view into target storage).
// The aliasing case is written so it FAILS if IoResult::dataIn is ever changed to
// a span/pointer -- that is the whole point of locking the contract now.
// doctest CHECK only.  Does NOT reference ioServiceCycles (declared-only).
// ============================================================================

#include "doctest.h"

#include <cstdint>
#include <utility>
#include <vector>

#include "deviceLib/IoSeam.h"

namespace {

using namespace deviceLib;

// A fake Stratum-4 target that KEEPS its media buffer live and COPIES it into the
// result (never moves/views it), so the C-VALUE aliasing test can actually fail
// if dataIn ever becomes a view into target storage.
class FakeTarget : public IIoTarget {
public:
    void setMedia(std::vector<uint8_t> bytes) { m_media = std::move(bytes); }
    void pokeMedia(std::size_t i, uint8_t v)  { if (i < m_media.size()) m_media[i] = v; }
    void setHasMedia(bool present)            { m_hasMedia = present; }

    void submit(IoCommand cmd, IoCompletion done) override {
        IoResult r;
        uint8_t const op = cmd.cdb[0];
        bool const dataCmd = (op == 0x28u /*READ(10)*/ || op == 0x08u /*READ(6)*/);
        if (!m_hasMedia) {
            // Fail-fast NOT READY / MEDIUM NOT PRESENT (02/3A/00), by value.
            r.status    = IoResult::Status::CheckCondition;
            r.sense[0]  = 0x70u;            // current-error, fixed format
            r.sense[2]  = 0x02u;            // sense key = NOT READY
            r.sense[7]  = 0x0Au;            // additional sense length
            r.sense[12] = 0x3Au;            // ASC  = MEDIUM NOT PRESENT
            r.sense[13] = 0x00u;            // ASCQ
            r.senseLen  = 18u;
        } else if (dataCmd) {
            r.status = IoResult::Status::Good;
            r.dataIn = m_media;            // COPY (not move); m_media stays live.
        } else {
            r.status = IoResult::Status::Good;   // e.g. TEST UNIT READY ok
        }
        done(std::move(r));                // fork-1: inline completion.
    }

private:
    std::vector<uint8_t> m_media;
    bool                 m_hasMedia = true;
};

} // namespace

TEST_CASE("InlineWorkQueue runs work synchronously (fork-1)") {
    InlineWorkQueue wq;
    bool ran = false;
    wq.post([&] { ran = true; });
    CHECK(ran);
}

TEST_CASE("submit delivers the result inline (fork-1 completion)") {
    FakeTarget t;
    t.setMedia({0x11u, 0x22u, 0x33u});
    IoCommand cmd;
    cmd.cdb[0] = 0x28u; cmd.cdbLen = 10u; cmd.expectedIn = 3u;
    bool fired = false;
    IoResult captured;
    t.submit(cmd, [&](IoResult r) { captured = std::move(r); fired = true; });
    CHECK(fired);
    CHECK(captured.status == IoResult::Status::Good);
    CHECK(captured.dataIn.size() == 3u);
    CHECK(captured.dataIn[0] == 0x11u);
}

TEST_CASE("IoResult OWNS its data-in (C-VALUE: a copy, not a view into target storage)") {
    FakeTarget t;
    t.setMedia({0x11u, 0x22u, 0x33u});
    IoCommand cmd;
    cmd.cdb[0] = 0x28u; cmd.cdbLen = 10u; cmd.expectedIn = 3u;
    IoResult captured;
    t.submit(cmd, [&](IoResult r) { captured = std::move(r); });
    CHECK(captured.dataIn[0] == 0x11u);
    // Mutate the target's STILL-LIVE media; the captured result must NOT change.
    // If dataIn were ever a span/pointer into m_media, this CHECK would fail.
    t.pokeMedia(0, 0xFFu);
    CHECK(captured.dataIn[0] == 0x11u);
}

TEST_CASE("IoResult carries CheckCondition + sense by value (NOT READY / 3A no media)") {
    FakeTarget t;
    t.setHasMedia(false);
    IoCommand cmd;
    cmd.cdb[0] = 0x00u; cmd.cdbLen = 6u;   // TEST UNIT READY
    IoResult captured;
    t.submit(cmd, [&](IoResult r) { captured = std::move(r); });
    CHECK(captured.status == IoResult::Status::CheckCondition);
    CHECK(captured.senseLen == 18u);
    CHECK((captured.sense[2] & 0x0Fu) == 0x02u);   // NOT READY
    CHECK(captured.sense[12] == 0x3Au);            // MEDIUM NOT PRESENT (ASC)
}

TEST_CASE("IoCommand owns its CDB + data-out (value copy is independent)") {
    IoCommand cmd;
    cmd.cdb[0] = 0x2Au; cmd.cdbLen = 10u;
    cmd.dataOut = {0xAAu, 0xBBu};
    IoCommand copy = cmd;            // value copy
    cmd.cdb[0]     = 0x00u;          // mutate the original
    cmd.dataOut[0] = 0x00u;
    CHECK(copy.cdb[0] == 0x2Au);
    CHECK(copy.dataOut.size() == 2u);
    CHECK(copy.dataOut[0] == 0xAAu);
}
