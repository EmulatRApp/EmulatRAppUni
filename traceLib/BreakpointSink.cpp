// ============================================================================
// traceLib/BreakpointSink.cpp -- gated full-complement retire sink impl
// ============================================================================
// Project: EmulatR -- Alpha AXP / EV6 Architecture Emulator (V4)
// Copyright (C) 2025, 2026 eNVy Systems, Inc.  All rights reserved.
// Licensed under eNVy Systems Non-Commercial License v1.1
// ============================================================================

#include "traceLib/BreakpointSink.h"

#include "coreLib/BoxResult.h"
#include "coreLib/CBoxState.h"
#include "coreLib/CpuState.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <iomanip>
#include <system_error>

#if defined(_MSC_VER)
// __debugbreak intrinsic.  MSVC-specific; raises STATUS_BREAKPOINT which
// the attached debugger catches.  Falls back to a no-op on other compilers
// via the macro below.
#include <intrin.h>
#define BREAKPOINT_SINK_DEBUGBREAK() __debugbreak()
#else
// No-op on non-MSVC builds.  V4 is Windows/MSVC only today, but this guard
// keeps the file portable if a Linux build is ever attempted.
#define BREAKPOINT_SINK_DEBUGBREAK() ((void)0)
#endif


namespace traceLib {

// ---------------------------------------------------------------------------
// Static atomics -- single-definition-per-program lives here.
// ---------------------------------------------------------------------------
// Initial values mirror the BP_DEFAULT_* constants in the header so a
// fresh build is armed for the 2026-05-12 sys__cbox investigation without
// external configuration.  Override before run() via the static setters
// or via direct atomic stores from the debugger.
std::atomic<uint64_t> BreakpointSink::s_gateOpenPc { BP_DEFAULT_GATE_OPEN_PC };
std::atomic<uint64_t> BreakpointSink::s_gateClosePc { BP_DEFAULT_GATE_CLOSE_PC };
std::atomic<int32_t>  BreakpointSink::s_revolutionsRemaining {
    static_cast<int32_t>(BP_DEFAULT_REVOLUTIONS) };
std::atomic<uint32_t> BreakpointSink::s_revolutionsCaptured { 0 };
std::atomic<bool>     BreakpointSink::s_gateOpen { false };
std::atomic<bool>     BreakpointSink::s_breakOnGateOpen { false };
std::atomic<bool>     BreakpointSink::s_breakOnGateClose { false };

// Checkpoint ledger control (2026-06-03).  All slots disarmed (PC 0) at
// startup; arm via setCheckpoint() or the EMULATR_CHECKPOINTS env parse
// in main.cpp before run().  std::array of atomics value-initializes each
// element to 0.
std::array<std::atomic<uint64_t>, BreakpointSink::kMaxCheckpoints>
    BreakpointSink::s_checkpointPc {};
char BreakpointSink::s_checkpointLabel[BreakpointSink::kMaxCheckpoints][24] = {};

void BreakpointSink::setCheckpoint(int idx, uint64_t pc, char const* label) noexcept
{
    if (idx < 0 || idx >= kMaxCheckpoints) {
        return;
    }
    s_checkpointPc[idx].store(pc, std::memory_order_release);
    if (label != nullptr) {
        std::snprintf(s_checkpointLabel[idx], sizeof(s_checkpointLabel[idx]),
                      "%s", label);
    } else {
        s_checkpointLabel[idx][0] = '\0';
    }
}


namespace {

// printf-into-string helper.  Same shape as DecListingSink.cpp's local
// vfmt -- duplicated here rather than coupled across translation units
// (each TU's vfmt has its own static buffer, no shared state).  256
// chars is generous for any single-record fragment; longer assemblies
// chain calls with << into the ofstream.
std::string vfmt(char const* fmt, ...) noexcept
{
    char buf[256];
    va_list args;
    va_start(args, fmt);
    int const n = std::vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    if (n < 0) return std::string{};
    return std::string{buf,
                       static_cast<size_t>(n < static_cast<int>(sizeof(buf))
                                              ? n
                                              : static_cast<int>(sizeof(buf)) - 1)};
}


// Compose YYYYMMDD-HHMMSS from local time at call time.  Used in the
// break filename so concurrent or repeat runs do not collide.  Returns
// "00000000-000000" if the platform's localtime conversion fails -- the
// sink keeps running, the file just lands at that anomalous stamp.
std::string composeStampNow() noexcept
{
    char buf[32] = "00000000-000000";
    auto const now    = std::chrono::system_clock::now();
    auto const now_tt = std::chrono::system_clock::to_time_t(now);
    std::tm tmLocal{};
#if defined(_WIN32)
    bool const ok = (localtime_s(&tmLocal, &now_tt) == 0);
#else
    bool const ok = (localtime_r(&now_tt, &tmLocal) != nullptr);
#endif
    if (ok) {
        std::strftime(buf, sizeof(buf), "%Y%m%d-%H%M%S", &tmLocal);
    }
    return std::string{buf};
}


// ISO-8601 stamp for the run_started field in the file header.  Same
// shape as DecListingSink's helper.  Local time with zone offset; "+HHMM"
// is reformatted to "+HH:MM" for strict ISO compliance.
std::string isoStampNow() noexcept
{
    auto const now    = std::chrono::system_clock::now();
    auto const now_tt = std::chrono::system_clock::to_time_t(now);
    std::tm tmLocal{};
#if defined(_WIN32)
    if (localtime_s(&tmLocal, &now_tt) != 0) {
        return std::string{"<timestamp-error>"};
    }
#else
    if (localtime_r(&now_tt, &tmLocal) == nullptr) {
        return std::string{"<timestamp-error>"};
    }
#endif
    char buf[40];
    if (std::strftime(buf, sizeof(buf),
                      "%Y-%m-%dT%H:%M:%S%z", &tmLocal) == 0) {
        return std::string{"<timestamp-error>"};
    }
    std::string s{buf};
    if (s.size() >= 5) {
        char const sign = s[s.size() - 5];
        if (sign == '+' || sign == '-') {
            s.insert(s.size() - 2, 1, ':');
        }
    }
    return s;
}


// Mnemonic match helpers.  Cheap strcmp variants; the mnemonic pointer
// is the codegen-emitted literal (static storage), so equality of the
// pointer values is also legal but a string compare is more robust if
// any leaf later returns a re-formatted copy.
bool isMemLoad(char const* m) noexcept
{
    if (m == nullptr) return false;
    // Covers HW_LD plus the architectural LDx family that we might also
    // gate on later; the suffix table here matches V4's mnemonic palette.
    return std::strcmp(m, "HW_LD") == 0;
}
bool isMemStore(char const* m) noexcept
{
    if (m == nullptr) return false;
    return std::strcmp(m, "HW_ST") == 0;
}
bool isMfpr(char const* m) noexcept
{
    if (m == nullptr) return false;
    return std::strcmp(m, "HW_MFPR") == 0;
}
bool isMtpr(char const* m) noexcept
{
    if (m == nullptr) return false;
    return std::strcmp(m, "HW_MTPR") == 0;
}

} // anonymous namespace


// ============================================================================
// armed -- static inspection helper
// ============================================================================
// True when at least one gate is armed AND the revolution budget is not
// yet exhausted.  Used by callers (debugger, tests) to confirm the sink
// is still active without poking the sink's internal counters.
// ============================================================================

bool BreakpointSink::armed() noexcept
{
    uint64_t const openPc  = s_gateOpenPc.load(std::memory_order_relaxed);
    uint64_t const closePc = s_gateClosePc.load(std::memory_order_relaxed);
    int32_t  const rem     = s_revolutionsRemaining.load(std::memory_order_relaxed);
    return (openPc != 0 || closePc != 0) && rem > 0;
}


// ============================================================================
// ctor / dtor / lifecycle
// ============================================================================

BreakpointSink::BreakpointSink(std::filesystem::path const& outputDir,
                               TraceSink*                   downstream)
    : m_outputDir(outputDir)
    , m_downstream(downstream)
    , m_startTime(std::chrono::steady_clock::now())
{
    // Stamp is captured at construction so all records reference the
    // same run-instance identifier even if the wall clock drifts during
    // a long capture.
    m_stamp = composeStampNow();
}


BreakpointSink::~BreakpointSink()
{
    // Belt-and-braces flush in case onRunEnd was not invoked (run loop
    // returned through an unexpected path).  The downstream is NOT
    // owned here; do not delete it.
    if (m_breakOpen) {
        m_breakLog.flush();
        m_breakLog.close();
        m_breakOpen = false;
    }
}


// ============================================================================
// ensureBreakOpen -- lazy file creation on first gate-open
// ============================================================================
// Directory resolution order matches DecListingSink::ctor:
//   1. m_outputDir if non-empty
//   2. env EMULATR_RETIRE_TRACE_DIR (same env var both sinks consult)
//   3. D:\EmulatR\traces (project trace folder default)
// On any failure the sink stays disabled; subsequent calls become silent
// no-ops.  Loud stderr diagnostics on every transition so an operator
// running interactively sees why the break file did or did not appear.
// ============================================================================

void BreakpointSink::ensureBreakOpen()
{
    if (m_breakOpen) return;

    // Resolve the directory.  Empty m_outputDir falls through to env then
    // to D:\EmulatR\traces; this matches DecListingSink so a single env
    // override configures both sinks together.
    std::string dirStr;
    if (!m_outputDir.empty()) {
        dirStr = m_outputDir.string();
    } else {
        char const* const envDir = std::getenv("EMULATR_RETIRE_TRACE_DIR");
        dirStr = (envDir != nullptr && envDir[0] != '\0')
                     ? std::string{envDir}
                     : std::string{"D:\\EmulatR\\traces"};
    }

    std::filesystem::path const breakDir{dirStr};

    // Probe the drive root before attempting create_directories.  Same
    // wedge-protection logic as DecListingSink: a missing iSCSI mount
    // would block on stat() inside create_directories; the root probe
    // returns ec quickly and lets us bail with a single error message.
    std::error_code probeEc;
    if (!std::filesystem::exists(breakDir.root_path(), probeEc)) {
        std::fprintf(stderr,
                     "BreakpointSink: root '%s' not reachable; "
                     "break sink DISABLED.  Set EMULATR_RETIRE_TRACE_DIR "
                     "to override.\n",
                     breakDir.root_path().string().c_str());
        std::fflush(stderr);
        return;
    }

    std::error_code mkdirEc;
    std::filesystem::create_directories(breakDir, mkdirEc);
    if (mkdirEc) {
        std::fprintf(stderr,
                     "BreakpointSink: create_directories('%s') FAILED: "
                     "%s; break sink DISABLED\n",
                     breakDir.string().c_str(),
                     mkdirEc.message().c_str());
        std::fflush(stderr);
        return;
    }

    m_breakPath = breakDir / (m_stamp + std::string{"_break.trc"});

    m_breakLog.open(m_breakPath, std::ios::out | std::ios::trunc);
    m_breakOpen = m_breakLog.is_open();
    if (!m_breakOpen) {
        std::fprintf(stderr,
                     "BreakpointSink: open('%s') FAILED; break sink DISABLED\n",
                     m_breakPath.string().c_str());
        std::fflush(stderr);
        return;
    }

    // Self-describing header.  A consumer that opens the file knows the
    // record taxonomy, the gate configuration, and the run-start time
    // without external context.
    uint64_t const gateOpenPc  = s_gateOpenPc.load(std::memory_order_relaxed);
    uint64_t const gateClosePc = s_gateClosePc.load(std::memory_order_relaxed);
    int32_t  const revsRemain  = s_revolutionsRemaining.load(std::memory_order_relaxed);

    m_breakLog << "# EmulatR V4 breakpoint trace -- full-complement gated\n";
    m_breakLog << "# run_started=" << isoStampNow() << "\n";
    m_breakLog << "# gate_open_pc=0x"
               << std::hex << std::setw(16) << std::setfill('0')
               << gateOpenPc
               << std::dec << std::setfill(' ') << "\n";
    m_breakLog << "# gate_close_pc=0x"
               << std::hex << std::setw(16) << std::setfill('0')
               << gateClosePc
               << std::dec << std::setfill(' ') << "\n";
    m_breakLog << "# revolutions_requested=" << revsRemain << "\n";
    m_breakLog << "# format: BRK rpcc=<n> pc=<hex16> encoded=<hex8> <mnem> "
                  "pal=<0|1> exc=<hex16> R00..R30=<hex16>"
                  " [ F00..F30=<hex16> ] [ ea=<hex16> val=<hex16> ]"
                  " [ iprid=<hex8> iprval=<hex16> ]\n";
    m_breakLog << "# transitions: BP_OPEN / BP_CLOSE markers, followed by "
                  "IPR_SNAP / PT_SNAP / CBX_SNAP records.\n";
    m_breakLog.flush();

    std::fprintf(stderr,
                 "BreakpointSink: break sink OPENED at %s\n",
                 m_breakPath.string().c_str());
    std::fflush(stderr);
}


// ============================================================================
// emitGateSnapshot -- gate transition record triplet
// ============================================================================
// Writes one of (BP_OPEN, BP_CLOSE) followed by IPR_SNAP, PT_SNAP, and
// CBX_SNAP.  Per the format docstring in the header, this is the heavy
// snapshot at the boundaries of a captured revolution.  All four records
// reference the same cycle and revolution index so a post-mortem reader
// can group them without ambiguity.
// ============================================================================

void BreakpointSink::emitGateSnapshot(uint64_t                 cycle,
                                      uint64_t                 pc,
                                      uint32_t                 revolution,
                                      char const*              kind,
                                      coreLib::CpuState const& s)
{
    if (!m_breakOpen) return;

    char const* const marker =
        (std::strcmp(kind, "open") == 0) ? "BP_OPEN" : "BP_CLOSE";

    m_breakLog << vfmt("%s rpcc=%llu rev=%u pc=%016llx\n",
                       marker,
                       static_cast<unsigned long long>(cycle),
                       revolution,
                       static_cast<unsigned long long>(pc));

    // IPR_SNAP: every named IPR field in CpuState as key=value tokens.
    // Ordering is the field order in CpuState.h so visual diffing
    // between consecutive snapshots aligns columns when piped through
    // a side-by-side viewer.
    m_breakLog << vfmt("IPR_SNAP rpcc=%llu kind=%s",
                       static_cast<unsigned long long>(cycle), kind);
    m_breakLog << vfmt(" ptbr=%016llx",         static_cast<unsigned long long>(s.ptbr));
    m_breakLog << vfmt(" asn=%016llx",          static_cast<unsigned long long>(s.asn));
    m_breakLog << vfmt(" va_ctl=%016llx",       static_cast<unsigned long long>(s.va_ctl));
    m_breakLog << vfmt(" i_ctl=%016llx",        static_cast<unsigned long long>(s.i_ctl));
    m_breakLog << vfmt(" m_ctl=%016llx",        static_cast<unsigned long long>(s.m_ctl));
    m_breakLog << vfmt(" i_spe=%02x",           static_cast<unsigned>(s.i_spe));
    m_breakLog << vfmt(" m_spe=%02x",           static_cast<unsigned>(s.m_spe));
    m_breakLog << vfmt(" mode=%d",              static_cast<int>(s.mode));
    m_breakLog << vfmt(" palMode=%d",           s.inPalMode() ? 1 : 0);
    m_breakLog << vfmt(" cycleCount=%016llx",   static_cast<unsigned long long>(s.cycleCount));
    m_breakLog << vfmt(" ccOffset=%016llx",     static_cast<unsigned long long>(s.ccOffset));
    m_breakLog << vfmt(" intrFlag=%016llx",     static_cast<unsigned long long>(s.intrFlag));
    m_breakLog << vfmt(" mm_stat=%016llx",      static_cast<unsigned long long>(s.mm_stat));
    m_breakLog << vfmt(" excAddr=%016llx",      static_cast<unsigned long long>(s.excAddr));
    m_breakLog << vfmt(" vptb=%016llx",         static_cast<unsigned long long>(s.vptb));
    m_breakLog << vfmt(" palBase=%016llx",      static_cast<unsigned long long>(s.palBase));
    m_breakLog << vfmt(" scbb=%016llx",         static_cast<unsigned long long>(s.scbb));
    m_breakLog << vfmt(" pcbb=%016llx",         static_cast<unsigned long long>(s.pcbb));
    m_breakLog << vfmt(" ksp=%016llx",          static_cast<unsigned long long>(s.ksp));
    m_breakLog << vfmt(" esp=%016llx",          static_cast<unsigned long long>(s.esp));
    m_breakLog << vfmt(" ssp=%016llx",          static_cast<unsigned long long>(s.ssp));
    m_breakLog << vfmt(" usp=%016llx",          static_cast<unsigned long long>(s.usp));
    m_breakLog << vfmt(" fen=%016llx",          static_cast<unsigned long long>(s.fen));
    m_breakLog << vfmt(" asten_sr=%016llx",     static_cast<unsigned long long>(s.asten_sr));
    m_breakLog << vfmt(" reservedCacheLine=%016llx",
                       static_cast<unsigned long long>(s.reservedCacheLine));
    m_breakLog << vfmt(" hasReservation=%d",    s.hasReservation ? 1 : 0);
    m_breakLog << vfmt(" halted=%d",            s.halted ? 1 : 0);
    m_breakLog << vfmt(" unalignTrapEnabled=%d", s.unalignTrapEnabled ? 1 : 0);
    m_breakLog << vfmt(" lastFaultCode=%04x",   static_cast<unsigned>(s.lastFaultCode));
    m_breakLog << '\n';

    // PT_SNAP: PAL_TEMP[0..31] in one record.  Values are 64-bit so the
    // line lands around 32*17 = ~544 chars; well within ofstream line
    // capacity and within any reasonable diff tool's column width.
    m_breakLog << vfmt("PT_SNAP rpcc=%llu kind=%s",
                       static_cast<unsigned long long>(cycle), kind);
    for (int i = 0; i < 32; ++i) {
        m_breakLog << vfmt(" PT%02d=%016llx",
                           i,
                           static_cast<unsigned long long>(s.palTemp[i]));
    }
    m_breakLog << '\n';

    // CBX_SNAP: CBoxState shadow per coreLib/CBoxState.h.  Four fields:
    // writeMany (36-bit chain), errorReg (60-bit chain), dataReg (6-bit
    // visible C_DATA window), shftCtrl (W1 trigger shadow).  Crucial for
    // the sys__cbox investigation: the open-vs-close diff of these
    // fields reveals exactly what the shift loop pulled out of the
    // ERROR_REG chain on this revolution.
    m_breakLog << vfmt("CBX_SNAP rpcc=%llu kind=%s",
                       static_cast<unsigned long long>(cycle), kind);
    m_breakLog << vfmt(" writeMany=%016llx", static_cast<unsigned long long>(s.cBox.writeMany));
    m_breakLog << vfmt(" errorReg=%016llx",  static_cast<unsigned long long>(s.cBox.errorReg));
    m_breakLog << vfmt(" dataReg=%02x",      static_cast<unsigned>(s.cBox.dataReg));
    m_breakLog << vfmt(" shftCtrl=%02x",     static_cast<unsigned>(s.cBox.shftCtrl));
    m_breakLog << '\n';

    m_breakLog.flush();
}


// ============================================================================
// emitBrkRecord -- per-retire full-complement record
// ============================================================================
// Emitted only while the gate is open.  Always carries the full GPR
// complement R00..R30 (R31 elided -- architecturally zero).  FPR block
// is emitted only when at least one F-register is non-zero; the boot/SRM
// path is FP-free by design, so the typical line stays ~640 bytes
// instead of expanding to ~1280.  Memory-effect suffix (ea/val) on
// HW_LD/HW_ST.  IPR-touch suffix (iprid/iprval) on HW_MFPR/HW_MTPR.
// ============================================================================

void BreakpointSink::emitBrkRecord(CommitRecord const&        record,
                                   coreLib::CpuState const&   s)
{
    if (!m_breakOpen) return;

    char const* const mnem = record.mnemonic ? record.mnemonic : "?";

    // Header.  Same field shape as the retire-compact RET line so a
    // reader familiar with _srm.trc reads BRK at a glance.  The encoded=
    // field is added inline because HW_LD/HW_ST/HW_MFPR/HW_MTPR all share
    // opcode 0x1E and the sub-function lives in bits 15:12 of the encoded
    // word -- without this field the trace cannot disambiguate which
    // variant retired.
    m_breakLog << vfmt("BRK rpcc=%llu pc=%016llx encoded=%08x %-8s "
                       "pal=%d exc=%016llx",
                       static_cast<unsigned long long>(record.cycle),
                       static_cast<unsigned long long>(record.pc),
                       record.encoded,
                       mnem,
                       s.inPalMode() ? 1 : 0,
                       static_cast<unsigned long long>(s.excAddr));

    // Full GPR block.  R00..R30 unconditionally.  R31 omitted (zero).
    for (int i = 0; i < 31; ++i) {
        m_breakLog << vfmt(" R%02d=%016llx",
                           i,
                           static_cast<unsigned long long>(s.intReg[i]));
    }

    // FPR block.  Probe first; if every F-register is zero, omit the
    // entire block.  Saves ~640 bytes per line on the FP-free SRM path.
    bool anyFp = false;
    for (int i = 0; i < 31; ++i) {
        if (s.fpReg[i] != 0) { anyFp = true; break; }
    }
    if (anyFp) {
        for (int i = 0; i < 31; ++i) {
            m_breakLog << vfmt(" F%02d=%016llx",
                               i,
                               static_cast<unsigned long long>(s.fpReg[i]));
        }
    }

    // Memory-effect suffix.  ea is the effective address; val is the
    // loaded value for HW_LD (from the regfile write side) or the
    // stored value for HW_ST (from BoxResult::memData).  Both come from
    // the BoxResult attached to the CommitRecord.
    if (record.result != nullptr) {
        coreLib::BoxResult const& r = *record.result;
        if (isMemLoad(mnem) && r.memSize != coreLib::kNoMemEffect) {
            m_breakLog << vfmt(" ea=%016llx val=%016llx",
                               static_cast<unsigned long long>(r.memAddr),
                               static_cast<unsigned long long>(r.regWriteValue));
        } else if (isMemStore(mnem) && r.memSize != coreLib::kNoMemEffect) {
            m_breakLog << vfmt(" ea=%016llx val=%016llx",
                               static_cast<unsigned long long>(r.memAddr),
                               static_cast<unsigned long long>(r.memData));
        }
    }

    // IPR-touch suffix.  iprid is the raw selector field from the
    // encoded instruction word at bits [15:8] -- the EV6 HW_MFPR/HW_MTPR
    // scbd index.  For HW_MFPR, iprval is the value read into the
    // destination GPR (regWriteValue on the BoxResult).  For HW_MTPR
    // there is no register write; iprval is omitted -- the IPR's new
    // value is observable in the next gate-close IPR_SNAP.
    if (isMfpr(mnem) || isMtpr(mnem)) {
        uint32_t const scbd =
            static_cast<uint32_t>((record.encoded >> 8) & 0xFFu);
        m_breakLog << vfmt(" iprid=%02x", scbd);
        if (isMfpr(mnem) && record.result != nullptr) {
            m_breakLog << vfmt(" iprval=%016llx",
                               static_cast<unsigned long long>(record.result->regWriteValue));
        }
    }

    m_breakLog << '\n';
    m_breakLog.flush();
}


// ============================================================================
// onCommit -- hot path
// ============================================================================
// Forwards to downstream first so the existing trace channel (e.g.,
// DecListingSink) is unperturbed.  Then evaluates the gates:
//
//   * If the gate is closed and PC equals s_gateOpenPc, transition to
//     open: lazy-open the break file, emit BP_OPEN + IPR/PT/CBX snapshots,
//     and start per-retire emission FROM THE NEXT retire (the open-gate
//     retire itself is the boundary, captured in the snapshot, not in
//     the per-retire stream).
//
//   * If the gate is open and PC equals s_gateClosePc, transition to
//     closed: emit BP_CLOSE + IPR/PT/CBX snapshots, increment
//     s_revolutionsCaptured, decrement s_revolutionsRemaining.  When
//     the remaining count reaches zero the sink stops reacting to
//     subsequent matches.
//
//   * If the gate is open AND the PC is neither boundary, emit a BRK
//     record.
//
// All three transitions are non-blocking and self-contained; if the
// break file failed to open the snapshot emit calls become no-ops.
// ============================================================================

void BreakpointSink::onCommit(CommitRecord const&        record,
                              coreLib::CpuState const&   postCommitCpu)
{
    if (m_downstream != nullptr) {
        m_downstream->onCommit(record, postCommitCpu);
    }
    // Checkpoint ledger runs independently of the gate revolution budget
    // (processCommit early-returns once revolutions are exhausted), so it
    // is tested here rather than inside processCommit.
    recordCheckpoint(record, postCommitCpu);
    (void)processCommit(record, postCommitCpu);
}


// ============================================================================
// recordCheckpoint -- per-retire tripwire test for the checkpoint ledger.
// Orthogonal to the open/close gate.  First hit of each armed PC snapshots
// the GPR file + cycleCount and emits a CKPT record; every hit updates the
// tally.  See the header's checkpoint-ledger comment for the rationale.
// ============================================================================

void BreakpointSink::recordCheckpoint(CommitRecord const&        record,
                                      coreLib::CpuState const&   postCommitCpu)
{
    for (int i = 0; i < kMaxCheckpoints; ++i) {
        uint64_t const cpc = s_checkpointPc[i].load(std::memory_order_relaxed);
        if (cpc == 0 || record.pc != cpc) {
            continue;
        }
        CheckpointLedger& e = m_ckpt[i];
        e.lastCycle = record.cycle;
        ++e.hitCount;
        if (e.hit) {
            continue;   // snapshot + CKPT record only on first hit
        }
        e.hit        = true;
        e.firstCycle = record.cycle;
        for (int r = 0; r < 31; ++r) {
            e.snapInt[r] = postCommitCpu.intReg[r];
        }
        e.snapCc = postCommitCpu.cycleCount;

        char const* const lbl =
            (s_checkpointLabel[i][0] != '\0') ? s_checkpointLabel[i] : "-";

        ensureBreakOpen();
        if (m_breakOpen) {
            m_breakLog << vfmt(
                "CKPT idx=%d label=%s first_cyc=%llu pc=%016llx "
                "r0=%016llx r16=%016llx r26=%016llx\n",
                i, lbl,
                static_cast<unsigned long long>(record.cycle),
                static_cast<unsigned long long>(record.pc),
                static_cast<unsigned long long>(e.snapInt[0]),
                static_cast<unsigned long long>(e.snapInt[16]),
                static_cast<unsigned long long>(e.snapInt[26]));
            m_breakLog.flush();
        }

        std::fprintf(stderr,
            "CKPT: reached [%d] %s first time cyc=%llu pc=%016llx r0=%016llx\n",
            i, lbl,
            static_cast<unsigned long long>(record.cycle),
            static_cast<unsigned long long>(record.pc),
            static_cast<unsigned long long>(e.snapInt[0]));
        std::fflush(stderr);
    }
}


// ============================================================================
// emitCheckpointSummary -- end-of-run report for the checkpoint ledger.
// Prints, to the break file and to stderr, every armed slot's hit/first/
// last/count + R0, and the decisive "LAST checkpoint reached" line (the
// slot with the largest first-hit cycle).  No-op when nothing is armed.
// ============================================================================

void BreakpointSink::emitCheckpointSummary(coreLib::CpuState const& finalCpu)
{
    (void)finalCpu;

    bool anyArmed = false;
    for (int i = 0; i < kMaxCheckpoints; ++i) {
        if (s_checkpointPc[i].load(std::memory_order_relaxed) != 0) {
            anyArmed = true;
            break;
        }
    }
    if (!anyArmed) {
        return;
    }

    // Last-reached = the hit slot with the greatest first-hit cycle.
    int      lastIdx = -1;
    uint64_t lastCyc = 0;
    for (int i = 0; i < kMaxCheckpoints; ++i) {
        if (m_ckpt[i].hit && m_ckpt[i].firstCycle >= lastCyc) {
            lastCyc = m_ckpt[i].firstCycle;
            lastIdx = i;
        }
    }

    ensureBreakOpen();
    if (m_breakOpen) {
        m_breakLog << "CKPT_SUMMARY\n";
        for (int i = 0; i < kMaxCheckpoints; ++i) {
            uint64_t const cpc = s_checkpointPc[i].load(std::memory_order_relaxed);
            if (cpc == 0) {
                continue;
            }
            CheckpointLedger const& e = m_ckpt[i];
            m_breakLog << vfmt(
                "  ckpt[%d] %s pc=%016llx hit=%d first_cyc=%llu "
                "last_cyc=%llu count=%llu r0=%016llx\n",
                i, (s_checkpointLabel[i][0] != '\0') ? s_checkpointLabel[i] : "-",
                static_cast<unsigned long long>(cpc),
                e.hit ? 1 : 0,
                static_cast<unsigned long long>(e.firstCycle),
                static_cast<unsigned long long>(e.lastCycle),
                static_cast<unsigned long long>(e.hitCount),
                static_cast<unsigned long long>(e.snapInt[0]));
        }
        m_breakLog << vfmt("  LAST_REACHED idx=%d label=%s first_cyc=%llu\n",
            lastIdx,
            (lastIdx >= 0 && s_checkpointLabel[lastIdx][0] != '\0')
                ? s_checkpointLabel[lastIdx] : "-",
            static_cast<unsigned long long>(lastCyc));
        m_breakLog.flush();
    }

    std::fprintf(stderr, "CKPT_SUMMARY (decisive):\n");
    for (int i = 0; i < kMaxCheckpoints; ++i) {
        uint64_t const cpc = s_checkpointPc[i].load(std::memory_order_relaxed);
        if (cpc == 0) {
            continue;
        }
        CheckpointLedger const& e = m_ckpt[i];
        std::fprintf(stderr,
            "  [%d] %-18s pc=%016llx %s first_cyc=%llu count=%llu r0=%016llx\n",
            i, (s_checkpointLabel[i][0] != '\0') ? s_checkpointLabel[i] : "-",
            static_cast<unsigned long long>(cpc),
            e.hit ? "REACHED    " : "NOT-REACHED",
            static_cast<unsigned long long>(e.firstCycle),
            static_cast<unsigned long long>(e.hitCount),
            static_cast<unsigned long long>(e.snapInt[0]));
    }
    if (lastIdx >= 0) {
        std::fprintf(stderr,
            "  >>> LAST checkpoint reached: [%d] %s (cyc=%llu)\n",
            lastIdx,
            (s_checkpointLabel[lastIdx][0] != '\0')
                ? s_checkpointLabel[lastIdx] : "-",
            static_cast<unsigned long long>(lastCyc));
    } else {
        std::fprintf(stderr, "  >>> NO checkpoint reached.\n");
    }
    std::fflush(stderr);
}


bool BreakpointSink::processCommit(CommitRecord const&        record,
                                   coreLib::CpuState const&   postCommitCpu)
{
    int32_t const revsRem = s_revolutionsRemaining.load(std::memory_order_relaxed);
    if (revsRem <= 0) {
        return false;   // budget exhausted; sink quiet for the rest of run
    }

    uint64_t const openPc  = s_gateOpenPc.load(std::memory_order_relaxed);
    uint64_t const closePc = s_gateClosePc.load(std::memory_order_relaxed);
    bool     const isOpen  = s_gateOpen.load(std::memory_order_relaxed);

    // Gate-open transition.
    if (!isOpen && openPc != 0 && record.pc == openPc) {
        ensureBreakOpen();
        uint32_t const rev = s_revolutionsCaptured.load(std::memory_order_relaxed);
        emitGateSnapshot(record.cycle, record.pc, rev, "open", postCommitCpu);
        s_gateOpen.store(true, std::memory_order_release);

        // Optional debugger trap.  Fires AFTER the snapshot triplet is
        // on disk, so killing the process at the JIT-debugger prompt
        // does not lose capture data.  Stderr line before the trap so
        // an operator running without a debugger sees what just
        // happened before the OS pops the JIT prompt.
        if (s_breakOnGateOpen.load(std::memory_order_relaxed)) {
            std::fprintf(stderr,
                         "BreakpointSink: __debugbreak at gate-open "
                         "cyc=%llu pc=%016llx rev=%u (set "
                         "s_breakOnGateOpen=false to disable)\n",
                         static_cast<unsigned long long>(record.cycle),
                         static_cast<unsigned long long>(record.pc),
                         rev);
            std::fflush(stderr);
            BREAKPOINT_SINK_DEBUGBREAK();
        }

        // The boundary retire itself is captured in the snapshot; do
        // not also emit a BRK record for it.
        return false;
    }

    // Gate-close transition.
    if (isOpen && closePc != 0 && record.pc == closePc) {
        uint32_t const rev =
            s_revolutionsCaptured.fetch_add(1, std::memory_order_acq_rel);
        emitGateSnapshot(record.cycle, record.pc, rev + 1, "close", postCommitCpu);
        s_gateOpen.store(false, std::memory_order_release);
        s_revolutionsRemaining.fetch_sub(1, std::memory_order_acq_rel);

        // 2026-06-01: loud stderr progress + completion marker so the operator
        // knows when capture is done and it is SAFE TO STOP the run.  The trace
        // file carries no in-band "done" signal during the run, so without this
        // you are guessing when to abort (and can stop mid-revolution).
        {
            uint32_t const capNow = rev + 1;
            int32_t  const remNow =
                s_revolutionsRemaining.load(std::memory_order_relaxed);
            if (remNow > 0) {
                std::fprintf(stderr,
                    "BRKSINK: revolution %u captured, %d remaining "
                    "(cyc=%llu) -- keep running\n",
                    capNow, remNow,
                    static_cast<unsigned long long>(record.cycle));
            } else {
                std::fprintf(stderr,
                    "BRKSINK: COMPLETE -- %u/%u revolutions captured "
                    "(cyc=%llu); trace closed, SAFE TO STOP.\n",
                    capNow, capNow,
                    static_cast<unsigned long long>(record.cycle));
            }
            std::fflush(stderr);
        }

        // Optional debugger trap at the close transition.  Same posture
        // as the open trap above.
        if (s_breakOnGateClose.load(std::memory_order_relaxed)) {
            std::fprintf(stderr,
                         "BreakpointSink: __debugbreak at gate-close "
                         "cyc=%llu pc=%016llx rev=%u (set "
                         "s_breakOnGateClose=false to disable)\n",
                         static_cast<unsigned long long>(record.cycle),
                         static_cast<unsigned long long>(record.pc),
                         rev + 1);
            std::fflush(stderr);
            BREAKPOINT_SINK_DEBUGBREAK();
        }

        return false;
    }

    // Mid-window retire.
    if (isOpen) {
        emitBrkRecord(record, postCommitCpu);
        return true;
    }

    return false;
}


// ============================================================================
// onPalEntry / onPalExit -- forwarding only
// ============================================================================
// The gate logic is keyed on retire-PC, not on PAL transitions, so these
// hooks just forward to downstream.  The downstream sink (DecListingSink
// in the canonical chain) consumes them for its PAL-window machinery.
// ============================================================================

void BreakpointSink::onPalEntry(uint64_t cycle,
                                uint64_t entryPc,
                                uint64_t excAddr)
{
    if (m_downstream != nullptr) {
        m_downstream->onPalEntry(cycle, entryPc, excAddr);
    }
}


void BreakpointSink::onPalExit(uint64_t cycle,
                               uint64_t targetPc)
{
    if (m_downstream != nullptr) {
        m_downstream->onPalExit(cycle, targetPc);
    }
}


// ============================================================================
// setEmitEnabled -- Phase C+ external trace-emit gate forwarding.
// ============================================================================
//
// BreakpointSink is a chain wrap; the gate target is the wrapped sink
// (DecListingSink in the canonical wiring).  We do nothing locally --
// BreakpointSink's own emission is already gated by its paired-PC
// machinery and never fires pre-relocation by construction.  Mirrors
// the forward-only pattern used by onPalEntry / onPalExit above.
//
// Without this forward, Machine's setEmitEnabled(true) call at PAL
// relocation lands on BreakpointSink (the outer sink Machine holds) and
// hits the base TraceSink no-op default -- DecListingSink's emit-gate
// stays closed for the entire run, no per-retire records are written
// to the .trc retire-compact stream, and the cold-boot artifact shows
// a header-only trace file.  Diagnosed 2026-05-18 morning from the
// 353-byte 20260518-115653_srm.trc.
// ============================================================================
void BreakpointSink::setEmitEnabled(bool enabled) noexcept
{
    if (m_downstream != nullptr) {
        m_downstream->setEmitEnabled(enabled);
    }
}


// ============================================================================
// onRunEnd -- forward, flush, mark
// ============================================================================
// If the break file was ever opened, emit a RUN_END marker so a
// post-mortem reader can distinguish "capture terminated cleanly" from
// "file truncated mid-revolution by a crash".  Then forward to
// downstream and close our own stream.
// ============================================================================

void BreakpointSink::onRunEnd(coreLib::CpuState const& finalCpu)
{
    if (m_downstream != nullptr) {
        m_downstream->onRunEnd(finalCpu);
    }

    // Checkpoint ledger report (file + decisive stderr).  Done before the
    // RUN_END marker so, if a checkpoint hit opened the file but no gate
    // ever fired, the summary is still written before the stream closes.
    emitCheckpointSummary(finalCpu);

    if (m_breakOpen) {
        uint32_t const captured =
            s_revolutionsCaptured.load(std::memory_order_relaxed);
        int32_t  const remaining =
            s_revolutionsRemaining.load(std::memory_order_relaxed);
        bool     const stillOpen =
            s_gateOpen.load(std::memory_order_relaxed);

        m_breakLog << vfmt(
            "RUN_END captured=%u remaining=%d gate_still_open=%d "
            "final_pc=%016llx final_cycleCount=%016llx final_halted=%d\n",
            captured,
            remaining,
            stillOpen ? 1 : 0,
            static_cast<unsigned long long>(finalCpu.pc),
            static_cast<unsigned long long>(finalCpu.cycleCount),
            finalCpu.halted ? 1 : 0);
        m_breakLog.flush();
        m_breakLog.close();
        m_breakOpen = false;

        // 2026-06-01: mirror the disposition to stderr so an operator who ran
        // to maxCycles knows whether the capture is whole or was cut off with
        // the gate still open (the partial-revolution case).
        if (stillOpen || remaining > 0) {
            std::fprintf(stderr,
                "BRKSINK: run ended PARTIAL -- captured=%u remaining=%d "
                "gate_still_open=%d (revolution incomplete; raise --max-cycles "
                "or widen the gate)\n",
                captured, remaining, stillOpen ? 1 : 0);
        } else {
            std::fprintf(stderr,
                "BRKSINK: run ended -- capture COMPLETE, captured=%u\n",
                captured);
        }
        std::fflush(stderr);
    }
}


// ============================================================================
// composeStamp -- static accessor used by tests and external tooling
// ============================================================================

std::string BreakpointSink::composeStamp()
{
    return composeStampNow();
}

} // namespace traceLib
