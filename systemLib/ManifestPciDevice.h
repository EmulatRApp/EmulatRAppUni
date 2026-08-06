// ============================================================================
// ManifestPciDevice.h -- config-only PCI responder synthesized from a platform
// manifest entry (PlatformConfig::PciDeviceEntry -> synthesizePciConfig()).
//
// PURPOSE (D-PCIMODEL, PciModel::Generic / Passive):
//   The platform manifest DECLARES every PCI device the guest firmware expects
//   to enumerate.  Behavioral models (south bridge, IDE) are bound by name to
//   real handlers; devices with no behavioral model yet (e.g. the DE500 tulip,
//   an option-only card) are presented by THIS class as a faithful 256-byte
//   type-0 config header with working BAR size-probe handshake -- so the SRM's
//   PCI sizing/enumeration (PCI_SIZE_CONFIG.C) sees a real vendor/device/class
//   and sane BAR sizes instead of the all-ones "no device responded" float.
//
//   This is presence + enumeration + INERT BAR DECODE: it answers config
//   space, absorbs config writes, and (P-16, 2026-08-02) CLAIMS the mem/IO
//   windows the firmware assigns to its BARs -- reads float all-ones,
//   writes are absorbed, throttled-forensic logged.  It still does NOT
//   model device behavior; driving a stub through its BAR gets the float,
//   not a working device (that tier is a behavioral model, e.g. the real
//   21143 tulip).
//
//   Ownership: Machine owns a vector of these and registers each with the Pchip
//   at the manifest-declared (bus, slot, func).  Lifetime spans the run.
// ============================================================================
// CHANGE HISTORY
// ============================================================================
//   2026-08-02  JRN-AUD-003 P-16 (architect-approved; found live by the DS20
//               PERROR<NDS> storm at PCI 0x0100_1148).
//               FUNCTION: class shape (IIoPortHandler added), pciConfigWrite
//               (BAR path), setRangeCallbacks (new), ioRead/ioWrite (new).
//               CHANGE:  A config-visible device that does not decode the
//                        ranges its own BARs claim is itself unfaithful --
//                        real PCI devices assert DEVSEL for their windows.
//                        The stub now registers its SRM-assigned BAR ranges
//                        through the same rebind seam the behavioral models
//                        use (Machine wires the callbacks to the Pchip
//                        registries).  Claimed accesses float all-ones /
//                        absorb WITHOUT latching PERROR<NDS>; genuinely
//                        empty space still master-aborts per Batch D.
//                        Before this, every driver poke at a stub's BAR
//                        (e.g. the ew driver bit-banging the generic
//                        tulip's CSR9) latched NDS and, with the SRM's
//                        PERRMASK open, stormed the IRQ0 error handler.
// ============================================================================
#ifndef SYSTEMLIB_MANIFESTPCIDEVICE_H
#define SYSTEMLIB_MANIFESTPCIDEVICE_H

#include <array>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>
#include <utility>

#include "chipsetLib/IDeviceHandlers.h"   // IPciDeviceHandler, IIoPortHandler
#include "systemLib/PlatformConfig.h"     // PciConfigImage

namespace systemLib {

// Config-only PCI device: 256-byte type-0 header + BAR size-probe handshake
// + inert claimed BAR windows (P-16, 2026-08-02 -- see CHANGE HISTORY).
class ManifestPciDevice final : public IPciDeviceHandler, public IIoPortHandler {
public:
    // Owner callback (same shape as the behavioral models' rebind seam):
    // (un)register this stub's window at the SRM-assigned BAR base.
    using RangeFn = std::function<void(uint64_t base, uint32_t len,
                                       bool isMem, IIoPortHandler* self)>;

    ManifestPciDevice(const PciConfigImage& img, std::string name) noexcept
        : m_cfg(img.cfg), m_name(std::move(name))
    {
        for (int i = 0; i < 6; ++i) {
            m_barMask[i]  = img.barMask[i];
            m_barIsMem[i] = img.barIsMem[i];
        }
    }

    void setRangeCallbacks(RangeFn reg, RangeFn unreg) noexcept
    {
        m_register   = std::move(reg);
        m_unregister = std::move(unreg);
    }

    const std::string& name() const noexcept { return m_name; }

    // ---- IPciDeviceHandler ------------------------------------------------
    uint32_t pciConfigRead(uint8_t reg, uint8_t width) noexcept override
    {
        uint32_t v = 0;
        for (uint8_t b = 0; b < width && (reg + b) < 256u; ++b)
            v |= static_cast<uint32_t>(m_cfg[reg + b]) << (8u * b);
        return v;
    }

    void pciConfigWrite(uint8_t reg, uint32_t value, uint8_t width) noexcept override
    {
        // Base Address Registers (0x10..0x27): implement the size-probe /
        // base-program handshake the firmware relies on.  Writing 0xFFFFFFFF
        // then reading back yields ~(size-1)|typeBits (the size mask); writing a
        // real base stores it.  Type (low) bits are read-only and preserved.
        if (reg >= 0x10 && reg <= 0x27 && (reg & 0x3u) == 0) {
            const int i = (reg - 0x10) / 4;
            if (i < 6 && m_barMask[i] != 0) {
                const uint32_t typeMask = m_barIsMem[i] ? 0xFu : 0x3u;
                const uint32_t typeBits = static_cast<uint32_t>(m_cfg[reg]) & typeMask;
                const uint32_t addrMask = m_barMask[i] & ~typeMask;
                storeLE(reg, (value & addrMask) | typeBits, 4);
                // P-16 (2026-08-02): a REAL base program (not the all-ones
                // size probe) rebinds this stub's inert claim so the window
                // decodes -- float, not master-abort.  Mirrors the
                // behavioral models' programBar shape (probe writes leave
                // the claim untouched; readback still yields the size mask).
                if (value != 0xFFFFFFFFu && m_register && m_unregister) {
                    const uint32_t newBase = value & addrMask;
                    if (newBase != m_barBase[i]) {
                        const uint32_t len = barLen(i, typeMask);
                        if (m_barBase[i] != 0)
                            m_unregister(m_barBase[i], len, m_barIsMem[i], this);
                        if (newBase != 0)
                            m_register(newBase, len, m_barIsMem[i], this);
                        m_barBase[i] = newBase;
                    }
                }
                return;
            }
            // Unused BAR: reads back 0 (never claimed); ignore writes.
            return;
        }
        // Read-only identity fields (vendor/device/rev/class/subsys/int-pin)
        // are protected; a small set of standard RW registers store through.
        if (!isWritable(reg)) return;
        storeLE(reg, value, width);
    }

    // ---- IIoPortHandler: the INERT claimed window (P-16) ------------------
    // A stub has no behavior behind its BARs; a claimed access floats
    // all-ones (reads) / is absorbed (writes) exactly as the pre-Batch-D
    // model behaved -- but WITHOUT a master abort, because the device DOES
    // assert DEVSEL for its own window on real PCI.  Throttled forensic
    // per the no-silent-absorbers rule (first 4 + every 4096th, per stub).
    uint64_t ioRead(uint16_t off, uint8_t width) noexcept override
    {
        stubTouch('R', off, 0, width);
        switch (width) {
        case 1:  return 0xFFull;
        case 2:  return 0xFFFFull;
        case 4:  return 0xFFFFFFFFull;
        default: return 0xFFFFFFFFFFFFFFFFull;
        }
    }
    void ioWrite(uint16_t off, uint64_t value, uint8_t width) noexcept override
    {
        stubTouch('W', off, value, width);
    }

private:
    void stubTouch(char rw, uint16_t off, uint64_t value, uint8_t width) noexcept
    {
        const uint64_t n = m_touches++;
        if (n < 4 || (n & 0xFFFull) == 0) {
            std::fprintf(stderr,
                "ManifestPciDevice(%s): STUB-BAR %c[%llu] off=0x%04X "
                "val=0x%llX w=%u (inert claimed window -- config-only "
                "device driven through its BAR)\n",
                m_name.c_str(), rw, static_cast<unsigned long long>(n),
                unsigned(off), static_cast<unsigned long long>(value),
                unsigned(width));
            std::fflush(stderr);
        }
    }

    // BAR window length from the stored size mask: mask = ~(size-1)|type,
    // so size = ~(mask & ~typeMask) + 1 (32-bit).
    uint32_t barLen(int i, uint32_t typeMask) const noexcept
    {
        return static_cast<uint32_t>(~(m_barMask[i] & ~typeMask)) + 1u;
    }

    static bool isWritable(uint8_t reg) noexcept
    {
        // Command (0x04-0x05), cache-line/latency/BIST (0x0C-0x0F excl header),
        // and interrupt line (0x3C) are the registers PCI_SIZE_CONFIG programs.
        return reg == 0x04 || reg == 0x05 ||
               reg == 0x0C || reg == 0x0D || reg == 0x0F ||
               reg == 0x3C;
    }

    void storeLE(uint8_t reg, uint32_t v, uint8_t width) noexcept
    {
        for (uint8_t b = 0; b < width && (reg + b) < 256u; ++b)
            m_cfg[reg + b] = static_cast<uint8_t>(v >> (8u * b));
    }

    std::array<uint8_t, 256> m_cfg;
    uint32_t                 m_barMask[6]  = { 0, 0, 0, 0, 0, 0 };
    bool                     m_barIsMem[6] = { false, false, false, false, false, false };
    uint32_t                 m_barBase[6]  = { 0, 0, 0, 0, 0, 0 };   // P-16
    uint64_t                 m_touches     = 0;                      // P-16
    RangeFn                  m_register;                             // P-16
    RangeFn                  m_unregister;                           // P-16
    std::string              m_name;
};

} // namespace systemLib

#endif // SYSTEMLIB_MANIFESTPCIDEVICE_H
