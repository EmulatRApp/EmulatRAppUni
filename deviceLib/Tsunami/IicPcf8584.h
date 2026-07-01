// ============================================================================
// deviceLib/Tsunami/IicPcf8584.h -- Philips PCF8584 I2C controller model
// ============================================================================
// Project: EmulatR -- Alpha AXP / EV6 Architecture Emulator (V4)
// Copyright (C) 2025, 2026 eNVy Systems, Inc.  All rights reserved.
// Licensed under eNVy Systems Non-Commercial License v1.1
//
// Project Architect: Timothy Peer
// AI Collaboration:  Claude (Anthropic)
//
// Commercial use prohibited without separate license.
// Contact:        peert@envysys.com  |  https://envysys.com
// ============================================================================
//
// PCF8584 model per journals/IIC_PCF8584_Specification.txt.
//
// v1 (2026-06-03): register-faithful controller, EMPTY bus -- every address
//   probe NAKs instantly (LRB=1, never LAB) so iic_init fails fast.
// v2 (2026-06-03, spec sec. 5A): SLAVE EEPROM emulation for the DS10 NVRAM /
//   FRU banks (256-byte parts), pipelined-receiver read contract (P1-P5).
// v3 (2026-06-06): seeded FRU JEDEC bank gated by an enableFruBank ctor flag.
// v4 (2026-06-07): MANIFEST-DRIVEN device table (P3 of the device-enumeration
//   scaffold; see journals/Device_Enumeration_Scaffold_Spec_20260607.md).  The
//   hardcoded FRU/RCM banks + seedFruBank/seedJedecImage are RETIRED.  The bus
//   is now an empty default; Machine (systemLib) loads the platform manifest,
//   synthesizes each device's on-wire image via PlatformConfig, and pushes a
//   neutral IicDevice list down through chipset().iic().configureDevices().
//   This makes board population declarative and adds the IIC_LED_TYPE status
//   registers (0x70/0x72) that build_power_hw reads -- the missing read that
//   truncated gct_init$pc264_hw before build_fru_root (the set sys_serial_num
//   hang).  Device IDENTITY comes from the manifest (re-applied every cold
//   boot); mutable CONTENT (set sys_serial_num / NVRAM writes) is snapshotted.
//
// Placement: Pchip0 PCI dense MEMORY space, rebased offset 0x0 = S0-area,
//   offset 0x1 = S1 (control write / status read).  Base 0xFFFF0000 proven
//   from the shipped image (iic_write_csr @0x81a68 ldah -1).
//
// Authority: Processor Support\Philips PCF8584-I2C Controller.txt sec 6.8/6.10.
// Driver contract (apisrm iic_driver.c iic_service, WEBBRICK; spec sec. 5A):
//   P1 after an ACK'd byte, S1 reads 0x00 exactly.  P2 first S0 read of a read
//   stream is a DUMMY.  P3 master NACK mid-read -> next S1 = 0x08 (LRB).  P4
//   PIN-writes mid-transaction are ACK control, not reset.  P5 address NAK =
//   status 0x08 (never LAB, the unbounded retry).
//
// Determinism: no host coupling, no cycle source; transactions complete
// instantly (spec sec. 5 fidelity note).
// ============================================================================

#ifndef DEVICELIB_TSUNAMI_IIC_PCF8584_H
#define DEVICELIB_TSUNAMI_IIC_PCF8584_H

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>                        // std::getenv (EMULATR_IIC_TRACE gate)
#include <cstring>                        // std::memcpy (content image serialize)
#include <vector>

#include "chipsetLib/IDeviceHandlers.h"   // IIoPortHandler seam

#if defined(EMULATR_DIAGNOSTIC_LOGGING)
#  include "traceLib/DecListingSink.h"     // setTraceWindowCountdown (IIC-armed retire window)
#endif

class IicPcf8584 : public IIoPortHandler
{
public:
    // ------------------------------------------------------------------------
    // Register-select offsets (rebased by the PciMemRange seam).
    // ------------------------------------------------------------------------
    static constexpr uint16_t kOffData   = 0x00;  // S0-area (A0 = 0)
    static constexpr uint16_t kOffCtrlSt = 0x01;  // S1      (A0 = 1)

    // S1 control bits (write side; datasheet sec 6.8 Table 4).
    static constexpr uint8_t kCtl_PIN = 0x80;     // status reset / ack handshake
    static constexpr uint8_t kCtl_ES0 = 0x40;     // serial interface enable
    static constexpr uint8_t kCtl_ES1 = 0x20;     // register select (S2 clock)
    static constexpr uint8_t kCtl_ES2 = 0x10;     // register select (S3 vector)
    static constexpr uint8_t kCtl_ENI = 0x08;     // interrupt enable (stored)
    static constexpr uint8_t kCtl_STA = 0x04;     // START + address
    static constexpr uint8_t kCtl_STO = 0x02;     // STOP
    static constexpr uint8_t kCtl_ACK = 0x01;     // auto-acknowledge

    // S1 status bits (read side).
    static constexpr uint8_t kSt_PIN = 0x80;      // 1 = idle, 0 = complete
    static constexpr uint8_t kSt_STS = 0x20;      // never set
    static constexpr uint8_t kSt_BER = 0x10;      // never set
    static constexpr uint8_t kSt_LRB = 0x08;      // 1 = no acknowledge
    static constexpr uint8_t kSt_AAS = 0x04;      // never set
    static constexpr uint8_t kSt_LAB = 0x02;      // never set (unbounded retry)
    static constexpr uint8_t kSt_BB  = 0x01;      // 1 = bus FREE (6.8.2.7)

    static constexpr uint32_t kImageSize = 256;   // bytes per device image

    // ------------------------------------------------------------------------
    // Manifest-driven device model (v4).  A device is present on the bus iff it
    // is in m_devices; absent addresses NAK ("module absent").  The kind only
    // affects content semantics; the transaction machine is uniform.
    // ------------------------------------------------------------------------
    enum class Kind : uint8_t { FruEeprom, Nvram, Status };

    struct IicDevice {
        uint8_t                          address = 0;   // even node address
        Kind                             kind    = Kind::Status;
        std::array<uint8_t, kImageSize>  image{};       // on-wire content
        uint8_t                          offset  = 0;   // internal addr pointer
    };

    // Empty bus by default (every probe NAKs); Machine populates it via
    // configureDevices() from the platform manifest.
    IicPcf8584() noexcept { reset(); }

    // Replace the bus population.  Content (image bytes) is taken as given
    // (already synthesized by PlatformConfig); per-device offsets reset.
    void configureDevices(const std::vector<IicDevice>& devs) noexcept
    {
        m_devices = devs;
        for (auto& d : m_devices) d.offset = 0;
        reset();
    }

    // Controller reset (datasheet sec 6.10).  Device content survives (the
    // EEPROMs/NVRAMs are separate chips on the bus); only the controller's
    // transaction state returns to idle.
    void reset() noexcept
    {
        m_status     = static_cast<uint8_t>(kSt_PIN | kSt_BB);  // idle, free
        m_control    = 0;
        m_ownAddr    = 0;                         // S0' = 0x00
        m_clock      = 0;                         // S2
        m_vector     = 0;                         // S3 = 0x00
        m_shift      = 0;                         // S0 staging byte
        m_phase      = Phase::kIdle;
        m_curDev     = -1;
        m_firstWrite = false;
        m_dummyDone  = false;
    }

    // Test / inspection accessor: 256-byte image for a node, or nullptr if the
    // node is not present on the configured bus.
    [[nodiscard]] uint8_t* deviceImage(uint8_t node) noexcept
    {
        int const idx = deviceLookup(static_cast<uint8_t>(node & 0xFEu));
        return (idx >= 0) ? m_devices[static_cast<size_t>(idx)].image.data()
                          : nullptr;
    }

    // ------------------------------------------------------------------------
    // IIoPortHandler -- byte-only device (wide reads float, wide writes drop).
    // ------------------------------------------------------------------------
    uint64_t ioRead(uint16_t port, uint8_t width) override
    {
        if (width != 1) {
            noteNonByteAccess();
            return 0xFFULL;                       // float, ToyRtc precedent
        }
        switch (port) {
        case kOffCtrlSt:                          // S1 status (read-only)
            return m_status;

        case kOffData:                            // S0-area
            if (m_phase == Phase::kReadData) {
                // P2: pipelined receiver -- first read is the dummy the
                // driver overwrites; subsequent reads stream device bytes.
                if (!m_dummyDone) {
                    m_dummyDone = true;
                    return 0xFFULL;
                }
                IicDevice& d = m_devices[static_cast<size_t>(m_curDev)];
                uint8_t const v = d.image[d.offset];
                if (iicTraceEnabled()) {
                    std::fprintf(stderr,
                        "IIC-RD  addr=0x%02x off=0x%02x v=0x%02x\n",
                        static_cast<unsigned>(d.address),
                        static_cast<unsigned>(d.offset),
                        static_cast<unsigned>(v));
                }
                d.offset = static_cast<uint8_t>(d.offset + 1u);   // mod-256 wrap
                return v;
            }
            if (m_phase != Phase::kIdle) {
                return 0xFFULL;                   // NAK'd / write phase float
            }
            switch (regSelect()) {                // Idle: register file (v1)
            case Sel::kS0:    return m_shift;
            case Sel::kS0Own: return m_ownAddr;
            case Sel::kS2:    return m_clock;
            case Sel::kS3:    return m_vector;
            }
            return 0xFFULL;                       // unreachable

        default:
            return 0xFFULL;                       // out-of-claim float
        }
    }

    void ioWrite(uint16_t port, uint64_t value, uint8_t width) override
    {
        if (width != 1) {
            noteNonByteAccess();
            return;                               // drop wide writes
        }
        uint8_t const v = static_cast<uint8_t>(value & 0xFFu);

        switch (port) {
        case kOffCtrlSt:                          // S1 control (write)
            m_control = v;
#if defined(EMULATR_DIAGNOSTIC_LOGGING)
            // DS20 badge diag (2026-06-29): observe every IIC control write so
            // we can tell whether iic_init's verify of node 0x40 ever enters
            // interrupt mode (ENI=0x08 -> value 0xcd) vs the polled PAL path
            // (0xc5).  Throttled; gated by EMULATR_IIC_CTRL_TRACE.
            if (std::getenv("EMULATR_IIC_CTRL_TRACE")) {
                // Log the first 40 writes AND, separately, the first 40 writes
                // that set ENI (0x08) -- so an interrupt-mode verify (0xcd) is
                // never hidden behind the polled bus-scan's 0xc5/0xc3 volume.
                static unsigned s_n = 0, s_eni = 0;
                bool const eni = (v & kCtl_ENI) != 0;
                if (s_n++ < 40 || (eni && s_eni++ < 40)) {
                    std::fprintf(stderr,
                        "IIC-CTRL wr=0x%02x ENI=%d node=0x%02x phase=%d\n",
                        static_cast<unsigned>(v),
                        eni ? 1 : 0,
                        static_cast<unsigned>(m_curDev >= 0
                            ? m_devices[static_cast<size_t>(m_curDev)].address : 0),
                        static_cast<int>(m_phase));
                    std::fflush(stderr);
                }
            }
#endif
            if (v & kCtl_STA) {
                startTransaction(v);              // (repeated) START + address
            } else if (v & kCtl_STO) {
                // STOP: release the bus, back to idle.
                m_status = static_cast<uint8_t>(kSt_PIN | kSt_BB);
                m_phase  = Phase::kIdle;
                m_curDev = -1;
            } else if (v & kCtl_PIN) {
                if (m_phase == Phase::kIdle) {
                    // Software reset of the status flags (sec 6.8.1.1).
                    m_status = static_cast<uint8_t>(kSt_PIN | kSt_BB);
                } else {
                    // P4: mid-transaction PIN-write is ACK control, NOT a
                    // reset.  IIC_NACK (ACK clear) during a read makes the
                    // next status read show LRB=1 (P3) so the driver's
                    // count-complete test (0x08) passes and it STOPs.
                    if (!(v & kCtl_ACK) && m_phase == Phase::kReadData) {
                        m_status = kSt_LRB;
                    }
                }
            }
            break;

        case kOffData:                            // S0-area
            if (m_phase == Phase::kWriteData) {
                // Write phase: byte #1 = internal offset (1-byte addressing,
                // 256-byte parts); subsequent bytes store -- except Status
                // registers, which are read-mostly and absorb data writes.
                IicDevice& d = m_devices[static_cast<size_t>(m_curDev)];
                if (m_firstWrite) {
                    d.offset     = v;
                    m_firstWrite = false;
                } else if (d.kind != Kind::Status) {
                    d.image[d.offset] = v;
                    d.offset = static_cast<uint8_t>(d.offset + 1u);
                }
                m_status = 0x00;                  // P1: byte ACK'd
                break;
            }
            if (m_phase != Phase::kIdle) {
                break;                            // NAK'd phase: absorb
            }
            switch (regSelect()) {                // Idle: register file (v1)
            case Sel::kS0:    m_shift   = v; break;   // address staging
            case Sel::kS0Own: m_ownAddr = v; break;   // S0' (e.g. 0x5B)
            case Sel::kS2:    m_clock   = v; break;   // S2 (e.g. 0x15)
            case Sel::kS3:    m_vector  = v; break;   // S3
            }
            break;

        default:
            break;                                // out-of-claim drop
        }
    }

private:
    // Transaction phases (spec sec. 5A state machine).
    enum class Phase : uint8_t { kIdle, kAddrNak, kWriteData, kReadData };

    // (Repeated) START: address byte staged in S0 (m_shift).  Present node ->
    // ACK (status 0x00 per P1) and enter the direction phase; absent -> NAK
    // (status 0x08 per P5; never LAB).
    void startTransaction(uint8_t ctl) noexcept
    {
        (void) ctl;                               // ACK bit tracked via P3/P4
        uint8_t const addr = m_shift;
        int const idx = deviceLookup(static_cast<uint8_t>(addr & 0xFEu));
        if (iicTraceEnabled()) {
            char const* tag = "NAK";
            if (idx >= 0) {
                switch (m_devices[static_cast<size_t>(idx)].kind) {
                case Kind::FruEeprom: tag = "ACK[FRU]";    break;
                case Kind::Nvram:     tag = "ACK[NVRAM]";  break;
                case Kind::Status:    tag = "ACK[STATUS]"; break;
                }
            }
            std::fprintf(stderr, "IIC-TXN addr=0x%02x dir=%c -> %s\n",
                         static_cast<unsigned>(addr & 0xFEu),
                         (addr & 0x01u) ? 'R' : 'W', tag);
        }
#if defined(EMULATR_DIAGNOSTIC_LOGGING)
        iicTraceArmCheck(static_cast<uint8_t>(addr & 0xFEu));
#endif
        if (idx < 0) {
            m_phase  = Phase::kAddrNak;
            m_curDev = -1;
            m_status = kSt_LRB;                   // PIN=0 BB=0 LRB=1
            return;
        }
        m_curDev     = idx;
        m_status     = 0x00;                      // P1: address ACK'd
        m_dummyDone  = false;
        m_firstWrite = true;
        m_phase      = (addr & 0x01u) ? Phase::kReadData
                                      : Phase::kWriteData;
    }

    // Linear lookup of an even node address in the configured bus.  -1 = absent
    // (NAK).  The bus is tiny (a handful of devices), so linear is fine and the
    // ordering is the manifest order (stable for snapshot content mapping).
    [[nodiscard]] int deviceLookup(uint8_t node) const noexcept
    {
        for (size_t i = 0; i < m_devices.size(); ++i) {
            if (m_devices[i].address == node) return static_cast<int>(i);
        }
        return -1;
    }

public:
    // ---- Snapshot support (kChipsetVersion 5) ----------------------------
    // Identity (which devices, their addresses/kinds) comes from the manifest
    // and is RE-APPLIED via configureDevices() before restore, so the snapshot
    // carries only the mutable CONTENT: each configured device's 256-byte image
    // in bus order.  contentBytes() is deviceCount() * kImageSize.  The caller
    // (TsunamiChipset) must configureDevices() first, then restoreContentImage()
    // with a byte count that matches; a mismatch is rejected by the caller.
    [[nodiscard]] size_t deviceCount() const noexcept { return m_devices.size(); }

    [[nodiscard]] size_t contentBytes() const noexcept
    {
        return m_devices.size() * static_cast<size_t>(kImageSize);
    }

    void contentImage(uint8_t* out) const noexcept   // out holds contentBytes()
    {
        for (size_t i = 0; i < m_devices.size(); ++i) {
            std::memcpy(out + i * kImageSize,
                        m_devices[i].image.data(), kImageSize);
        }
    }

    // Adopt content bytes on load.  n must equal contentBytes() (caller checks);
    // a stale/mismatched snapshot is ignored so it cannot corrupt a freshly
    // configured bus.
    void restoreContentImage(const uint8_t* in, size_t n) noexcept
    {
        if (n != contentBytes()) return;
        for (size_t i = 0; i < m_devices.size(); ++i) {
            std::memcpy(m_devices[i].image.data(),
                        in + i * kImageSize, kImageSize);
        }
    }

    // ------------------------------------------------------------------------
    // IIC completion-interrupt level -- RETAINED INERT (2026-06-30 correction).
    //
    // HISTORY: a 2026-06-29 theory held that the DS20 "AlphaPC 264DP" mis-badge
    // came from EmulatR never raising the PCF8584 transfer-completion INT that
    // an interrupt-driven SRM IIC driver waits on.  That theory is DISPROVEN.
    //
    // PROVEN (apisrm/ref/iic_driver.c): on the shipped V7.3-2 DS20 firmware the
    // IIC driver is POLLED, not interrupt-driven.  iic_driver.c:138 sets
    //   #define POLLED (MIKASA || ALCOR || RAWHIDE || PC264 || K2 || TAKARA)
    // so on PC264 iic_init takes the #if POLLED arm: it krn$_create's the
    // "srom_poll" process iic_poll_rt and NEVER sets int_flag = IIC_ENI or
    // mode = DDB$K_INTERRUPT (the #else interrupt arm with int_vector_set is
    // compiled out).  Measurement agrees: ENI (0x08) is never written all boot.
    //
    // Completion is delivered by the poll process, not an interrupt: iic_poll_rt
    // spins reading S1, and on PIN == 0 calls iic_service, which advances the
    // transfer and krn$_post's the misr_t.sem that iic_rw_common's krn$_wait
    // blocks on.  A 1-byte node-0x40 read completes in exactly three iic_service
    // calls (dummy, real byte + IIC_NACK -> status 0x08, then STOP +
    // IIC_SR_DONE), returning rec_count == 1 so iic_init registers iic_ocp0 and
    // get_sysvar's fopen("iic_ocp0") succeeds (member 6 = "AlphaServer DS20").
    // This model already presents that exact sequence (PIN == 0 on ACK; P2
    // dummy; P3 NACK -> 0x08), so the live badge failure is that the poll
    // process is not serviced during iic_init's early verify -- NOT a missing
    // interrupt and NOT a device-model gap.  The 2-second IIC_MAS_TIMEOUT then
    // wins the race, rec_count stays 0, and the node is left unregistered.
    // See journals/DS20_Badge_IIC_Polled_Completion_topic.xml.
    //
    // interruptPending() below is kept (default-OFF, snapshot-clean, reads false
    // until the guest sets ENI -- which this firmware never does) only for the
    // WEBBRICK-class interrupt-driven IIC path; it is INERT on PC264/DS20.  The
    // paired EMULATR_IIC_IRQ_BIT wiring in TsunamiChipset is likewise inert here.
    // ------------------------------------------------------------------------
    [[nodiscard]] bool interruptPending() const noexcept
    {
        return ((m_status & kSt_PIN) == 0) && ((m_control & kCtl_ENI) != 0);
    }

private:
    // S0-area register selection from the latched ES bits (datasheet Table 5):
    //   ES0=1                -> S0  (data)
    //   ES0=0, ES1=1, ES2=0  -> S2  (clock)
    //   ES0=0, ES1=0, ES2=1  -> S3  (interrupt vector)
    //   ES0=0, ES1=0, ES2=0  -> S0' (own address)
    enum class Sel { kS0, kS0Own, kS2, kS3 };
    Sel regSelect() const noexcept
    {
        if (m_control & kCtl_ES0) return Sel::kS0;
        if (m_control & kCtl_ES1) return Sel::kS2;
        if (m_control & kCtl_ES2) return Sel::kS3;
        return Sel::kS0Own;
    }

    // Env-gated IIC transaction trace (EMULATR_IIC_TRACE).  When set,
    // startTransaction logs every START (address + R/W + ACK/NAK + kind tag)
    // and ioRead logs every device byte read (address/offset/value).  A single
    // cold boot then reveals which bus addresses the firmware probes and what
    // it pulls back.  Evaluated once (function-local static).
    [[nodiscard]] static bool iicTraceEnabled() noexcept
    {
        static bool const on = (std::getenv("EMULATR_IIC_TRACE") != nullptr);
        return on;
    }

#if defined(EMULATR_DIAGNOSTIC_LOGGING)
    // Observe-only retire-trace arm at the IIC bus (the MMIO device path the
    // GuestMemory DRAM sink cannot see).  Pairs with EMULATR_TRACE_DISARM_PA
    // (GuestMemory) so a window can span iic_init's registration probe up to
    // the HWRPB base store (0x2000).  Runtime default-OFF; absent in Release.
    //   EMULATR_TRACE_ARM_ON_IIC=<node|0|1>  arm on the first START transaction;
    //       0 or 1 = any address, else only when (addr & 0xFE) == (node & 0xFE)
    //       (e.g. 0x40 to arm on the first status-controller probe).
    //   EMULATR_TRACE_ARM_INSTRS=<n>         window length in retired instrs
    //       (default 8M; set large or rely on EMULATR_TRACE_DISARM_PA to bound).
    // Fires once per run.
    static void iicTraceArmCheck(uint8_t node) noexcept
    {
        static bool const s_enabled =
            (std::getenv("EMULATR_TRACE_ARM_ON_IIC") != nullptr);
        if (!s_enabled) return;
        static uint64_t const s_armNode = []() -> uint64_t {
            char const* e = std::getenv("EMULATR_TRACE_ARM_ON_IIC");
            return (e && *e) ? std::strtoull(e, nullptr, 0) : 0ULL; }();
        static int64_t const s_armInstrs = []() -> int64_t {
            char const* e = std::getenv("EMULATR_TRACE_ARM_INSTRS");
            return (e && *e) ? std::strtoll(e, nullptr, 0) : 8000000LL; }();
        static bool s_fired = false;
        if (s_fired) return;
        if (s_armNode > 1ULL &&
            (node & 0xFEu) != static_cast<uint8_t>(s_armNode & 0xFEu)) {
            return;                                   // waiting for the chosen node
        }
        s_fired = true;
        traceLib::DecListingSink::setTraceWindowCountdown(s_armInstrs);
        std::fprintf(stderr,
            "IIC-TRACE-ARM node=0x%02x -> retire window %lld instrs\n",
            static_cast<unsigned>(node & 0xFEu), (long long)s_armInstrs);
        std::fflush(stderr);
    }
#endif

    // One-shot note for non-byte access (house width discipline).
    static void noteNonByteAccess() noexcept
    {
        static bool s_noted = false;
        if (!s_noted) {
            s_noted = true;
            std::fprintf(stderr,
                "IicPcf8584: non-byte access dropped (IIC is "
                "byte-only); further occurrences silent\n");
        }
    }

    uint8_t m_status     = 0;     // S1 read view
    uint8_t m_control    = 0;     // S1 last control write (ES latch)
    uint8_t m_ownAddr    = 0;     // S0'
    uint8_t m_clock      = 0;     // S2
    uint8_t m_vector     = 0;     // S3
    uint8_t m_shift      = 0;     // S0 staging byte (address for next START)
    Phase   m_phase      = Phase::kIdle;
    int     m_curDev     = -1;    // index into m_devices, -1 = none
    bool    m_firstWrite = false; // next write byte is the internal offset
    bool    m_dummyDone  = false; // P2 dummy read consumed

    std::vector<IicDevice> m_devices;   // configured bus population (v4)
};

#endif // DEVICELIB_TSUNAMI_IIC_PCF8584_H
