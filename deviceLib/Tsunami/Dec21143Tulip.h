// ============================================================================
// Dec21143Tulip.h -- DECchip 21143 "tulip" PCI Fast-Ethernet controller
// (DE500-AA, `ewa`).  PHASE 1: enumeration + init-satisfy (CSR file, soft
// reset, serial-ROM/MAC, status) so the SRM's EW_DRIVER.C init COMPLETES and
// the ES40 boot advances past "Initializing ewa".  Packet TX/RX descriptor DMA
// + a real L2 (TAP) backend are PHASE 2 (deferred).  See journals/JRN-ES40-001.
//
// SPEC (source-confirmed 2026-07-20): apisrm srmconsole/EW_DRIVER.C + TU.H,
// 21140A HRM (EC-QN7NC-TE), 21143 datasheet; AXPBox src/DEC21143.cpp cross-check.
//   * PCI id 0x1011/0x0019, class 0x020000.  CSR block = 16 regs @ 8-byte
//     stride (CSR0..CSR15 = 0x00..0x78), reachable via BAR0 (I/O) or BAR1
//     (mem); the driver loads one base into pb->csr and does inl/inmeml(csr|reg).
//   * SROM = 93C46 microwire on CSR9: bit0=CS, bit1=CLK, bit2=DI, bit3=DO(read).
//     Stream after CS-assert: start bit(1) + 2-bit READ opcode(10) + 6-bit word
//     addr, then 16 data bits clocked OUT on DO, MSB first (leading zeros before
//     the start bit are ignored -- absorbs the setup clock pulse).
//     microwire_nirom reads 64 words; MAC = words 10-12 (bytes 20-25); the
//     address checksum (tu_nirom_checksum) is word 13 (bytes 26-27):
//       c=0; for the 3 MAC words w=(byte[2k]<<8)|byte[2k+1]: c=c*2+w; c%=65535.
//
// INTEGRATION (Pchip routing is STATIC ranges, no dynamic BAR consult): when
// the SRM programs BAR0/BAR1 (config write of a real base), this model invokes
// the owner-supplied callback to register its 0x80-byte CSR window at that base
// (I/O or mem) so CSR accesses route here.  Microwire timing is the fiddly bit;
// validate the MAC read against a CSR9 trace and refine if needed.
// ============================================================================
// CHANGE HISTORY
// ============================================================================
//   2026-08-02  JRN-SES-001 Batch C1 (architect-approved): interrupt_pin is
//               SILICON, not configuration.
//               FUNCTION: (class constant) kInterruptPin (new).
//               CHANGE:  Config 0x3D is hardwired 01h = INTA# on the 21143
//                        (21143 datasheet configuration-register section;
//                        single-function PCI device -> INTA# per PCI spec).
//                        The model owns the value as a constant; the loader
//                        validates any manifest declaration against it
//                        (Machine.cpp Batch C1) instead of obeying it.
// ============================================================================
#ifndef DEVICELIB_TSUNAMI_DEC21143TULIP_H
#define DEVICELIB_TSUNAMI_DEC21143TULIP_H

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <utility>

#include "chipsetLib/IDeviceHandlers.h"   // IPciDeviceHandler, IIoPortHandler

namespace deviceLib {

class Dec21143Tulip final : public IPciDeviceHandler, public IIoPortHandler {
public:
    // Interrupt Pin is SILICON (Batch C1, 2026-08-02; see CHANGE HISTORY):
    // 21143 hardwires config 0x3D to 01h = INTA# (21143 DS; PCI spec
    // single-function rule).  Loader validates the manifest against this.
    static constexpr uint8_t kInterruptPin = 0x1;   // INTA#

    // Owner callback: (un)register this model's CSR window at the SRM-assigned
    // BAR base.  isMem selects mem vs I/O space.
    using RangeFn = std::function<void(uint64_t base, uint32_t len,
                                       bool isMem, IIoPortHandler* self)>;

    explicit Dec21143Tulip(uint8_t nicIndex = 0) noexcept
    {
        initConfig();
        buildSrom(nicIndex);
        reset();
    }

    void setRangeCallbacks(RangeFn reg, RangeFn unreg) noexcept
    {
        m_register = std::move(reg);
        m_unregister = std::move(unreg);
    }

    // Guest-memory DMA access (descriptor read/write for TX/RX completion).
    using MemReadFn  = std::function<uint32_t(uint64_t)>;
    using MemWriteFn = std::function<void(uint64_t, uint32_t)>;
    void setDmaAccess(MemReadFn rd, MemWriteFn wr) noexcept
    {
        m_dmaRead  = std::move(rd);
        m_dmaWrite = std::move(wr);
    }

    // Interrupt line callback: level=true asserts INTA, false deasserts.
    using IntrFn = std::function<void(bool)>;
    void setIntrCallback(IntrFn fn) noexcept { m_intr = std::move(fn); }

    // ---- reset: CSR power-on defaults (soft reset via CSR0<0>) -----------
    void reset() noexcept
    {
        m_csr.fill(0);
        m_csr[kCSR0 >> 3] = 0xFE000000u;  // bus mode reset value
        m_csr[kCSR5 >> 3] = 0xF0000000u;  // status: TS/RS stopped, no ints
        m_csr[kCSR6 >> 3] = 0x32000040u;  // operating mode reset value
        m_csr[kCSR8 >> 3] = 0xFFFE0000u;  // missed-frame counter
        srReset(false);
    }

    // ========================================================================
    // IPciDeviceHandler -- config space
    // ========================================================================
    uint32_t pciConfigRead(uint8_t reg, uint8_t width) noexcept override
    {
        uint32_t v = 0;
        for (uint8_t b = 0; b < width && (reg + b) < 256u; ++b)
            v |= static_cast<uint32_t>(m_cfg[reg + b]) << (8u * b);
        return v;
    }

    void pciConfigWrite(uint8_t reg, uint32_t value, uint8_t width) noexcept override
    {
        if (reg == 0x10 || reg == 0x14) {                 // BAR0 (I/O) / BAR1 (mem)
            const bool     isMem    = (reg == 0x14);
            const uint32_t typeBits = isMem ? 0x00u : 0x01u;
            const uint32_t addrMask = ~(kCsrWindow - 1u);
            if (value == 0xFFFFFFFFu) {                    // size probe
                storeCfgLE(reg, (addrMask & ~0xFu) | typeBits);
                return;
            }
            storeCfgLE(reg, (value & addrMask) | typeBits);
            programBar(isMem, value & addrMask);
            return;
        }
        if (reg == 0x40) { storeCfgLE(reg, value & ~0x80000000u, width); return; } // CFDD bit31 clear
        if (!cfgWritable(reg)) return;
        storeCfgLE(reg, value, width);
    }

    // ========================================================================
    // IIoPortHandler -- CSR access (BAR0 I/O or the registered BAR1 mem window;
    // the Pchip rebases to a window-relative offset == CSR byte offset).
    // ========================================================================
    uint64_t ioRead(uint16_t off, uint8_t width) noexcept override
    { return csrRead(static_cast<uint8_t>(off & 0x78u), width); }
    void ioWrite(uint16_t off, uint64_t value, uint8_t width) noexcept override
    { csrWrite(static_cast<uint8_t>(off & 0x78u), static_cast<uint32_t>(value), width); }

    // Direct CSR access (unit-testable).
    uint32_t csrRead(uint8_t off, uint8_t /*width*/) noexcept
    {
        const uint32_t v = (off == kCSR9) ? sromRead() : m_csr[off >> 3];
        trace('R', off, v);
        return v;
    }
    void csrWrite(uint8_t off, uint32_t value, uint8_t /*width*/) noexcept
    {
        trace('W', off, value);
        switch (off) {
        case kCSR0: m_csr[off >> 3] = value; if (value & 0x1u) reset(); return; // SWR
        case kCSR9: sromWrite(value); return;
        case kCSR5: m_csr[off >> 3] &= ~(value & 0x0001FFFFu);                  // W1C
                    updateInterrupt(); return;                                  // clearing may deassert
        case kCSR7: m_csr[off >> 3] = value; updateInterrupt(); return;         // interrupt mask
        // ---- TX setup-frame completion (unblocks ewa init) ----
        // The ew driver builds a MAC-filter SETUP FRAME descriptor (TDES0<OWN>=
        // 0x80000000), starts TX (CSR6<ST>=0x2000), kicks the poll demand (CSR1),
        // then WAITS (tu_out) for the descriptor's OWN bit to clear.  Complete it:
        // walk the TX ring (base = CSR4), clear OWN on any device-owned descriptor,
        // and post CSR5<TI|NIS>.  Minimal Phase-1 TX completion (no real frame
        // transmit / RX yet -- that's Phase 2 + the TAP backend).
        case kCSR1:                                                            // TX poll demand
            m_csr[off >> 3] = value;
            completeTxRing();
            return;
        case kCSR6:                                                            // operating mode
            m_csr[off >> 3] = value;
            if (value & 0x00002000u) completeTxRing();                        // ST: start transmit
            return;
        default:    m_csr[off >> 3] = value; return;
        }
    }

private:
    static constexpr uint8_t  kCSR0 = 0x00, kCSR1 = 0x08, kCSR4 = 0x20,
                              kCSR5 = 0x28, kCSR6 = 0x30, kCSR7 = 0x38,
                              kCSR8 = 0x40, kCSR9 = 0x48;

    // INTA state: asserted while an enabled+pending interrupt sits in CSR5.
    // (CSR5<TI|RI|...> & CSR7 mask, incl NIS/AIS summary bits.)  Recomputed
    // after any CSR5/CSR7 change; drives raise/lowerPciInterrupt via m_intr.
    void updateInterrupt() noexcept
    {
        bool const pending =
            (m_csr[kCSR5 >> 3] & m_csr[kCSR7 >> 3] & 0x0001FFFFu) != 0;
        if (pending == m_intrAsserted) return;
        m_intrAsserted = pending;
        intrTrace(pending, m_csr[kCSR5 >> 3], m_csr[kCSR7 >> 3]);
        if (m_intr) m_intr(pending);
    }
    static void intrTrace(bool lvl, uint32_t csr5, uint32_t csr7) noexcept
    {
        static bool const on = (std::getenv("EMULATR_TULIP_TRACE") != nullptr);
        if (!on) return;
        static int n = 0;
        if (n++ < 40) {
            std::fprintf(stderr, "TULIP-INTR %s CSR5=0x%08x CSR7=0x%08x\n",
                         lvl ? "ASSERT" : "deassert", csr5, csr7);
            std::fflush(stderr);
        }
    }

    // TX setup-frame completion: walk the TX ring (base=CSR4), clear each
    // device-owned (TDES0<OWN>=0x80000000) descriptor so tu_out's OWN-poll
    // completes, and post CSR5<TI|NIS>.  Bounded ring walk; honors TER/TCH.
    void completeTxRing() noexcept
    {
        m_csr[kCSR5 >> 3] |= 0x00000001u | 0x00010000u;   // TI | NIS
        if (m_dmaRead && m_dmaWrite) {
            uint64_t const base = m_csr[kCSR4 >> 3];
            uint64_t desc = base;
            for (int i = 0; i < 32 && desc != 0; ++i) {
                uint32_t const tdes0 = m_dmaRead(desc);
                txTrace(desc, tdes0);
                if (!(tdes0 & 0x80000000u)) break;             // host-owned -> done
                m_dmaWrite(desc, tdes0 & ~0x80000000u);        // clear OWN = TX complete
                uint32_t const tdes1 = m_dmaRead(desc + 4);
                if      (tdes1 & 0x02000000u) desc = base;                 // TER: wrap
                else if (tdes1 & 0x01000000u) desc = m_dmaRead(desc + 12); // TCH: next ptr
                else                          desc += 16;                  // contiguous
            }
        }
        updateInterrupt();                                 // assert INTA if CSR7 enables TI/NIS
    }
    static void txTrace(uint64_t desc, uint32_t tdes0) noexcept
    {
        static bool const on = (std::getenv("EMULATR_TULIP_TRACE") != nullptr);
        if (!on) return;
        static int n = 0;
        if (n++ < 40) {
            std::fprintf(stderr, "TULIP-TX desc=0x%011llx TDES0=0x%08x\n",
                         static_cast<unsigned long long>(desc), tdes0);
            std::fflush(stderr);
        }
    }
    static constexpr uint32_t kCsrWindow = 0x80;

    // ====================================================================
    // 93C46 serial-ROM (microwire) state machine on CSR9.
    // ====================================================================
    enum class Sr { Start, CmdAddr, Data };

    void srReset(bool cs) noexcept
    {
        m_srState = Sr::Start; m_srCs = cs; m_srClk = false;
        m_srBits = 0; m_srShift = 0; m_srWord = 0; m_srDataPos = 0; m_srDO = false;
    }

    void sromWrite(uint32_t v) noexcept
    {
        m_csr[kCSR9 >> 3] = v;
        const bool cs  = (v & 0x1u) != 0;
        const bool clk = (v & 0x2u) != 0;
        const bool di  = (v & 0x4u) != 0;

        if (!cs) { srReset(false); return; }          // deselect -> reset shifter
        const bool rising = clk && !m_srClk;
        m_srCs = true; m_srClk = clk;
        if (!rising) return;

        switch (m_srState) {
        case Sr::Start:                               // ignore leading 0s until start=1
            if (di) { m_srState = Sr::CmdAddr; m_srBits = 0; m_srShift = 0; }
            return;
        case Sr::CmdAddr:                             // 2-bit opcode + 6-bit addr
            m_srShift = (m_srShift << 1) | (di ? 1u : 0u);
            if (++m_srBits == 8) {
                const uint32_t opcode = (m_srShift >> 6) & 0x3u;   // 10b = READ
                const uint8_t  addr   = static_cast<uint8_t>(m_srShift & 0x3Fu);
                m_srWord    = (opcode == 0x2u) ? sromWord(addr) : 0xFFFFu;
                m_srAddrSeq = addr;
                m_srDataPos = 0;                        // first Data clock presents bit 15
                m_srState   = Sr::Data;
                sromTrace(addr, m_srWord);             // loop-vs-progress: word addr sequence
            }
            return;
        case Sr::Data:                                // 16 data bits, MSB first
            m_srDO = (m_srWord >> (15 - m_srDataPos)) & 1u;   // present for imminent read
            if (++m_srDataPos >= 16) {                          // sequential-read next word
                m_srWord = sromWord(static_cast<uint8_t>(++m_srAddrSeq));
                m_srDataPos = 0;
            }
            return;
        }
    }

    uint32_t sromRead() noexcept
    {
        uint32_t v = m_csr[kCSR9 >> 3] & ~0x8u;        // clear DO
        if (m_srCs && m_srState == Sr::Data && m_srDO) v |= 0x8u;
        return v;
    }

    uint16_t sromWord(uint8_t addr) const noexcept
    {
        const int a = (addr & 0x3F) * 2;
        return static_cast<uint16_t>(m_srom[a] | (m_srom[a + 1] << 8));
    }

    // ---- SROM image: DEC format, MAC @20-25, addr-checksum @26-27 --------
    void buildSrom(uint8_t nicIndex) noexcept
    {
        m_srom.fill(0);
        const uint8_t mac[6] = { 0x08, 0x00, 0x2B,        // Digital OUI
                                 0xE5, 0x40, static_cast<uint8_t>(nicIndex) };
        for (int k = 0; k < 6; ++k) m_srom[20 + k] = mac[k];

        uint32_t c = 0;                                    // tu_nirom_checksum
        for (int k = 0; k < 6; k += 2) {
            c += c;
            c += static_cast<uint32_t>((mac[k] << 8) | mac[k + 1]);
        }
        while (c >= 65535u) c -= 65535u;
        m_srom[26] = static_cast<uint8_t>((c >> 8) & 0xFF);
        m_srom[27] = static_cast<uint8_t>(c & 0xFF);

        m_srom[18] = 0x03;    // SROM format version
        m_srom[19] = 0x01;    // chip count

        // Board name at bytes 29-36: EW_DRIVER.C microwire_nirom (1663 variant)
        // reads lni[29..36] and strcmp's it to "DE500-AA/BA/FA/XA" to set
        // controller_type.  "DE500-BA" == the DECchip 21143 board this models;
        // without it controller_type stays unset and media init can't proceed.
        static const char kName[] = "DE500-BA";
        for (int k = 0; k < 8; ++k) m_srom[29 + k] = static_cast<uint8_t>(kName[k]);
        // byte 37 left 0 -> null-terminates the driver's 8-char name compare.
    }

    // ---- config helpers --------------------------------------------------
    void initConfig() noexcept
    {
        m_cfg.fill(0);
        storeCfgLE(0x00, 0x00191011u);  // vendor 0x1011 / device 0x0019
        storeCfgLE(0x08, 0x02000041u);  // rev 0x41 / class 0x020000 (Ethernet)
        m_cfg[0x10] = 0x01;             // BAR0 = I/O; BAR1 (0x14) = mem (bit0=0)
        m_cfg[0x3D] = kInterruptPin;    // interrupt pin INTA# -- silicon
                                        // constant (Batch C1, 21143 DS)
    }
    static bool cfgWritable(uint8_t reg) noexcept
    {
        return reg == 0x04 || reg == 0x05 || reg == 0x0C ||
               reg == 0x0D || reg == 0x0F || reg == 0x3C;
    }
    static void trace(char rw, uint8_t off, uint32_t val) noexcept
    {
        static bool const on = (std::getenv("EMULATR_TULIP_TRACE") != nullptr);
        if (!on) return;
        static int n = 0;
        if (n++ < 4000) {
            std::fprintf(stderr, "TULIP %c CSR%-2d off=0x%02x val=0x%08x\n",
                         rw, off >> 3, off, val);
            std::fflush(stderr);
        }
    }
    // SROM word-read trace: the addr sequence reveals loop (0..19,0..19,...) vs
    // one-pass progress (0..19).  Separately capped so it survives the CSR spam.
    static void sromTrace(uint8_t addr, uint16_t word) noexcept
    {
        static bool const on = (std::getenv("EMULATR_TULIP_TRACE") != nullptr);
        if (!on) return;
        static int n = 0;
        if (n++ < 400) {
            std::fprintf(stderr, "TULIP-SROM word[%2u] = 0x%04x\n", addr, word);
            std::fflush(stderr);
        }
    }
    void storeCfgLE(uint8_t reg, uint32_t v, uint8_t width = 4) noexcept
    {
        for (uint8_t b = 0; b < width && (reg + b) < 256u; ++b)
            m_cfg[reg + b] = static_cast<uint8_t>(v >> (8u * b));
    }
    void programBar(bool isMem, uint64_t base) noexcept
    {
        uint64_t& cur = isMem ? m_memBase : m_ioBase;
        if (base == cur) return;
        if (cur != 0 && m_unregister) m_unregister(cur, kCsrWindow, isMem, this);
        cur = base;
        if (base != 0 && m_register) m_register(base, kCsrWindow, isMem, this);
    }

    // ---- state -----------------------------------------------------------
    std::array<uint8_t, 256>  m_cfg{};
    std::array<uint32_t, 16>  m_csr{};
    std::array<uint8_t, 128>  m_srom{};

    Sr       m_srState = Sr::Start;
    bool     m_srCs = false, m_srClk = false, m_srDO = false;
    int      m_srBits = 0, m_srDataPos = 0;
    uint32_t m_srShift = 0;
    uint16_t m_srWord = 0;
    uint8_t  m_srAddrSeq = 0;

    uint64_t m_ioBase = 0, m_memBase = 0;
    RangeFn  m_register, m_unregister;
    MemReadFn  m_dmaRead;
    MemWriteFn m_dmaWrite;
    IntrFn     m_intr;
    bool       m_intrAsserted = false;
};

} // namespace deviceLib

#endif // DEVICELIB_TSUNAMI_DEC21143TULIP_H
