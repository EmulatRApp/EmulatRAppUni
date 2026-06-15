// ============================================================================
// TsunamiTig.h -- DS10/Tsunami TIG-bus device register file.
// Project: EmulatR -- Alpha AXP / EV6 (21264) emulator (V4)
// ----------------------------------------------------------------------------
// The Tsunami Cchip mediates a byte-wide "TIG" (Timer / Interrupt / GPIO) bus
// to board-level glue logic.  The SRM addresses the TIG *devices* directly:
//   tsunami_io.c xtig():  PA = (TIG_BASE<<24) | (offset<<6)
//                            = 0x801_0000_0000 + (offset<<6)
// EV6_OSF_PC264_PAL.MAR documents the control bank at 0x801_3000_0000.  The
// device registers occupy the UPPER TIG region (control 0x801_3000_0000,
// arbiter/PLD 0x801_3800_0000); the low part (0x801_0000_0000) is the flash,
// decoded separately by TsunamiChipset.  (The Cchip's TTR/TDR timing regs --
// HRM 10.2.2.14/15 -- are the *interface* to this bus, modeled storage-only
// in TsunamiCchip; they are NOT these device registers.)
//
// ROOT CAUSE this models: EmulatR modeled NONE of these device registers, so
// the SRM's read of smir (TIG+0x40) fell through to the all-ones mmioRead
// default and the firmware read it as "Halt Button is IN" -> refused `boot`
// (HALTPROBE: TIG read pa=0x80130000040 v=0xffffffff).
//
// CLEAN-ROOM: register set + semantics are derived from DEC's own material
// (apisrm tsunami_io.c/pc264*.c intig/outtig sites, EV6_OSF_PC264_PAL.MAR,
// ev6_regatta_logout_def.h tig_smir).  AXPBox was a cross-check oracle ONLY;
// no AXPBox-only registers are modeled and no values are copied from it.
// ============================================================================
#ifndef TSUNAMI_TIG_H
#define TSUNAMI_TIG_H

#include <cstdint>
#include <cstdio>
#include <cstdlib>   // std::getenv (EMULATR_TIG_TRACE)

class TsunamiTig
{
public:
    // ---- PA window (device registers; flash is below, decoded elsewhere) ----
    static constexpr uint64_t kDevBase = 0x80130000000ULL;  // 0x801_3000_0000
    static constexpr uint64_t kDevEnd  = 0x80140000000ULL;  // 0x801_4000_0000 (excl)
    static constexpr bool decodes(uint64_t pa) noexcept {
        return pa >= kDevBase && pa < kDevEnd;
    }

    void reset() noexcept {
        m_halt[0] = m_halt[1] = 0;
        m_ipcr[0] = m_ipcr[1] = m_ipcr[2] = m_ipcr[3] = 0;
        m_arbCtrl = 0;
    }

    // Snapshot deferral guard (decision 3).  TIG state is NOT yet serialized
    // by Snapshot; the save path asserts this to PROVE the deferral is safe
    // (every R/W reg transient/0 through the captured cold path).  If it ever
    // returns false (e.g. arb_ctrl set by pc264_init outtig(0xE00004), or an
    // ipcr write), the deferral is wrong for that reg -> add TIG to Snapshot.
    bool isAtResetState() const noexcept {
        return m_halt[0] == 0 && m_halt[1] == 0
            && m_ipcr[0] == 0 && m_ipcr[1] == 0
            && m_ipcr[2] == 0 && m_ipcr[3] == 0
            && m_arbCtrl == 0;
    }

    uint64_t read(uint64_t pa) const noexcept {
        switch (pa) {
        // smir (TIG+0x40) -- System Management Interrupt Register; the
        // front-panel Halt / SMI status the SRM boot gate reads (HALTPROBE;
        // named tig_smir in ev6_regatta_logout_def.h).  STATUS-ONLY: always
        // reads 0 = "no SMI / no halt pending" (the normal running state).
        // No backing store ON PURPOSE -- a stored W1C value handed back would
        // re-assert "Halt Button is IN".  DEFERRED: front-panel-halt and
        // SMI/logout EVENT INJECTION (a real source setting a bit firmware
        // then polls/clears).
        case kSmir:      return 0;

        // Per-CPU halt-IPI registers (EV6_OSF_PC264_PAL.MAR:1264-1289):
        // 0x3C0 = CPU0 (bit0), 0x5C0 = CPU1 (bit1).  R/W; reset 0.
        case kHaltCpu0:  return m_halt[0];
        case kHaltCpu1:  return m_halt[1];

        // clr_irq4 (TIG ctrl +0x440) -- write-to-acknowledge IRQ4, the
        // interval/clock interrupt (AXPBox-map name).  Observed live during
        // the 2026-06-14 `b dqa1' run (EMULATR_TIG_TRACE off=0x440).  There is
        // NO TIG-level interrupt latch here -- the source is the Cchip
        // interval timer / DRIR -- so AXPBox absorbs the write with no side
        // effect (System.cpp tig_write default arm); EmulatR mirrors: read 0,
        // write absorbs.  Modeled explicitly so the unmodeled-TIG canary stops
        // flagging it.  Sibling clears (clr_irq5 +0x400, clr_pwr_flt_det
        // +0x480, clr_temp_warn +0x4C0, clr_temp_fail +0x500) are the same
        // write-1-to-clear pattern -- add on demand if firmware touches them.
        case kClrIrq4:   return 0;

        // Inter-processor communication regs (pc264.c outtig(0xC00028+id)).
        // STORAGE-ONLY: a write does NOT inject a target-CPU interrupt.
        // Harmless on UP DS10; on ES40/ES45 (up to 4x 21264) an ipcr write
        // meant to IPI a secondary just sits here -> SMP secondary startup
        // stalls on an IPI that never fires (ties to per-CPU HWRPB BIP/state
        // bring-up).  TODO(SMP): wire ipcr writes to IPI injection.
        case kIpcr0:     return m_ipcr[0];
        case kIpcr1:     return m_ipcr[1];
        case kIpcr2:     return m_ipcr[2];
        case kIpcr3:     return m_ipcr[3];

        // Arbiter / PLD control + revision (0x801_3800_0xxx).
        case kArbCtrl:   return m_arbCtrl;            // outtig 0xE00004
        case kArbRev:                                 // intig 0xE00005 "Arbiter Rev"
        case kTigPldRev:                              // intig 0xE00006 "TIG Rev"
        case kArbRev2:   return kRevValue;            // intig 0xE00007 "Arbiter (0x%x)"

        default:         return missDefault(pa, /*write=*/false);
        }
    }

    void write(uint64_t pa, uint64_t v) noexcept {
        switch (pa) {
        case kSmir:      return;                      // absorb; NO store (see read)
        case kHaltCpu0:  m_halt[0] = v; return;
        case kHaltCpu1:  m_halt[1] = v; return;
        case kClrIrq4:   return;                      // IRQ4 ack -- absorb (no TIG latch; see read())
        case kIpcr0:     m_ipcr[0] = v; return;
        case kIpcr1:     m_ipcr[1] = v; return;
        case kIpcr2:     m_ipcr[2] = v; return;
        case kIpcr3:     m_ipcr[3] = v; return;
        case kArbCtrl:   m_arbCtrl = v; return;
        case kArbRev:
        case kTigPldRev:
        case kArbRev2:   return;                      // read-only revision -- absorb
        default:         (void) missDefault(pa, /*write=*/true); return;
        }
    }

private:
    // Register PAs.  Control bank = 0x801_3000_0000 + off; arbiter/PLD bank =
    // 0x801_3800_0000 + off (off = TIG offset<<6 per xtig()).
    static constexpr uint64_t kSmir      = 0x80130000040ULL;  // ctrl +0x040
    static constexpr uint64_t kHaltCpu0  = 0x801300003C0ULL;  // ctrl +0x3C0
    static constexpr uint64_t kHaltCpu1  = 0x801300005C0ULL;  // ctrl +0x5C0
    static constexpr uint64_t kClrIrq4   = 0x80130000440ULL;  // ctrl +0x440 (clear IRQ4 / clock ack; AXPBox map)
    static constexpr uint64_t kIpcr0     = 0x80130000A00ULL;  // ctrl +0xA00
    static constexpr uint64_t kIpcr1     = 0x80130000A40ULL;
    static constexpr uint64_t kIpcr2     = 0x80130000A80ULL;
    static constexpr uint64_t kIpcr3     = 0x80130000AC0ULL;
    static constexpr uint64_t kArbCtrl   = 0x80138000100ULL;  // arb +0x100 (outtig 0xE00004)
    static constexpr uint64_t kArbRev    = 0x80138000140ULL;  // arb +0x140 (intig 0xE00005)
    static constexpr uint64_t kTigPldRev = 0x80138000180ULL;  // arb +0x180 (intig 0xE00006)
    static constexpr uint64_t kArbRev2   = 0x801380001C0ULL;  // arb +0x1C0 (intig 0xE00007)

    // Revision: PROVISIONAL 0.  XREF-confirmed display/store-only -- no
    // firmware gate (show_config_pc264.c:346 printf; galaxy_pc264.c:373
    // smb_platform.tig_rev; no comparison on (val>>5)&7).  0 -> "Rev 0.0",
    // the honest unknown.  Set to a real DS10 value if a `show config` dump
    // ever provides one.  (Deliberately NOT AXPBox's 0xfe -- clean-room.)
    static constexpr uint64_t kRevValue = 0;

    // Catch-all for an unmodeled-but-accessed TIG register.  Returns 0 /
    // absorbs (faithful "no device responded") BUT -- gated by
    // EMULATR_TIG_TRACE (cached getenv; hot-path-safe) -- logs the miss, so a
    // register the firmware starts to depend on surfaces LOUDLY during
    // bring-up instead of hiding behind a plausible 0 (the exact failure mode
    // that hid smir behind 0xFFFFFFFF).  Quiet by default (release).
    static uint64_t missDefault(uint64_t pa, bool write) noexcept {
        static int const s_trace =
            (std::getenv("EMULATR_TIG_TRACE") != nullptr) ? 1 : 0;
        if (s_trace) {
            std::fprintf(stderr,
                "EMULATR_TIG_TRACE: unmodeled TIG %s pa=0x%011llx off=0x%llx\n",
                write ? "WRITE" : "READ ",
                static_cast<unsigned long long>(pa),
                static_cast<unsigned long long>(pa - kDevBase));
            std::fflush(stderr);
        }
        return 0;
    }

    uint64_t m_halt[2] = { 0, 0 };
    uint64_t m_ipcr[4] = { 0, 0, 0, 0 };
    uint64_t m_arbCtrl = 0;
};

#endif // TSUNAMI_TIG_H
