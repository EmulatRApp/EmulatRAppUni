// ============================================================================
// Titan21274_CsrSpec.h -- DECchip 21274 "Titan" CSR / address-map spec
// ============================================================================
// Project: ASA-EMulatR -- Alpha AXP / EV6 Architecture Emulator (V4)
// Copyright (C) 2025, 2026 eNVy Systems, Inc. All rights reserved.
// Licensed under eNVy Systems Non-Commercial License v1.1
//
// Project Architect: Timothy Peer
// AI Code Generation: Claude (Anthropic)
// ============================================================================
//
// PURPOSE
//   Authoritative, self-contained constant spec for the Titan (21274) core
//   logic chipset used by DS15 / DS25 / ES45.  This is the Titan analog of
//   Tsunami21272_RegisterMap.h.  It depends on nothing but <cstdint> so it
//   can be unit-tested in isolation and shared by TitanCchip / TitanDchip /
//   TitanPchip / TitanChipset.
//
// PROVENANCE (every offset/field below is traceable; do not "tidy" silently)
//   [K-C] Linux arch/alpha/include/asm/core_titan.h  (Titan Chipset
//         Engineering Specification Rev 0.12, 13 Jul 1999) -- struct layouts,
//         base addresses, PCTL/SERR/PERR/AGPERR bit-fields, hose map, MCHK
//         SCB vectors.  Fetched 2026-06-16.
//   [K-S] Linux arch/alpha/kernel/core_titan.c -- window programming, AGP,
//         pchip1-present probe (CSC<14>), TIG access.
//   [APD] "Titan Chipset - EK-ES450-SV.A01" (ES45 Service Guide) Appendix D
//         -- field-level MISC / DIRn / PERROR / SERROR / AGPERROR + the
//         physical addresses (MISC @ 0x801_A000_0080, DIR0-3 @ 0x280/2C0/
//         680/6C0).
//   [PAL] Processor Support/Palcode/.../EV6_OSF_PC264_PAL.MAR -- confirms the
//         shared 21272/21274 PA map the firmware actually uses.
//
// KEY TAXONOMY NOTE
//   Titan shares the TOP-LEVEL PA map with the 21272 (Cchip 0x801_A000_0000,
//   Pchip0 0x801_8000_0000, Pchip1 0x803_8000_0000, hose stride h<<33).  The
//   genuine Titan-specific silicon is: (a) each PA-chip has TWO ports -- a
//   G-port (PCI) and an A-port (AGP); (b) a richer error register set
//   (SERR / GPERR / APERR / AGPERR) replacing the 21272 single PERR; (c) AGP.
//   The Cchip/Dchip/TIG register *offsets* are 21272-compatible (the 21272
//   map in this project already carries the 4-CPU DIM/DIR layout), so Titan
//   reuses TsunamiCchip/TsunamiDchip/TsunamiTig semantics.
// ============================================================================

#ifndef TITAN_21274_CSR_SPEC_H
#define TITAN_21274_CSR_SPEC_H

#include <cstdint>

namespace Titan21274 {

// ---------------------------------------------------------------------------
// Top-level chip base physical addresses (the firmware/PAL "superpage" form,
// i.e. TI_BIAS = 0x800_0000_0000).  [K-C], [PAL], [APD]
// ---------------------------------------------------------------------------
namespace Base {
    constexpr uint64_t kSuperpage   = 0x80000000000ULL; // 0x800.0000.0000 (TI_BIAS)

    constexpr uint64_t kCchip       = 0x801A0000000ULL; // 0x801.A000.0000  [K-C][APD]
    constexpr uint64_t kDchip       = 0x801B0000800ULL; // 0x801.B000.0800  [K-C]
                                                        // (note +0x800 vs 21272's 0x801B0000000)
    constexpr uint64_t kPachip0     = 0x80180000000ULL; // 0x801.8000.0000  [K-C]
    constexpr uint64_t kPachip1     = 0x80380000000ULL; // 0x803.8000.0000  [K-C]

    // TIG space: TITAN_BASE + 0x1_0000_0000 = 0x801.0000.0000.  Byte at
    // (offset << 6).  Identical mechanism to the 21272 TIG (TsunamiTig.h).
    constexpr uint64_t kTigSpace    = 0x80100000000ULL; // 0x801.0000.0000  [K-C][K-S]

    // pchip1 presence is read from Cchip CSC bit 14.  [K-S]
    constexpr uint64_t kCscPchip1PresentBit = (1ULL << 14);
}

// ---------------------------------------------------------------------------
// Per-hose memory / IO / config spaces.  Hose numbering [K-C]:
//   0 = pachip0 / G-port      1 = pachip1 / G-port
//   2 = pachip0 / A-port      3 = pachip1 / A-port
// TITAN_HOSE(h) = h << 33 ; spaces are TITAN_BASE + TITAN_HOSE(h) + window.
// ---------------------------------------------------------------------------
namespace Hose {
    constexpr int      kCount        = 4;
    constexpr uint64_t kHoseShift    = 33;
    constexpr uint64_t kHoseStride   = (1ULL << kHoseShift);       // 0x2.0000.0000

    constexpr uint64_t kMemOffset    = 0x000000000ULL;            // dense MEM
    constexpr uint64_t kIackScOffset = 0x1F8000000ULL;            // interrupt-ACK / special-cycle
    constexpr uint64_t kIoOffset     = 0x1FC000000ULL;            // dense IO
    constexpr uint64_t kConfOffset   = 0x1FE000000ULL;            // Type-0/1 config

    constexpr uint64_t mem (int h) noexcept { return Base::kSuperpage + (uint64_t(h) << kHoseShift) + kMemOffset;  }
    constexpr uint64_t io  (int h) noexcept { return Base::kSuperpage + (uint64_t(h) << kHoseShift) + kIoOffset;   }
    constexpr uint64_t conf(int h) noexcept { return Base::kSuperpage + (uint64_t(h) << kHoseShift) + kConfOffset; }
    constexpr uint64_t iack(int h) noexcept { return Base::kSuperpage + (uint64_t(h) << kHoseShift) + kIackScOffset; }
}

// ---------------------------------------------------------------------------
// Cchip register offsets (from Base::kCchip).  Each register is 64-byte
// aligned (titan_64).  [K-C] struct titan_cchip.  Offsets <= 0x6C0 are
// 21272-compatible (see TsunamiCchip.h kCSC..kDIR3); Titan adds the IIC2/3,
// PWR, monitor (CMON*) and CPEN registers.
// ---------------------------------------------------------------------------
namespace Cchip {
    constexpr uint64_t CSC   = 0x000; // configuration & status (CSC<14> = pchip1 present)
    constexpr uint64_t MTR   = 0x040; // memory timing
    constexpr uint64_t MISC  = 0x080; // miscellaneous: IPREQ/IPINTR, NXM/NXS, arbitration, REV  [APD]
    constexpr uint64_t MPD   = 0x0C0; // memory presence detect
    constexpr uint64_t AAR0  = 0x100; // array address 0
    constexpr uint64_t AAR1  = 0x140;
    constexpr uint64_t AAR2  = 0x180;
    constexpr uint64_t AAR3  = 0x1C0;
    constexpr uint64_t DIM0  = 0x200; // device interrupt mask, CPU0
    constexpr uint64_t DIM1  = 0x240; // ... CPU1
    constexpr uint64_t DIR0  = 0x280; // device interrupt request, CPU0   [APD]
    constexpr uint64_t DIR1  = 0x2C0; // ... CPU1                          [APD]
    constexpr uint64_t DRIR  = 0x300; // device raw interrupt request (64 raw inputs)
    constexpr uint64_t PRBEN = 0x340; // probe enable
    constexpr uint64_t IIC0  = 0x380; // interrupt-interval count, CPU0
    constexpr uint64_t IIC1  = 0x3C0; // ... CPU1
    constexpr uint64_t MPR0  = 0x400; // memory programming 0
    constexpr uint64_t MPR1  = 0x440;
    constexpr uint64_t MPR2  = 0x480;
    constexpr uint64_t MPR3  = 0x4C0;
    // 0x500, 0x540 reserved
    constexpr uint64_t TTR   = 0x580; // TIGbus timing
    constexpr uint64_t TDR   = 0x5C0; // TIGbus device
    constexpr uint64_t DIM2  = 0x600; // device interrupt mask, CPU2
    constexpr uint64_t DIM3  = 0x640; // ... CPU3
    constexpr uint64_t DIR2  = 0x680; // device interrupt request, CPU2   [APD]
    constexpr uint64_t DIR3  = 0x6C0; // ... CPU3                          [APD]
    constexpr uint64_t IIC2  = 0x700;
    constexpr uint64_t IIC3  = 0x740;
    constexpr uint64_t PWR   = 0x780; // power management
    // 0x7C0..0xBC0 reserved[17]
    constexpr uint64_t CMONCTLA = 0xC00; // performance monitor control A
    constexpr uint64_t CMONCTLB = 0xC40; // ... B
    constexpr uint64_t CMONCNT01 = 0xC80; // monitor counters 0/1
    constexpr uint64_t CMONCNT23 = 0xCC0; // monitor counters 2/3
    constexpr uint64_t CPEN  = 0xD00; // CPU enable

    // Per-CPU DIR offset helper (CPU 0..3 -> 0x280/0x2C0/0x680/0x6C0).  [APD]
    constexpr uint64_t dir(int cpu) noexcept {
        switch (cpu & 3) { case 0: return DIR0; case 1: return DIR1;
                           case 2: return DIR2; default: return DIR3; }
    }
    constexpr uint64_t dim(int cpu) noexcept {
        switch (cpu & 3) { case 0: return DIM0; case 1: return DIM1;
                           case 2: return DIM2; default: return DIM3; }
    }

    // MISC field masks (Appendix D, 21274 MISC).  IPREQ/IPINTR are the IPI
    // substrate; ABW/ABT/ACL the arbitration lock; NXM/NXS the bus-error
    // capture; REV the chip revision read by firmware.
    namespace Misc {
        constexpr uint64_t kNxs    = 0x00000000000000FFULL; // <7:0>   NXM source (device)
        constexpr uint64_t kNxm    = 0x0000000000000100ULL; // <8>     NXM latched (W1C)
        constexpr uint64_t kIpreq  = 0x000000000000FF00ULL; // <15:8>? per-CPU IPI request (see APD)
        constexpr uint64_t kIpintr = 0x0000000000FF0000ULL; // inter-processor interrupt pending
        constexpr uint64_t kItintr = 0x00000000FF000000ULL; // interval-timer interrupt (per CPU)
        constexpr uint64_t kAbw    = 0x0000000300000000ULL; // <33:32> arbitration won (per-CPU)
        constexpr uint64_t kAbt    = 0x0000000C00000000ULL; // <35:34> arbitration try
        constexpr uint64_t kAcl    = 0x0000001000000000ULL; // <36>    arbitration clear (W1)
        constexpr uint64_t kRevShift = 39;                  // <46:39> CREV (chip revision)
        constexpr uint64_t kRevMask  = 0xFFULL;
        // NOTE: exact IPREQ/IPINTR/ITINTR bit widths vary by stepping; the
        // 21272 model in TsunamiCchip.h is the working reference -- Titan is
        // bit-compatible at the offsets that matter for the IPI/halt path.
        // TODO-verify the W1C/RW split of each field against the 21274 spec.
    }
}

// ---------------------------------------------------------------------------
// Dchip register offsets (from Base::kDchip).  [K-C] struct titan_dchip.
// ---------------------------------------------------------------------------
namespace Dchip {
    constexpr uint64_t DSC  = 0x000; // system configuration
    constexpr uint64_t STR  = 0x040; // stream buffer
    constexpr uint64_t DREV = 0x080; // revision (firmware reads chip rev here)
    constexpr uint64_t DSC2 = 0x0C0; // system configuration 2
}

// ---------------------------------------------------------------------------
// PA-chip PORT register layout (from a port base; one G-port and one A-port
// per pachip).  [K-C] struct titan_pachip_port.  Each titan_64 = 0x40.
// A port is 0x1000 bytes; within a pachip, g_port @ +0x000, a_port @ +0x1000.
// ---------------------------------------------------------------------------
namespace Port {
    constexpr uint64_t kPortStride = 0x1000; // g_port -> a_port within a pachip

    // Window registers (4 DMA windows). [K-C]
    constexpr uint64_t WSBA0 = 0x000, WSBA1 = 0x040, WSBA2 = 0x080, WSBA3 = 0x0C0; // window base addr
    constexpr uint64_t WSM0  = 0x100, WSM1  = 0x140, WSM2  = 0x180, WSM3  = 0x1C0; // window size mask
    constexpr uint64_t TBA0  = 0x200, TBA1  = 0x240, TBA2  = 0x280, TBA3  = 0x2C0; // translated base addr
    constexpr uint64_t PCTL  = 0x300; // port control (GPCTL or APCTL)
    constexpr uint64_t PLAT  = 0x340; // latency
    // 0x380, 0x3C0 reserved

    // G-port specific (offset within port). [K-C] union .g
    namespace G {
        constexpr uint64_t SERROR  = 0x400; // system error           [APD]
        constexpr uint64_t SERREN  = 0x440;
        constexpr uint64_t SERRSET = 0x480;
        constexpr uint64_t GPERROR = 0x500; // G PCI error            [APD]
        constexpr uint64_t GPERREN = 0x540;
        constexpr uint64_t GPERRSET= 0x580;
        constexpr uint64_t GTLBIV  = 0x600; // sg-tlb invalidate virtual
        constexpr uint64_t GTLBIA  = 0x640; // sg-tlb invalidate all
        constexpr uint64_t SCTL    = 0x700; // split-completion control
    }
    // A-port specific (offset within port). [K-C] union .a
    namespace A {
        constexpr uint64_t AGPERROR  = 0x400; // AGP error            [APD]
        constexpr uint64_t AGPERREN  = 0x440;
        constexpr uint64_t AGPERRSET = 0x480;
        constexpr uint64_t AGPLASTWR = 0x4C0; // AGP last write
        constexpr uint64_t APERROR   = 0x500; // A PCI error          [APD]
        constexpr uint64_t APERREN   = 0x540;
        constexpr uint64_t APERRSET  = 0x580;
        constexpr uint64_t ATLBIV    = 0x600;
        constexpr uint64_t ATLBIA    = 0x640;
    }
    constexpr uint64_t SPRST = 0x800; // soft/PCI reset (both ports)

    // WSBA bit masks. [K-C]
    constexpr uint64_t kWsbaEna  = 0x1;          // window enable
    constexpr uint64_t kWsbaSg   = 0x2;          // scatter-gather
    constexpr uint64_t kWsbaAddr = 0xFFF00000ULL;// base address <31:20>

    // PCTL bit masks (GPCTL/APCTL share <51:0>; APCTL adds AGP <63:52>). [K-C]
    constexpr uint64_t kPctlMwin       = 0x00000040ULL;         // monster window enable
    constexpr uint64_t kPctlArbena     = 0x00000080ULL;
    constexpr uint64_t kApctlAgpRate   = 0x0030000000000000ULL; // <53:52>
    constexpr uint64_t kApctlAgpSbaEn  = 0x0040000000000000ULL; // <54>
    constexpr uint64_t kApctlAgpEn     = 0x0080000000000000ULL; // <55>
    constexpr uint64_t kApctlAgpPresent= 0x0200000000000000ULL; // <57> AGP port present
}

// ---------------------------------------------------------------------------
// Default PCI->memory window programming the kernel installs (and SRM leaves
// in a compatible state).  [K-S] titan_init_one_pachip_port.
//   W0: scatter-gather  8 MB @ 8 MB    (ISA)
//   W1: direct-mapped   1 GB @ 2 GB
//   W2: scatter-gather  1 GB @ 3 GB
//   W3: scatter-gather ONLY (left disabled by the kernel)
// ---------------------------------------------------------------------------
namespace Window {
    constexpr uint64_t kDirectMapBase = 0x80000000ULL; // 2 GB
    constexpr uint64_t kDirectMapSize = 0x40000000ULL; // 1 GB
    constexpr uint64_t kSgIsaBase     = 0x00800000ULL; // 8 MB
    constexpr uint64_t kSgIsaSize     = 0x00800000ULL; // 8 MB
    constexpr uint64_t kSgPciBase     = 0xC0000000ULL; // 3 GB
    constexpr uint64_t kSgPciSize     = 0x40000000ULL; // 1 GB
}

// ---------------------------------------------------------------------------
// Machine-check / SCB vectors and the Titan system-mcheck logout frame. [K-C]
// These are what TitanCchip error promotion and the PAL MCHK path consume.
// ---------------------------------------------------------------------------
namespace Mcheck {
    constexpr uint64_t SCB_SYSERR   = 0x620;
    constexpr uint64_t SCB_PROCERR  = 0x630;
    constexpr uint64_t SCB_SYSMCHK  = 0x660;
    constexpr uint64_t SCB_PROCMCHK = 0x670;
    constexpr uint64_t SCB_SYSEVENT = 0x680; // environmental / system management (Privateer 680)
}

} // namespace Titan21274

#endif // TITAN_21274_CSR_SPEC_H
