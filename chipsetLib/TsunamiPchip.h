// ============================================================================
// TsunamiPchip.h -- Tsunami Chipset Pchip (PCI Host Bridge)
// ============================================================================
// Project: ASA-EMulatR - Alpha AXP Architecture Emulator
// Copyright (C) 2025, 2026 eNVy Systems, Inc. All rights reserved.
// Licensed under eNVy Systems Non-Commercial License v1.1
//
// Project Architect: Timothy Peer
// AI Code Generation: Claude (Anthropic)
// ============================================================================
//
// PURPOSE:
//   Emulates the Tsunami Pchip -- the PCI host bridge that provides:
//     - PCI window configuration (memory and I/O space mapping)
//     - PCI configuration space access (Type 0 / Type 1 cycles)
//     - PCI I/O space access (legacy ISA port routing)
//     - PCI control and error reporting
//
// MMIO ADDRESS:
//   Pchip0 Base PA: 0x801.8000.0000 (fixed by Tsunami architecture)
//   Size:           512 MB (0x2000.0000) -- includes CSR + PCI windows
//
//   Tsunami Pchip address space layout:
//     0x801.8000.0000 + 0x0000.0000  CSR registers (WSBA, WSM, TBA, PCTL, ...)
//     0x801.8000.0000 + 0x0100.0000  PCI sparse I/O space
//     0x801.8000.0000 + 0x0180.0000  PCI dense I/O space
//     0x801.8000.0000 + 0x0200.0000  PCI Type 0 config space
//     0x801.8000.0000 + 0x0280.0000  PCI Type 1 config space
//     0x801.8000.0000 + 0x0300.0000  PCI sparse memory space
//     0x801.8000.0000 + 0x0380.0000  PCI dense memory space
//
// PCI CONFIG SPACE DECODE:
//   Type 0 (bus 0):
//     PA = Pchip_Config_Base + (device << 11) + (function << 8) + (reg & 0xFC)
//     Extracts: device [15:11], function [10:8], register [7:0]
//
//   Type 1 (bus > 0):
//     PA encodes bus/device/function for forwarding through bridges.
//     Not needed until PCI-to-PCI bridges are implemented.
//
// PCI I/O SPACE DECODE:
//   Dense I/O:
//     PA = Pchip_IO_Dense_Base + port_address
//     //Port address = PA & 0x00FFFFFF
//     Port address = PA & 0xFC0 
//
// IMPLEMENTATION:
//   Minimal viable stub with:
//     - Read/write storage for window configuration registers
//     - PCI config space dispatch to registered device handlers
//     - PCI I/O space dispatch for UART access through ISA bridge
//     - Empty slot returns 0xFFFFFFFF (no device present)
//
// REFERENCES:
//   Tsunami/Typhoon Hardware Reference Manual (EC-RE2CA-TE)
//   Chapter 3: Pchip Registers
//   Chapter 6: PCI Address Translation
//
// ============================================================================
// TODO Register Table -- unwired or partially-wired behavior (Phase B)
// ============================================================================
// Phase B (2026-05-17) added uniform CSR diagnostic instrumentation
// against the Phase A spec scaffolding.  PERROR's W1C and PERRSET's
// W1S semantics were already correct in the prior code (no change in
// Phase B; just confirmed and commented).  The HRM-defined registers
// below have storage but their PCI-side behavior is NOT yet wired.
// Each carries a `// TODO(unwired): ...` line comment at its decode
// site pointing back here.
//
//   WSBA0-3 / WSM0-3 / TBA0-3 -- DMA window storage; no scatter-
//                   gather translation engine.  PCI DMA cycles bypass
//                   the window check today (no DMA traffic reaches
//                   Pchip in current bring-up scope).
//   PCTL          -- storage RW; bits stored but no enforcement of
//                   chaindis / hole / mwin / ptevrfy semantics.
//                   Firmware reads back what it writes.
//   PLAT          -- storage RW; no master-latency-timer behavior.
//   PERROR        -- W1C already wired (correct in prior code).
//                   Error-injection paths are not wired -- nothing
//                   in V4 asserts NDS, TA, RDPE, PERR, SGE, APE,
//                   SERR, or DCRTO conditions today.
//   PERRMASK      -- storage RW; no interrupt routing on error.
//   PERRSET       -- W1S already wired; diagnostic use only.
//   TLBIV / TLBIA -- WO; no Pchip TLB model.  Writes are sinks.
//   PMONCTL       -- storage RW; no perf monitor counter.
//   PMONCNT       -- storage RO; no perf monitor counter source.
//   SPRST         -- WO; PCI software-reset has no physical effect.
//
// ============================================================================
// CHANGE HISTORY
// ============================================================================
//
//   2026-05-17  Phase B uniform-CSR-surface refactor.  Surface: new
//               readCSR(offset, cpuId) / writeCSR(offset, value,
//               cpuId) overloads (cpuId unused in Pchip today;
//               uniform surface for Phase C dispatch parity).
//               Diagnostics: every recognized case in readCSR /
//               writeCSR instruments CSR_LOG_R / CSR_LOG_W against
//               the spec name.  PERROR (W1C) and PERRSET (W1S)
//               semantics confirmed -- no behavior change.  Storage
//               unchanged.  The outer read(offset, width) /
//               write(offset, value, width) dispatcher path is
//               unchanged; coarse-grained PCI-mem / sparse-IO /
//               Pchip1-stub blocks retain their throttled forensic
//               stderr backstops.
//
// ============================================================================

#ifndef TSUNAMI_PCHIP_H
#define TSUNAMI_PCHIP_H


#include <QDataStream>
#include <atomic>
#include <cstdio>
#include <cstdlib>   // std::getenv (PCICFG-TRACE diagnostic)
#include <map>
#include <memory>
#include <vector>
#include <fmt/base.h>

#include "CsrDiag.h"                // Phase B: per-CSR-access diagnostic sink
#include "Tsunami21272_CsrSpec.h"   // Phase B: spec parity
#include "Tsunami21272_RegisterMap.h"
#include "TsunamiVariant.h"         // Ticket 1.5: chipset variant
#include "IDeviceHandlers.h"        // IPciDeviceHandler, IIoPortHandler, IoPortRange

class TsunamiChipset; // Forward declaration

// ============================================================================
// PCI Device Handler Interface
// ============================================================================


// 1. Unified Interface for Memory-Mapped I/O regions hooked to the System Bus
class IMmioRegion
{
public:
    virtual      ~IMmioRegion() = default;
    virtual auto Read(uint64_t physical_addr, size_t size_bytes) -> uint32_t = 0;
    virtual auto Write(uint64_t physical_addr, uint32_t value, size_t size_bytes) -> void = 0;
};

// 2. Interface for downstream PCI Endpoint devices
class IPciDevice
{
public:
    virtual      ~IPciDevice() = default;
    virtual auto GetVendorId() const -> uint16_t = 0;
    virtual auto GetDeviceId() const -> uint16_t = 0;

    // Handles reading/writing the 256-byte standard configuration space header
    virtual auto ConfigRead(uint32_t reg_offset, size_t size_bytes) -> uint32_t = 0;
    virtual auto ConfigWrite(uint32_t reg_offset, uint32_t value, size_t size_bytes) -> void = 0;
};

// 3. Interface for the PCI Bus controller handling enumeration and topology
class IPciBus
{
public:
    virtual      ~IPciBus() = default;
    virtual auto RegisterDevice(uint8_t device_num, std::shared_ptr<IPciDevice> device) -> void = 0;
    virtual auto FindDevice(uint8_t device_num) -> std::shared_ptr<IPciDevice> = 0;
};

// Interface for PCI Memory/IO operations
class IPciMemoryHandler
{
public:
    virtual auto write(uint64_t addr, uint64_t val, uint8_t width) -> bool = 0;
    virtual auto read(uint64_t addr, uint64_t& val, uint8_t width) -> bool = 0;
};

// IPciDeviceHandler, IIoPortHandler, and IoPortRange moved to the neutral
// plug-in-seam header IDeviceHandlers.h (included above) so device modules
// can implement them without pulling in this full Pchip TU.  The dead
// function-pointer scaffolding (PciDeviceEntry / IoPortEntry, "TEP 20260314")
// was unreferenced and has been removed.

// ============================================================================
// TsunamiPchip
// ============================================================================

class TsunamiPchip
{
public:
    // ========================================================================
    // Pchip0 base address and sub-region offsets (fixed by architecture)
    // ========================================================================
    // # ============================================================================
    // # TsunamiPchip.h -- Address constant corrections
    // # ============================================================================
    // #
    // # HRM Table 10 - 1 System Address Map :
    // #   Pchip0 CSRs          : 0x801.8000.0000  (256MB)
    // #   Pchip0 PCI memory    : 0x800.0000.0000  (4GB linear)
    // #   PCI IACK / special   : 0x801.F800.0000  (64MB)
    // #   Pchip0 PCI I / O     : 0x801.FC00.0000  (32MB)
    // #   Pchip0 PCI config    : 0x801.FE00.0000  (16MB)
    // #
    // # Current code uses offsets from kBasePA(0x8018000000) which
    // # does NOT match the HRM.PCI I / O and config are at completely
    // # different base addresses.
    // #
    // # Two options :
    // #   A) Register separate MMIO regions per sub - space(clean)
    // #   B) Make kBasePA cover the full 8GB Pchip0 space and compute
    // #      offsets from 0x8000000000 (matches current dispatch code)
    // #
    // # Option B is simpler and matches current architecture :
    // #
    // # ============================================================================

    // The entire Pchip0 space spans 0x800.0000.0000 to 0x801.FFFF.FFFF
    // Register as one 8GB region starting at the PCI memory base.
    // All offsets are relative to this base.
    static constexpr uint64_t kBasePA = 0x80000000000ULL;  // 0x800.0000.0000
    //static constexpr uint64_t kSize = 0x200000000ULL;    
    static constexpr uint64_t kSize = 0x400000000ULL;  // 16GB -- covers Pchip0 + Pchip1
    // Sub-region offsets (relative to kBasePA = 0x800.0000.0000)
    static constexpr uint64_t kPciMemOffset = 0x000000000ULL;            // 0x800.0000.0000 PCI memory (4GB)
    static constexpr uint64_t kCSROffset    = 0x180000000ULL;               // 0x801.8000.0000 Pchip CSRs
    static constexpr uint64_t kCSRSize      = 0x010000000ULL;                 // 256MB

    // These are at fixed absolute PAs per HRM Table 10-1:
    static constexpr uint64_t kIACKOffset     = 0x1F8000000ULL;              // 0x801.F800.0000 PCI IACK
    static constexpr uint64_t kIODenseOffset  = 0x1FC000000ULL;           // 0x801.FC00.0000 PCI I/O (32MB)
    static constexpr uint64_t kCfgType0Offset = 0x1FE000000ULL;          // 0x801.FE00.0000 PCI config (16MB)

    // ========================================================================
    // CSR register offsets
    // ========================================================================
    /* 
     // readCSR now uses Pchip:: namespace
     static constexpr uint64_t kWSBA0    = 0x0000;    // Window Space Base 0
     static constexpr uint64_t kWSBA1    = 0x0040;
     static constexpr uint64_t kWSBA2    = 0x0080;
     static constexpr uint64_t kWSBA3    = 0x00C0;
     static constexpr uint64_t kWSM0     = 0x0100;    // Window Space Mask 0
     static constexpr uint64_t kWSM1     = 0x0140;
     static constexpr uint64_t kWSM2     = 0x0180;
     static constexpr uint64_t kWSM3     = 0x01C0;
     static constexpr uint64_t kTBA0     = 0x0200;    // Translated Base 0
     static constexpr uint64_t kTBA1     = 0x0240;
     static constexpr uint64_t kTBA2     = 0x0280;
     static constexpr uint64_t kTBA3     = 0x02C0;
     static constexpr uint64_t kPCTL     = 0x0300;    // PCI Control
     static constexpr uint64_t kPLAT     = 0x0340;    // PCI Latency
     static constexpr uint64_t kRES = 0x0380;    // NEW -- reserved
     static constexpr uint64_t kPERROR = 0x03C0;    // WAS 0x0800  
     static constexpr uint64_t kPERRMASK = 0x0400;    // WAS 0x0840  
     static constexpr uint64_t kPERRSET = 0x0440;    // NEW -- write-only
     static constexpr uint64_t kTLBIV = 0x0480;    // WAS 0x0C80  
     static constexpr uint64_t kTLBIA = 0x04C0;    // WAS 0x0CC0  
     static constexpr uint64_t kPMONCTL = 0x0500;    // NEW
     static constexpr uint64_t kPMONCNT = 0x0540;    // NEW -- read-only
     static constexpr uint64_t kSPRST = 0x0800;    // NEW -- PCI software reset
     */


    // ========================================================================
    // Construction
    // ========================================================================

    explicit TsunamiPchip(ChipsetVariant variant = ChipsetVariant::Tsunami) noexcept
        : m_variant(variant)
    {
        reset();

        static_assert(kBasePA + kCSROffset == 0x80180000000ULL, "Pchip CSR address mismatch with HRM");
        static_assert(kBasePA + kIODenseOffset == 0x801FC000000ULL, "Pchip I/O address mismatch with HRM");
        static_assert(kBasePA + kCfgType0Offset == 0x801FE000000ULL, "Pchip config address mismatch with HRM");
    }

    // --- Bus Arbiter Gatekeeper Interface ---
    // These methods replace the old Pchip0 direct access logic.
    auto routeMmioRead(uint64_t pa, uint8_t width, uint64_t& out) noexcept -> bool
    {
        // 1. Calculate offset relative to Pchip base
        uint64_t offset = pa - Tsunami21272::Base::kTsunamiBase;
        auto     region = Tsunami21272::MMIOOffset::routeMmioOffset(offset);

        // 2. Dispatch based on RegionId
        switch (region)
        {
        case Tsunami21272::MMIOOffset::RegionId::Pchip0_CSR:
        case Tsunami21272::MMIOOffset::RegionId::Cchip_CSR:
        case Tsunami21272::MMIOOffset::RegionId::Dchip_CSR:
            out = handleCsrRead(offset, width);
            return true;
        // Route to ISA/IO Bus bridge
        case Tsunami21272::MMIOOffset::RegionId::Pchip0_SparseIO:
            // Route via interface, NOT direct bridge member
            if (m_ioPortHandler)
            {
                // The port calculation is specific to Sparse I/O mapping
                uint16_t port = static_cast<uint16_t>((offset >> 5) & 0xFFFF);
                out           = m_ioPortHandler->ioRead(port, width);
                return true;
            }

        default:
            return false; // NXM
        }
    }

    auto routeMmioWrite(uint64_t pa, uint8_t width, uint64_t val) noexcept -> bool
    {
        uint64_t offset = pa - Tsunami21272::Base::kTsunamiBase;
        auto     region = Tsunami21272::MMIOOffset::routeMmioOffset(offset);

        switch (region)
        {
        case Tsunami21272::MMIOOffset::RegionId::Pchip0_CSR:
        case Tsunami21272::MMIOOffset::RegionId::Cchip_CSR:
        case Tsunami21272::MMIOOffset::RegionId::Dchip_CSR:
            handleCsrWrite(offset, width, val);
            return true;

        case Tsunami21272::MMIOOffset::RegionId::Pchip0_SparseMem:
            return handleSparseMemWrite(offset, width, val);

        case Tsunami21272::MMIOOffset::RegionId::Pchip0_SparseIO:
            return handleSparseIoWrite(offset, width, val);

        case Tsunami21272::MMIOOffset::RegionId::Pchip0_IODense:
            return handleDenseWrite(offset, width, val);

        case Tsunami21272::MMIOOffset::RegionId::Pchip0_CfgType0:
            return handleConfigWrite(offset, width, val);

        case Tsunami21272::MMIOOffset::RegionId::Pchip0_IACK:
            // IACK is usually read-only; writes may be ignored or fault
            return false;

        case Tsunami21272::MMIOOffset::RegionId::None:
        default:
            return false; // NXM / Unmapped
        }
    }

    // Add this setter to allow the Chipset to wire the bridge
    auto setIoPortHandler(IIoPortHandler* handler) noexcept -> void
    {
        m_ioPortHandler = handler;
    }

    // Ticket 1.5: chipset variant (inert in Pchip today -- present for
    // symmetry with Cchip/Dchip and the chipset consistency guard).
    auto variant() const noexcept -> ChipsetVariant { return m_variant; }

    // ========================================================================
    // Reset
    // ========================================================================

    auto reset() noexcept -> void
    {
        for (int i = 0; i < 4; ++i)
        {
            m_wsba[i] = 0;
            m_wsm[i]  = 0;
            m_tba[i]  = 0;
        }
        m_pctl     = 0;
        m_plat     = 0;
        m_perror   = 0;
        m_perrmask = 0;
        m_pmonctl  = 0;
        m_pmoncnt  = 0;
        // INFO_LOG("TsunamiPchip: reset");
    }

    // ========================================================================
    // PCI Device Registration
    // ========================================================================

    /**
     * @brief Register a PCI device at (bus, device, function)
     * @param bus      PCI bus number (0 for Type 0)
     * @param device   PCI device number (0-31)
     * @param function PCI function number (0-7)
     * @param handler  Device handler for config space access
     */
    auto registerPciDevice(uint8_t            bus, uint8_t device, uint8_t function,
                           IPciDeviceHandler* handler) noexcept -> void
    {
        uint32_t key      = makeBDF(bus, device, function);
        m_pciDevices[key] = handler;

        std::fprintf(stderr, "TsunamiPchip: registered PCI %02d:%02d.%d\n",
            static_cast<int>(bus),
            static_cast<int>(device),
            static_cast<int>(function));

    }

    // ========================================================================
    // I/O Port Registration
    // ========================================================================

    /**
     * @brief Register an I/O port range handler
     * @param startPort First port in range (inclusive)
     * @param endPort   Last port in range (exclusive)
     * @param handler   I/O port handler
     */
    auto registerIoPortRange(uint16_t        startPort, uint16_t endPort,
                             IIoPortHandler* handler) noexcept -> void
    {
        m_ioPortRegistry.push_back({startPort, endPort, handler});


        std::fprintf(stderr, "TsunamiPchip: registered I/O ports 0x%04X-0x%04X\n",
            static_cast<unsigned int>(startPort),
            static_cast<unsigned int>(endPort - 1));

    }

    // ========================================================================
    // PCI Dense-Memory Claim Registration (G3-lite, 2026-06-03)
    // ========================================================================

    /**
     * @brief Register a fixed-range claimant in PCI dense memory space.
     * @param start    Window-relative offset, inclusive (< 0x1_0000_0000)
     * @param end      Window-relative offset, exclusive
     * @param handler  Reused IIoPortHandler; receives offset - start as
     *                 the port parameter (so a 2-register device sees 0/1).
     *
     * Claims are consulted in read()/write() BEFORE the all-ones float /
     * UNHANDLED fallthrough -- see V4_IO_Machinery_Map.txt sec. 7 rule A
     * (no silent absorbers).  First consumer: PCF8584 IIC at 0xFFFF0000.
     */
    auto registerPciMemRange(uint64_t start, uint64_t end,
                             IIoPortHandler* handler) noexcept -> void
    {
        // Rebased offset must fit IIoPortHandler's uint16_t port parameter.
        if (end <= start || (end - start) > 0x10000ULL) {
            std::fprintf(stderr,
                "TsunamiPchip: registerPciMemRange REJECTED "
                "start=0x%llx end=0x%llx (span > 64K or empty)\n",
                static_cast<unsigned long long>(start),
                static_cast<unsigned long long>(end));
            return;
        }
        m_pciMemRegistry.push_back({start, end, handler});

        std::fprintf(stderr,
            "TsunamiPchip: registered PCI mem 0x%08llX-0x%08llX\n",
            static_cast<unsigned long long>(start),
            static_cast<unsigned long long>(end - 1));
    }

    // ========================================================================
    // MMIO Read Handler (top-level dispatch)
    // ========================================================================

    /**
     * @brief Top-level MMIO read handler
     *
     * Dispatches based on offset within the Pchip address space:
     *   CSR region      -> read CSR register
     *   Config Type 0   -> read PCI config space (bus 0)
     *   Dense I/O       -> read I/O port
     *   Other regions   -> log and return 0
     */
    static auto mmioRead(void* ctx, uint64_t offset, uint8_t width) noexcept -> uint64_t
    {
        auto* self = static_cast<TsunamiPchip*>(ctx);
        return self->read(offset, width);
    }

    // # ============================================================================
    // # 6. TsunamiPchip.h -- read() / write() offset dispatch update
    // # ============================================================================
    // #
    // # The mmio handler receives(offset = PA - kBasePA).
    // # CSR access : offset in[0x180000000, 0x190000000)
    // # PCI I / O:    offset in[0x1FC000000, 0x1FE000000)
    // # PCI config : offset in[0x1FE000000, 0x1FF000000)
    // #
    // # Update the dispatch in read() and write() :
    // #
    // # ============================================================================

    inline auto read(uint64_t offset, uint8_t width) const noexcept -> uint64_t
    {
        // ================================================================
        // UARTBP#4 -- Pchip::read entry, diagnostic 2026-05-28
        // ================================================================
        // Fires once at first UART-PA hit (recognised by offset, since
        // this layer no longer sees the original PA).  Dense I/O range
        // expected: 0x1FC000000 <= offset < 0x1FE000000, so offsets
        // 0x1FC0003F8 (THR/RBR) and 0x1FC0003FD (LSR) should both land
        // in the dense-I/O branch below.  If BP#5 doesn't fire after
        // BP#4 does, the range check at line ~481 is wrong.
        //
        // REMOVAL TRIGGER: delete when LSR-wedge diagnostic is closed.
        // ================================================================
#if EMULATR_BRINGUP_PROBES
        {
            static std::atomic<bool> s_fired{false};
            bool const isUartOff =
                (offset == 0x1FC0003F8ULL) || (offset == 0x1FC0003FDULL);
            if (isUartOff &&
                !s_fired.exchange(true, std::memory_order_acq_rel))
            {
                std::fprintf(stderr,
                             "UARTBP#4 Pchip::read entry  off=0x%012llx "
                             "width=%u kIODenseOffset=0x%012llx\n",
                             static_cast<unsigned long long>(offset),
                             static_cast<unsigned>(width),
                             static_cast<unsigned long long>(kIODenseOffset));
                std::fflush(stderr);
                // __debugbreak();  // 2026-05-28: muted post-verification; probe still emits stderr marker.
            }
        }
#endif

        // PCI sparse memory: offset 0x100000000 - 0x13FFFFFFF
        if (offset >= 0x100000000ULL && offset < 0x140000000ULL)
            return 0xFFFFFFFFULL;  // no PCI memory device present

        // Pchip1 stub -- no second PCI hose present
        if (offset >= 0x200000000ULL && offset < 0x400000000ULL)
        {
            return 0;  // read: no device; write: discard
        }

        // PCI sparse I/O: offset 0x140000000 - 0x17FFFFFFF
        if (offset >= 0x140000000ULL && offset < 0x180000000ULL)
            return 0xFFFFFFFFULL;  // no sparse I/O device


        // CSR registers
        if (offset >= kCSROffset && offset < kCSROffset + kCSRSize)
        {
            return readCSR(offset - kCSROffset);
        }

        // PCI Type 0 configuration space
        if (offset >= kCfgType0Offset &&
            offset < kCfgType0Offset + 0x01000000ULL)  // 16MB
        {
            return readPciConfig0(offset - kCfgType0Offset, width);
        }

        // PCI dense I/O space
        if (offset >= kIODenseOffset &&
            offset < kIODenseOffset + 0x02000000ULL)    // 32MB
        {
            uint16_t port = static_cast<uint16_t>(
                (offset - kIODenseOffset) & 0xFFFF);

            // ============================================================
            // UARTBP#5 -- dense-I/O range matched, diagnostic 2026-05-28
            // ============================================================
            // Fires once at first UART access that successfully matched
            // the dense-I/O range and produced a port value.  Expected
            // port: 0x3F8 (THR/RBR) or 0x3FD (LSR).  If port is anything
            // else, the offset-to-port arithmetic is wrong.
            //
            // REMOVAL TRIGGER: delete when LSR-wedge diagnostic is closed.
            // ============================================================
#if EMULATR_BRINGUP_PROBES
            {
                static std::atomic<bool> s_fired{false};
                bool const isUartPort = (port == 0x3F8) || (port == 0x3FD);
                if (isUartPort &&
                    !s_fired.exchange(true, std::memory_order_acq_rel))
                {
                    std::fprintf(stderr,
                                 "UARTBP#5 Pchip::read dense-I/O match  "
                                 "off=0x%012llx port=0x%04x width=%u\n",
                                 static_cast<unsigned long long>(offset),
                                 static_cast<unsigned>(port),
                                 static_cast<unsigned>(width));
                    std::fflush(stderr);
                    // __debugbreak();  // 2026-05-28: muted post-verification; probe still emits stderr marker.
                }
            }
#endif

            return readIoPort(port, width);
        }

        // PCI memory space (4GB)
        if (offset < 0x100000000ULL)
        {
            // G3-lite claimants first (2026-06-03): fixed-range devices in
            // dense memory space.  The PCF8584 IIC status read at
            // 0xFFFF0001 previously hit the all-ones float below, decoding
            // as S1 = 0xFF -> LAB=1 -> the unbounded lost-arbitration
            // retry loop in iic_rw_common (the powerup stall).
            for (auto const& r : m_pciMemRegistry) {
                if (offset >= r.start && offset < r.end) {
                    return r.handler->ioRead(
                        static_cast<uint16_t>(offset - r.start), width);
                }
            }
            /*
            // PCI memory read -- stub for now
            TRACE_LOG(QString("TsunamiPchip: PCI memory read at offset 0x%1")
                .arg(offset, 10, 16, QChar('0')));
                */
            return 0xFFFFFFFFFFFFFFFFULL;
        }

        // Forensic: throttled stderr for outer-dispatcher read fallthrough.
        // Reached only when offset matches NONE of the PCI mem / sparse I/O /
        // Pchip1 stub / CSR / Config0 / dense I/O / PCI memory ranges above.
        // Indicates an offset the firmware is poking that V4's Pchip model
        // does not recognise at all.  Distinct from the readCSR/writeCSR
        // unknown-register paths, which only fire when offset IS in CSR
        // space but the specific register is unmodelled.
#if EMULATR_BRINGUP_PROBES
        {
            static std::atomic<uint64_t> s_cnt{0};
            const uint64_t               n = s_cnt.fetch_add(1, std::memory_order_relaxed);
            if (n < 32)
            {

                std::fprintf(stderr,
                             "TsunamiPchip: UNHANDLED OUTER READ offset=0x%012llx "
                             "width=%u (event %llu)\n",
                             static_cast<unsigned long long>(offset),
                             static_cast<unsigned>(width),
                             static_cast<unsigned long long>(n));

                std::fflush(stderr);
            }
            else if ((n & 0xFFFFu) == 0)
            {

                std::fprintf(stderr,
                             "TsunamiPchip: %llu outer-read unhandled events "
                             "(loud-stderr muted past 32)\n",
                             static_cast<unsigned long long>(n + 1));

                std::fflush(stderr);
            }
        }
#endif

        return 0;
    }

    // ========================================================================
    // MMIO Write Handler (top-level dispatch)
    // ========================================================================

    static auto mmioWrite(void* ctx, uint64_t offset, uint64_t value, uint8_t width) noexcept -> void
    {
        auto* self = static_cast<TsunamiPchip*>(ctx);
        self->write(offset, value, width);
    }

    inline auto write(uint64_t offset, uint64_t value, uint8_t width) noexcept -> void
    {
        // PCI dense memory claimants (G3-lite, 2026-06-03).  Checked first:
        // dense memory is offset < 0x1_0000_0000, disjoint from every other
        // branch below.  Unclaimed dense-mem writes still reach the
        // UNHANDLED OUTER WRITE forensic log at the bottom (rule A:
        // no silent absorbers).
        if (offset < 0x100000000ULL) {
            for (auto const& r : m_pciMemRegistry) {
                if (offset >= r.start && offset < r.end) {
                    r.handler->ioWrite(
                        static_cast<uint16_t>(offset - r.start), value, width);
                    return;
                }
            }
        }

        // PCI sparse memory -- absorb writes
        if (offset >= 0x100000000ULL && offset < 0x140000000ULL)
            return;


        // Pchip1 stub -- no second PCI hose present
        if (offset >= 0x200000000ULL && offset < 0x400000000ULL)
        {
            return;  // read: no device; write: discard
        }

        // PCI sparse I/O -- absorb writes
        if (offset >= 0x140000000ULL && offset < 0x180000000ULL)
            return;


        // CSR registers
        if (offset >= kCSROffset && offset < kCSROffset + kCSRSize)
        {
            writeCSR(offset - kCSROffset, value);
            return;
        }

        // PCI Type 0 configuration space
        if (offset >= kCfgType0Offset &&
            offset < kCfgType0Offset + 0x01000000ULL)
        {
            writePciConfig0(offset - kCfgType0Offset,
                            static_cast<uint32_t>(value), width);
            return;
        }

        // PCI dense I/O space
        if (offset >= kIODenseOffset &&
            offset < kIODenseOffset + 0x02000000ULL)
        {
            uint16_t port = static_cast<uint16_t>(
                (offset - kIODenseOffset) & 0xFFFF);
            writeIoPort(port, value, width);
            return;
        }
        // Forensic: throttled stderr for outer-dispatcher write fallthrough.
        // Matching block to the read-side instrumentation above.  Writes
        // that land here are completely lost -- V4 has no model for the
        // address range -- so logging them is the only way to surface the
        // firmware's intent.
#if EMULATR_BRINGUP_PROBES
        {
            static std::atomic<uint64_t> s_cnt{0};
            const uint64_t               n = s_cnt.fetch_add(1, std::memory_order_relaxed);
            if (n < 32)
            {

                std::fprintf(stderr,
                             "TsunamiPchip: UNHANDLED OUTER WRITE offset=0x%012llx "
                             "value=0x%016llx width=%u (event %llu)\n",
                             static_cast<unsigned long long>(offset),
                             static_cast<unsigned long long>(value),
                             static_cast<unsigned>(width),
                             static_cast<unsigned long long>(n));

                std::fflush(stderr);
            }
            else if ((n & 0xFFFFu) == 0)
            {

                std::fprintf(stderr,
                             "TsunamiPchip: %llu outer-write unhandled events "
                             "(loud-stderr muted past 32)\n",
                             static_cast<unsigned long long>(n + 1));

                std::fflush(stderr);
            }
        }
#endif
    }


    // ========================================================================
    // CSR read/write
    // ========================================================================

    // -----------------------------------------------------------------
    // Phase B: readCSR(offset) delegates to readCSR(offset, -1).
    // Pchip has no per-CPU CSRs, but the uniform surface lets Phase C
    // pass the originating CPU through for diagnostic correlation
    // without a signature mismatch against TsunamiCchip.
    // -----------------------------------------------------------------
    inline auto readCSR(uint64_t offset) const noexcept -> uint64_t
    {
        return readCSR(offset, -1);
    }

    inline auto readCSR(uint64_t offset, int cpuId) const noexcept -> uint64_t
    {
        using namespace Tsunami21272;

        constexpr uint64_t kPhaseBNoCycle = 0;

        switch (offset)
        {
        // ----------------------------------------------------------
        // DMA window configuration -- storage only.
        // TODO(unwired): scatter-gather translation engine.
        // ----------------------------------------------------------
        case Pchip::WSBA0:
            CSR_LOG_R("Pchip", "WSBA0", m_wsba[0], offset, cpuId, kPhaseBNoCycle);
            return m_wsba[0];
        case Pchip::WSBA1:
            CSR_LOG_R("Pchip", "WSBA1", m_wsba[1], offset, cpuId, kPhaseBNoCycle);
            return m_wsba[1];
        case Pchip::WSBA2:
            CSR_LOG_R("Pchip", "WSBA2", m_wsba[2], offset, cpuId, kPhaseBNoCycle);
            return m_wsba[2];
        case Pchip::WSBA3:
            CSR_LOG_R("Pchip", "WSBA3", m_wsba[3], offset, cpuId, kPhaseBNoCycle);
            return m_wsba[3];
        case Pchip::WSM0:
            CSR_LOG_R("Pchip", "WSM0", m_wsm[0], offset, cpuId, kPhaseBNoCycle);
            return m_wsm[0];
        case Pchip::WSM1:
            CSR_LOG_R("Pchip", "WSM1", m_wsm[1], offset, cpuId, kPhaseBNoCycle);
            return m_wsm[1];
        case Pchip::WSM2:
            CSR_LOG_R("Pchip", "WSM2", m_wsm[2], offset, cpuId, kPhaseBNoCycle);
            return m_wsm[2];
        case Pchip::WSM3:
            CSR_LOG_R("Pchip", "WSM3", m_wsm[3], offset, cpuId, kPhaseBNoCycle);
            return m_wsm[3];
        case Pchip::TBA0:
            CSR_LOG_R("Pchip", "TBA0", m_tba[0], offset, cpuId, kPhaseBNoCycle);
            return m_tba[0];
        case Pchip::TBA1:
            CSR_LOG_R("Pchip", "TBA1", m_tba[1], offset, cpuId, kPhaseBNoCycle);
            return m_tba[1];
        case Pchip::TBA2:
            CSR_LOG_R("Pchip", "TBA2", m_tba[2], offset, cpuId, kPhaseBNoCycle);
            return m_tba[2];
        case Pchip::TBA3:
            CSR_LOG_R("Pchip", "TBA3", m_tba[3], offset, cpuId, kPhaseBNoCycle);
            return m_tba[3];

        // ----------------------------------------------------------
        // PCI control / latency -- storage only.
        // TODO(unwired): enforce chaindis / hole / mwin / ptevrfy.
        // ----------------------------------------------------------
        case Pchip::PCTL:
            CSR_LOG_R("Pchip", "PCTL", m_pctl, offset, cpuId, kPhaseBNoCycle);
            return m_pctl;
        case Pchip::PLAT:
            CSR_LOG_R("Pchip", "PLAT", m_plat, offset, cpuId, kPhaseBNoCycle);
            return m_plat;

        // ----------------------------------------------------------
        // Error registers -- PERROR W1C is wired in writeCSR.
        // TODO(unwired): no error-injection paths assert these bits.
        // ----------------------------------------------------------
        case Pchip::PERROR:
            CSR_LOG_R("Pchip", "PERROR", m_perror, offset, cpuId, kPhaseBNoCycle);
            return m_perror;
        case Pchip::PERRMASK:
            CSR_LOG_R("Pchip", "PERRMASK", m_perrmask, offset, cpuId, kPhaseBNoCycle);
            return m_perrmask;

        // ----------------------------------------------------------
        // Reserved / WO-as-zero registers.
        // ----------------------------------------------------------
        case Pchip::RES:
            CSR_LOG_R("Pchip", "RES", 0, offset, cpuId, kPhaseBNoCycle);
            return 0;
        case Pchip::PERRSET:
            // HRM: WO -- reads UNPREDICTABLE; V4 returns 0.
            CSR_LOG_R("Pchip", "PERRSET", 0, offset, cpuId, kPhaseBNoCycle);
            return 0;
        case Pchip::SPRST:
            // HRM: WO; reads return 0.
            CSR_LOG_R("Pchip", "SPRST", 0, offset, cpuId, kPhaseBNoCycle);
            return 0;

        // ----------------------------------------------------------
        // Performance monitor -- storage only, no counter source.
        // TODO(unwired): wire to cycleCount / event counter.
        // ----------------------------------------------------------
        case Pchip::PMONCTL:
            CSR_LOG_R("Pchip", "PMONCTL", m_pmonctl, offset, cpuId, kPhaseBNoCycle);
            return m_pmonctl;
        case Pchip::PMONCNT:
            CSR_LOG_R("Pchip", "PMONCNT", m_pmoncnt, offset, cpuId, kPhaseBNoCycle);
            return m_pmoncnt;

        default:
            {
                // Forensic: throttled stderr for unhandled Pchip CSR reads.  Pchip
                // is the most likely chipset to hit during firmware init because
                // PCI device probing and ISA-bridge access go through it.  Phase B
                // also routes the event through the CSR_LOG_R sink.
                CSR_LOG_R("Pchip", "UNKNOWN", 0, offset, cpuId, kPhaseBNoCycle);
#if EMULATR_BRINGUP_PROBES
                static std::atomic<uint64_t> s_cnt{0};
                const uint64_t               n = s_cnt.fetch_add(1, std::memory_order_relaxed);
                if (n < 32)
                {

                    std::fprintf(stderr,
                                 "TsunamiPchip: UNKNOWN CSR READ offset=0x%08llx "
                                 "(event %llu)\n",
                                 static_cast<unsigned long long>(offset),
                                 static_cast<unsigned long long>(n));

                    std::fflush(stderr);
                }
                else if ((n & 0xFFFFu) == 0)
                {

                    std::fprintf(stderr,
                                 "TsunamiPchip: %llu unknown CSR reads "
                                 "(loud-stderr muted past 32)\n",
                                 static_cast<unsigned long long>(n + 1));

                    std::fflush(stderr);
                }
#endif
                return 0;
            }
        }
    }

    // -----------------------------------------------------------------
    // Phase B: writeCSR(offset, value) delegates to writeCSR(offset,
    // value, -1).  See readCSR for rationale.
    // -----------------------------------------------------------------
    inline auto writeCSR(uint64_t offset, uint64_t value) noexcept -> void
    {
        writeCSR(offset, value, -1);
    }

    inline auto writeCSR(uint64_t offset, uint64_t value, int cpuId) noexcept -> void
    {
        using namespace Tsunami21272;

        constexpr uint64_t kPhaseBNoCycle = 0;

        switch (offset)
        {
        // ----------------------------------------------------------
        // DMA window writes -- storage only.
        // TODO(unwired): scatter-gather translation engine.
        // ----------------------------------------------------------
        case Pchip::WSBA0:
            CSR_LOG_W("Pchip", "WSBA0", value, offset, cpuId, kPhaseBNoCycle);
            m_wsba[0] = value;
            break;
        case Pchip::WSBA1:
            CSR_LOG_W("Pchip", "WSBA1", value, offset, cpuId, kPhaseBNoCycle);
            m_wsba[1] = value;
            break;
        case Pchip::WSBA2:
            CSR_LOG_W("Pchip", "WSBA2", value, offset, cpuId, kPhaseBNoCycle);
            m_wsba[2] = value;
            break;
        case Pchip::WSBA3:
            CSR_LOG_W("Pchip", "WSBA3", value, offset, cpuId, kPhaseBNoCycle);
            m_wsba[3] = value;
            break;
        case Pchip::WSM0:
            CSR_LOG_W("Pchip", "WSM0", value, offset, cpuId, kPhaseBNoCycle);
            m_wsm[0] = value;
            break;
        case Pchip::WSM1:
            CSR_LOG_W("Pchip", "WSM1", value, offset, cpuId, kPhaseBNoCycle);
            m_wsm[1] = value;
            break;
        case Pchip::WSM2:
            CSR_LOG_W("Pchip", "WSM2", value, offset, cpuId, kPhaseBNoCycle);
            m_wsm[2] = value;
            break;
        case Pchip::WSM3:
            CSR_LOG_W("Pchip", "WSM3", value, offset, cpuId, kPhaseBNoCycle);
            m_wsm[3] = value;
            break;
        case Pchip::TBA0:
            CSR_LOG_W("Pchip", "TBA0", value, offset, cpuId, kPhaseBNoCycle);
            m_tba[0] = value;
            break;
        case Pchip::TBA1:
            CSR_LOG_W("Pchip", "TBA1", value, offset, cpuId, kPhaseBNoCycle);
            m_tba[1] = value;
            break;
        case Pchip::TBA2:
            CSR_LOG_W("Pchip", "TBA2", value, offset, cpuId, kPhaseBNoCycle);
            m_tba[2] = value;
            break;
        case Pchip::TBA3:
            CSR_LOG_W("Pchip", "TBA3", value, offset, cpuId, kPhaseBNoCycle);
            m_tba[3] = value;
            break;

        // ----------------------------------------------------------
        // PCI control / latency -- storage only.
        // TODO(unwired): enforce chaindis / hole / mwin / ptevrfy.
        // ----------------------------------------------------------
        case Pchip::PCTL:
            CSR_LOG_W("Pchip", "PCTL", value, offset, cpuId, kPhaseBNoCycle);
            m_pctl = value;
            break;
        case Pchip::PLAT:
            CSR_LOG_W("Pchip", "PLAT", value, offset, cpuId, kPhaseBNoCycle);
            m_plat = value;
            break;

        // ----------------------------------------------------------
        // Error registers.
        //   PERROR   W1C -- write 1 to clear matching bit.
        //   PERRSET  W1S -- write 1 to set matching bit (diagnostic).
        //   PERRMASK RW.
        // ----------------------------------------------------------
        case Pchip::PERROR:
            CSR_LOG_W("Pchip", "PERROR", value, offset, cpuId, kPhaseBNoCycle);
            m_perror &= ~value;   // W1C
            break;
        case Pchip::PERRSET:
            CSR_LOG_W("Pchip", "PERRSET", value, offset, cpuId, kPhaseBNoCycle);
            m_perror |= value;    // W1S (diagnostic)
            break;
        case Pchip::PERRMASK:
            CSR_LOG_W("Pchip", "PERRMASK", value, offset, cpuId, kPhaseBNoCycle);
            m_perrmask = value;
            break;

        // ----------------------------------------------------------
        // TLB invalidates -- WO sinks; no Pchip TLB model in V4.
        // TODO(unwired): scatter-gather TLB.
        // ----------------------------------------------------------
        case Pchip::TLBIV:
            CSR_LOG_W("Pchip", "TLBIV(sink)", value, offset, cpuId, kPhaseBNoCycle);
            break;
        case Pchip::TLBIA:
            CSR_LOG_W("Pchip", "TLBIA(sink)", value, offset, cpuId, kPhaseBNoCycle);
            break;

        // ----------------------------------------------------------
        // Reserved / read-only / soft-reset registers.
        // ----------------------------------------------------------
        case Pchip::RES:
            CSR_LOG_W("Pchip", "RES(ignored)", value, offset, cpuId, kPhaseBNoCycle);
            break;
        case Pchip::PMONCTL:
            CSR_LOG_W("Pchip", "PMONCTL", value, offset, cpuId, kPhaseBNoCycle);
            m_pmonctl = value;
            break;
        case Pchip::PMONCNT:
            // RO per HRM -- writes logged but discarded.
            CSR_LOG_W("Pchip", "PMONCNT(ignored)", value, offset, cpuId, kPhaseBNoCycle);
            break;
        case Pchip::SPRST:
            // WO software reset -- no physical effect today.
            // TODO(unwired): assert PCI RST# pulse model.
            CSR_LOG_W("Pchip", "SPRST(sink)", value, offset, cpuId, kPhaseBNoCycle);
            break;

        default:
            {
                // Forensic: throttled stderr for unhandled Pchip CSR writes.
                // Phase B also routes the event through the CSR_LOG_W sink.
                CSR_LOG_W("Pchip", "UNKNOWN", value, offset, cpuId, kPhaseBNoCycle);
#if EMULATR_BRINGUP_PROBES
                static std::atomic<uint64_t> s_cnt{0};
                const uint64_t               n = s_cnt.fetch_add(1, std::memory_order_relaxed);
                if (n < 32)
                {
                    std::fprintf(stderr,
                                 "TsunamiPchip: UNKNOWN CSR WRITE offset=0x%08llx "
                                 "value=0x%016llx (event %llu)\n",
                                 static_cast<unsigned long long>(offset),
                                 static_cast<unsigned long long>(value),
                                 static_cast<unsigned long long>(n));
                    std::fflush(stderr);
                }
                else if ((n & 0xFFFFu) == 0)
                {
                    std::fprintf(stderr,
                                 "TsunamiPchip: %llu unknown CSR writes "
                                 "(loud-stderr muted past 32)\n",
                                 static_cast<unsigned long long>(n + 1));
                    std::fflush(stderr);
                }
#endif
                break;
            }
        }
    }

    inline auto readSparseMem(uint64_t sparseOffset) const noexcept -> uint64_t
    {
        using namespace Tsunami21272;
        uint32_t pciAddr  = SparseSpace::decodePciAddr(sparseOffset);
        uint8_t  byteLane = SparseSpace::decodeByteLane(sparseOffset);
        uint8_t  xferLen  = SparseSpace::decodeXferLen(sparseOffset);
        (void)pciAddr;
        (void)byteLane;
        (void)xferLen;

        // For now: no PCI memory devices mapped
        return 0xFFFFFFFFULL;  // empty slot
    }

    inline auto readSparseIO(uint64_t sparseOffset) const noexcept -> uint64_t
    {
        using namespace Tsunami21272;
        uint32_t pciAddr  = SparseSpace::decodePciAddr(sparseOffset);
        uint8_t  byteLane = SparseSpace::decodeByteLane(sparseOffset);
        (void)byteLane;
        uint16_t port = static_cast<uint16_t>(pciAddr & 0xFFFF);

        return readIoPort(port, SparseSpace::xferLenToBytes(
                              SparseSpace::decodeXferLen(sparseOffset)));
    }

    // Symmetric writer for the sparse PCI I/O window.  The transfer width
    // is carried in the sparse-encoded offset (decodeXferLen), exactly as
    // readSparseIO derives it on read -- so the caller passes only the
    // offset and value.
    inline auto writeSparseIO(uint64_t sparseOffset, uint64_t value) noexcept -> void
    {
        using namespace Tsunami21272;
        uint32_t pciAddr  = SparseSpace::decodePciAddr(sparseOffset);
        uint8_t  byteLane = SparseSpace::decodeByteLane(sparseOffset);
        (void)byteLane;
        uint16_t port = static_cast<uint16_t>(pciAddr & 0xFFFF);

        writeIoPort(port, value, SparseSpace::xferLenToBytes(
                        SparseSpace::decodeXferLen(sparseOffset)));
    }

private:
    // ========================================================================
    // PCI Type 0 config space decode
    // ========================================================================

    /**
     * @brief Decode Type 0 config address and dispatch to device
     * @param cfgOffset  Offset within Type 0 config region
     * @param width      Access width
     * @return           Config register value, or 0xFFFFFFFF if no device
     *
     * Type 0 address format:
     *   Bits [15:11] = device (IDSEL)
     *   Bits [10:8]  = function
     *   Bits [7:0]   = register (naturally aligned to width)
     */
    // TEMP config-cycle trace (EMULATR_PCI_CFG_TRACE=1) -- behavior-neutral when
    // unset (static bool short-circuits).  Diagnostic for the BAR-write / probe
    // analysis (PCI_Fabric build step 2); strip when the fabric work lands.
    // b0 is hardcoded -- DS10 is single-hose (type-0 cycles only), NOT valid for a
    // bridged type-1 cycle.  width is in BYTES (1/2/4).  raw = the pre-decode
    // config offset, so a swizzle/decode bug is visible rather than trusted.
    static void cfgTrace(char rw, uint64_t cfgOff, uint8_t dev, uint8_t fn,
                         uint8_t reg, uint8_t width, uint32_t val, bool hit) noexcept
    {
        static bool const on = (std::getenv("EMULATR_PCI_CFG_TRACE") != nullptr);
        if (!on) return;
        std::fprintf(stderr,
            "PCICFG-TRACE %c b0 d%02u f%u reg=0x%02X w=%u raw=0x%06llX "
            "val=0x%08X %s\n",
            rw, static_cast<unsigned>(dev), static_cast<unsigned>(fn),
            static_cast<unsigned>(reg), static_cast<unsigned>(width),
            static_cast<unsigned long long>(cfgOff), val, hit ? "hit" : "MISS");
    }

    inline auto readPciConfig0(uint64_t cfgOffset, uint8_t width) const noexcept -> uint64_t
    {
        const uint8_t device   = static_cast<uint8_t>((cfgOffset >> 11) & 0x1F);
        const uint8_t function = static_cast<uint8_t>((cfgOffset >> 8) & 0x07);
        const uint8_t reg      = static_cast<uint8_t>(cfgOffset & 0xFF);
        uint32_t      key      = makeBDF(0, device, function);
        auto          it       = m_pciDevices.find(key);
        const bool     hit = (it != m_pciDevices.end() && it->second);
        const uint64_t val = hit ? it->second->pciConfigRead(reg, width)
                                  : 0xFFFFFFFFULL;
        cfgTrace('R', cfgOffset, device, function, reg, width,
                 static_cast<uint32_t>(val), hit);
        return val;
    }


    inline auto writePciConfig0(uint64_t cfgOffset, uint32_t value, uint8_t width) noexcept -> void
    {
        const uint8_t device   = static_cast<uint8_t>((cfgOffset >> 11) & 0x1F);
        const uint8_t function = static_cast<uint8_t>((cfgOffset >> 8) & 0x07);
        const uint8_t reg      = static_cast<uint8_t>(cfgOffset & 0xFF);

        uint32_t key = makeBDF(0, device, function);
        auto     it  = m_pciDevices.find(key);
        const bool hit = (it != m_pciDevices.end() && it->second);
        cfgTrace('W', cfgOffset, device, function, reg, width, value, hit);
        if (!hit) return;
        it->second->pciConfigWrite(reg, value, width);
    }

    // ========================================================================
    // I/O port dispatch
    // ========================================================================

    inline auto readIoPort(uint16_t port, uint8_t width) const noexcept -> uint64_t
    {
        // ================================================================
        // UARTBP#6 -- readIoPort entry, diagnostic 2026-05-28
        // ================================================================
        // Fires once at first UART-port hit to confirm the port registry
        // search is reached.  Dumps the current registry size so we can
        // see whether any handlers are registered at all at the time of
        // access (vs. having been wiped by a reset or never installed).
        //
        // REMOVAL TRIGGER: delete when LSR-wedge diagnostic is closed.
        // ================================================================
#if EMULATR_BRINGUP_PROBES
        {
            static std::atomic<bool> s_fired{false};
            bool const isUartPort = (port == 0x3F8) || (port == 0x3FD);
            if (isUartPort &&
                !s_fired.exchange(true, std::memory_order_acq_rel))
            {
                std::fprintf(stderr,
                             "UARTBP#6 readIoPort entry  port=0x%04x "
                             "width=%u registry_size=%zu\n",
                             static_cast<unsigned>(port),
                             static_cast<unsigned>(width),
                             m_ioPortRegistry.size());
                std::fflush(stderr);
                // __debugbreak();  // 2026-05-28: muted post-verification; probe still emits stderr marker.
            }
        }
#endif

        for (const auto& range : m_ioPortRegistry)
        {
            if (port >= range.startPort && port < range.endPort)
            {
                return range.handler->ioRead(port, width);
            }
        }

        // ================================================================
        // UARTBP#7 -- unhandled-port fall-through, diagnostic 2026-05-28
        // ================================================================
        // Fires once if a UART port iterated the registry without
        // matching any range.  Strong signal that COM1's registration
        // (m_pchip.registerIoPortRange(0x3F8, 0x400, &m_com1) in
        // TsunamiChipset.h) either didn't run or was clobbered.
        //
        // REMOVAL TRIGGER: delete when LSR-wedge diagnostic is closed.
        // ================================================================
#if EMULATR_BRINGUP_PROBES
        {
            static std::atomic<bool> s_fired{false};
            bool const isUartPort = (port == 0x3F8) || (port == 0x3FD);
            if (isUartPort &&
                !s_fired.exchange(true, std::memory_order_acq_rel))
            {
                std::fprintf(stderr,
                             "UARTBP#7 readIoPort UNHANDLED  port=0x%04x "
                             "registry_size=%zu -- BUG: COM1 not in "
                             "registry\n",
                             static_cast<unsigned>(port),
                             m_ioPortRegistry.size());
                std::fflush(stderr);
                // __debugbreak();  // 2026-05-28: muted post-verification; probe still emits stderr marker.
            }
        }
#endif

        // Suppress noise for ports the firmware reads as ISA timing
        // delays or POST diagnostics we don't model.  ISA convention:
        //   port 0x0080 = POST/microsecond-delay port.  BIOSes write
        //                 byte POST codes here; firmware reads it as
        //                 a ~1us bus-cycle delay.  Returning 0xFF
        //                 (the ISA "unconnected pull-up" default)
        //                 satisfies both uses; firmware ignores value.
        // TODO: replace with a registered no-op ISA handler (matches
        // 0x60/64, 0x70/71 pattern) when we add the ISA POST device.
#if EMULATR_BRINGUP_PROBES
        bool const isKnownSilentPort = (port == 0x0080);
        if (!isKnownSilentPort) {
            std::fprintf(stderr,
                "TsunamiPchip: I/O read unhandled port 0x%04x\n", port);
        }
#endif

        return 0xFFULL;     // ISA default for unconnected ports
    }

    inline auto writeIoPort(uint16_t port, uint64_t value, uint8_t width)  noexcept -> void
    {
        for (const auto& range : m_ioPortRegistry)
        {
            if (port >= range.startPort && port < range.endPort)
            {
                range.handler->ioWrite(port, value, width);
                return;
            }
        }
        /*
            TRACE_LOG(QString("TsunamiPchip: I/O write unhandled port 0x%1 = 0x%2")
                .arg(port, 4, 16, QChar('0'))
                .arg(value, 2, 16, QChar('0')));
        */
    }

public:
    // ========================================================================
    // Snapshot serialize / deserialize
    // ========================================================================
    // Captures the CSR storage fields written after construction.  Does
    // NOT capture m_pciDevices or m_ioPorts -- those are function-pointer
    // registries reconstructed unconditionally by device wiring at
    // Machine construction.  Snapshot restore relies on the same
    // registration code running again with the same device set.

    auto serialize(QDataStream& ds) const noexcept -> void
    {
        for (int i = 0; i < 4; ++i) ds << m_wsba[i];
        for (int i = 0; i < 4; ++i) ds << m_wsm[i];
        for (int i = 0; i < 4; ++i) ds << m_tba[i];
        ds << m_pctl << m_plat << m_perror << m_perrmask
            << m_pmonctl << m_pmoncnt;
    }

    auto deserialize(QDataStream& ds) noexcept -> void
    {
        for (int i = 0; i < 4; ++i) ds >> m_wsba[i];
        for (int i = 0; i < 4; ++i) ds >> m_wsm[i];
        for (int i = 0; i < 4; ++i) ds >> m_tba[i];
        ds >> m_pctl >> m_plat >> m_perror >> m_perrmask
            >> m_pmonctl >> m_pmoncnt;
    }

private:
    // ========================================================================
    // BDF key helper
    // ========================================================================

    inline static auto makeBDF(uint8_t bus, uint8_t device, uint8_t function) noexcept -> uint32_t
    {
        return (static_cast<uint32_t>(bus) << 16) |
            (static_cast<uint32_t>(device) << 8) |
            static_cast<uint32_t>(function);
    }

    // --- MMIO Dispatch Handlers ---
    // Dispatches based on the MMIOOffset::RegionId we defined earlier.

    auto handleCsrRead(uint64_t offset, uint8_t width) const noexcept -> uint64_t
    {
        // The offset is relative to the Pchip CSR base.
        // Ensure access aligns with architectural register map.
        switch (offset)
        {
        // Window Space Base Addresses
        case 0x000: return m_wsba[0];
        case 0x008: return m_wsba[1];
        case 0x010: return m_wsba[2];
        case 0x018: return m_wsba[3];

        // Window Space Masks
        case 0x020: return m_wsm[0];
        case 0x028: return m_wsm[1];
        case 0x030: return m_wsm[2];
        case 0x038: return m_wsm[3];

        // Translated Base Addresses
        case 0x040: return m_tba[0];
        case 0x048: return m_tba[1];
        case 0x050: return m_tba[2];
        case 0x058: return m_tba[3];

        // Control and Status Registers
        case 0x060: return m_pctl;
        case 0x068: return m_plat;
        case 0x070: return m_perror;
        case 0x078: return m_perrmask;

        // Performance Monitor
        case 0x080: return m_pmonctl;
        case 0x088: return m_pmoncnt;

        default:
            // Log access to undefined CSR if debugging is enabled
            return 0;
        }
    }

    // --- CSR Write Handler ---
    auto handleCsrWrite(uint64_t offset, uint8_t width, uint64_t val) noexcept -> void
    {
        // Tsunami Pchip CSRs are 64-bit; check alignment
        if (offset & 0x7) return;

        switch (offset)
        {
        // Window 0-3: WSBA (Window Space Base Address)
        case 0x000: m_wsba[0] = val;
            break;
        case 0x008: m_wsba[1] = val;
            break;
        case 0x010: m_wsba[2] = val;
            break;
        case 0x018: m_wsba[3] = val;
            break;

        // Window 0-3: WSM (Window Space Mask)
        case 0x020: m_wsm[0] = val;
            break;
        case 0x028: m_wsm[1] = val;
            break;
        case 0x030: m_wsm[2] = val;
            break;
        case 0x038: m_wsm[3] = val;
            break;

        // Window 0-3: TBA (Translated Base Address)
        case 0x040: m_tba[0] = val;
            break;
        case 0x048: m_tba[1] = val;
            break;
        case 0x050: m_tba[2] = val;
            break;
        case 0x058: m_tba[3] = val;
            break;

        // PCI Control and Status
        case 0x060: m_pctl = val;
            break;
        case 0x068: m_plat = val;
            break;
        case 0x070: m_perror = val; // Often W1C (Write 1 to Clear) in real silicon
            break;
        case 0x078: m_perrmask = val;
            break;

        // Performance Monitoring
        case 0x080: m_pmonctl = val;
            break;
        case 0x088: m_pmoncnt = val;
            break;

        default:
            // Optional: Log an implementation trap for reserved address space
            break;
        }
    }

    // --- Sparse Memory Handler ---
    // Sparse memory writes use the bottom 5 bits of the offset as a mask.
    auto handleSparseMemWrite(uint64_t offset, uint8_t width, uint64_t val) const noexcept -> bool
    {
        // In sparse space, [4:0] are the byte enable mask
        uint64_t actualAddr = (offset >> 5);
        // Dispatch to system bus / PCI memory
        return m_pciMemory->write(actualAddr, val, static_cast<uint8_t>(fmt::detail::state::width));
    }

    // --- Dense Memory Handler ---
    auto handleDenseWrite(uint64_t offset, uint8_t width, uint64_t val) const noexcept -> bool
    {
        // Dense space is direct-mapped; offset is the address
        return m_pciMemory->write(offset, val, static_cast<uint8_t>(fmt::detail::state::width));
    }

    // --- Configuration Space Handler ---

    auto handleConfigRead(uint64_t offset, uint8_t width, uint64_t& out) noexcept -> bool
    {
        uint32_t bdf = offsetToBDF(offset);
        uint32_t reg = static_cast<uint32_t>(offset & 0xFC);

        auto it = m_pciDevices.find(bdf);
        if (it != m_pciDevices.end())
        {
            // Perform the read
            out = static_cast<uint64_t>(it->second->pciConfigRead(reg, width));
            return true;
        }

        // Master Abort: PCI specification requires returning all 1s (0xFFFFFFFF)
        // for config space reads to non-existent devices.
        out = 0xFFFFFFFF;
        return true; // Still returns 'true' because the bus cycle completed (aborted)
    }

    auto handleConfigWrite(uint64_t offset, uint8_t width, uint64_t val) noexcept -> bool
    {
        uint32_t bdf = offsetToBDF(offset);
        uint32_t reg = static_cast<uint32_t>(offset & 0xFC);

        auto it = m_pciDevices.find(bdf);
        if (it != m_pciDevices.end())
        {
            it->second->pciConfigWrite(reg, static_cast<uint32_t>(val),
                                       static_cast<uint8_t>(fmt::detail::state::width));
            return true;
        }
        return false; // Master Abort
    }

    auto handleSparseIoWrite(uint64_t offset, uint8_t width, uint64_t val) noexcept -> bool
    {
        // 1. Calculate Port
        uint16_t port = static_cast<uint16_t>((offset >> 5) & 0xFFFF);

        // 2. Dispatch using the Pchip's registry scan logic
        for (const auto& range : m_ioPortRegistry)
        {
            if (port >= range.startPort && port < range.endPort)
            {
                range.handler->ioWrite(port, val, width);
                return true; // Write handled
            }
        }

        // 3. No device claimed it
        return false; // Signals Master Abort to the bus
    }

    // Helper to extract BDF from config window offsets
    static constexpr auto offsetToBDF(uint64_t offset) noexcept -> uint32_t
    {
        return (static_cast<uint32_t>((offset >> 16) & 0xFF) << 16) |
            (static_cast<uint32_t>((offset >> 11) & 0x1F) << 8) |
            (static_cast<uint32_t>((offset >> 8) & 0x07));
    }


    // Assume m_ioPorts is an object implementing a write/read interface
    // e.g., class IIoPortHandler { ... }

   
    IIoPortHandler*    m_ioPortHandler{nullptr};
    IPciMemoryHandler* m_pciMemory{nullptr}; // Link to the PCI bus memory space
    // ========================================================================
    // CSR register storage
    // ========================================================================
    ChipsetVariant m_variant;  // Ticket 1.5: latched chipset variant
    uint64_t       m_wsba[4];      // Window Space Base Address
    uint64_t       m_wsm[4];       // Window Space Mask
    uint64_t       m_tba[4];       // Translated Base Address
    uint64_t       m_pctl;         // PCI Control
    uint64_t       m_plat;         // PCI Latency
    uint64_t       m_perror;       // PCI Error
    uint64_t       m_perrmask;     // PCI Error Mask

    uint64_t m_pmonctl{0};
    uint64_t m_pmoncnt{0};
    // ========================================================================
    // PCI device registry
    // ========================================================================
    std::map<uint32_t, IPciDeviceHandler*> m_pciDevices;

    // ========================================================================
    // I/O port registry
    // ========================================================================
    // This maintains the registry of which devices own which I/O ports.
    // It is a member property because the registry must persist for 
    // the lifetime of the Pchip.
    std::vector<IoPortRange> m_ioPortRegistry;

    // PCI dense-memory claimants (G3-lite, 2026-06-03).  Fixed ranges
    // checked ahead of the all-ones float (read) / UNHANDLED log (write).
    // First consumer: PCF8584 IIC at 0xFFFF0000-0xFFFF0001.
    std::vector<PciMemRange> m_pciMemRegistry;
};

#endif // TSUNAMI_PCHIP_H
