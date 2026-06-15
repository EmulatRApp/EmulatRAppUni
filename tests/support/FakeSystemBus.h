// ============================================================================
// tests/support/FakeSystemBus.h -- minimal ISystemBus for CPU-side unit tests
// ============================================================================
// Project: EmulatR -- Alpha AXP / EV6 Architecture Emulator (V4)
// Copyright (C) 2025, 2026 eNVy Systems, Inc.  All rights reserved.
// Licensed under eNVy Systems Non-Commercial License v1.1
// ============================================================================
//
// FakeSystemBus is a DRAM-only ISystemBus that services every PA from a backing
// GuestMemory.  It lets CPU-side unit tests (MemDrainer, PipelineDriver) inject
// a bus without constructing the chipset.  MMIO / NXM routing is NOT modelled
// here -- that is covered by tests/chipsetLib/test_systembus_arbiter.cpp.
// ============================================================================

#ifndef TESTS_SUPPORT_FAKESYSTEMBUS_H
#define TESTS_SUPPORT_FAKESYSTEMBUS_H

#include <cstdint>

#include "memoryLib/GuestMemory.h"
#include "memoryLib/ISystemBus.h"

struct FakeSystemBus final : memoryLib::ISystemBus {
    memoryLib::GuestMemory& mem;
    explicit FakeSystemBus(memoryLib::GuestMemory& m) noexcept : mem(m) {}

    memoryLib::BusResult read(uint64_t pa, uint8_t width) noexcept override {
        if (pa + width > mem.sizeBytes()) return { memoryLib::BusStatus::BusError, 0 };
        uint64_t out = 0;
        switch (width) {
        case 1: { uint8_t  v = 0; mem.read1(pa, v); out = v; break; }
        case 2: { uint16_t v = 0; mem.read2(pa, v); out = v; break; }
        case 4: { uint32_t v = 0; mem.read4(pa, v); out = v; break; }
        case 8: {                 mem.read8(pa, out);       break; }
        default: return { memoryLib::BusStatus::BusError, 0 };
        }
        return { memoryLib::BusStatus::Ok, out };
    }
    memoryLib::BusResult write(uint64_t pa, uint64_t value, uint8_t width) noexcept override {
        if (pa + width > mem.sizeBytes()) return { memoryLib::BusStatus::BusError, 0 };
        switch (width) {
        case 1: mem.write1(pa, static_cast<uint8_t> (value)); break;
        case 2: mem.write2(pa, static_cast<uint16_t>(value)); break;
        case 4: mem.write4(pa, static_cast<uint32_t>(value)); break;
        case 8: mem.write8(pa,                       value);  break;
        default: return { memoryLib::BusStatus::BusError, 0 };
        }
        return { memoryLib::BusStatus::Ok, 0 };
    }
    memoryLib::BusResult fetch(uint64_t pa, uint8_t width) noexcept override { return read(pa, width); }
};

#endif // TESTS_SUPPORT_FAKESYSTEMBUS_H
