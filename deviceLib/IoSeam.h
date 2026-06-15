// ============================================================================
// deviceLib/IoSeam.h -- Stratum-0/3 I/O seam: access envelope + submit/completion
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
// ============================================================================
//
// Spec: journals/PCI_Fabric_Strata_and_BuildOrder_20260609.md (strata + the five
// thread-boundary seam constraints C-VALUE/C-WORKQ/C-DELIVERY/C-LATENCY/C-SNAPSHOT)
// and journals/PCI_Fabric_Section7_Proposal_20260609.md.
//
// STEP 1 of the locked PCI-fabric build order: the contract every later piece
// (Stratum-2 BAR binding, the Stratum-3 SCSI HBA, the Stratum-4 media backend)
// inherits.  Qt-free by design (no QtCore include) so it can be used anywhere in
// the core; the fork-2 QThreadPool backing of IWorkQueue lives elsewhere and stays
// behind the IWorkQueue interface (C-WORKQ) -- Qt never enters the core or the
// completion path.
//
// THIS HEADER lands the TYPES + an inline (fork-1) IWorkQueue + doctest.  It does
// NOT yet integrate BusAccess into TsunamiChipset::mmioRead (same-pass nicety,
// deferred), and it is introduced ALONGSIDE scsi::ScsiCommand with NO rip-out --
// the existing IDE / VirtualIsoDevice path is untouched (dqa0 keeps working).
// ============================================================================

#ifndef DEVICELIB_IOSEAM_H
#define DEVICELIB_IOSEAM_H

#include <array>
#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

namespace deviceLib {

// ---------------------------------------------------------------------------
// STRATUM 0 -- the bus access envelope.
// The single shape that crosses the fabric: (space, address, size, data,
// isWrite).  A value type, NOT an interface; it carries NO device identity (no
// name, no driver) -- if anything richer ever rides this envelope a higher
// concern has leaked downward.  Register/config/mem accesses are SYNCHRONOUS
// (a read returns its data); the async path is the Stratum-3/4 command seam
// below, not this.  NOTE: defined here now; the mmioRead/mmioWrite integration
// that replaces the (pa,width,cpuId) param sprawl is deferred (same pass later).
// ---------------------------------------------------------------------------
enum class BusSpace : uint8_t { Config, Io, Mem };

struct BusAccess {
    BusSpace space   = BusSpace::Mem;
    uint64_t address = 0;       // post-decode PCI coordinate; Config packs
                                //   (bus<<16)|(dev<<11)|(func<<8)|reg
    uint8_t  size    = 0;       // transfer width in BYTES: 1/2/4/8
    uint64_t data    = 0;       // write: data in; read: filled by the responder
    bool     isWrite = false;
};

// ---------------------------------------------------------------------------
// STRATUM 3<->4 -- the command/completion seam (the load-bearing half).
//
// C-VALUE: IoCommand and IoResult are SELF-CONTAINED value types that OWN their
// data.  No pointers/views into guest-visible state -- a worker (fork-2) gets a
// COPY of the inputs and produces a RESULT VALUE applied on the emulation thread.
// (This is why dataOut/dataIn are owning std::vector, never a span/pointer; the
// doctest deliberately fails if that invariant is ever broken.)
//
// IoCommand vs scsi::ScsiCommand (boundary, NOT just deferral): ScsiCommand is the
// REGISTER/PROTOCOL-level working state (the HBA's per-CSR taskfile/PIO decode,
// with its in-place dataBuffer pointer -- the fork-1 sync pattern).  IoCommand is
// the SEAM-LEVEL TRANSPORT CURRENCY -- the self-contained packet that crosses
// submit()->completion.  Intended end state: the HBA translates its register-level
// activity into an IoCommand and submits it; the device consumes IoCommand and
// returns IoResult.  IoCommand/IoResult SUBSUME the data-carrying role of
// ScsiCommand's buffer; ScsiCommand remains for register/taskfile decode until the
// IDE path is migrated.  Populate IoCommand at the seam, ScsiCommand at the CSRs.
// ---------------------------------------------------------------------------
struct IoCommand {
    std::array<uint8_t, 16> cdb{};   // CDB bytes (ATAPI 12 / SCSI up to 16)
    uint8_t              cdbLen = 0;
    uint8_t              lun    = 0; // logical unit behind the (channel,unit)/(target) addressed by the HBA
    std::vector<uint8_t> dataOut;    // WRITE-direction payload, owned by value
    uint32_t             expectedIn = 0; // max data-in bytes the initiator will accept
};

struct IoResult {
    // NARROWING (conscious): only Good vs CheckCondition -- the full SCSI status
    // byte (Busy/Reservation-Conflict/Task-Set-Full/...) is NOT modeled.  Adequate
    // for the ATAPI CD-ROM contract: "becoming ready" returns CheckCondition with
    // NOT READY / 0x04 sense, never Busy.  Widening this enum later is
    // source-compatible, so deferring it is free.
    enum class Status : uint8_t { Good, CheckCondition };

    Status               status   = Status::Good;
    std::array<uint8_t, 18> sense{};   // fixed-format sense (up to 18 bytes)
    uint8_t              senseLen = 0;
    std::vector<uint8_t> dataIn;       // READ-direction payload, owned by value
};

// Completion token.  Invoked with the result BY VALUE.  C-DETERM: it is meant to
// fire on the EMULATION thread at issue_cycle + N (the deadline scheduler).  In
// the fork-1 InlineWorkQueue path it fires INLINE at N=0 (see the caveat on
// ioServiceCycles below).
using IoCompletion = std::function<void(IoResult)>;

// The Stratum-4 device seam: one logical unit.  Addressing-agnostic -- it knows
// nothing of its (channel,unit)/(target,lun) or which HBA owns it (M3: topology
// lives above).  fork-1 implementations invoke `done' inline before returning.
struct IIoTarget {
    virtual ~IIoTarget() = default;
    virtual void submit(IoCommand cmd, IoCompletion done) = 0;
};

// ---------------------------------------------------------------------------
// C-WORKQ -- core-owned offload abstraction.  fork-1 = InlineWorkQueue (run the
// labor synchronously, in-thread).  fork-2 = a QThreadPool-backed implementation
// that lives OUTSIDE the core and is injected behind this interface, so QtCore
// never enters the core and never touches completion delivery.
// ---------------------------------------------------------------------------
struct IWorkQueue {
    virtual ~IWorkQueue() = default;
    virtual void post(std::function<void()> work) = 0;
};

struct InlineWorkQueue : IWorkQueue {
    void post(std::function<void()> work) override { work(); }
};

// ---------------------------------------------------------------------------
// C-LATENCY -- deterministic service-time model: emulated cycles from issue to
// completion, derived from the COMMAND (type + byte count), NEVER from wall-clock
// (wall-clock N diverges replays/traces).  DECLARED-ONLY here: the definition
// lands with the fork-2 deadline scheduler that actually defers completion to
// issue_cycle + N.  fork-1 inline completion is effectively N=0.
//   CAVEAT (issue+N not yet mechanized): inline completion fires instantly, so a
//   firmware path that depends on OBSERVING latency (BSY->!BSY transitions, a
//   non-zero service time) rather than just getting an answer would behave
//   differently than the cycle-deferred path.  The SRM probe loops most likely
//   just need the device to exist and answer IDENTIFY PACKET / TEST UNIT READY,
//   so N=0 is expected to be fine for fork-1 -- but do not assume the inline path
//   exits a probe loop identically to a deferred one.
//   NOTE: declared-only -- do NOT ODR-use (e.g. call) this until it is defined,
//   or the link breaks.  The doctest deliberately does not reference it.
// ---------------------------------------------------------------------------
uint64_t ioServiceCycles(const IoCommand& cmd);

} // namespace deviceLib

#endif // DEVICELIB_IOSEAM_H
