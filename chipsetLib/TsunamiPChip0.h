#ifndef TSUNAMIPCHIP0_H
#define TSUNAMIPCHIP0_H
#include <cstdint>
#include <memory>

#include "TsunamiPchip.h"

class IPciBus;

class TsunamiPchip0 : public IMmioRegion {
private:
    static constexpr uint64_t kConfigSpaceBase = 0x801'FE00'0000ULL;
    static constexpr uint64_t kConfigSpaceSize = 0x0000'0100'0000ULL; // 16MB
    
    std::shared_ptr<IPciBus> bus_zero_;

public:
    explicit TsunamiPchip0(std::shared_ptr<IPciBus> root_bus) : bus_zero_(root_bus) {}

    uint32_t Read(uint64_t physical_addr, size_t size_bytes) override {
        if (physical_addr < kConfigSpaceBase || physical_addr >= (kConfigSpaceBase + kConfigSpaceSize)) {
            return 0xFFFFFFFF; // Master Abort / Out of bounds
        }

        // Calculate offset into the 16MB window
        uint64_t offset = physical_addr - kConfigSpaceBase;

        // Decode BDF + Register according to the Tsunami specification
        uint8_t  bus  = static_cast<uint8_t>((offset >> 16) & 0xFF);
        uint8_t  dev  = static_cast<uint8_t>((offset >> 11) & 0x1F);
        uint8_t  func = static_cast<uint8_t>((offset >> 8)  & 0x07);
        uint32_t reg  = static_cast<uint32_t>(offset & 0xFC); // Keep Dword alignment

        // Route exclusively if we are targeting the local Root Bus (Bus 0)
        if (bus == 0 && bus_zero_) {
            auto target_device = bus_zero_->FindDevice(dev);
            if (target_device) {
                // Pass configuration access directly to the registered device surface
                return target_device->ConfigRead(reg, size_bytes);
            }
        }
        
        return 0xFFFFFFFF; // Return target-abort or master-abort float value
    }

    void Write(uint64_t physical_addr, uint32_t value, size_t size_bytes) override {
        if (physical_addr < kConfigSpaceBase || physical_addr >= (kConfigSpaceBase + kConfigSpaceSize)) {
            return;
        }

        uint64_t offset = physical_addr - kConfigSpaceBase;
        uint8_t  bus  = static_cast<uint8_t>((offset >> 16) & 0xFF);
        uint8_t  dev  = static_cast<uint8_t>((offset >> 11) & 0x1F);
        uint32_t reg  = static_cast<uint32_t>(offset & 0xFC);

        if (bus == 0 && bus_zero_) {
            auto target_device = bus_zero_->FindDevice(dev);
            if (target_device) {
                target_device->ConfigWrite(reg, value, size_bytes);
            }
        }
    }
};
#endif // TSUNAMIPCHIP0_H
