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
//   This is presence + enumeration ONLY: it answers config space and absorbs
//   config writes.  It does NOT model the device's memory/IO BAR behavior --
//   a device the firmware then DRIVES through its BAR still needs a behavioral
//   model (that's the next tier, e.g. a real 21143 tulip).  See
//   journals + the PlatformConfig D-PCIMODEL notes.
//
//   Ownership: Machine owns a vector of these and registers each with the Pchip
//   at the manifest-declared (bus, slot, func).  Lifetime spans the run.
// ============================================================================
#ifndef SYSTEMLIB_MANIFESTPCIDEVICE_H
#define SYSTEMLIB_MANIFESTPCIDEVICE_H

#include <array>
#include <cstdint>
#include <string>
#include <utility>

#include "chipsetLib/IDeviceHandlers.h"   // IPciDeviceHandler
#include "systemLib/PlatformConfig.h"     // PciConfigImage

namespace systemLib {

// Config-only PCI device: 256-byte type-0 header + BAR size-probe handshake.
class ManifestPciDevice final : public IPciDeviceHandler {
public:
    ManifestPciDevice(const PciConfigImage& img, std::string name) noexcept
        : m_cfg(img.cfg), m_name(std::move(name))
    {
        for (int i = 0; i < 6; ++i) {
            m_barMask[i]  = img.barMask[i];
            m_barIsMem[i] = img.barIsMem[i];
        }
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

private:
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
    std::string              m_name;
};

} // namespace systemLib

#endif // SYSTEMLIB_MANIFESTPCIDEVICE_H
