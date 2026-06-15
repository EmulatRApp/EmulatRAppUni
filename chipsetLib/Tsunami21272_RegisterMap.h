// ============================================================================
// Tsunami21272_RegisterMap.h
// ============================================================================
// Tsunami/Typhoon 21272 Chipset - Complete CSR Register Map
//
// Source: Tsunami/Typhoon 21272 Chipset Hardware Reference Manual
//         Document: EC-RE2CA-TE, Revision 4.0, 21 October 1999
//         Table 10-7: Chipset Register Addresses
//
// Project: ASA-EMulatR - Alpha AXP Architecture Emulator
// Copyright (C) 2025, 2026 eNVy Systems, Inc. All rights reserved.
//
// USAGE:
//   This header is the SINGLE SOURCE OF TRUTH for all Tsunami chipset
//   register offsets. All chipset emulation code must use these constants.
//   DO NOT define register offsets elsewhere.
//
// ADDRESS FORMAT:
//   System PA = Chip Base PA + Register Offset
//
//   Example: Cchip CSC = 0x801.A000.0000 + 0x0000 = 0x801.A000.0000
//   Example: Pchip0 PCTL = 0x801.8000.0000 + 0x0300 = 0x801.8000.0300
//
// REGISTER SPACING:
//   All registers are spaced 0x40 (64 bytes) apart.
//   Register number = offset / 0x40.
//
// ============================================================================

#ifndef TSUNAMI_21272_REGISTER_MAP_H
#define TSUNAMI_21272_REGISTER_MAP_H

#include <cstdint>

namespace Tsunami21272 {

    // ============================================================================
    // SYSTEM ADDRESS MAP ++ Chip Base Addresses
    // ============================================================================
    // HRM Table 10-1: System Address Map
    //
    //   Region                          Base PA              Size
    //   -----------------------------------------------------------
    //   Main Memory                     0x000.0000.0000      variable
    //   Pchip0 PCI Memory (dense)       0x800.0000.0000      4 GB
    //   Pchip0 CSRs                     0x801.8000.0000      256 MB
    //   Cchip CSRs                      0x801.A000.0000
    //   Dchip CSRs                      0x801.B000.0000
    //   Pchip0 PCI IACK                 0x801.F800.0000      64 MB
    //   Pchip0 PCI I/O (dense)          0x801.FC00.0000      32 MB
    //   Pchip0 PCI Config (Type 0)      0x801.FE00.0000      16 MB
    //   Pchip1 PCI Memory (dense)       0x802.0000.0000      4 GB
    //   Pchip1 CSRs                     0x803.8000.0000      256 MB
    //   Pchip1 PCI IACK                 0x803.F800.0000      64 MB
    //   Pchip1 PCI I/O (dense)          0x803.FC00.0000      32 MB
    //   Pchip1 PCI Config (Type 0)      0x803.FE00.0000      16 MB
    // ============================================================================

    namespace Base {
        // Full system PAs
        constexpr uint64_t kMainMemory = 0x00000000000ULL;

        // Pchip0 regions
        constexpr uint64_t kPchip0_PciMem = 0x80000000000ULL;  // 0x800.0000.0000
        constexpr uint64_t kPchip0_CSR = 0x80180000000ULL;  // 0x801.8000.0000
        constexpr uint64_t kPchip0_IACK = 0x801F8000000ULL;  // 0x801.F800.0000
        constexpr uint64_t kPchip0_IODense = 0x801FC000000ULL;  // 0x801.FC00.0000
        constexpr uint64_t kPchip0_CfgType0 = 0x801FE000000ULL;  // 0x801.FE00.0000

        // Cchip
        constexpr uint64_t kCchip_CSR = 0x801A0000000ULL;  // 0x801.A000.0000

        // Dchip
        constexpr uint64_t kDchip_CSR = 0x801B0000000ULL;  // 0x801.B000.0000

        // Pchip1 regions (Typhoon dual-Pchip, or ES40 with second PCI bus)
        constexpr uint64_t kPchip1_PciMem = 0x80200000000ULL;  // 0x802.0000.0000
        constexpr uint64_t kPchip1_CSR = 0x80380000000ULL;  // 0x803.8000.0000
        constexpr uint64_t kPchip1_IACK = 0x803F8000000ULL;  // 0x803.F800.0000
        constexpr uint64_t kPchip1_IODense = 0x803FC000000ULL;  // 0x803.FC00.0000
        constexpr uint64_t kPchip1_CfgType0 = 0x803FE000000ULL;  // 0x803.FE00.0000

        // Full MMIO window (for GuestMemory routing)
        constexpr uint64_t kMMIO_Start = 0x80000000000ULL;  // 0x800.0000.0000
        constexpr uint64_t kMMIO_End = 0x84000000000ULL;  // 0x840.0000.0000
        constexpr uint64_t kMMIO_Size = kMMIO_End - kMMIO_Start;  // 16 GB

        constexpr uint64_t kTsunamiBase = 0x80000000000ULL;
    }

    // ============================================================================
    // CCHIP REGISTERS
    // ============================================================================
    // Base PA: 0x801.A000.0000 (Tsunami)
    //
    // The Cchip provides:
    //   - System configuration and memory timing
    //   - Memory presence detect (SPD interface)
    //   - Memory array address registers (memory sizing)
    //   - Device interrupt mask/request/routing
    //   - Interprocessor interrupt control
    //   - Interval timer (TIG)
    //   - Performance monitoring (Typhoon only)
    //
    // All registers are 64-bit, spaced 0x40 apart.
    // ============================================================================

    namespace Cchip {

        // System Configuration & Memory
        constexpr uint64_t CSC = 0x0000;  // Reg 00  RW   System Configuration
        //   CPU count, memory config, revision
        constexpr uint64_t MTR = 0x0040;  // Reg 01  RW   Memory Timing Register
        constexpr uint64_t MISC = 0x0080;  // Reg 02  RW   Miscellaneous Control
        //   NXM detect, ABW, revision, error status
        //   Bits [47:44]: NXM source
        //   Bits [39:32]: Chip revision
        constexpr uint64_t MPD = 0x00C0;  // Reg 03  RW   Memory Presence Detect (SPD/I2C)

        // Memory Array Address Registers ++ define memory bank sizes/bases
        constexpr uint64_t AAR0 = 0x0100;  // Reg 04  RW   Address Array 0
        constexpr uint64_t AAR1 = 0x0140;  // Reg 05  RW   Address Array 1
        constexpr uint64_t AAR2 = 0x0180;  // Reg 06  RW   Address Array 2
        constexpr uint64_t AAR3 = 0x01C0;  // Reg 07  RW   Address Array 3

        // Device Interrupt Mask ++ controls which device interrupts reach CPUs
        constexpr uint64_t DIM0 = 0x0200;  // Reg 08  RW   Device Interrupt Mask (CPU 0)
        constexpr uint64_t DIM1 = 0x0240;  // Reg 09  RW   Device Interrupt Mask (CPU 1)

        // Device Interrupt Request ++ active interrupt sources (masked by DIMx)
        constexpr uint64_t DIR0 = 0x0280;  // Reg 0A  RO   Device Interrupt Request (CPU 0)
        constexpr uint64_t DIR1 = 0x02C0;  // Reg 0B  RO   Device Interrupt Request (CPU 1)

        // Raw Interrupt Request ++ all device interrupts before masking
        constexpr uint64_t DRIR = 0x0300;  // Reg 0C  RO   Device Raw Interrupt Request

        // Probe Enable
        constexpr uint64_t PRBEN = 0x0340;  // Reg 0D  Special  Probe Enable

        // Interprocessor Interrupt Control
        constexpr uint64_t IIC0 = 0x0380;  // Reg 0E  RW   Interprocessor Interrupt (CPU 0)
        constexpr uint64_t IIC1 = 0x03C0;  // Reg 0F  RW   Interprocessor Interrupt (CPU 1)

        // Memory Programming ++ SDRAM mode register writes
        constexpr uint64_t MPR0 = 0x0400;  // Reg 10  WO   Memory Programming 0
        constexpr uint64_t MPR1 = 0x0440;  // Reg 11  WO   Memory Programming 1
        constexpr uint64_t MPR2 = 0x0480;  // Reg 12  WO   Memory Programming 2
        constexpr uint64_t MPR3 = 0x04C0;  // Reg 13  WO   Memory Programming 3

        // Reserved
        constexpr uint64_t RSVD_14 = 0x0500;  // Reg 14  RW   Reserved

        // TIG (Timer/Interrupt/GPIO)
        constexpr uint64_t TTR = 0x0580;  // Reg 16  RW   TIG Timer Register
        //   Interval timer for clock interrupts
        constexpr uint64_t TDR = 0x05C0;  // Reg 17  RW   TIG Data Register

        // ---- Typhoon-only registers (4-CPU support) ----

        constexpr uint64_t DIM2 = 0x0600;  // Reg 18  RW   Device Interrupt Mask (CPU 2)
        constexpr uint64_t DIM3 = 0x0640;  // Reg 19  RW   Device Interrupt Mask (CPU 3)
        constexpr uint64_t DIR2 = 0x0680;  // Reg 1A  RO   Device Interrupt Request (CPU 2)
        constexpr uint64_t DIR3 = 0x06C0;  // Reg 1B  RO   Device Interrupt Request (CPU 3)
        constexpr uint64_t IIC2 = 0x0700;  // Reg 1C  RW   Interprocessor Interrupt (CPU 2)
        constexpr uint64_t IIC3 = 0x0740;  // Reg 1D  RW   Interprocessor Interrupt (CPU 3)
        constexpr uint64_t PWR = 0x0780;  // Reg 1E  RW   Power Management (ACPI)

        // Reserved range
        // 0x07C0 - 0x0BC0  Regs 1F-2F  Reserved

        // Performance Monitoring (Typhoon only)
        constexpr uint64_t CMONCTLA = 0x0C00;  // Reg 30  RW   Monitor Control A
        constexpr uint64_t CMONCTLB = 0x0C40;  // Reg 31  RW   Monitor Control B
        constexpr uint64_t CMONCNT01 = 0x0C80;  // Reg 32  RO   Monitor Counter 0/1
        constexpr uint64_t CMONCNT23 = 0x0CC0;  // Reg 33  RO   Monitor Counter 2/3

        // Number of registers for array sizing
        constexpr int kRegCount = 34;


    }

    // ============================================================================
    // DCHIP REGISTERS
    // ============================================================================
    // Base PA: 0x801.B000.0000
    //
    // The Dchip provides the data path between CPUs, memory, and Pchips.
    // Minimal register set ++ can be stubbed for initial SRM boot.
    //
    // NOTE: HRM shows Dchip registers at high offsets (0x0800+).
    //       This is unusual but matches the documented addresses.
    // ============================================================================

    namespace Dchip {

        constexpr uint64_t DSC = 0x0800;  // Reg 20  RO   System Configuration
        constexpr uint64_t STR = 0x0840;  // Reg 21  RW   Stripe Register
        constexpr uint64_t DREV = 0x0880;  // Reg 22  RW   Dchip Revision
        constexpr uint64_t DSC2 = 0x08C0;  // Reg 23  RO   Reserved / future use

        // Suggested init values for stubs
        constexpr uint64_t kDREV_Default = 0x10;   // Revision 1.0 (from TsunamiDchip reset)

        constexpr int kRegCount = 4;
    }

    // ============================================================================
    // PCHIP REGISTERS (per Pchip instance: Pchip0 and Pchip1)
    // ============================================================================
    // Base PA: Pchip0 = 0x801.8000.0000
    //          Pchip1 = 0x803.8000.0000
    //
    // The Pchip provides:
    //   - PCI host bridge (bus mastering, config space, I/O space)
    //   - DMA window translation (scatter-gather via WSBA/WSM/TBA)
    //   - PCI error detection and reporting
    //   - PCI arbitration
    //   - Performance monitoring
    //
    // All registers are 64-bit, spaced 0x40 apart.
    // Register number = offset >> 6.
    // ============================================================================

    namespace Pchip {

        // ---- Window Space Base Address (DMA window configuration) ----
        // WSBA[n] defines the PCI address base for DMA window n.
        // Bit 0: Window enable (1 = enabled)
        // Bit 1: Scatter-gather enable (1 = use page table, 0 = direct)
        constexpr uint64_t WSBA0 = 0x0000;  // Reg 00  RW   Window Space Base 0
        constexpr uint64_t WSBA1 = 0x0040;  // Reg 01  RW   Window Space Base 1
        constexpr uint64_t WSBA2 = 0x0080;  // Reg 02  RW   Window Space Base 2
        constexpr uint64_t WSBA3 = 0x00C0;  // Reg 03  RW   Window Space Base 3
        //   WSBA3 has extended addressing bits [63:40]

// ---- Window Space Mask (DMA window size) ----
// WSM[n] defines the window size. Size = WSM + 0x100000.
        constexpr uint64_t WSM0 = 0x0100;  // Reg 04  RW   Window Space Mask 0
        constexpr uint64_t WSM1 = 0x0140;  // Reg 05  RW   Window Space Mask 1
        constexpr uint64_t WSM2 = 0x0180;  // Reg 06  RW   Window Space Mask 2
        constexpr uint64_t WSM3 = 0x01C0;  // Reg 07  RW   Window Space Mask 3

        // ---- Translation Base Address (DMA target in system memory) ----
        // TBA[n] is the system memory base that DMA window n maps to.
        // For scatter-gather: TBA points to the page table base.
        constexpr uint64_t TBA0 = 0x0200;  // Reg 08  RW   Translation Base 0
        constexpr uint64_t TBA1 = 0x0240;  // Reg 09  RW   Translation Base 1
        constexpr uint64_t TBA2 = 0x0280;  // Reg 0A  RW   Translation Base 2
        constexpr uint64_t TBA3 = 0x02C0;  // Reg 0B  RW   Translation Base 3

        // ---- PCI Control ----
        // Master control register for the PCI bridge.
        //   Bit  3: chaindis ++ disable PCI chaining
        //   Bit  5: hole ++ enable ISA memory hole (640K-1M)
        //   Bit  6: mwin ++ monster window enable (64-bit PCI)
        //   Bit 44: ptevrfy ++ PTE verify on scatter-gather
        constexpr uint64_t PCTL = 0x0300;  // Reg 0C  RW   PCI Control

        // ---- PCI Latency ----
        constexpr uint64_t PLAT = 0x0340;  // Reg 0D  RW   PCI Master Latency Timer

        // ---- Reserved ----
        constexpr uint64_t RES = 0x0380;  // Reg 0E  RW   Reserved

        // ---- PCI Error Registers ----
        // PERROR: captures first PCI error. Write-1-to-clear semantics.
        //   Bits [11:0]: Error type flags
        //     NDS, TA, RDPE, PERR, SGE, APE, SERR, DCRTO, etc.
        constexpr uint64_t PERROR = 0x03C0;  // Reg 0F  RW   PCI Error (W1C)

        // PERRMASK: controls which errors generate interrupts
        constexpr uint64_t PERRMASK = 0x0400;  // Reg 10  RW   PCI Error Mask

        // PERRSET: force-set error bits for diagnostics (write-only)
        constexpr uint64_t PERRSET = 0x0440;  // Reg 11  WO   PCI Error Set

        // ---- TLB Management ----
        constexpr uint64_t TLBIV = 0x0480;  // Reg 12  WO   TLB Invalidate (single entry)
        constexpr uint64_t TLBIA = 0x04C0;  // Reg 13  WO   TLB Invalidate All

        // ---- Performance Monitoring ----
        constexpr uint64_t PMONCTL = 0x0500;  // Reg 14  RW   Performance Monitor Control
        constexpr uint64_t PMONCNT = 0x0540;  // Reg 15  RO   Performance Monitor Counter

        // ---- PCI Software Reset ----
        // Writing triggers a PCI bus reset (assert RST# for ~1ms)
        constexpr uint64_t SPRST = 0x0800;  // Reg 20  WO   PCI Software Reset

        // Number of registers for validation
        constexpr int kRegCount = 22;

        // ---- Useful masks for register decode ----
        constexpr uint64_t kWSBA_AddrMask = 0xFFF00003ULL;      // WSBA[0-2] valid bits
        constexpr uint64_t kWSBA3_AddrMask = 0xFFFFFF80FFF00003ULL; // WSBA3 extended
        constexpr uint64_t kWSM_Mask = 0xFFF00000ULL;      // WSM valid bits
        constexpr uint64_t kTBA_Mask = 0x7FFFFFC00ULL;     // TBA valid bits

        constexpr uint64_t kWSBA_Enable = 0x1ULL;             // Bit 0: window enable
        constexpr uint64_t kWSBA_SG = 0x2ULL;             // Bit 1: scatter-gather

        // PCTL bit definitions
        constexpr uint64_t kPCTL_ChainDis = (1ULL << 3);
        constexpr uint64_t kPCTL_Hole = (1ULL << 5);
        constexpr uint64_t kPCTL_MWin = (1ULL << 6);
        constexpr uint64_t kPCTL_PteVrfy = (1ULL << 44);
    }

    // ============================================================================
    // CROSS-REFERENCE: Offsets from MMIO base (0x800.0000.0000)
    // ============================================================================
    // When GuestMemory routes to the Tsunami MMIO region, the offset from
    // kMMIO_Start (0x800.0000.0000) determines which chip handles the access:
    //
    //   Offset Range                    Target
    //   ----------------------------------------------------------
    //   0x000000000 - 0x0FFFFFFFF       Pchip0 PCI Dense Memory (4 GB)
    //   0x180000000 - 0x18FFFFFFF       Pchip0 CSRs
    //   0x1A0000000 - 0x1AFFFFFFF       Cchip CSRs
    //   0x1B0000000 - 0x1BFFFFFFF       Dchip CSRs
    //   0x1F8000000 - 0x1FBFFFFFF       Pchip0 PCI IACK
    //   0x1FC000000 - 0x1FDFFFFFF       Pchip0 PCI I/O (dense)
    //   0x1FE000000 - 0x1FEFFFFFF       Pchip0 PCI Config Type 0
    //   0x200000000 - 0x2FFFFFFFF       Pchip1 PCI Dense Memory (4 GB)
    //   0x380000000 - 0x38FFFFFFF       Pchip1 CSRs
    //   0x3F8000000 - 0x3FBFFFFFF       Pchip1 PCI IACK
    //   0x3FC000000 - 0x3FDFFFFFF       Pchip1 PCI I/O (dense)
    //   0x3FE000000 - 0x3FEFFFFFF       Pchip1 PCI Config Type 0
    //
    // IMPORTANT: The gap 0x100000000 - 0x17FFFFFFF is PCI sparse memory.
    //            The gap 0x190000000 - 0x19FFFFFFF is between Pchip0 CSR and Cchip.
    //            The gap 0x1C0000000 - 0x1F7FFFFFF is between Dchip and PCI IACK.
    //            Accesses to gaps should return 0 or log warnings.
    // ============================================================================

    namespace MMIOOffset {
        // Offsets from kMMIO_Start (0x800.0000.0000)
        constexpr uint64_t kPchip0_PciMem = 0x000000000ULL;
        constexpr uint64_t kPchip0_PciMem_End = 0x100000000ULL;  // 4 GB

        // PCI Sparse Space (Pchip0)
        constexpr uint64_t kPchip0_SparseMem = 0x100000000ULL;
        constexpr uint64_t kPchip0_SparseMem_End = 0x140000000ULL;  // 1 GB
        constexpr uint64_t kPchip0_SparseIO = 0x140000000ULL;
        constexpr uint64_t kPchip0_SparseIO_End = 0x180000000ULL;  // 1 GB

        // Chipset CSRs
        constexpr uint64_t kPchip0_CSR = 0x180000000ULL;
        constexpr uint64_t kPchip0_CSR_End = 0x190000000ULL;  // 256 MB
        constexpr uint64_t kCchip_CSR = 0x1A0000000ULL;
        constexpr uint64_t kCchip_CSR_End = 0x1B0000000ULL;
        constexpr uint64_t kDchip_CSR = 0x1B0000000ULL;
        constexpr uint64_t kDchip_CSR_End = 0x1C0000000ULL;

        // PCI Access Windows (Pchip0)
        constexpr uint64_t kPchip0_IACK = 0x1F8000000ULL;
        constexpr uint64_t kPchip0_IODense = 0x1FC000000ULL;
        constexpr uint64_t kPchip0_CfgType0 = 0x1FE000000ULL;

        // Pchip1 regions (Typhoon / dual-Pchip)
        constexpr uint64_t kPchip1_PciMem = 0x200000000ULL;
        constexpr uint64_t kPchip1_SparseMem = 0x300000000ULL;
        constexpr uint64_t kPchip1_SparseMem_End = 0x340000000ULL;
        constexpr uint64_t kPchip1_SparseIO = 0x340000000ULL;
        constexpr uint64_t kPchip1_SparseIO_End = 0x380000000ULL;
        constexpr uint64_t kPchip1_CSR = 0x380000000ULL;
        constexpr uint64_t kPchip1_CSR_End = 0x390000000ULL;
        constexpr uint64_t kPchip1_IACK = 0x3F8000000ULL;
        constexpr uint64_t kPchip1_IODense = 0x3FC000000ULL;
        constexpr uint64_t kPchip1_CfgType0 = 0x3FE000000ULL;

        // Gaps (no hardware mapped ++ return 0 or log warning)
        //   0x190000000 - 0x19FFFFFFF  (between Pchip0 CSR and Cchip)
        //   0x1C0000000 - 0x1F7FFFFFF  (between Dchip and IACK)

        // ====================================================================
        // Authoritative MMIO routing -- executable form of the Pchip0
        // address-space dispatch table.  The chipset owns NO addresses; it
        // routes via routeMmioOffset() and switches on RegionId.  Offsets
        // are relative to kMMIO_Start and cover the Pchip0 half
        // [0, kPchip1_PciMem); the Pchip1 half is a coarse mirror handled
        // by the caller.  Documented gaps are simply absent from the table,
        // so routeMmioOffset returns RegionId::None for any offset they
        // cover.  See project_chipset_dispatch_codegen_optimization: a
        // future codegen pass may emit a perfect hash behind this same
        // signature without changing callers.
        // ====================================================================
        enum class RegionId : uint8_t {
            None = 0,
            Pchip0_PciMem,
            Pchip0_SparseMem,
            Pchip0_SparseIO,
            Pchip0_CSR,
            Cchip_CSR,
            Dchip_CSR,
            Pchip0_IACK,
            Pchip0_IODense,
            Pchip0_CfgType0,
        };

        struct RegionRange {
            uint64_t startOff;   // inclusive
            uint64_t endOff;     // exclusive
            RegionId id;
        };

        // Sorted ascending, non-overlapping.  Windows with no explicit
        // _End constant end at the next region's base (IACK -> IODense ->
        // CfgType0 -> Pchip1 half).
        inline constexpr RegionRange kRegionTable[] = {
            { kPchip0_PciMem,    kPchip0_PciMem_End,    RegionId::Pchip0_PciMem    },
            { kPchip0_SparseMem, kPchip0_SparseMem_End, RegionId::Pchip0_SparseMem },
            { kPchip0_SparseIO,  kPchip0_SparseIO_End,  RegionId::Pchip0_SparseIO  },
            { kPchip0_CSR,       kPchip0_CSR_End,       RegionId::Pchip0_CSR       },
            { kCchip_CSR,        kCchip_CSR_End,        RegionId::Cchip_CSR        },
            { kDchip_CSR,        kDchip_CSR_End,        RegionId::Dchip_CSR        },
            { kPchip0_IACK,      kPchip0_IODense,       RegionId::Pchip0_IACK      },
            { kPchip0_IODense,   kPchip0_CfgType0,      RegionId::Pchip0_IODense   },
            { kPchip0_CfgType0,  kPchip1_PciMem,        RegionId::Pchip0_CfgType0  },
        };

        // Linear scan of the constexpr table.  N is small and fixed; this
        // is a handful of compares and only runs on actual MMIO (DRAM is
        // filtered upstream by the GuestMemory hook before the chipset is
        // reached).
        inline constexpr RegionId routeMmioOffset(uint64_t off) noexcept {
            for (auto const& r : kRegionTable) {
                if (off >= r.startOff && off < r.endOff) return r.id;
            }
            return RegionId::None;
        }

        // Compile-time invariant: sorted, non-overlapping, non-empty.
        inline constexpr bool regionTableIsOrdered() noexcept {
            uint64_t prevEnd = 0;
            for (auto const& r : kRegionTable) {
                if (r.startOff < prevEnd)  return false;   // overlap / unsorted
                if (r.endOff <= r.startOff) return false;  // empty / inverted
                prevEnd = r.endOff;
            }
            return true;
        }

        // Compile-time guards: the table cannot drift from the named region
        // constants, stays ordered, and routes the key offsets correctly.
        static_assert(regionTableIsOrdered(),
                      "kRegionTable must be sorted and non-overlapping");
        static_assert(routeMmioOffset(kCchip_CSR)        == RegionId::Cchip_CSR,
                      "Cchip routing");
        static_assert(routeMmioOffset(kDchip_CSR)        == RegionId::Dchip_CSR,
                      "Dchip routing");
        static_assert(routeMmioOffset(kPchip0_SparseMem) == RegionId::Pchip0_SparseMem,
                      "Pchip0 sparse-mem routing");
        static_assert(routeMmioOffset(kPchip0_CfgType0)  == RegionId::Pchip0_CfgType0,
                      "Pchip0 config routing");
        static_assert(routeMmioOffset(0x190000000ULL)    == RegionId::None,
                      "gap (Pchip0 CSR..Cchip) routes to None");
        static_assert(routeMmioOffset(0x1C0000000ULL)    == RegionId::None,
                      "gap (Dchip..IACK) routes to None");
    }

    // ============================================================================
    // PCI SPARSE SPACE ++ Address Translation Windows
    // ============================================================================
    // HRM Section 10.1.3: PIO Address Translation (System-to-PCI)
    //
    // Alpha systems use "sparse space" to perform byte/word-granular accesses
    // to PCI address space. Dense space only supports naturally-aligned
    // longword/quadword access. Sparse space encodes the PCI address, byte
    // lane, and transfer length into the system physical address.
    //
    // SPARSE MEMORY (Pchip0):
    //   System PA range: 0x801.0000.0000 ++ 0x801.3FFF.FFFF (1 GB window)
    //   MMIO offset:     0x100000000 ++ 0x13FFFFFFF
    //   Covers:          PCI memory space with byte-level access
    //
    // SPARSE I/O (Pchip0):
    //   System PA range: 0x801.4000.0000 ++ 0x801.7FFF.FFFF (1 GB window)
    //   MMIO offset:     0x140000000 ++ 0x17FFFFFFF
    //   Covers:          PCI I/O space with byte-level access
    //
    // SPARSE MEMORY (Pchip1, Typhoon only):
    //   System PA range: 0x803.0000.0000 ++ 0x803.3FFF.FFFF
    //   MMIO offset:     0x300000000 ++ 0x33FFFFFFF
    //
    // SPARSE I/O (Pchip1, Typhoon only):
    //   System PA range: 0x803.4000.0000 ++ 0x803.7FFF.FFFF
    //   MMIO offset:     0x340000000 ++ 0x37FFFFFFF
    //
    // ============================================================================
    // SPARSE ADDRESS ENCODING
    // ============================================================================
    //
    // System PA within sparse window encodes:
    //
    //   Bit Field     Width   Meaning
    //   ---------------------------------------------------------
    //   PA[28:5]      24      PCI address bits [23:0]
    //   PA[4:3]       2       Byte offset within longword
    //   PA[2:0]       3       Transfer length code
    //
    // Transfer Length Codes:
    //   000 = Byte      (8 bits)
    //   001 = Word      (16 bits)
    //   010 = Tribyte   (24 bits)
    //   011 = Longword  (32 bits)
    //   1xx = Reserved
    //
    // Full PCI Address Reconstruction:
    //   PCI_addr[31:0] = { HAE_MEM[31:25], PA[28:5], PA[4:3] }
    //
    //   Where HAE_MEM is the Cchip High Address Extension register
    //   that provides the upper 7 bits of the PCI address not
    //   encodable in the 24-bit sparse window.
    //
    // For sparse I/O, the same encoding applies but PA maps to
    // PCI I/O space instead of PCI memory space:
    //   PCI_io_addr[31:0] = { HAE_IO[31:25], PA[28:5], PA[4:3] }
    //
    // EXAMPLE DECODE:
    //   Failing offset from log: 0x110000300
    //   Relative to sparse mem base: 0x110000300 - 0x100000000 = 0x10000300
    //
    //   PA[28:5]  = 0x10000300 >> 5         = 0x00800018
    //   PA[4:3]   = (0x10000300 >> 3) & 0x3 = 0x0  (byte 0)
    //   PA[2:0]   = 0x10000300 & 0x7        = 0x0  (byte transfer)
    //
    //   PCI addr (low 26 bits) = (0x00800018 << 2) | 0x0 = 0x02000060
    //   Full PCI addr = { HAE_MEM[31:25], 0x02000060[24:0] }
    //
    // ============================================================================

    namespace SparseSpace {

        // ---- Pchip0 Sparse Windows (offsets from kMMIO_Start) ----
        constexpr uint64_t kPchip0_SparseMem = 0x100000000ULL;  // 0x801.0000.0000
        constexpr uint64_t kPchip0_SparseMem_End = 0x140000000ULL;  // 0x801.4000.0000
        constexpr uint64_t kPchip0_SparseMem_Size = 0x040000000ULL;  // 1 GB

        constexpr uint64_t kPchip0_SparseIO = 0x140000000ULL;  // 0x801.4000.0000
        constexpr uint64_t kPchip0_SparseIO_End = 0x180000000ULL;  // 0x801.8000.0000
        constexpr uint64_t kPchip0_SparseIO_Size = 0x040000000ULL;  // 1 GB

        // ---- Pchip1 Sparse Windows (Typhoon / dual-Pchip only) ----
        constexpr uint64_t kPchip1_SparseMem = 0x300000000ULL;  // 0x803.0000.0000
        constexpr uint64_t kPchip1_SparseMem_End = 0x340000000ULL;  // 0x803.4000.0000
        constexpr uint64_t kPchip1_SparseIO = 0x340000000ULL;  // 0x803.4000.0000
        constexpr uint64_t kPchip1_SparseIO_End = 0x380000000ULL;  // 0x803.8000.0000

        // ---- Transfer Length Codes (PA[2:0]) ----
        constexpr uint8_t kXferByte = 0x0;  // 8-bit transfer
        constexpr uint8_t kXferWord = 0x1;  // 16-bit transfer
        constexpr uint8_t kXferTribyte = 0x2;  // 24-bit transfer
        constexpr uint8_t kXferLong = 0x3;  // 32-bit transfer

        // ---- Bit field masks ----
        constexpr uint64_t kPciAddrMask = 0x1FFFFFE0ULL;  // PA[28:5]
        constexpr int      kPciAddrShift = 5;               // >> 5 to extract
        constexpr uint64_t kByteLaneMask = 0x18ULL;         // PA[4:3]
        constexpr int      kByteLaneShift = 3;               // >> 3 to extract
        constexpr uint64_t kXferLenMask = 0x07ULL;         // PA[2:0]


        // ============================================================================
        // Inline Decode Helpers
        // ============================================================================

        /// Extract PCI address bits [23:0] from sparse offset
        /// @param sparseOffset  Offset relative to sparse window base
        /// @return PCI address bits [23:0] (24 bits)
        inline constexpr uint32_t decodePciAddr(uint64_t sparseOffset) noexcept
        {
            return static_cast<uint32_t>((sparseOffset >> kPciAddrShift) & 0x00FFFFFF);
        }

        /// Extract byte lane (0-3) within longword
        /// @param sparseOffset  Offset relative to sparse window base
        /// @return Byte lane (0-3)
        inline constexpr uint8_t decodeByteLane(uint64_t sparseOffset) noexcept
        {
            return static_cast<uint8_t>((sparseOffset >> kByteLaneShift) & 0x3);
        }

        /// Extract transfer length code (0=byte, 1=word, 2=tribyte, 3=long)
        /// @param sparseOffset  Offset relative to sparse window base
        /// @return Transfer length code (0-3)
        inline constexpr uint8_t decodeXferLen(uint64_t sparseOffset) noexcept
        {
            return static_cast<uint8_t>(sparseOffset & kXferLenMask);
        }

        /// Get transfer size in bytes from transfer length code
        /// @param xferLen  Transfer length code (0-3)
        /// @return Size in bytes (1, 2, 3, or 4)
        inline constexpr uint8_t xferLenToBytes(uint8_t xferLen) noexcept
        {
            return static_cast<uint8_t>(xferLen + 1);
        }

        /// Reconstruct full PCI byte address (low 26 bits, without HAE)
        /// Combines PCI address bits with byte lane for byte-precise addressing
        /// @param sparseOffset  Offset relative to sparse window base
        /// @return PCI address with byte precision (26 bits)
        inline constexpr uint32_t decodePciByteAddr(uint64_t sparseOffset) noexcept
        {
            uint32_t pciAddr = decodePciAddr(sparseOffset);
            uint8_t  byteLane = decodeByteLane(sparseOffset);
            return (pciAddr << 2) | byteLane;
        }

        /// Apply HAE (High Address Extension) to form full 32-bit PCI address
        /// @param pciByteAddr  Low 26-bit PCI address from decodePciByteAddr()
        /// @param hae          HAE register value (HAE_MEM or HAE_IO)
        /// @return Full 32-bit PCI address
        inline constexpr uint32_t applyHAE(uint32_t pciByteAddr, uint64_t hae) noexcept
        {
            // HAE provides bits [31:25], pciByteAddr provides bits [24:0]
            return (static_cast<uint32_t>(hae) & 0xFE000000U) | (pciByteAddr & 0x01FFFFFFU);
        }

        /// Check if an MMIO offset falls in Pchip0 sparse memory
        inline constexpr bool isPchip0SparseMem(uint64_t mmioOffset) noexcept
        {
            return mmioOffset >= kPchip0_SparseMem && mmioOffset < kPchip0_SparseMem_End;
        }

        /// Check if an MMIO offset falls in Pchip0 sparse I/O
        inline constexpr bool isPchip0SparseIO(uint64_t mmioOffset) noexcept
        {
            return mmioOffset >= kPchip0_SparseIO && mmioOffset < kPchip0_SparseIO_End;
        }

        /// Check if an MMIO offset falls in Pchip1 sparse memory
        inline constexpr bool isPchip1SparseMem(uint64_t mmioOffset) noexcept
        {
            return mmioOffset >= kPchip1_SparseMem && mmioOffset < kPchip1_SparseMem_End;
        }

        /// Check if an MMIO offset falls in Pchip1 sparse I/O
        inline constexpr bool isPchip1SparseIO(uint64_t mmioOffset) noexcept
        {
            return mmioOffset >= kPchip1_SparseIO && mmioOffset < kPchip1_SparseIO_End;
        }

    } // namespace SparseSpace

    // ============================================================================
    // COMPLETE PCHIP0 ADDRESS SPACE MAP (offsets from kMMIO_Start)
    // ============================================================================
    // This is the authoritative dispatch table for the TsunamiChipset
    // mmioRead/mmioWrite handlers. Every access must match exactly ONE entry.
    //
    //   Offset Range                    Target          Handler
    //   --------------------------------------------------------------
    //   0x000000000 - 0x0FFFFFFFF       PCI Dense Mem   Pchip (PCI memory)
    //   0x100000000 - 0x13FFFFFFF       PCI Sparse Mem  Pchip (decode + PCI memory)
    //   0x140000000 - 0x17FFFFFFF       PCI Sparse I/O  Pchip (decode + I/O port)
    //   0x180000000 - 0x18FFFFFFF       Pchip0 CSRs     Pchip (readCSR/writeCSR)
    //   0x190000000 - 0x19FFFFFFF       (gap)           return 0 / log warning
    //   0x1A0000000 - 0x1AFFFFFFF       Cchip CSRs      Cchip
    //   0x1B0000000 - 0x1BFFFFFFF       Dchip CSRs      Dchip
    //   0x1C0000000 - 0x1F7FFFFFF       (gap)           return 0 / log warning
    //   0x1F8000000 - 0x1FBFFFFFF       PCI IACK        Pchip (interrupt ack)
    //   0x1FC000000 - 0x1FDFFFFFF       PCI Dense I/O   Pchip (I/O port)
    //   0x1FE000000 - 0x1FEFFFFFF       PCI Config T0   Pchip (config space)
    //   0x200000000 - 0x3FFFFFFFF       Pchip1 space    (mirror for dual-Pchip)
    //
    // VERIFIED AGAINST CRASH LOG:
    //   offset 0x1300005c0 -> Sparse Mem -> PCI addr decode
    //   offset 0x1100002c0 -> Sparse Mem -> PCI addr decode
    //   offset 0x110000300 -> Sparse Mem -> PCI addr decode
    //   offset 0x130000040 -> Sparse Mem -> PCI addr decode
    //
    // These were falling through as "unhandled" because the sparse memory
    // region (0x100000000-0x13FFFFFFF) had no dispatch entry.
    // ============================================================================

    // ============================================================================
    // SRM BOOT-CRITICAL REGISTERS
    // ============================================================================
    // These registers MUST return valid data for SRM firmware to boot.
    // Listed in approximate order of first access during boot.
    //
    // Cchip:
    //   CSC    ++ CPU count, memory configuration (SRM reads first)
    //   MISC   ++ chip revision, NXM status, error flags
    //   AAR0-3 ++ memory array sizing (SRM uses to build HWRPB memory map)
    //   DIM0-1 ++ interrupt mask (SRM configures interrupt routing)
    //   DIR0-1 ++ interrupt request (SRM polls for pending interrupts)
    //   DRIR   ++ raw interrupt (SRM checks for device interrupts)
    //   TTR    ++ interval timer (SRM uses for timeouts/delays)
    //
    // Dchip:
    //   DREV   ++ revision (SRM reads to identify Dchip version)
    //   DSC    ++ system config (SRM reads for memory path info)
    //
    // Pchip:
    //   PCTL     ++ PCI bridge control (SRM reads/writes during PCI init)
    //   PERROR   ++ error status (SRM checks after PCI operations)
    //   PERRMASK ++ error mask (SRM configures error reporting)
    //   WSBA0-3  ++ DMA window base (SRM configures for OS)
    //   WSM0-3   ++ DMA window mask
    //   TBA0-3   ++ DMA translation base
    // ============================================================================

    // ============================================================================
    // ES40 SPECIFIC INITIALIZATION VALUES
    // ============================================================================
    // These match the ES40 platform as configured in the EMulatR.

    namespace ES40_Init {
        // Cchip CSC ++ System Configuration
        //   Bits [3:0]:   CPU count - 1 (0 = 1 CPU)
        //   Bits [7:4]:   System revision
        //   Bits [35:32]: Cchip revision
        constexpr uint64_t kCSC_1CPU_32GB = 0x0000000000000000ULL;  // Placeholder ++ set per config

        // Cchip MISC
        //   Bits [39:32]: Chip revision (0x10 typical)
        //   Bit 28:       ABW (address bus width, 0 = 34-bit, 1 = 44-bit)
        constexpr uint64_t kMISC_Default = 0x0000001000000000ULL;  // Rev 1.0, 44-bit

        // Dchip DREV
        constexpr uint64_t kDREV_Default = 0x10ULL;               // Revision 1.0

        // Pchip PCTL ++ default after reset
        constexpr uint64_t kPCTL_Default = 0x0000000000000000ULL;  // All features disabled
    }

} // namespace Tsunami21272

#endif // TSUNAMI_21272_REGISTER_MAP_H