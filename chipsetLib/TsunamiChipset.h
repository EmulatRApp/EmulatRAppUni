#ifndef TSUNAMI_CHIPSET_H
#define TSUNAMI_CHIPSET_H

#include <atomic>
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
#include "Cypress_CY82C693ISABridge.h"
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
#include "deviceLib/Tsunami/Smc37c669SuperIo.h" // FDC37C669 SuperIO: config port + FDC (#22)
#include "deviceLib/scsi/VirtualIsoDevice.h"


/* considerations
 *
 * TSUNAMI controller interface constraint applies to **main memory (DRAM)**. 
 * I/O space is completely separate and is not affected by it. 
 **  RAM is constrained to 4GB on Tsunami:**
 *
 **  The Cchip AAR registers encode the physical base address of each DRAM 
 **  array in bits '[34:24]' - that is a 35-bit PA field. The ASIZ encoding caps 
 **  at '0x7 = 1GB' per array on Tsunami. Four arrays x 1GB = 4GB maximum DRAM. 
 **  The Cchip simply cannot tell the memory controller about DRAM above 4GB. 
 **  It is a silicon limit of the 21272.
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

**The real problem with 32GB on Tsunami:**
* The configured 32GB exists in 'GuestMemory' but SRM will only discover 4GB 
* of it through the AAR registers. The remaining 28GB is invisible to the guest 
* - SRM builds its memory descriptor table from the AAR values, so the HWRPB 
* memory map will show only 4GB. The OS inherits that map and never 
* allocates from the missing 28GB. It is not a crash - it is silent capacity loss.

**The correct solution:**

ES40  + Tsunami (21272)  ->  4GB RAM maximum   (ASIZ max 0x7 = 1GB x 4 arrays)
ES45  + Typhoon (21274)  ->  32GB RAM maximum  (ASIZ extends to 0xA = 8GB x 4 arrays)

For 32GB,  switch the variant to 'ChipsetVariant::Typhoon' in the config, which changes the model string to 'ES45' 
and enables the extended ASIZ encodings in 'computeAAR'. The I/O stack, Pchip, and MMIO handlers are 
identical between the two variants - only the Cchip AAR encoding and the 'MISC.REV' field differ.

If you want to stay with ES40/Tsunami, the honest RAM limit is 4GB. 
Anything above that in the config is silently discarded by the AAR register initialization.
 
 *
 */

class TsunamiChipset : public memoryLib::ISystemBus
{
public:
    explicit TsunamiChipset(const std::string& model,
        int cpuCount = 4,
        uint64_t memSizeBytes = 0x800000000ULL) noexcept
        : m_variant(normalizeVariant(variantFromModel(model)))
        , m_model(model)
        , m_guestMemory(memSizeBytes)
        , m_cchip(m_variant, cpuCount, memSizeBytes)
        , m_dchip(m_variant, memSizeBytes)
        , m_pchip(m_variant)
    {
        assertVariantConsistency();
        wireDevices();
    }

    explicit TsunamiChipset(ChipsetVariant variant,
        int cpuCount = 4,
        uint64_t memSizeBytes = 0x800000000ULL) noexcept;

    void reset() noexcept {
        m_cchip.reset();
        m_dchip.reset();
        m_pchip.reset();
        m_tig.reset();
    }

    // Inject an already-open block medium built by the media_kind factory.
    // setDiskMedia -> an ATA fixed-disk unit (channel,unit, e.g. dqa0 master);
    // setCdMedia   -> the ATAPI CD (dqa1, primary slave).  The drives hold the
    // IBlockMedia and no longer open files themselves.  2026-06-12 (seam).
    bool setDiskMedia(int channel, int unit,
                      std::unique_ptr<scsi::IBlockMedia> media) noexcept {
        return m_ide.attachMedia(channel, unit, std::move(media));
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

        bool const level = m_pic.outputAsserted();
        if (level != m_lastPicLevel) {
            m_lastPicLevel = level;
            if (level) m_cchip.assertInterrupt(kIsaBridgeDrirBit);
            else       m_cchip.deassertInterrupt(kIsaBridgeDrirBit);
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
    TsunamiDchip& dchip()       noexcept { return m_dchip; }
    const TsunamiDchip& dchip() const noexcept { return m_dchip; }
    TsunamiPchip& pchip()       noexcept { return m_pchip; }
    const TsunamiPchip& pchip() const noexcept { return m_pchip; }
    TsunamiTig& tig()           noexcept { return m_tig; }
    const TsunamiTig& tig() const noexcept { return m_tig; }

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

    void wireDevices() noexcept {
        // 1. Register the Cypress bridge in the PCI device map
        m_pchip.registerPciDevice(0, 5, 0, &m_cypress); // CY82C693 ISA bridge

        // 2. Wire the I/O port handler interface
        m_pchip.setIoPortHandler(&m_cypress);

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
        m_ide.attachDevice(0, 1, &m_cdrom);                 // primary slave = ATAPI CD (dqa1)
        m_pchip.registerPciDevice(0, 5, 1, &m_ide);          // func 1 config space
        m_pchip.registerIoPortRange(0x1F0, 0x1F8, &m_ide);   // primary command block
        m_pchip.registerIoPortRange(0x170, 0x178, &m_ide);   // secondary command block
        m_pchip.registerIoPortRange(0x3F6, 0x3F7, &m_ide);   // primary alt-status/control
        m_pchip.registerIoPortRange(0x376, 0x377, &m_ide);   // secondary alt-status/control

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

        // 3. PCI dense-memory claimants (G3-lite seam, 2026-06-03).
        // PCF8584 IIC controller at the DS10's fixed platform address
        // (proven from the shipped image's iic_write_csr: base 0xFFFF0000).
        // S0-area at +0, S1 control/status at +1.  Empty-bus NAK semantics
        // bound the iic_init probe loop that stalled powerup; see
        // journals/IIC_PCF8584_Specification.txt.  The driver's dummy
        // chip-select reads of ISA port 0x80 stay with the Cypress
        // fallback by design.
        m_pchip.registerPciMemRange(0xFFFF0000ULL, 0xFFFF0002ULL, &m_iic);
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

    // (Bus arbiter surface is now the public ISystemBus override above.)



    // Ticket 6: chipset-owned ISA bridge + serial devices, registered with
    // Pchip0 in wireDevices().  Declared after m_pchip so it constructs
    // first (the Pchip registries hold raw pointers into these members).
    Cy82C693IsaBridge m_cypress;
    Uart16550         m_com1{ nullptr, 0x3F8, "COM1" };
    Uart16550         m_com2{ nullptr, 0x2F8, "COM2" };

    // CY82C693 IDE (PCI Function 1) + a no-media ATAPI CD on primary master.
   // After m_pchip (registries hold raw pointers); m_cdrom before m_ide so the
   // CD outlives the controller that points at it.
    scsi::VirtualIsoDevice m_cdrom;          // no-media ATAPI CD
    Cy82C693Ide            m_ide;            // CY82C693 IDE controller (func 1)
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