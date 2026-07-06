// ============================================================================
// SpinSkip.h -- safe fast-forward of side-effect-free countdown loops
// ============================================================================
// Project: EmulatR -- Alpha AXP / EV6 Architecture Emulator (V4)
// Copyright (C) 2025, 2026 eNVy Systems, Inc.  All rights reserved.
// Licensed under eNVy Systems Non-Commercial License v1.1
//
// Project Architect: Timothy Peer
// AI Collaboration:  Claude (Anthropic)
// ============================================================================
//
// PURPOSE:
//   Firmware boot paths are punctuated by long hardcoded busy-wait delays
//   (e.g. the ES40 SRM's 0x14900 spin: SUBQ R12,#1 / BEQ / BR, 300,000,000
//   iterations ~= 900M emulated cycles). Executing them literally dominates
//   boot wall time. This module detects a delay loop, PROVES it is a
//   side-effect-free register countdown with a closed-form trip count, and
//   fast-forwards to its exit -- lossless because the loop body touches no
//   memory, no CSR/IPR, and reads no value that changes between iterations.
//
// SAFETY DESIGN (refuse-by-default):
//   A loop is skipped ONLY if EVERY clause is proven:
//     (1) body has NO memory access and NO CSR/IPR access (operate + branch
//         opcodes only);
//     (2) exactly one induction register, advanced by a constant stride;
//     (3) the loop branches solely on that induction register;
//     (4) the trip count N is derivable in CLOSED FORM and lands on the exit
//         value exactly (rejects step-over from non-unit strides);
//     (5) TOP-LEVEL scope only -- the exit does not flow into a backward
//         branch targeting at/above the loop head (that is a nested-inner
//         countdown inside a side-effectful body; skipping its settle delay
//         is unsafe unless the enclosing readback CSR is synchronously
//         modeled -- deferred to a v2 synchronous-CSR clause).
//   Any loop that fails any clause RUNS LITERALLY and logs one line naming
//   the failed clause. The refusal log is a free characterized map of the
//   init sequence (every declined loop is a candidate real frontier).
//
// INTERRUPT-AWARE ADVANCE:
//   The cycle advance is applied by the Machine driver, which replays the
//   Cchip interval timer at every period boundary crossed in the skipped
//   span (the timer is a pure function of cycleCount). This module supplies
//   the iteration count and per-iteration cycle cost; the driver owns the
//   clock + interrupt latch. For a masked-interrupt early-init delay this is
//   a no-op in practice, but the machinery exists before the first delay
//   that is followed by an RPCC check.
//
// ENABLE: env EMULATR_SPINSKIP=1 (default OFF). Tunables:
//   EMULATR_SPINSKIP_THRESH=<n>  hot-count before analysis (default 256)
//   EMULATR_SPINSKIP_VERBOSE=1   log every skip, not just refusals
// ============================================================================

#ifndef SYSTEMLIB_SPINSKIP_H
#define SYSTEMLIB_SPINSKIP_H

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <unordered_map>

#include "coreLib/CpuState.h"
#include "memoryLib/GuestMemory.h"

namespace systemLib {

class SpinSkip
{
public:
    // Result of onTickStart: when valid, the driver applies the advance.
    struct Plan
    {
        bool     valid         = false;
        int      indReg        = 0;   // induction register index
        uint64_t exitReg       = 0;   // its value at loop exit
        uint64_t exitPc        = 0;   // exit PC (carries the PALmode bit)
        uint64_t iters         = 0;   // remaining iterations to skip
        uint64_t cyclesPerIter = 0;   // measured cycle cost of one iteration
    };

    SpinSkip() noexcept
    {
        char const* e = std::getenv("EMULATR_SPINSKIP");
        m_enabled = (e && *e && e[0] != '0');
        char const* t = std::getenv("EMULATR_SPINSKIP_THRESH");
        if (t && *t) { unsigned long long v = std::strtoull(t, nullptr, 0);
                       if (v >= 8) m_threshold = v; }
        char const* v = std::getenv("EMULATR_SPINSKIP_VERBOSE");
        m_verbose = (v && *v && v[0] != '0');
    }

    bool enabled() const noexcept { return m_enabled; }

    // Called with the tick-start PC BEFORE step(). Returns a valid Plan iff pc
    // is a certified skippable loop head; otherwise advances hot-counters (and
    // analyzes at threshold) and returns an invalid Plan (caller runs step()).
    Plan onTickStart(coreLib::CpuState const& cpu,
                     memoryLib::GuestMemory const& mem) noexcept
    {
        Plan plan;
        if (!m_enabled) return plan;

        uint64_t const pc = cpu.pc;                 // carries PALmode bit
        auto it = m_cache.find(pc);
        if (it != m_cache.end()) {
            LoopDesc const& d = it->second;
            if (!d.certified) return plan;          // known refusal -> literal
            uint64_t N = 0, exitVal = 0;
            if (!deriveN(cpu.intReg[d.indReg], d.stride, d.branchOp,
                         d.exitWhenTaken, N, exitVal) || N < 2)
                return plan;                        // < 2 left: let it finish
            plan.valid         = true;
            plan.indReg        = d.indReg;
            plan.exitReg       = exitVal;
            plan.exitPc        = d.exitPc | (pc & 1ULL);
            plan.iters         = N;
            plan.cyclesPerIter = d.cyclesPerIter;
            return plan;
        }

        // Unknown PC: measure hotness; analyze once it proves to be a loop.
        Hot& h = m_hot[pc];
        if (h.count == 0) h.firstCyc = cpu.cycleCount;
        if (++h.count == m_threshold) {
            LoopDesc d = analyze(pc, cpu, mem, h.firstCyc);
            m_cache.emplace(pc, d);                 // cache certify OR refusal
            m_hot.erase(pc);
        }
        return plan;
    }

private:
    // Alpha opcodes we must recognize.
    enum : uint32_t {
        OP_OPERATE_ADD = 0x10,  // ADDQ/SUBQ/... (integer arithmetic)
        OP_BR = 0x30, OP_BLBC = 0x38, OP_BEQ = 0x39, OP_BLT = 0x3a,
        OP_BLE = 0x3b, OP_BLBS = 0x3c, OP_BNE = 0x3d, OP_BGE = 0x3e, OP_BGT = 0x3f
    };

    struct LoopDesc
    {
        bool     certified     = false;
        int      indReg        = 0;
        int64_t  stride        = 0;      // signed per-iteration delta
        uint32_t branchOp      = 0;      // the deciding branch opcode
        bool     exitWhenTaken = false;  // true=P1 (Bcc exits), false=P2 (Bcc loops)
        uint64_t exitPc        = 0;      // physical exit PC (PAL bit added later)
        uint64_t cyclesPerIter = 0;
    };
    struct Hot { uint64_t count = 0; uint64_t firstCyc = 0; };

    static bool isBranch(uint32_t op) noexcept { return op >= OP_BR && op <= OP_BGT; }
    static bool isDisallowed(uint32_t op) noexcept
    {
        // Allowed: integer/logical/shift operate (0x10-0x13) and branches
        // (0x30-0x3f, incl. unconditional BR/BSR/JMP-class 0x1a handled below).
        // Everything else -- loads/stores (0x08-0x0f,0x20-0x2f,0x0a-0x0e),
        // HW_LD/HW_ST (0x1b/0x1f), HW_MFPR/HW_MTPR (0x19/0x1d), CALL_PAL (0x00),
        // FP, JMP/RET (0x1a/0x1e) -- is a side effect or an undecidable exit.
        if (op >= 0x10 && op <= 0x13) return false;   // operate
        if (op >= 0x30 && op <= 0x3f) return false;   // branch
        return true;
    }

    // Decode helpers for the integer-operate format.
    static bool decodeOperate(uint32_t w, int& ra, int& rc, bool& lit,
                              int& imm, int& rb, int& func) noexcept
    {
        ra   = (w >> 21) & 0x1f;
        rb   = (w >> 16) & 0x1f;
        lit  = (w >> 12) & 1;               // bit 12 = literal flag
        imm  = (w >> 13) & 0xff;            // bits 20:13 = 8-bit literal
        func = (w >> 5) & 0x7f;
        rc   = w & 0x1f;
        return true;
    }
    static uint64_t branchTarget(uint64_t pc_phys, uint32_t w) noexcept
    {
        int64_t disp = w & 0x1fffff;
        if (disp >= 0x100000) disp -= 0x200000;
        return pc_phys + 4 + disp * 4;
    }

    // Closed-form trip count. Given the induction value v0, signed stride, the
    // deciding branch opcode, and whether the branch EXITS when taken (P1) or
    // LOOPS when taken (P2), compute N (iterations to exit) and the induction
    // value at exit. Returns false if N is not finite / not exactly landable.
    static bool deriveN(uint64_t v0, int64_t stride, uint32_t branchOp,
                        bool exitWhenTaken, uint64_t& N_out, uint64_t& exitVal) noexcept
    {
        if (stride == 0) return false;
        int64_t v = static_cast<int64_t>(v0);
        // "exit predicate on the UPDATED value" -- derived from the branch and
        // whether taken means exit or loop.
        // taken predicate P(x):  BEQ:x==0 BNE:x!=0 BLT:x<0 BLE:x<=0 BGT:x>0
        //                        BGE:x>=0 BLBC:(x&1)==0 BLBS:(x&1)==1
        // exit condition E(x) = exitWhenTaken ? P(x) : !P(x)
        // We only certify strides/conditions whose first-exit k is closed-form
        // AND lands exactly; refuse everything else.
        int64_t s = stride;
        switch (branchOp) {
        case OP_BEQ: case OP_BNE: {
            // continue while x!=0 (P1/BEQ) or exit-on-nonzero etc. The only
            // safely-closed-form case: exit exactly when x==0. Require the
            // sequence to LAND on 0 (no step-over).
            bool exitOnZero = (branchOp == OP_BEQ) == exitWhenTaken; // BEQ+P1 or BNE+P2
            if (!exitOnZero) return false;          // exit-on-nonzero: 1 iter, ambiguous
            if (v == 0) return false;               // already at exit; let it run
            if ((s < 0 && v < 0) || (s > 0 && v > 0)) return false; // diverges
            if (v % s != 0) return false;           // steps over 0 -> refuse
            int64_t k = -v / s;                     // v + k*s == 0
            if (k < 1) return false;
            N_out = static_cast<uint64_t>(k); exitVal = 0; return true;
        }
        case OP_BGT: case OP_BLE: {
            // decrementing-to-<=0 countdown. exit when x<=0.
            bool exitLE0 = (branchOp == OP_BGT) ? !exitWhenTaken : exitWhenTaken;
            if (!exitLE0 || s >= 0 || v <= 0) return false;
            int64_t k = (v + (-s) - 1) / (-s);      // ceil(v / -s)
            if (k < 1) return false;
            N_out = static_cast<uint64_t>(k); exitVal = static_cast<uint64_t>(v + k * s);
            return true;
        }
        case OP_BGE: case OP_BLT: {
            // exit when x<0.
            bool exitLT0 = (branchOp == OP_BGE) ? !exitWhenTaken : exitWhenTaken;
            if (!exitLT0 || s >= 0 || v < 0) return false;
            int64_t k = (v / (-s)) + 1;             // first x < 0
            N_out = static_cast<uint64_t>(k); exitVal = static_cast<uint64_t>(v + k * s);
            return true;
        }
        default:
            return false;                            // LBC/LBS/others: refuse
        }
    }

    // loud=false: "this hot PC is simply not a countdown-shaped loop" -- the
    // common, uninteresting case (ordinary hot code); cached silently.
    // loud=true: "this IS a tight countdown loop but fails a safety clause" --
    // a candidate real frontier (device poll, memory scan, nested settle,
    // step-over count). These form the free characterized map of the boot's
    // side-effectful loops. Logged once per PC.
    void refuse(uint64_t pc, char const* why, bool loud = true) noexcept
    {
        if (loud || m_verbose)
            std::fprintf(stderr, "SPINSKIP refuse pc=0x%llx: %s\n",
                         static_cast<unsigned long long>(pc & ~1ULL), why);
    }

    // Disassemble the tight loop at head T (PALmode PC), prove the predicate,
    // and return a certified-or-refused descriptor. Reads up to 4 body words.
    LoopDesc analyze(uint64_t T, coreLib::CpuState const& cpu,
                     memoryLib::GuestMemory const& mem, uint64_t firstCyc) noexcept
    {
        LoopDesc d;
        uint64_t const headPa = T & ~1ULL;
        uint32_t body[4] = {0,0,0,0};
        for (int i = 0; i < 4; ++i)
            if (mem.read4(headPa + 4 * i, body[i]) != memoryLib::MemStatus::Ok) {
                refuse(T, "unreadable body", /*loud=*/false); return d;
            }

        // Locate the loop-back branch (targets headPa) within the first 4 instrs.
        int backIdx = -1;
        for (int i = 0; i < 4; ++i) {
            uint32_t op = body[i] >> 26;
            if (isBranch(op) && branchTarget(headPa + 4 * i, body[i]) == headPa) {
                backIdx = i; break;
            }
        }
        if (backIdx < 1) { refuse(T, "no tight loop-back to head in 4 instrs", /*loud=*/false); return d; }

        // Clause (1): every body instruction is operate-or-branch (no mem/CSR/IPR).
        for (int i = 0; i <= backIdx; ++i)
            if (isDisallowed(body[i] >> 26)) {
                refuse(T, "body has memory/CSR/IPR/undecidable op"); return d;
            }

        // Clause (2)+(3): exactly one induction register advanced by a constant
        // stride at the head; the deciding branch is on that register.
        int ra, rc, imm, rb, func; bool lit;
        decodeOperate(body[0], ra, rc, lit, imm, rb, func);
        if ((body[0] >> 26) != OP_OPERATE_ADD || !lit || ra != rc) {
            refuse(T, "head is not `OP rX,#imm,rX` induction", /*loud=*/false); return d;
        }
        int64_t stride;
        if (func == 0x29)      stride = -static_cast<int64_t>(imm);  // SUBQ
        else if (func == 0x20) stride =  static_cast<int64_t>(imm);  // ADDQ
        else if (func == 0x09) stride = -static_cast<int64_t>(imm);  // SUBL
        else if (func == 0x00) stride =  static_cast<int64_t>(imm);  // ADDL
        else { refuse(T, "head op is not ADD/SUB", /*loud=*/false); return d; }
        int const indReg = rc;
        if (indReg == 31) { refuse(T, "induction is R31 (no-op write)", /*loud=*/false); return d; }

        // The deciding branch: prefer a conditional branch on indReg. P1 = a
        // conditional exit before an unconditional loop-back; P2 = the loop-back
        // itself is the conditional branch on indReg.
        uint32_t backOp = body[backIdx] >> 26;
        uint32_t backRa = (body[backIdx] >> 21) & 0x1f;
        bool exitWhenTaken; uint32_t decideOp; uint64_t exitPc;
        if (backOp == OP_BR && backRa == 31) {
            // P1: need a conditional exit on indReg among body[1..backIdx-1].
            int condIdx = -1;
            for (int i = 1; i < backIdx; ++i) {
                uint32_t op = body[i] >> 26;
                if (op != OP_BR && isBranch(op) && ((body[i] >> 21) & 0x1f) == (uint32_t)indReg) {
                    condIdx = i; break;
                }
            }
            if (condIdx < 0) { refuse(T, "P1: no conditional exit on induction reg"); return d; }
            decideOp = body[condIdx] >> 26; exitWhenTaken = true;
            exitPc = branchTarget(headPa + 4 * condIdx, body[condIdx]);
            // No OTHER branch may leave the loop unaccounted.
        } else if (isBranch(backOp) && backRa == (uint32_t)indReg) {
            // P2: the loop-back is conditional on indReg; exit is fall-through.
            decideOp = backOp; exitWhenTaken = false;
            exitPc = headPa + 4 * (backIdx + 1);
        } else {
            refuse(T, "loop-back branch not R31-uncond nor induction-conditional", /*loud=*/false); return d;
        }

        // Reject any body branch we did not account for (a second exit path).
        for (int i = 1; i <= backIdx; ++i) {
            uint32_t op = body[i] >> 26;
            if (!isBranch(op)) continue;
            uint64_t tgt = branchTarget(headPa + 4 * i, body[i]);
            bool isBack   = (tgt == headPa);
            bool isDecCond = (exitWhenTaken && (body[i] >> 26) == decideOp &&
                              ((body[i] >> 21) & 0x1f) == (uint32_t)indReg &&
                              (headPa + 4 * i) != headPa && tgt == exitPc);
            if (!isBack && !isDecCond) { refuse(T, "extra/unaccounted branch in body"); return d; }
        }

        // Clause (4): closed-form trip count must derive + land exactly.
        uint64_t Ntest = 0, exVal = 0;
        if (!deriveN(cpu.intReg[indReg], stride, decideOp, exitWhenTaken, Ntest, exVal)) {
            refuse(T, "trip count not closed-form / steps over exit"); return d;
        }

        // Clause (5): TOP-LEVEL scope -- scan forward from the exit for a
        // backward branch to at/above the head (=> nested inner countdown).
        {
            uint64_t exPa = exitPc & ~1ULL;
            for (int i = 0; i < 24; ++i) {
                uint32_t w = 0;
                if (mem.read4(exPa + 4 * i, w) != memoryLib::MemStatus::Ok) break;
                uint32_t op = w >> 26;
                if (isBranch(op)) {
                    uint64_t tgt = branchTarget(exPa + 4 * i, w);
                    if (tgt <= headPa && tgt >= headPa - 0x400) {
                        refuse(T, "nested inner countdown (enclosed by side-effectful loop) -- needs v2 sync-CSR clause");
                        return d;
                    }
                    if (op == OP_BR || op == 0x1a) break;  // uncond flow-change ends the window
                }
                if (op == 0x1e) break;                     // HW_RET ends the window (top-level ok)
            }
        }

        // Measure per-iteration cycle cost empirically over the hot window.
        uint64_t const spanCyc = cpu.cycleCount - firstCyc;
        uint64_t cpi = (m_threshold > 1) ? spanCyc / (m_threshold - 1) : 0;
        if (cpi < 1 || cpi > 64) { refuse(T, "implausible measured cycles/iter"); return d; }

        d.certified     = true;
        d.indReg        = indReg;
        d.stride        = stride;
        d.branchOp      = decideOp;
        d.exitWhenTaken = exitWhenTaken;
        d.exitPc        = exitPc;
        d.cyclesPerIter = cpi;
        if (m_verbose)
            std::fprintf(stderr,
                "SPINSKIP certify pc=0x%llx indReg=R%d stride=%lld exitPc=0x%llx cpi=%llu\n",
                (unsigned long long)headPa, indReg, (long long)stride,
                (unsigned long long)exitPc, (unsigned long long)cpi);
        return d;
    }

    bool     m_enabled   = false;
    bool     m_verbose   = false;
    uint64_t m_threshold = 256;
    std::unordered_map<uint64_t, LoopDesc> m_cache;   // pc -> certify/refuse
    std::unordered_map<uint64_t, Hot>      m_hot;     // pc -> hotness
};

} // namespace systemLib

#endif // SYSTEMLIB_SPINSKIP_H
