#ifndef TSUNAMI_CHIPSET_H
#define TSUNAMI_CHIPSET_H

#include <algorithm>   // std::min (dmaReadBytes/dmaWriteBytes chunking, S1 seam)
#include <array>       // manifest-pinned SCSI disk instances (2026-07-25)
#include <atomic>
#include <memory>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include "TsunamiVariant.h"
#include "TsunamiCchip.h"
#include "TsunamiDchip.h"
#include "TsunamiPchip.h"
#include "TsunamiTig.h"     // TIG-bus device register file (smir/halt/ipcr/arbiter)
#include "TsunamiDpr.h"     // task #25: RMC Dual-Port RAM (ES40/Typhoon; TIG-bus mailbox)
#include "Cypress_CY82C693ISABridge.h"
#include "AliM1543C.h"   // ES40/ES45 south bridge (ALi M1543C) -- model-gated in wireDevices()
#include "deviceLib/Tsunami/Uart16550.h"
#include "deviceLib/Tsunami/MinimalIsaStub.h"  // Kbd8042Stub (2026-05-28 unblocker; RTC stub superseded)
#include "deviceLib/Tsunami/ToyRtc.h"          // MC146818 TOY clock + CMOS (2026-06-03)
#include "deviceLib/Tsunami/IicPcf8584.h"  // PCF8584 IIC controller model (2026-06-03)
#include "Tsunami21272_RegisterMap.h"
#include "CchipIntervalTimer.h"
#include "FlashRom.h"
#include "chipsetLib/Pic8259Pair.h"      // Increment 2: Cypress 8259 pair (serial-console interrupt design, Section 6)
#include "memoryLib/GuestMemory.h"
#include "memoryLib/ISystemBus.h"
#include "deviceLib/Tsunami/IicPcf8584.h"  // PCF8584 IIC controller model (2026-06-03)
#include "deviceLib/Tsunami/Cy82C693Ide.h" // CY82C693 IDE func1 + ATAPI CD (2026-06-08)
#include "deviceLib/Tsunami/AliM5229Ide.h" // ALi M5229 IDE func1 (ES40/ES45/DS25, Phase 2B)
#include "deviceLib/Tsunami/Dec21143Tulip.h" // DE500-AA 21143 NIC (ewa) -- Phase 1 enumerate+init
#include "deviceLib/Tsunami/Smc37c669SuperIo.h" // FDC37C669 SuperIO: config port + FDC (#22)
#include "deviceLib/Tsunami/VgaTextConsole.h"    // VGA text-console interface (JRN-VMB-006, 2026-07-18)
#include "deviceLib/Tsunami/Ncr53C810.h"         // NCR 53C810 SCSI HBA (pka) -- JRN-SCSI-001 P1/P2
#include "deviceLib/scsi/VirtualIsoDevice.h"
#include "deviceLib/scsi/VirtualDiskDevice.h"    // SCSI direct-access target (dka0) -- JRN-SCSI-001


/* considerations
 *
 * TSUNAMI controller interface constraint applies to **main memory (DRAM)**. 
 * I/O space is completely separate and is not affected by it. 
 **  DRAM capacity is set by the Cchip AAR ASIZ encoding and differs by 21272
 **  variant (base Tsunami vs Typhoon):**
 *
 **  The Cchip AAR registers encode each DRAM array's base in ADDR<34:24> and its
 **  size in ASIZ<15:12> (HRM Table 10-15). BASE Tsunami caps ASIZ at 0x7 = 1GB
 **  per array -> 4 arrays x 1GB = 4GB max (DS10/DS20/DS20E). TYPHOON (the 21272
 **  high-bandwidth variant, ES40) uses the ASIZ<15> extension bit: 0x8=2GB,
 **  0x9=4GB, 0xA=8GB per array -> 4 x 8GB = 32GB max. Same 21272 part; the
 **  difference is the extended ASIZ range (+ dual-Dchip bandwidth), NOT a
 **  different chipset. computeAAR()/reset() in TsunamiCchip.h implement both.
 ** 
 * Why I/O space is not affected:**
 ** I/O space is decoded entirely separately. The chip field 'bits[43:32]' of 
 ** the PA is what routes to MMIO - and that decode happens in the Pchip, 
 ** not through the AAR registers at all. The MMIO window at '0x800.0000.0000' 
 ** onwards is accessible regardless of how much DRAM is installed:
 *
 *
 * -- 44-bit PA space (EV6 / 21264):   0x000.0000.0000 - 0xFFF.FFFF.FFFF  (16 TB total)
                                       ________________________________________________
    DRAM window:     0x000.0000.0000 - 0x7FF.FFFF.FFFF  (8TB addressable by PA)
    ?_ Cchip AAR constrains actual DRAM to 4GB (Tsunami) or 32GB (Typhoon)
    ?_ Above the AAR limit: NXM (Non-eXistent Memory) - machine check

    I/O window:      0x800.0000.0000 - 0xFFF.FFFF.FFFF  (8TB, chip >= 0x800)
    +_ Fully accessible regardless of DRAM amount
    +_ Pchip decodes independently of Cchip AAR registers
    +- PCI Dense Memory, CSRs, PCI I/O, PCI Config all live here

**Per-model DRAM ceilings (HRM-verified; see chipsetLib/TsunamiVariant.h):**

  DS10 / DS20 / DS20E  = Tsunami (21272)            ->  4GB   (ASIZ 0x7 = 1GB x 4)
  ES40                 = Typhoon (21272 high-bw)    ->  32GB  (ASIZ 0xA = 8GB x 4)
  DS15 / DS25 / ES45   = Titan   (21274, separate)  ->  32GB  (TitanChipset path)

variantFromModel(model) selects the variant; reset() sets isExtendedAar for
Typhoon AND Titan so computeAAR emits ASIZ 0x8/0x9/0xA. A --mem request above
the selected variant's ceiling is a LOUD hard-stop in TsunamiCchip::reset()
(no silent capacity loss). The I/O stack, Pchip, and MMIO decode are identical
across Tsunami/Typhoon; only the Cchip AAR ASIZ range, MISC<REV> (Tsunami=1,
Typhoon=8) and Dchip DREV (0x10 / 0x20) differ. Titan (21274) is a DISTINCT
chipset (dual discrete Pchips + AGP) with its own path.
 
 *
 */

namespace scsi
{
    struct IBlockMedia;
}

class TsunamiChipset : public memoryLib::ISystemBus
{
public:
    explicit TsunamiChipset(const std::string& model,
        int cpuCount = 4,
        uint64_t memSizeBytes = 0x100000000ULL) noexcept
        : m_variant(normalizeVariant(variantFromModel(model)))
        , m_model(model)
        , m_southBridge(southBridgeFromModel(m_model))   // 2026-07-12: model-keyed south-bridge
        , m_guestMemory(memSizeBytes)
        , m_cchip(m_variant, cpuCount, memSizeBytes)
        , m_dchip(m_variant, memSizeBytes)
        , m_pchip(m_variant)
        // task #25: DPR present on the RMC platform (ES40 == Typhoon) only.
        , m_hasRmcDpr(m_variant == ChipsetVariant::Typhoon)
        , m_dpr(cpuCount)
    {
        assertVariantConsistency();
        wireDevices();
    }

    explicit TsunamiChipset(ChipsetVariant variant,
        int cpuCount = 4,
        uint64_t memSizeBytes = 0x100000000ULL) noexcept;

    void reset() noexcept {
        m_cchip.reset();
        m_dchip.reset();
        m_pchip.reset();
        m_tig.reset();
        m_dpr.reset();     // task #25: re-seed the RMC DPR structure
    }

    // Inject an already-open block medium built by the media_kind factory.
    // setDiskMedia -> an ATA fixed-disk unit (channel,unit, e.g. dqa0 master);
    // setCdMedia   -> the ATAPI CD (dqa1, primary slave).  The drives hold the
    // IBlockMedia and no longer open files themselves.  2026-06-12 (seam).
    bool setDiskMedia(int channel, int unit,
                      std::unique_ptr<scsi::IBlockMedia> media) noexcept {
        return m_activeIde ? m_activeIde->attachMedia(channel, unit, std::move(media))
                           : m_ide.attachMedia(channel, unit, std::move(media));
    }
    bool setCdMedia(std::unique_ptr<scsi::IBlockMedia> media) noexcept {
        m_cdrom.setMedia(std::move(media));
        return m_cdrom.hasMedia();
    }

    // ====================================================================
    // ISystemBus -- the CPU's system-bus seam (the arbiter surface).
    // ====================================================================
    // Every CPU load / store / fetch enters here.  read/write decode the PA:
    //   DRAM (isDramAddress)  -> m_guestMemory
    //   I/O window            -> mmioRead/mmioWrite (Cchip/Dchip/Pchip routing)
    //   unclaimed             -> reportNxm + BusStatus::BusError (-> MCHK)
    [[nodiscard]] memoryLib::BusResult read(uint64_t pa, uint8_t width) noexcept override;
    [[nodiscard]] memoryLib::BusResult write(uint64_t pa, uint64_t value, uint8_t width) noexcept override;
    [[nodiscard]] memoryLib::BusResult fetch(uint64_t pa, uint8_t width) noexcept override;

    // ====================================================================
    // MMIO Dispatch (Surface 1)
    // ====================================================================
    //
    // Canonical surface: the instance methods take a FULL PA and a cpuId.
    // Per-CPU MISC.CPUID is real silicon, so cpuId is threaded into the
    // Cchip (read/write(offset, value, cpuId)).  The chipset owns NO
    // addresses -- it computes the window offset and routes via
    // Tsunami21272::MMIOOffset::routeMmioOffset(), switching on RegionId.
    //
    // The static (ctx, offset, width) handlers below are thin shims that
    // the GuestMemory hook (Machine.cpp::machineMmioRead) and the
    // deprecated MmioRegistry still call; they reconstruct the full PA,
    // default cpuId = 0, and delegate here.  Behaviour is byte-identical
    // to the prior hand-written ladder -- Ticket 1 is plumbing only, no
    // register-semantics change.
    // ====================================================================

    uint64_t mmioRead(uint64_t pa, uint8_t width, int cpuId = 0) noexcept
    {
        using namespace Tsunami21272;
        uint64_t const off = pa - Base::kMMIO_Start;

        // ================================================================
        // UARTBP#3 -- mmioRead entry, diagnostic 2026-05-28
        // ================================================================
        // Fires once at first UART-PA hit to confirm MMIO dispatch is
        // reached.  Dumps 'off' (PA - kMMIO_Start), 'kPchip1_PciMem'
        // (the early-return threshold), and 'kMMIO_Size' so we can see
        // whether the upcoming Pchip1 early-return at line 136 will
        // swallow the access (= the dispatch bug we are hunting).
        //
        // REMOVAL TRIGGER: delete when LSR-wedge diagnostic is closed.
        // ================================================================
#if EMULATR_BRINGUP_PROBES
        {
            static std::atomic<bool> s_fired{ false };
            bool const isUartPa =
                (pa == 0x801FC0003F8ULL) || (pa == 0x801FC0003FDULL);
            if (isUartPa &&
                !s_fired.exchange(true, std::memory_order_acq_rel))
            {
                std::fprintf(stderr,
                    "UARTBP#3 mmioRead entry  pa=0x%012llx "
                    "off=0x%012llx kPchip1_PciMem=0x%012llx "
                    "kMMIO_Size=0x%012llx\n",
                    static_cast<unsigned long long>(pa),
                    static_cast<unsigned long long>(off),
                    static_cast<unsigned long long>(MMIOOffset::kPchip1_PciMem),
                    static_cast<unsigned long long>(Base::kMMIO_Size));
                std::fflush(stderr);
                // __debugbreak();  // 2026-05-28: muted post-verification; probe still emits stderr marker.
            }
        }
#endif

        // ----------------------------------------------------------------
        // PCI IACK intercept (2026-06-04, design Section 6 promotion).
        // ----------------------------------------------------------------
        // The SRM ISA interrupt dispatcher resolves the 8259 source with
        // a 4-byte read of the Pchip0 PCI interrupt-acknowledge window
        // (chipset offset 0x1_F800_0000 = PA 0x801_F800_0000) -- observed
        // live in run 20260604-152214 (UNHANDLED OUTER READ events, one
        // per b_irq<1> divert; the zero it read back produced a wrong
        // specific-EOI level 0 and a re-divert livelock).  Matches the
        // AXPBox analog (AliM1543C pic_read_vector at its IACK decode).
        //
        // THIS read is the INTA cycle: acknowledgeDeviceInterrupt()
        // performs the PIC IRR->ISR transfer (suppressing the level
        // until the guest ISR's EOI) and refreshes the DRIR<55> mirror
        // in the same boundary.  A spurious IACK (nothing pending)
        // returns the architectural spurious vector, master base | 7.
        if (off == kPchip0IackOffset) {
            int const vec = acknowledgeDeviceInterrupt();
            return (vec >= 0) ? static_cast<uint64_t>(vec)
                : kSpuriousIackVector;
        }

        // Pchip1 half is an unpopulated coarse mirror today: reads off-bus.
        if (off >= MMIOOffset::kPchip1_PciMem && off < Base::kMMIO_Size)
            return 0xFFFFFFFFULL;

        switch (MMIOOffset::routeMmioOffset(off)) {
        case MMIOOffset::RegionId::Pchip0_SparseMem:
            // 0x801_0000_0000-0x801_3FFF_FFFF: PCI Sparse Memory (HRM Table 10-1),
            // physically the Cchip TIGbus window on Tsunami boards.  Flash + TIG
            // control registers are decoded ahead of here (TsunamiChipset.cpp);
            // unmodeled reads are unpopulated TIGbus and read 0.  readSparseMem()
            // returns 0 -- NOT the PCI-empty all-ones float.  See readSparseMem().
            return m_pchip.readSparseMem(off - MMIOOffset::kPchip0_SparseMem);
        case MMIOOffset::RegionId::Pchip0_SparseIO:
            return m_pchip.readSparseIO(off - MMIOOffset::kPchip0_SparseIO);
        case MMIOOffset::RegionId::Cchip_CSR:
            return m_cchip.read(off - MMIOOffset::kCchip_CSR, cpuId);
        case MMIOOffset::RegionId::Dchip_CSR:
            return m_dchip.read(off - MMIOOffset::kDchip_CSR);
        default:
            // Pchip0 PciMem / CSR / IACK / IODense / Cfg, and any gap, fall
            // through to the Pchip generic reader with the window-relative
            // offset -- exactly as the prior ladder did.
            return m_pchip.read(off, width);
        }
    }

    void mmioWrite(uint64_t pa, uint64_t value, uint8_t width, int cpuId = 0) noexcept
    {
        using namespace Tsunami21272;
        uint64_t const off = pa - Base::kMMIO_Start;

        // Pchip1 half: writes dropped (unpopulated mirror).
        if (off >= MMIOOffset::kPchip1_PciMem && off < Base::kMMIO_Size)
            return;

        switch (MMIOOffset::routeMmioOffset(off)) {
        case MMIOOffset::RegionId::Cchip_CSR:
            m_cchip.write(off - MMIOOffset::kCchip_CSR, value, cpuId);
            return;
        case MMIOOffset::RegionId::Dchip_CSR:
            m_dchip.write(off - MMIOOffset::kDchip_CSR, value);
            return;
        case MMIOOffset::RegionId::Pchip0_SparseIO:
            // Ticket 6: sparse PCI I/O writes now reach the registered
            // I/O-port handlers (UART, ISA bridge), symmetric to the
            // readSparseIO case in mmioRead above.
            m_pchip.writeSparseIO(off - MMIOOffset::kPchip0_SparseIO, value);
            return;
        default:
            // Sparse memory and all other Pchip0 windows route to the Pchip
            // generic writer (window-relative offset).  Sparse MEM writes
            // remain a generic-path no-op until a PCI memory device exists.
            m_pchip.write(off, value, width);
            return;
        }
    }

    // --------------------------------------------------------------------
    // Tick.  Advance chipset time by `cycles`, driving the Cchip interval
    // timer via the existing stateless mask predicate against an internal
    // monotonic accumulator (mirrors how Machine::run uses the CPU cycle
    // counter).  Ticket 4 finalizes rate / chunk-alignment semantics.
    // --------------------------------------------------------------------
    void step(uint64_t cycles) noexcept
    {
        uint64_t const prev = m_cycleAccum;
        m_cycleAccum += cycles;
        // Fire on each interval-timer boundary crossed.  b_irq<2> is a
        // level latch (set until the CPU W1C-clears MISC<ITINTR>), so one
        // fire per step suffices even when a large delta crosses several
        // boundaries -- and unlike an exact-landing test it never misses a
        // boundary stepped over within a single chunk.
        using Tsunami21272::Spec::kCchipTimerBit;
        if ((m_cycleAccum >> kCchipTimerBit) != (prev >> kCchipTimerBit))
            m_cchip.fireIntervalTimer();
    }

    // --------------------------------------------------------------------
    // evalDeviceIrqs -- step-boundary mirror of the serial interrupt
    // chain (Increment 2, 2026-06-04).
    //
    //   uart_int_pending -> PIC IRQ4 -> [IMR + in-service + priority]
    //                    -> Cchip DRIR<55>
    //
    // Called once per Machine::run iteration (the design doc Section 5
    // storm guard: at most one interrupt edge per step boundary).  The
    // DRIR mirror writes only on LEVEL CHANGE -- m_lastPicLevel caches
    // the previous output so the hot path is two cheap computed reads
    // and a compare, no atomics, until something actually transitions.
    //
    // DRIR<55> is the ISA/SIO bridge output line, firmware-source-
    // confirmed: pc264_io.c:533 unmasks DIM0 bit 55 itself.
    //
    // COM2 rides the same bridge via ISA IRQ3 (kept wired for free --
    // both inputs are evaluated; only COM1 matters to the milestone).
    // --------------------------------------------------------------------
    static constexpr int kIsaBridgeDrirBit = 55;   // pc264_io.c:533

    // Pchip0 PCI interrupt-acknowledge window (chipset offset; PA =
    // kMMIO_Start + this).  Firmware-observed (run 20260604-152214);
    // AXPBox decodes its IACK analogously.  Spurious vector = master
    // ICW2 base (0x00) | 7 per 8259 spurious-IRQ7 semantics.
    static constexpr uint64_t kPchip0IackOffset = 0x1F8000000ULL;
    static constexpr uint64_t kSpuriousIackVector = 0x07ULL;

    void evalDeviceIrqs() noexcept
    {
        // RX injection drain (Increment 3, 2026-06-04): move at most ONE
        // byte per step boundary from the console backend's thread-safe
        // queue (PuTTY -> SRMConsoleDevice::m_rxQueue, QMutex-guarded;
        // the sole thread boundary) into the UART's deterministic RX
        // FIFO.  Consumption happens only here, never mid-instruction.
        // Live-interactive mode: arrival timing is host-nondeterministic
        // by nature; record/replay of an injection schedule is the
        // deferred follow-up (design doc Section 5 sidecar contract --
        // TODO(replay-schedule)).
        if (m_com1.rxFifoCount() < Uart16550::kRxFifoDepth) {
            if (IConsoleDevice* be = m_com1.backend()) {
                int const ch = be->getChar(false, 0);   // non-blocking
                if (ch >= 0) {
                    m_com1.feedRxByte(static_cast<uint8_t>(ch));
                }
            }
        }

        m_pic.setIrqInput(4, m_com1.intPending());   // COM1 = ISA IRQ4
        m_pic.setIrqInput(3, m_com2.intPending());   // COM2 = ISA IRQ3
        m_pic.setIrqInput(6, m_superio.fdcInterruptPending()); // FDC = ISA IRQ6 (F5)
        m_pic.setIrqInput(1, m_kbd8042.irq1Pending()); // 8042 KBD = ISA IRQ1 (JRN-VMB-006)

        bool const level = m_pic.outputAsserted();
        if (level != m_lastPicLevel) {
            m_lastPicLevel = level;
            if (level) m_cchip.assertInterrupt(kIsaBridgeDrirBit);
            else       m_cchip.deassertInterrupt(kIsaBridgeDrirBit);
        }

        // ----------------------------------------------------------------
        // IIC completion interrupt (DS20 badge root-cause fix, 2026-06-29).
        // The PCF8584 is a Cchip-DIRECT interrupt (not on the 8259), so its
        // INT level drives a DRIR bit straight into the Cchip; the firmware
        // maps that bit to SCB vector 0xa9/0xaa (iic_service) via the PAL
        // 0x800 + vector*16 rule.  Proven need: the iic_init .trc shows the
        // interrupt-driven driver krn$_wait-timing-out because no INT is
        // delivered -> rec_count 0 -> iic_ocp0 unregistered -> member 1.
        //
        // The exact DS20 DRIR bit is selected at runtime via
        // EMULATR_IIC_IRQ_BIT (device class DRIR<55:0>) so it can be hunted
        // without a rebuild.  DEFAULT OFF (unset) == today's faithful
        // behavior (no IIC INT; boot byte-identical).  Once the bit that
        // makes iic_init register iic_ocp0 (SYSVAR -> 0x1805, member 6) is
        // confirmed, replace this env read with a named constant.
        // ----------------------------------------------------------------
        static int const s_iicDrirBit = []() -> int {
            char const* e = std::getenv("EMULATR_IIC_IRQ_BIT");
            int const b = (e && *e)
                ? static_cast<int>(std::strtol(e, nullptr, 0)) : -1;
            return (b >= 0 && b <= 55) ? b : -1;   // device class DRIR<55:0>
        }();
        if (s_iicDrirBit >= 0) {
            static bool s_lastIicLevel = false;
            bool const iicLevel = m_iic.interruptPending();
            if (iicLevel != s_lastIicLevel) {
                s_lastIicLevel = iicLevel;
                if (iicLevel) m_cchip.assertInterrupt(s_iicDrirBit);
                else          m_cchip.deassertInterrupt(s_iicDrirBit);
#if defined(EMULATR_DIAGNOSTIC_LOGGING)
                // DS20 badge diag (2026-06-29): prove whether the IIC INT level
                // ever asserts (interruptPending == PIN-clear && ENI).  If this
                // never prints, the verify never set ENI -> the interrupt path
                // is not the gate.  Throttled.
                static unsigned s_n = 0;
                if (s_n++ < 64) {
                    std::fprintf(stderr, "IIC-IRQ-%s bit=%d\n",
                                 iicLevel ? "ASSERT" : "deassert", s_iicDrirBit);
                    std::fflush(stderr);
                }
#endif
            }
        }
    }

    // INTA seam for Machine::run's device-divert block: acknowledges the
    // PIC (IRR -> ISR transfer, output drops until the guest ISR's EOI)
    // and refreshes the DRIR<55> mirror in the same boundary so the
    // pendingIrq1 level falls before the next arbitration poll.
    int acknowledgeDeviceInterrupt() noexcept
    {
        int const vec = m_pic.acknowledge();
        evalDeviceIrqs();
        return vec;
    }

    Pic8259Pair& pic()       noexcept { return m_pic; }
    Pic8259Pair const& pic() const noexcept { return m_pic; }

    // --------------------------------------------------------------------
    // Cross-chip wires (called by the realms, not by the CPU).
    // --------------------------------------------------------------------
    // Pchip -> Cchip interrupt promotion.  The 21272 exposes 64 raw device
    // interrupt inputs (DRIR<63:0>); which PCI INTx pin lands on which bit
    // is board wiring, NOT fixed by the HRM.  V4 convention (see
    // docs/hrm_deviations.md): Pchip0 INTA-D -> DRIR[35:32], Pchip1 INTA-D
    // -> DRIR[39:36].  DRIR<63> is reserved for the error/NXM interrupt.
    static constexpr int pciIntxToDrirBit(int pchipId, int intxLine) noexcept
    {
        return 32 + (pchipId * 4) + (intxLine & 0x3);
    }

    void raisePciInterrupt(int pchipId, int intxLine) noexcept
    {
        m_cchip.assertInterrupt(pciIntxToDrirBit(pchipId, intxLine));
    }

    void lowerPciInterrupt(int pchipId, int intxLine) noexcept
    {
        m_cchip.deassertInterrupt(pciIntxToDrirBit(pchipId, intxLine));
    }

    // --------------------------------------------------------------------
    // Bus-master bulk DMA (S1 seam, JRN-SCSI-002 G-A).
    // --------------------------------------------------------------------
    // A PCI bus-master device (SCSI HBA SCRIPTS fetch/block-move, tulip
    // descriptor rings) reads/writes GUEST memory using PCI addresses; the
    // Pchip inbound windows (WSBA/WSM/TBA) map those to PAs.  These helpers
    // are the device-facing capability: arbitrary length, byte-accurate,
    // window-translated per 4 KiB chunk so a burst crossing a window/page
    // boundary re-translates rather than assuming contiguity.  Built on the
    // existing translateDmaToPa (direct-map; SG walk stays a TODO inside
    // that seam per JRN-VMB-019 B4 -- consumers are unaffected when it
    // lands).  GuestMemory accessors are page-crossing-safe (memory.md 2.3).
    // Returns bytes transferred (== n today; short only if a future SG
    // fault path needs to report one).
    size_t dmaReadBytes(uint64_t pciAddr, void* dst, size_t n) noexcept
    {
        auto* out = static_cast<uint8_t*>(dst);
        size_t done = 0;
        while (done < n) {
            uint64_t const pci   = pciAddr + done;
            size_t const   chunk = static_cast<size_t>(
                std::min<uint64_t>(n - done, 0x1000ULL - (pci & 0xFFFULL)));
            uint64_t const paBase = m_pchip.translateDmaToPa(pci);
            for (size_t i = 0; i < chunk; ++i) {
                uint8_t b = 0;
                (void) m_guestMemory.read1(paBase + i, b);
                out[done + i] = b;
            }
            done += chunk;
        }
        return done;
    }

    size_t dmaWriteBytes(uint64_t pciAddr, void const* src, size_t n) noexcept
    {
        auto const* in = static_cast<uint8_t const*>(src);
        size_t done = 0;
        while (done < n) {
            uint64_t const pci   = pciAddr + done;
            size_t const   chunk = static_cast<size_t>(
                std::min<uint64_t>(n - done, 0x1000ULL - (pci & 0xFFFULL)));
            uint64_t const paBase = m_pchip.translateDmaToPa(pci);
            for (size_t i = 0; i < chunk; ++i) {
                (void) m_guestMemory.write1(paBase + i, in[done + i]);
            }
            done += chunk;
        }
        return done;
    }

    // Bus -> Cchip NXM promotion.  Latches MISC<NXM> and locks MISC<NXS>
    // (source).  Firmware later W1C-clears NXM.  The faulting PA is captured
    // separately by the bus trace; the Cchip only needs the source per HRM.
    void reportNxm(uint64_t pa, int sourceCode) noexcept
    {
        // Phase B-NXMA (2026-05-28): sticky semantics per HRM 10.2.x.
        // While MISC<NXM> is already latched, subsequent NXM events are
        // silently absorbed -- no extra latch transition, no extra IRQ
        // pulse, no extra stderr log.  Firmware W1C of MISC<NXM> arms
        // the path again for the next event.  Matches real-silicon
        // behavior: the NXS source-code field is undefined while NXM
        // is set, so suppressing the second-event capture is the
        // architecturally honest move.
        //
        // The fast-path early-out also keeps the stderr log meaningful
        // -- one log line per probe-sweep "round" instead of one per
        // unclaimed PA in the sweep.
        if (m_cchip.miscNxmLatched()) return;

        // DIAG (task #3): make the unclaimed-PA decode observable on stderr.
        // Throttled to the first 64 like the CBOX loud-stderr cap so a deep
        // run cannot be swamped.  Unconditional (not debug-gated) so it is
        // visible in release builds too.
        static std::atomic<unsigned> s_nxmSeen{ 0 };
        unsigned const k = s_nxmSeen.fetch_add(1, std::memory_order_relaxed);
        if (k < 64) {
            std::fprintf(stderr,
                "reportNxm: NXM pa=0x%016llx src=%d (#%u)\n",
                static_cast<unsigned long long>(pa), sourceCode, k);
            std::fflush(stderr);
        }
        // latchNxm now also fetch_or's DRIR<63> (Phase B-NXMA Gap 2 in
        // TsunamiCchip.h), so Machine::run's b_irq<0> arbitration block
        // will pick up the event via pendingIrq0() at its next poll.
        m_cchip.latchNxm(static_cast<unsigned>(sourceCode));
    }

    // ====================================================================
    // Static GuestMemory / MmioRegistry shims
    // ====================================================================
    // Reconstruct the full PA from the window-relative offset and delegate
    // to the instance methods with cpuId = 0.  Keeps machineMmioRead and
    // the deprecated MmioRegistry working with no changes to Machine.cpp.

    static uint64_t mmioRead(void* ctx, uint64_t offset, uint8_t width) noexcept
    {
        return static_cast<TsunamiChipset*>(ctx)->mmioRead(
            offset + Tsunami21272::Base::kMMIO_Start, width, 0);
    }

    static void mmioWrite(void* ctx, uint64_t offset, uint64_t value, uint8_t width) noexcept
    {
        static_cast<TsunamiChipset*>(ctx)->mmioWrite(
            offset + Tsunami21272::Base::kMMIO_Start, value, width, 0);
    }

    // ====================================================================
    // Accessors
    // ====================================================================

    ChipsetVariant          variant() const noexcept { return m_variant; }
    const std::string& model()   const noexcept { return m_model; }
    const ChipsetVariantInfo* info()  const noexcept { return variantInfo(m_variant); }

    TsunamiCchip& cchip()       noexcept { return m_cchip; }
    const TsunamiCchip& cchip() const noexcept { return m_cchip; }

    // ES40 TIG-bus module-reset request (prototype, 2026-07-13).  Delegates to
    // the TIG device, which raises the request on the co-gated reset triad
    // (behind EMULATR_TIG_RESET).  Machine::run polls this and applies the reset.
    [[nodiscard]] bool tigResetRequested() const noexcept { return m_tig.resetRequested(); }
    void clearTigResetRequest() noexcept { m_tig.clearResetRequest(); }
    TsunamiDchip& dchip()       noexcept { return m_dchip; }
    const TsunamiDchip& dchip() const noexcept { return m_dchip; }
    TsunamiPchip& pchip()       noexcept { return m_pchip; }
    const TsunamiPchip& pchip() const noexcept { return m_pchip; }
    TsunamiTig& tig()           noexcept { return m_tig; }
    const TsunamiTig& tig() const noexcept { return m_tig; }

    // task #25: RMC Dual-Port RAM accessor (tests / diagnostics).
    TsunamiDpr&       dpr()       noexcept { return m_dpr; }
    const TsunamiDpr& dpr() const noexcept { return m_dpr; }
    bool              hasRmcDpr() const noexcept { return m_hasRmcDpr; }

    // Direct byte-store access for boot-strap loaders / initialization that
    // legitimately bypass the arbiter (ROM/firmware load into DRAM before the
    // CPU runs).  Steady-state CPU traffic always goes through read/write/fetch.
    memoryLib::GuestMemory& guestMemory()       noexcept { return m_guestMemory; }
    const memoryLib::GuestMemory& guestMemory() const noexcept { return m_guestMemory; }

    // Ticket 6: chipset-owned ISA bridge + serial devices (plug-in seam).
    Cy82C693IsaBridge& cypress() noexcept { return m_cypress; }
    Uart16550& com1()    noexcept { return m_com1; }
    Uart16550& com2()    noexcept { return m_com2; }
    ToyRtc& rtc()     noexcept { return m_rtc; }   // 2026-06-03
    IicPcf8584& iic()     noexcept { return m_iic; }   // 2026-06-03

    // Manifest-driven PCI enumeration (D-PCIMODEL): resolve a PciModel::Named
    // manifest modelName to the chipset's behavioral PCI config handler, so
    // Machine::run can register it at the manifest-declared (slot,func).  The
    // active IDE (m_activeIde) is chosen by wireDevices() per south bridge, so
    // both "ali_m5229" and "cypress_ide" resolve to whichever is active.
    // Returns nullptr for an unknown name -> caller presents a config stub.
    IPciDeviceHandler* pciHandlerForModel(const std::string& modelName) noexcept {
        if (modelName == "ali_m1543c")  return &m_ali;
        if (modelName == "cypress_isa") return &m_cypress;
        if (modelName == "ali_m5229" || modelName == "cypress_ide")
            return m_activeIde;
        if (modelName == "de500" || modelName == "dec21143")
            return &m_tulip;
        if (modelName == "ncr53c810")                     // JRN-SCSI-001 (pka)
            return &m_scsi;
        return nullptr;
    }

    // Manifest-driven SCSI wiring (JRN-SCSI-003): Machine resolves the DRIR
    // bit from the manifest's board routing table (slot x interrupt_pin) and
    // pushes it + the config-space pin here after manifest load.
    void setScsiIntxRouting(int drirBit) noexcept { m_scsiIntxDrir = drirBit; }
    void setScsiInterruptPin(uint8_t pin) noexcept { m_scsi.setInterruptPin(pin); }

    // SCSI disk media attach (JRN-SCSI-001/-003; mirrors setDiskMedia for
    // IDE).  MANIFEST-PINNED (2026-07-25, per architect direction): a disk
    // target INSTANCE exists on the bus ONLY for a platform.json storage row
    // that attaches media here -- there are no compiled-in disk devices.  An
    // id with no declared media stays empty and the console's selection of
    // it times out (STO), matching an unpopulated bus.  One instance per id
    // (ids 0..6; id 7 = the HBA itself).
    bool setScsiDiskMedia(int scsiId,
                          std::unique_ptr<scsi::IBlockMedia> media) noexcept
    {
        if (scsiId < 0 || scsiId > 6) return false;   // 7 = HBA
        auto disk = std::make_unique<scsi::VirtualDiskDevice>(std::move(media));
        if (!disk->hasMedia()) return false;
        if (!m_scsi.attachTarget(static_cast<unsigned>(scsiId), disk.get()))
            return false;
        m_scsiDisks[scsiId] = std::move(disk);
        return true;
    }

    // TIG-bus flash / NVRAM (AMD Am29F016).  Machine binds its backing file
    // via flash().loadRaw(path) after construction and calls forceFlush() on
    // clean shutdown; steady-state persistence is the debounce poll in step().
    FlashRom& flash()   noexcept { return m_flash; }

    // --------------------------------------------------------------------
    // Snapshot serialization of chipset-owned DEVICES (kChipsetVersion 2,
    // 2026-06-05).  Cchip/Dchip/Pchip serialize separately (Snapshot.cpp
    // calls them directly, format-stable since v1); this pair covers the
    // interrupt-chain devices whose loss bricked restored consoles:
    // COM1/COM2 UARTs, the 8259 pair, and m_lastPicLevel (the DRIR<55>
    // mirror's change detector -- must stay coherent with the restored
    // cchip DRIR bit so the first evalDeviceIrqs() doesn't double-edge).
    // kChipsetVersion 3 (2026-06-06): + FlashRom 2 MB image (below).  SRM
    // environment variables live in the TIG flash, so a faithful snapshot
    // must carry it or restored env is lost.
    // kChipsetVersion 4 (2026-06-06): + IicPcf8584 FRU EEPROM bank image
    // (below).  set sys_serial_num / buildfru write the FRU JEDEC EEPROMs, so
    // a faithful snapshot must carry the bank or those writes are lost.  Still
    // NOT serialized (future candidates if symptoms appear): ToyRtc, the RCM
    // NVRAM bank (guest re-reads, zero default).
    // --------------------------------------------------------------------
    void serializeDevices(QDataStream& ds) const noexcept
    {
        m_com1.serialize(ds);
        m_com2.serialize(ds);
        m_pic.serialize(ds);
        ds << static_cast<quint8>(m_lastPicLevel ? 1 : 0);
        // kChipsetVersion 3: TIG flash / NVRAM image (length-prefixed raw
        // bytes).  D2 read-array mode is implied on restore; see
        // FlashRom::image()/restoreImage().
        const std::vector<uint8_t>& img = m_flash.image();
        ds << static_cast<quint32>(img.size());
        if (!img.empty()) {
            ds.writeRawData(reinterpret_cast<const char*>(img.data()),
                static_cast<int>(img.size()));
        }
        // kChipsetVersion 5 (2026-06-07): IIC device CONTENT, manifest-driven.
        // Identity (which devices, addresses, kinds) is re-applied from the
        // platform manifest before restore, so only mutable content travels:
        // count + count*kImageSize raw bytes in configured bus order.  See
        // IicPcf8584::contentImage()/restoreContentImage().
        const quint32 iicCount = static_cast<quint32>(m_iic.deviceCount());
        ds << iicCount;
        if (iicCount > 0) {
            std::vector<uint8_t> content(m_iic.contentBytes());
            m_iic.contentImage(content.data());
            ds.writeRawData(reinterpret_cast<const char*>(content.data()),
                static_cast<int>(content.size()));
        }
    }

    void deserializeDevices(QDataStream& ds) noexcept
    {
        m_com1.deserialize(ds);
        m_com2.deserialize(ds);
        m_pic.deserialize(ds);
        quint8 lvl = 0;
        ds >> lvl;
        m_lastPicLevel = (lvl != 0);
        // kChipsetVersion 3: TIG flash / NVRAM image.
        quint32 flashBytes = 0;
        ds >> flashBytes;
        if (flashBytes > 0) {
            std::vector<uint8_t> img(static_cast<size_t>(flashBytes));
            ds.readRawData(reinterpret_cast<char*>(img.data()),
                static_cast<int>(flashBytes));
            m_flash.restoreImage(img.data(), img.size());
        }
        // kChipsetVersion 5: IIC device content.  The bus is already configured
        // from the manifest (Machine applies it before restore), so the count
        // must match; a mismatch means a stale snapshot -- skip its content
        // rather than corrupt the freshly configured bus.
        quint32 iicCount = 0;
        ds >> iicCount;
        const quint32 expect = static_cast<quint32>(m_iic.deviceCount());
        const size_t  bytes  =
            static_cast<size_t>(iicCount) * static_cast<size_t>(IicPcf8584::kImageSize);
        if (iicCount == expect && iicCount > 0) {
            std::vector<uint8_t> content(bytes);
            ds.readRawData(reinterpret_cast<char*>(content.data()),
                static_cast<int>(bytes));
            m_iic.restoreContentImage(content.data(), content.size());
        }
        else if (iicCount > 0) {
            ds.skipRawData(static_cast<int>(bytes));
        }
    }

    // registerWithMMIO removed - GSEA uses chipset_adapter.h instead

private:

    // South-bridge selection (2026-07-12): driven by the single model-keyed
    // lever m_southBridge (southBridgeFromModel, TsunamiVariant.h), which
    // RETIRES the former isAliPlatform(model) string bool.  The SAME lever
    // selects the IDE controller (func1) in Phase 2, so bridge + IDE stay
    // consistent by construction.  DS10/DS20 -> Cypress (byte-identical);
    // ES40/ES45/DS25 -> ALi M1543C.  The manifest ISA-bridge identity
    // (PlatCap::SbAli) is the VALUE source that must agree; Machine warns on
    // drift.  The south-bridge axis is orthogonal to the chipset variant.

    void wireDevices() noexcept {
        // 1. Register the south bridge (func0) in the PCI device map, and wire
        //    it as the I/O-port fallback handler.  ALi for ES40/ES45/DS25, else
        //    Cypress (DS10/DS20).
        // PCI CONFIG-SPACE registration for the south bridge + IDE is now
        // DRIVEN BY THE MANIFEST (Machine::run -> registerManifestPci(), which
        // resolves each PciModel::Named entry via pciHandlerForModel() and
        // registers it at the manifest-declared (slot,func)).  This makes the
        // BDF topology data, not code -- one binary serves DS10/DS20/DS25/
        // ES40/ES45, each placing its south bridge/IDE where its SRM expects
        // (e.g. Cypress at dev 5 on DS10/DS20, ALi M1543C at dev 15 on ES40).
        // Here we wire ONLY the legacy I/O-port fallback (not BDF-dependent).
        if (m_southBridge == SouthBridge::AliM1543C) {
            m_pchip.setIoPortHandler(&m_ali);
        } else {
            m_pchip.setIoPortHandler(&m_cypress);
        }

        m_pchip.registerIoPortRange(0x3F8, 0x400, &m_com1); // COM1
        m_pchip.registerIoPortRange(0x2F8, 0x300, &m_com2); // COM2

        // 2026-05-28 unblocker: minimal stubs for the two ISA legacy devices
        // the SRM firmware polls in the early console-init phase, between
        // chipset probe completion and COM1 banner emit.  Without these the
        // firmware spins on port 0x64 (8042 status) waiting for the keyboard
        // controller to declare "self-test passed", never reaching the
        // putChar path.  See deviceLib/Tsunami/MinimalIsaStub.h for the
        // full TODO list and the planned per-device implementations.
        m_pchip.registerIoPortRange(0x60, 0x65, &m_kbd8042); // 8042 KBD/MOUSE
        m_pchip.registerIoPortRange(0x70, 0x72, &m_rtc);     // MC146818 TOY/CMOS (ToyRtc)

        // 2026-06-04 (Increment 2): 8259 PIC pair + ELCR.  pc264_io.c
        // initialize_hardware programs ICW1-4/OCW1 through these ports.
        m_pchip.registerIoPortRange(0x20, 0x22, &m_pic);   // master 8259
        m_pchip.registerIoPortRange(0xA0, 0xA2, &m_pic);   // slave 8259
        m_pchip.registerIoPortRange(0x4D0, 0x4D2, &m_pic);   // ELCR (stored)
        // CY82C693 IDE function 1: enumerate it + claim the legacy ATA taskfile
       // windows.  Primary master (dqa0) is the bootable ATA fixed disk; its
       // IBlockMedia is injected post-construction via setDiskMedia() from
       // Machine's media_kind factory (path resolved from [Storage] diskDir +
       // the manifest media).  The ATAPI CD is primary slave (dqa1), media via
       // setCdMedia().  Both enumerate no-media until a backing is provided.
        // Phase 2B: select the model's IDE controller via the SAME south-bridge
        // lever that picks the bridge (m_southBridge).  ES40/ES45/DS25 -> the
        // faithful ALi M5229 (so the pc264 console recognizes the controller it
        // expects and probes the taskfile / IDENTIFY); DS10/DS20 -> the Cypress
        // CY82C693 (byte-identical).  Both wrap the shared AtaTaskfileEngine; the
        // ATAPI CD attach + func-1 config + taskfile ports route to the active one.
        m_activeIde = (m_southBridge == SouthBridge::AliM1543C)
                    ? static_cast<ITsunamiIde*>(&m_aliIde)
                    : static_cast<ITsunamiIde*>(&m_ide);
        m_activeIde->attachDevice(0, 1, &m_cdrom);              // primary slave = ATAPI CD (dqa1)
        // func-1 config space is registered from the manifest (Machine::run ->
        // pciHandlerForModel("ali_m5229"/"cypress_ide") -> m_activeIde).

        // DE500 21143 (ewa): its CSR window is relocatable via BAR0(I/O)/BAR1(mem).
        // When the SRM programs a BAR, the model calls back to register its
        // 0x80-byte CSR window at the assigned base so CSR accesses route here.
        // (Pchip mem/IO registries are offset-from-kMMIO_Start / port space; a
        // BAR base is exactly that offset.)  2026-07-24 (S1 seam, JRN-SCSI-002
        // G-B): removal is now REAL -- a BAR re-program retires the old claim
        // via unregister{PciMemRange,IoPortRange}, so re-assignment during a
        // later SRM/OS enumeration pass cannot leave a stale first-match range
        // shadowing the new base.
        m_tulip.setRangeCallbacks(
            [this](uint64_t base, uint32_t len, bool isMem, IIoPortHandler* self) {
                if (isMem) m_pchip.registerPciMemRange(base, base + len, self);
                else       m_pchip.registerIoPortRange(
                               static_cast<uint16_t>(base),
                               static_cast<uint16_t>(base + len), self);
            },
            [this](uint64_t base, uint32_t len, bool isMem, IIoPortHandler* self) {
                if (isMem) m_pchip.unregisterPciMemRange(base, base + len, self);
                else       m_pchip.unregisterIoPortRange(
                               static_cast<uint16_t>(base),
                               static_cast<uint16_t>(base + len), self);
            });
        // DMA access for TX/RX descriptor completion.  Descriptor addresses in
        // CSR3/4 are PCI-DMA addresses; wired direct-to-guest-PA for now (the
        // tulip's TULIP-TX trace verifies whether a Pchip DMA-window translation
        // is needed).  read4/write4 bounds-check against DRAM.
        m_tulip.setDmaAccess(
            [this](uint64_t dma) -> uint32_t {
                uint32_t v = 0; m_guestMemory.read4(m_pchip.translateDmaToPa(dma), v); return v;
            },
            [this](uint64_t dma, uint32_t v) {
                m_guestMemory.write4(m_pchip.translateDmaToPa(dma), v);
            });
        // Interrupt: DE500 (hose 0 / Pchip0, INTA) -> Cchip DRIR[32].  On an
        // enabled TX/RX interrupt the tulip asserts INTA so the SRM's ew ISR
        // runs, signals tu_out's semaphore, and ewa init completes.
        m_tulip.setIntrCallback([this](bool level) {
            if (level) raisePciInterrupt(/*pchip*/0, /*INTA*/0);
            else       lowerPciInterrupt(0, 0);
        });

        // NCR 53C810 SCSI HBA (pka; JRN-SCSI-001 P1/P2).  Same three seams as
        // the tulip: relocatable BARs (S1 rebind, register+unregister both
        // real), bulk bus-master DMA (G-A: SCRIPTS fetch + block moves through
        // dmaRead/WriteBytes -> Pchip window translation), and INTx (INTB =
        // line 1, distinct from the tulip's INTA).  The console pke driver
        // accesses the CSR file through the MEM BAR (n810_read_byte_csr ->
        // inmemb) and may run POLLED (ISTAT reads) -- both paths served.
        m_scsi.setRangeCallbacks(
            [this](uint64_t base, uint32_t len, bool isMem, IIoPortHandler* self) {
                if (isMem) m_pchip.registerPciMemRange(base, base + len, self);
                else       m_pchip.registerIoPortRange(
                               static_cast<uint16_t>(base),
                               static_cast<uint16_t>(base + len), self);
            },
            [this](uint64_t base, uint32_t len, bool isMem, IIoPortHandler* self) {
                if (isMem) m_pchip.unregisterPciMemRange(base, base + len, self);
                else       m_pchip.unregisterIoPortRange(
                               static_cast<uint16_t>(base),
                               static_cast<uint16_t>(base + len), self);
            });
        m_scsi.setDmaAccess(
            [this](uint64_t pci, void* dst, size_t n) { dmaReadBytes(pci, dst, n); },
            [this](uint64_t pci, void const* src, size_t n) { dmaWriteBytes(pci, src, n); });
        // INTx routing is BOARD DATA, not a formula (2026-07-25 finding: the
        // generic pciIntxToDrirBit(32+..) convention does NOT match PC264
        // option slots; the tulip's DRIR32 is equally suspect on DS20).  The
        // DRIR bit comes from the MANIFEST's pci_irq_table_hose0 (mirroring
        // the console's own pci_irq_table, pc264_io.c:727), delivered by
        // Machine via setScsiIntxRouting() AFTER manifest load.  Until then
        // m_scsiIntxDrir = -1 and an assert warns loud instead of lighting a
        // wrong bit.  S3 generalizes this to every option-card device.
        m_scsi.setIntrCallback([this](bool level) {
            if (m_scsiIntxDrir < 0) {
                static bool warned = false;
                if (!warned) {
                    warned = true;
                    std::fprintf(stderr,
                        "TsunamiChipset: SCSI INTx asserted but no DRIR routing "
                        "configured (manifest pci_irq_table_hose0 missing?) -- "
                        "interrupt DROPPED\n");
                }
                return;
            }
            if (level) m_cchip.assertInterrupt(m_scsiIntxDrir);
            else       m_cchip.deassertInterrupt(m_scsiIntxDrir);
        });
        m_pchip.registerIoPortRange(0x1F0, 0x1F8, m_activeIde); // primary command block
        m_pchip.registerIoPortRange(0x170, 0x178, m_activeIde); // secondary command block
        m_pchip.registerIoPortRange(0x3F6, 0x3F7, m_activeIde); // primary alt-status/control
        m_pchip.registerIoPortRange(0x376, 0x377, m_activeIde); // secondary alt-status/control

        // FDC37C669 SuperIO (#22): owns the 0x3F0 window.  In config mode
        // 0x3F0/0x3F1 are the config index/data port (detect reads CR0D=0x03 so
        // SMC_init proceeds); otherwise they + 0x3F2-0x3F5/0x3F7 are the legacy
        // FDC, delegated to the embedded Floppy82077 fast-fail (#20).  0x3F6=IDE.
        m_pchip.registerIoPortRange(0x3F0, 0x3F6, &m_superio); // config port + FDC SRA..FIFO
        m_pchip.registerIoPortRange(0x3F7, 0x3F8, &m_superio); // FDC DIR / CCR
        // TURBO floppy interrupt-poll register (dv_driver.c ide_poll): the
        // SuperIO forwards non-config ports to the embedded Floppy82077, which
        // answers 0x536 bit 0x80 = floppy IRQ6 pending.  Breaks the ~20-min
        // polled-recalibrate timeout on dva0.  2026-06-11.
        m_pchip.registerIoPortRange(0x536, 0x537, &m_superio); // floppy IRQ6 poll (0x536)

        // VGA text-console interface (JRN-VMB-006, 2026-07-18): the SRM
        // firmware drives a PC/VGA color-text console -- it paints cells into
        // the legacy framebuffer at 0xB8000 and touches the VGA register file
        // at 0x3B0-0x3DF.  V5 modeled no VGA, so every 0xB8000 write fell
        // through as UNHANDLED OUTER WRITE (lost).  m_vga CLAIMS both apertures
        // so those accesses land: the mem handler stores the text buffer (the
        // 32 KiB 0xB8000-0xC0000 window fits the registerPciMemRange 64 KiB
        // cap), the I/O handler answers the VGA registers benignly (toggling
        // Input-Status-1 so retrace polls terminate).  Minimal-unblock: retain
        // + optional EMULATR_VGA_DUMP snapshot, no rendering.  Shared across
        // all five platforms (single wireDevices()).
        m_pchip.registerIoPortRange(0x3B0, 0x3E0, m_vga.ioHandler());   // VGA CRTC/attr/seq/gfx/DAC/status
        m_pchip.registerPciMemRange(0xB8000, 0xC0000, m_vga.memHandler()); // color text buffer (32 KiB)

        // 3. PCI dense-memory claimants (G3-lite seam, 2026-06-03).
        // PCF8584 IIC controller -- FIXED platform mapping ("low BIOS region
        // of ROM space", pc264_init.c:43), so the base is PER-MODEL, NOT a PCI
        // BAR (the PCF8584 is a hardwired CSR region, not config-relocatable).
        // S0-area at +0, S1 control/status at +1.  Empty-bus NAK semantics
        // bound the iic_init probe loop that stalled powerup; see
        // journals/IIC_PCF8584_Specification.txt.  m_iic is model-agnostic;
        // only the decode base differs by platform.  Bases proven from each
        // shipped image's IIC byte traffic (S0 at +0, S1 at +1):
        //   DS10  = 0xFFFF0000  iic_write_csr                        [2026-06-03]
        //   DS20  = 0xFFF80000  writeb @0x1ade60 (EMULATR_IIC_WATCH)  [2026-06-22]
        //   DS20E = 0xFFF80000  shares DS20 chassis/IIC mapping (defensive;
        //                       m_model is the raw INI string, unnormalized)
        // ES45/DS25 IIC base intentionally NOT mapped -- left UNMAPPED rather
        // than guessed.  ES40 (V7.3) is now TRIAL-mapped at 0xFFF80000 below
        // (2026-07-07): the apisrm/ref/pc264_io.c:1229 CLIPPER/PC264 path uses a
        // fixed PCF8584 @ 0xFFF80000, and the es40_v7_3 image carries that base
        // (per-model base table @ decompressed VA 0x8d28 + inline LDAH 0xfff8).
        // The older "blocked UPSTREAM by CSERVE 0x66" note is SUPERSEDED: the
        // build_power_hw/IIC read IS the gate (authoritative call stack +
        // err_printf storm) -- see journals/20260707_es40_interface_coverage_
        // audit.md (sec 2.7/4) and 20260707_es40_printf_deadlock_root.md.  The
        // contested alternative -- srmconsole/5.8 SHARK/M1543C ALi SMBus with a
        // PROGRAMMABLE base (M7101 PCI cfg SBASMB 0x14) -- remains possible; the
        // ES40 row is a SELF-FALSIFYING trial (wrong base -> mapping simply
        // never touched, clean revert; DS10/DS20 unaffected -- model-keyed,
        // southbridge axis).  registerPciMemRange
        // takes a WINDOW-RELATIVE offset, half-open [start,end); +2 claims the
        // two byte ports (S0,S1) exactly.
        static constexpr struct { char const* model; uint64_t base; }
            kIicBaseByModel[] = {
                { "DS10",  0xFFFF0000ULL },   // proven: iic_write_csr     [2026-06-03]
                { "DS20",  0xFFF80000ULL },   // proven: writeb@0x1ade60    [2026-06-22]
                { "DS20E", 0xFFF80000ULL },   // shares DS20 chassis/IIC map (defensive)
                { "ES40",  0xFFF80000ULL },   // TRIAL 2026-07-07: pc264/CLIPPER PCF8584 (apisrm
                                              // pc264_io.c:1229); img base table @0x8d28 + inline
                                              // LDAH 0xfff8.  See audit sec 2.7/4 (self-falsifying).
            };
        // No silent default: an unmatched model is NOT laundered into DS10's
        // base (that converts "unknown" into "confidently wrong" and re-hangs
        // downstream).  DS10 is safe because it is an explicit ROW, not a
        // fallback.  Find-or-fail:
        uint64_t const* iicBase = nullptr;
        for (auto const& e : kIicBaseByModel)
            if (m_model == e.model) { iicBase = &e.base; break; }
        if (iicBase != nullptr) {
            m_pchip.registerPciMemRange(*iicBase, *iicBase + 2, &m_iic);
        } else {
            // Models we drive to the SRM console with the IIC on the boot path:
            // a missing table row for THESE is a build error in kIicBaseByModel,
            // not a runtime unknown -- hard-stop (same posture as the fpBox x87
            // guard) rather than paper over it.  Expand as models reach console
            // bring-up.  NOTE this set is intentionally narrower than
            // variantFromModel's recognized models: ES45/DS25 are accepted
            // configs but their IIC base is not yet proven, so they are NOT
            // hard-stopped here.  ES40 is IIC-required as of 2026-07-07 (it now
            // has a proven-trial row above); the hard-stop is hygiene -- if the
            // row is ever removed, abort instead of silently unmapping.
            auto const iicBaseRequired = [](std::string const& m) noexcept {
                return m == "DS10" || m == "DS20" || m == "DS20E" || m == "ES40";
            };
            std::fprintf(stderr,
                "TsunamiChipset: no proven IIC base for model '%s' -- IIC left "
                "UNMAPPED.  First poke -> UNHANDLED OUTER WRITE will surface the "
                "real base (the signal that located DS20); add a proven "
                "kIicBaseByModel row when known.\n",
                m_model.c_str());
            if (iicBaseRequired(m_model)) {
                std::fprintf(stderr,
                    "TsunamiChipset: FATAL -- model '%s' is IIC-required but has "
                    "no kIicBaseByModel row; refusing to launder into a default "
                    "base.\n", m_model.c_str());
                std::abort();
            }
            // else: unproven/unknown model -> leave IIC unmapped (no guessed
            // registration that could shadow a real device at the wrong base).
        }
    }

    static ChipsetVariant normalizeVariant(ChipsetVariant v) noexcept
    {
        return (v == ChipsetVariant::Unknown) ? ChipsetVariant::Tsunami : v;
    }

    // All three sub-chips must agree with the chipset's variant.
    void assertVariantConsistency() const noexcept
    {
        if (m_cchip.variant() != m_variant ||
            m_dchip.variant() != m_variant ||
            m_pchip.variant() != m_variant) {
            std::fprintf(stderr,
                "TsunamiChipset: sub-chip variant mismatch -- aborting\n");
            std::abort();
        }
    }



    ChipsetVariant  m_variant;
    std::string     m_model;
    // 2026-07-12: single model-keyed south-bridge lever (func0 ISA bridge now;
    // func1 IDE in Phase 2).  Derived from m_model via southBridgeFromModel();
    // replaces the retired isAliPlatform() string bool.
    SouthBridge     m_southBridge;

    // Internal routing
    bool isDramAddress(uint64_t pa) const noexcept;

    // PAL scratchpad carve-out: the top 1 MB of PA space (16 TB - 1 MB .. 16 TB)
    // is PALcode-private spill/fill storage.  The arbiter decodes it AHEAD of
    // DRAM/I/O/NXM so it never reaches a window claimant (it collides with the
    // I/O window otherwise).  Heap-backed (vector) so the 1 MB does not inflate
    // the chipset object on the stack.
    static constexpr uint64_t kPalScratchBase = 0xFFFFFF00000ULL;
    static constexpr uint64_t kPalScratchSize = 0x100000ULL;   // 1 MB
    static constexpr bool isPalScratchAddr(uint64_t pa) noexcept {
        return pa >= kPalScratchBase && pa < (kPalScratchBase + kPalScratchSize);
    }
    std::vector<uint8_t> m_palScratch = std::vector<uint8_t>(kPalScratchSize, 0);

    // TIG-bus flash window.  The firmware's xtig() maps flash byte offset to
    // PA (TIG_BASE << 24) | (offset << 6), i.e. base 0x801_0000_0000 with each
    // byte at a 64-byte stride.  The populated extent is the 2 MB device times
    // the stride = 0x8000000 (128 MB), matching AXPBox's registration.  Decoded
    // AHEAD of the kMMIO_Start branch (the base lies above kMMIO_Start), so it
    // never falls through to the Cchip/Pchip CSR routing or NXM.
    static constexpr uint64_t kTigFlashBase = 0x80100000000ULL;
    static constexpr uint64_t kTigFlashSize = 0x8000000ULL;  // FlashRom::kSize << 6
    static constexpr bool isTigFlashAddr(uint64_t pa) noexcept {
        return pa >= kTigFlashBase && pa < (kTigFlashBase + kTigFlashSize);
    }
    static constexpr uint32_t tigFlashOffset(uint64_t pa) noexcept {
        return static_cast<uint32_t>((pa - kTigFlashBase) >> 6);
    }

    // ------------------------------------------------------------------
    // TIG-bus device registers -- modeled in TsunamiTig (m_tig).
    // ------------------------------------------------------------------
    // The TIG control + arbiter registers (smir, per-CPU halt-IPI, ipcr,
    // arbiter/PLD rev) are a faithful register file in TsunamiTig.h, decoded
    // via m_tig.decodes()/read()/write() in TsunamiChipset.cpp ahead of the
    // generic mmioRead branch.  Root cause this fixed: the SRM's read of smir
    // (TIG+0x40) fell through to the all-ones mmioRead default and the
    // firmware read it as "Halt Button is IN" -> refused `boot` (HALTPROBE:
    // pa=0x80130000040 v=0xffffffff).  See TsunamiTig.h for the register
    // table + DEC source citations; journals/20260613_halt_switch_tig_
    // register.md; memory project_tig_halt_register_boot_refusal.
    // NOTE: kTigTraceArmReg (below) lives in the TIG window and MUST be
    // decoded BEFORE m_tig.

    // ------------------------------------------------------------------
    // EmulatR debug: console-armable retire-trace trigger (2026-06-13).
    // ------------------------------------------------------------------
    // A reserved TIG PA (not a real register) used to open/close the
    // DecListingSink retire-compact window FROM THE SRM PROMPT, so we can
    // trace EXACTLY the `b dqa0/1` command after a cold boot to `>>>`,
    // skipping the multi-billion-cycle boot.  Both the SRM `examine` and
    // `deposit` paths route through this chipset read()/write(), so:
    //   >>> e pmem:80130000FF8        -> READ  : trace ON until run end
    //                                    (sets a very large window count;
    //                                     returns the current count)
    //   >>> d pmem:80130000FF8 N      -> WRITE : window = N instructions
    //   >>> d pmem:80130000FF8 0      -> WRITE : trace OFF
    // Requires the run to construct the DecListingSink with the _srm.trc
    // open but no continuous RETIRE_COMPACT stream -- set env
    // EMULATR_TRACE_WINDOW=1 (see main.cpp), so emission is purely
    // window-gated.  Diagnostic only; remove once the halt-decision source
    // is pinned.  See memory project_tig_halt_register_boot_refusal.
    static constexpr uint64_t kTigTraceArmReg = 0x80130000FF8ULL;  // 0x801_3000_0FF8

    // Member components
    memoryLib::GuestMemory m_guestMemory; // Now owned by the chipset
    TsunamiCchip    m_cchip;
    TsunamiDchip    m_dchip;
    TsunamiPchip    m_pchip;
    TsunamiTig      m_tig;     // TIG-bus device register file (smir/halt/ipcr/arbiter)

    // task #25: RMC Dual-Port RAM.  m_hasRmcDpr gates the DPR window decode to
    // the RMC-bearing platform (ES40 == Typhoon); DS10/DS20 (Tsunami) leave the
    // window unclaimed -> byte-identical (they never access 0x801_1000_0000).
    // ES45 is Titan (separate TitanChipset) and gets its own DPR wiring later.
    bool            m_hasRmcDpr;
    TsunamiDpr      m_dpr;      // RMC dual-port RAM device (see TsunamiDpr.h)

    // (Bus arbiter surface is now the public ISystemBus override above.)



    // Ticket 6: chipset-owned ISA bridge + serial devices, registered with
    // Pchip0 in wireDevices().  Declared after m_pchip so it constructs
    // first (the Pchip registries hold raw pointers into these members).
    Cy82C693IsaBridge m_cypress;
    AliM1543C         m_ali;       // ES40/ES45/DS25 south bridge; wired when m_southBridge==AliM1543C
    Uart16550         m_com1{ nullptr, 0x3F8, "COM1" };
    Uart16550         m_com2{ nullptr, 0x2F8, "COM2" };

    // CY82C693 IDE (PCI Function 1) + a no-media ATAPI CD on primary master.
   // After m_pchip (registries hold raw pointers); m_cdrom before m_ide so the
   // CD outlives the controller that points at it.
    scsi::VirtualIsoDevice m_cdrom;          // no-media ATAPI CD
    Cy82C693Ide            m_ide;            // CY82C693 IDE controller (func 1; DS10/DS20)
    AliM5229Ide            m_aliIde;         // ALi M5229 IDE controller (func 1; ES40/ES45/DS25)
    deviceLib::Dec21143Tulip m_tulip;        // DE500-AA 21143 NIC (ewa); wired via the manifest (slot 7)
    deviceLib::Ncr53C810     m_scsi;         // NCR 53C810 SCSI HBA (pka); manifest slot 8 (JRN-SCSI-001)
    // Disk targets ids 0..6, instantiated ONLY from platform.json storage
    // rows (setScsiDiskMedia); id 7 = the HBA.  2026-07-25 manifest-pinning.
    std::array<std::unique_ptr<scsi::VirtualDiskDevice>, 7> m_scsiDisks{};
    int                      m_scsiIntxDrir = -1;  // DRIR bit from manifest routing (JRN-SCSI-003)
    ITsunamiIde*           m_activeIde = nullptr;  // -> m_ide or m_aliIde; set in wireDevices()
    Smc37c669SuperIo       m_superio;       // FDC37C669 SuperIO: config port + FDC LDN (#22)


    // 2026-05-28 minimal stubs (idle-ready behavior only -- see header for TODOs).
    // Declared after the Pchip / serial members so they construct first; the
    // Pchip I/O port registry holds raw pointers into these slots and the
    // construction order keeps the pointers valid for the chipset's lifetime.
    Kbd8042Stub       m_kbd8042;

    // 2026-06-04 (Increment 2): Cypress CY82C693 embedded 8259A pair.
    // The DS10 SRM programs it at init (pc264_io.c:520-571) and the
    // serial driver's IRQ4 must pass the master IMR before DRIR<55>
    // may assert -- the mask is load-bearing (design doc Section 6).
    // Registered at ports 0x20-0x21 / 0xA0-0xA1 / 0x4D0-0x4D1 in
    // wireDevices(); output consumed by evalDeviceIrqs() below.
    Pic8259Pair       m_pic;

    // 2026-06-03: functional MC146818 TOY clock + CMOS (deviceLib/Tsunami/
    // ToyRtc.h) replacing Mc146818RtcStub.  Root-caused the cold-boot PC=0
    // halt: krn$_reset_toy -> fclose on the "toy" device during tick-3
    // clock servicing dispatched through a dead pointer chain while the
    // stub answer

    ToyRtc            m_rtc;

    // 2026-06-03: PCF8584 IIC controller stub (deviceLib/Tsunami/
    // IicPcf8584Stub.h) at PCI dense memory 0xFFFF0000-0xFFFF0001 via the
    // G3-lite registerPciMemRange seam.  Empty-bus NAK semantics: register-
    // faithful controller, no slaves, every address probe completes
    // instantly with LRB=1 so iic_init fails fast instead of looping on
    // the 2000 ms lost-arbitration retry (the powerup stall).  Spec:
    // journals/IIC_Stub_Specification.txt.
    IicPcf8584    m_iic;

    // JRN-VMB-006 (2026-07-18): VGA text-console interface.  Declared after
    // m_pchip (the Pchip registries hold raw pointers into this member; it
    // constructs after the Pchip and outlives the registry entries).  Claims
    // the 0xB8000 text framebuffer + 0x3B0-0x3DF VGA register I/O in
    // wireDevices() so the firmware's graphics-console writes land instead of
    // faulting to UNHANDLED OUTER WRITE.  Absorb + optional snapshot only.
    VgaTextConsole    m_vga;

    // TIG-bus flash / NVRAM (AMD Am29F016, 2 MB).  Self-contained; decoded
    // directly in read()/write() via the kTigFlash* window helpers above.
    FlashRom          m_flash;

    // Monotonic cycle accumulator driving step()'s interval-timer poll.
    uint64_t        m_cycleAccum = 0;

    // Cached PIC output level -- evalDeviceIrqs writes the DRIR<55>
    // mirror only on change (hot-path economy; see method comment).
    bool            m_lastPicLevel = false;


};
#endif