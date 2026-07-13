// ============================================================================
// TsunamiChipset.cpp -- Tsunami/Typhoon Chipset out-of-line constructor
// ============================================================================
// Project: ASA-EMulatR - Alpha AXP Architecture Emulator
// Copyright (C) 2025, 2026 eNVy Systems, Inc. All rights reserved.
// Licensed under eNVy Systems Non-Commercial License v1.1
//
// Project Architect: Timothy Peer
// AI Code Generation: Claude (Anthropic)
// ============================================================================

#include "TsunamiChipset.h"
#include "traceLib/DecListingSink.h"   // 2026-06-13: console-armable retire-trace window

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>

// ============================================================================
// UART chokepoint constants -- diagnostic session 2026-05-28
// ============================================================================
// PA targets for the COM1 LSR/RBR poll loop the firmware is currently
// wedged on (trace 123456f.txt @ cyc 756M).  Used by UARTBP#1..#2 below
// and by sibling probes in TsunamiChipset.h / TsunamiPchip.h /
// Uart16550.h.
//
// REMOVAL TRIGGER: delete these constants once the wedge is rooted out.
// ============================================================================
namespace {
    constexpr uint64_t kUartBpPaThrRbr = 0x801FC0003F8ULL;
    constexpr uint64_t kUartBpPaLsr    = 0x801FC0003FDULL;
    inline bool isUartBpPa(uint64_t pa) noexcept {
        return pa == kUartBpPaThrRbr || pa == kUartBpPaLsr;
    }
}

TsunamiChipset::TsunamiChipset(ChipsetVariant variant,
                               int cpuCount,
                               uint64_t memSizeBytes) noexcept
    : m_variant(variant) // Initialize the passive byte-store
    // Fallback representative model for the variant-ONLY ctor (RAM-only /
    // doc snippets).  The real Machine path uses the model-string ctor, which
    // sets m_model from the ini.  Corrected 2026-07-03: Typhoon is ES40-class
    // (21272 high-bw), NOT ES45 (that is Titan/21274); base Tsunami is DS10/DS20.
    , m_model(variant == ChipsetVariant::Typhoon ? "ES40" : "DS20")
    // 2026-07-12: derive the south-bridge lever from the representative m_model
    // (Typhoon->ES40->ALi; else DS20->Cypress) -- byte-identical to the prior
    // isAliPlatform(m_model) selection this ctor produced.
    , m_southBridge(southBridgeFromModel(m_model))
    , m_guestMemory(memSizeBytes)
    // TODO(aar-decode-width): pass the loaded firmware's ASIZ decode width as
    // the 4th arg (default false = 3-bit, pc264-consumable).  The AAR encoding
    // width is a FIRMWARE property, not the board -- task #5.  A 4-bit-decoding
    // firmware would pass true to re-enable 2/4/8 GB arrays.  See TsunamiCchip
    // reset() + journals/20260710_es40_memtest_acv_RESOLVED_aar_asiz_and_tiling.md.
    , m_cchip(variant, cpuCount, memSizeBytes)
    , m_dchip(variant, memSizeBytes)
    , m_pchip(variant)
    // task #25: DPR present on the RMC platform (ES40 == Typhoon) only.
    , m_hasRmcDpr(variant == ChipsetVariant::Typhoon)
    , m_dpr(cpuCount)
{
    if (variant == ChipsetVariant::Tsunami)
        std::fprintf(stderr, "  Chipset:       Tsunami 21272 (wired)\n");
    else if (variant == ChipsetVariant::Typhoon)
        std::fprintf(stderr, "  Chipset:       Typhoon 21272 (wired)\n");
    else
        std::fprintf(stderr, "  Chipset:       Disabled (RAM-only mode)\n");

    assertVariantConsistency();
    wireDevices();
}

bool TsunamiChipset::isDramAddress(uint64_t pa) const noexcept {
    // Logic: Interrogate Cchip AAR (Address Array Registers)
    return m_cchip.isDramAddress(pa);
}
using memoryLib::BusResult;
using memoryLib::BusStatus;

// ----------------------------------------------------------------------------
// ISystemBus arbiter surface.  Decode order: DRAM -> GuestMemory; I/O window
// -> mmioRead/mmioWrite (Cchip/Dchip/Pchip CSR + PCI routing); else NXM.
// ----------------------------------------------------------------------------
BusResult TsunamiChipset::read(uint64_t pa, uint8_t width) noexcept {
    // ====================================================================
    // UARTBP#1 -- chipset read entry, diagnostic 2026-05-28
    // ====================================================================
    // Fires once at first PA-match to confirm the UART probe actually
    // enters TsunamiChipset::read at all.  If never fires across a run
    // that produces UART activity in the Alpha trace, the load is being
    // intercepted upstream (PAL scratch, DRAM hook, etc.).
    //
    // REMOVAL TRIGGER: delete when LSR-wedge diagnostic is closed.
    // ====================================================================
#if EMULATR_BRINGUP_PROBES
    {
        static std::atomic<bool> s_fired{false};
        if (isUartBpPa(pa) &&
            !s_fired.exchange(true, std::memory_order_acq_rel))
        {
            std::fprintf(stderr,
                         "UARTBP#1 TsunamiChipset::read entry  pa=0x%012llx "
                         "width=%u\n",
                         static_cast<unsigned long long>(pa),
                         static_cast<unsigned>(width));
            std::fflush(stderr);
            // __debugbreak();  // 2026-05-28: muted post-verification; probe still emits stderr marker.
        }
    }
#endif

    // PAL scratchpad carve-out -- decoded first so it never reaches a window.
    if (isPalScratchAddr(pa)) {
        uint64_t out = 0;
        uint64_t const off = pa - kPalScratchBase;
        if (off + width <= kPalScratchSize)
            std::memcpy(&out, m_palScratch.data() + off, width);
        return { BusStatus::Ok, out };
    }
    if (isDramAddress(pa)) {
        uint64_t out = 0;
        switch (width) {
        case 1: { uint8_t  v = 0; m_guestMemory.read1(pa, v); out = v; break; }
        case 2: { uint16_t v = 0; m_guestMemory.read2(pa, v); out = v; break; }
        case 4: { uint32_t v = 0; m_guestMemory.read4(pa, v); out = v; break; }
        case 8: {                 m_guestMemory.read8(pa, out);       break; }
        default: return { BusStatus::BusError, 0 };
        }
        return { BusStatus::Ok, out };
    }
    // TIG-bus flash / NVRAM window.  Decoded ahead of the generic I/O window
    // (its base lies above kMMIO_Start).  off = (pa - base) >> 6; width is
    // ignored -- the device drives one byte per shifted address (C4).
    if (isTigFlashAddr(pa)) {
        return { BusStatus::Ok, m_flash.read(tigFlashOffset(pa)) };
    }
    // EmulatR debug: trace-arm trigger READ -- MUST precede the TIG control
    // default below (it lives in the same window).  `e pmem:80130000FF8` at
    // >>> opens the retire window until run end (INT64_MAX); returns an
    // "armed" sentinel so the examine confirms it.  See kTigTraceArmReg.
    if (pa == kTigTraceArmReg) {
        traceLib::DecListingSink::setTraceWindowCountdown(INT64_MAX);
        return { BusStatus::Ok, static_cast<uint64_t>(INT64_MAX) };
    }
    // TIG-bus device registers (smir / halt-IPI / ipcr / arbiter) -- faithful
    // register file in TsunamiTig, decoded ahead of the generic mmioRead
    // branch.  smir (TIG+0x40) reads 0 = "no halt"; before this it fell to the
    // all-ones default and the firmware refused `boot`.  See TsunamiTig.h.
    if (m_tig.decodes(pa)) {
        return { BusStatus::Ok, m_tig.read(pa) };
    }
    // RMC Dual-Port RAM (task #25) -- the TIG-bus mailbox at 0x801_1000_0000.
    // Gated on the RMC platform (ES40 == Typhoon); on DS10/DS20 m_hasRmcDpr is
    // false so this window falls through to NXM (byte-identical).  Clears the
    // "TIG load failure" console error (DPR ram[0xda]=0xaa).  See TsunamiDpr.h.
    if (m_hasRmcDpr && m_dpr.decodes(pa)) {
        return { BusStatus::Ok, m_dpr.read(pa, width) };
    }
    // I/O window -> Cchip / Dchip / Pchip CSR + PCI routing (existing dispatch).
    if (pa >= Tsunami21272::Base::kMMIO_Start) {
        uint64_t const v = mmioRead(pa, width, 0);
        // HALTPROBE (debug): any NON-ZERO read in the TIG control window
        // (0x801_3000_0000 .. +0x1000) announces PA + value, so the firmware's
        // halt-state read is caught even un-armed.  Silence during `b dqa1`
        // => the halt read is the impure DRAM flag, not MMIO.  Throttled.
#if EMULATR_BRINGUP_PROBES
        if (pa >= 0x80130000000ULL && pa < 0x80130001000ULL && v != 0) {
            static std::atomic<int> s_haltProbeN{ 0 };
            if (s_haltProbeN.fetch_add(1, std::memory_order_relaxed) < 64) {
                std::fprintf(stderr,
                             "HALTPROBE: TIG read pa=0x%011llx w=%u v=0x%llx\n",
                             static_cast<unsigned long long>(pa),
                             static_cast<unsigned>(width),
                             static_cast<unsigned long long>(v));
                std::fflush(stderr);
            }
        }
#endif
        return { BusStatus::Ok, v };
    }
    // ----------------------------------------------------------------
    // Unclaimed PA -> NXM (asynchronous per HRM 10.2.x).
    // ----------------------------------------------------------------
    // CHANGE 2026-05-28 (Phase B-NXMA): reads from non-existent memory
    // return BusStatus::Ok with all-ones data (Alpha convention for
    // "no device responded").  reportNxm latches MISC<NXM>/NXS and
    // mirrors to DRIR<63>; Machine::run picks up the b_irq<0> divert
    // at the next IPL-acceptance window.  No synchronous fault is
    // delivered -- the CPU continues executing past this access, which
    // is the correct probe-sweep behavior the firmware expects.
    //
    // Previously delivered as synchronous BusError -> MCHK; that model
    // caused multi-million-cycle probe loops (see
    // [[nxm-should-be-async-not-sync-mchk]] memory).
    // ====================================================================
    // UARTBP#2 -- NXM-path catch, diagnostic 2026-05-28
    // ====================================================================
    // Fires once if a UART PA falls into the NXM fall-through.  This is
    // the smoking-gun BP: if it fires, the chipset is NOT routing
    // 0x801_fc00_03fX into mmioRead despite the PA satisfying
    // (pa >= kMMIO_Start).  Inspect 'pa' and 'kMMIO_Start' in MSVC to
    // see which check failed.
    //
    // REMOVAL TRIGGER: delete when LSR-wedge diagnostic is closed.
    // ====================================================================
#if EMULATR_BRINGUP_PROBES
    {
        static std::atomic<bool> s_fired{false};
        if (isUartBpPa(pa) &&
            !s_fired.exchange(true, std::memory_order_acq_rel))
        {
            std::fprintf(stderr,
                         "UARTBP#2 TsunamiChipset::read NXM     pa=0x%012llx "
                         "(routing bypassed mmioRead -- BUG)\n",
                         static_cast<unsigned long long>(pa));
            std::fflush(stderr);
            // __debugbreak();  // 2026-05-28: muted post-verification; probe still emits stderr marker.
        }
    }
#endif

    reportNxm(pa, /*sourceCode*/ 0);
    return { .status = BusStatus::Ok, .data = ~uint64_t{0} };
}

BusResult TsunamiChipset::write(uint64_t pa, uint64_t value, uint8_t width) noexcept {
    if (width != 1 && width != 2 && width != 4 && width != 8)
        return { BusStatus::BusError, 0 };

    // PAL scratchpad carve-out -- decoded first so it never reaches a window.
    if (isPalScratchAddr(pa)) {
        uint64_t const off = pa - kPalScratchBase;
        if (off + width <= kPalScratchSize)
            std::memcpy(m_palScratch.data() + off, &value, width);
        return {.status = BusStatus::Ok, .data = 0 };
    }

    if (isDramAddress(pa)) {
        switch (width) {
        case 1: m_guestMemory.write1(pa, static_cast<uint8_t> (value)); break;
        case 2: m_guestMemory.write2(pa, static_cast<uint16_t>(value)); break;
        case 4: m_guestMemory.write4(pa, static_cast<uint32_t>(value)); break;
        case 8: m_guestMemory.write8(pa,                       value);  break;
        default: ;
        }
        return { BusStatus::Ok, 0 };
    }
    // TIG-bus flash / NVRAM window -- routes to the AMD command machine.
    if (isTigFlashAddr(pa)) {
        m_flash.write(tigFlashOffset(pa), value);
        return { BusStatus::Ok, 0 };
    }
    // EmulatR debug: trace-arm trigger WRITE -- MUST precede the TIG control
    // absorb below (same window).  `d pmem:80130000FF8 N` at >>>: N>0 = trace
    // next N retired instrs; N=0 = trace OFF.  See kTigTraceArmReg header.
    if (pa == kTigTraceArmReg) {
        traceLib::DecListingSink::setTraceWindowCountdown(
            static_cast<int64_t>(value));
        return { BusStatus::Ok, 0 };
    }
    // TIG-bus device register writes -> TsunamiTig (smir absorbed; halt-IPI /
    // ipcr / arbiter R/W stored).  Never falls through to NXM.  See read()
    // and TsunamiTig.h.
    if (m_tig.decodes(pa)) {
        m_tig.write(pa, value);
        return { BusStatus::Ok, 0 };
    }
    // RMC Dual-Port RAM (task #25) -- host-side writes to the TIG-bus mailbox
    // (RMC commands: FRU/OCP/flash + secondary-CPU start).  Gated on the RMC
    // platform (ES40 == Typhoon); DS10/DS20 fall through (byte-identical).
    if (m_hasRmcDpr && m_dpr.decodes(pa)) {
        m_dpr.write(pa, width, value);
        return { BusStatus::Ok, 0 };
    }
    if (pa >= Tsunami21272::Base::kMMIO_Start) {
        mmioWrite(pa, value, width, 0);
        return { BusStatus::Ok, 0 };
    }
    // ----------------------------------------------------------------
    // Unclaimed PA -> NXM (asynchronous per HRM 10.2.x).
    // ----------------------------------------------------------------
    // CHANGE 2026-05-28 (Phase B-NXMA): writes to non-existent memory
    // are silently dropped at the bus level and returned as Ok.  Same
    // rationale as read above: reportNxm latches MISC<NXM>/NXS and
    // mirrors to DRIR<63>, Machine::run delivers the b_irq<0> divert
    // asynchronously.  No synchronous BusError fault.
    reportNxm(pa, /*sourceCode*/ 0);
    return { BusStatus::Ok, 0 };
}

BusResult TsunamiChipset::fetch(uint64_t pa, uint8_t width) noexcept {
    // Instruction fetch: DRAM or NXM.  PAL scratch is serviced CPU-side and
    // never reaches the bus; LL/SC is irrelevant to a fetch.
    return read(pa, width);
}