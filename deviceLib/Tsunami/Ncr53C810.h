// ============================================================================
// Ncr53C810.h -- NCR 53C810 PCI SCSI HBA (KZPAA, `pka`), SCRIPTS-driven
// ============================================================================
// Project: EmulatR -- Alpha AXP / EV6 Architecture Emulator (V5)
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
// JRN-SCSI-001/-002.  AUTHORITIES: apisrm ref/n810_def.h (register file +
// SCRIPTS encodings -- DEC's own defs), ref/pke_script.mar (the EXACT SCRIPTS
// program the DS20 v7.3-2 console downloads and runs), ref/pke_driver.c +
// ref/n810_driver.c (init/ISR contract), io_device_list.h (bind row: VID/DID
// 0x00011000 -> "NCR 53C810" -> pk/PKE).  AXPBox src/Sym53C810.cpp is the
// SECONDARY corroboration model.
//
// EXECUTION MODEL: the console driver is synchronous (write DSP -> the chip
// runs SCRIPTS until an interrupt; ISR re-kicks with DCNTL<STD>).  This model
// executes SCRIPTS TO COMPLETION inside the DSP/STD write: instruction fetch
// and all block moves go through the owner-supplied bulk DMA callbacks (the
// G-A bus-master seam -> TsunamiChipset::dmaRead/WriteBytes -> Pchip window
// translation).  The SCSI side is a phase engine over VirtualScsiDevice
// targets (ScsiBus-lite: id -> target map; LUN from the IDENTIFY message).
//
// FAITHFULNESS DEVIATIONS (deliberate, each traced loudly, revisit at P4):
//   D1 Data-in block moves PAD with zeros when the target supplies fewer
//      bytes than the move count, so the phase-mismatch (SIST0<MA>) residue
//      path never fires.  Benign for INQUIRY/MODE SENSE allocation-length
//      semantics; READ counts always match exactly.
//   D2 Targets never DISCONNECT mid-command; WAIT DISCONNECT after status
//      completes immediately; reselection never occurs.
//   D3 Single-step mode (DCNTL<SSM>) raises SSI after every instruction as
//      documented but is untested (the pke driver's SSM path is #if'd out).
// ============================================================================
#ifndef DEVICELIB_TSUNAMI_NCR53C810_H
#define DEVICELIB_TSUNAMI_NCR53C810_H

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <ostream>
#include <utility>
#include <vector>

#include "chipsetLib/IDeviceHandlers.h"   // IPciDeviceHandler, IIoPortHandler
#include "deviceLib/scsi/ScsiCommand.h"
#include "deviceLib/scsi/ScsiTypes.h"
#include "deviceLib/scsi/VirtualScsiDevice.h"

namespace deviceLib {

class Ncr53C810 final : public IPciDeviceHandler, public IIoPortHandler {
public:
    // ---- owner-supplied seams (wired by TsunamiChipset::wireDevices) ------
    using RangeFn  = std::function<void(uint64_t base, uint32_t len,
                                        bool isMem, IIoPortHandler* self)>;
    using DmaRdFn  = std::function<void(uint64_t pciAddr, void* dst, size_t n)>;
    using DmaWrFn  = std::function<void(uint64_t pciAddr, void const* src, size_t n)>;
    using IntrFn   = std::function<void(bool level)>;

    Ncr53C810() noexcept { initConfig(); reset(); }

    void setRangeCallbacks(RangeFn reg, RangeFn unreg) noexcept
    { m_register = std::move(reg); m_unregister = std::move(unreg); }
    void setDmaAccess(DmaRdFn rd, DmaWrFn wr) noexcept
    { m_dmaRead = std::move(rd); m_dmaWrite = std::move(wr); }
    void setIntrCallback(IntrFn fn) noexcept { m_intr = std::move(fn); }

    // Manifest-driven identity (JRN-SCSI-003): the config-space Interrupt Pin
    // register MUST match the manifest's interrupt_pin -- the console reads
    // 0x3D and indexes its board routing table with it (pc264_io.c
    // assign_pci_vector), so a mismatch mis-routes the completion interrupt.
    void setInterruptPin(uint8_t pin) noexcept { m_cfg[0x3D] = pin; }

    // SCSI bus population: id 0..7 -> LUN-0 target (HBA itself is id 7 by
    // pke convention; attaching there is rejected).  ScsiBus-lite: one
    // target per id, LUN selected by IDENTIFY (only LUN 0 served today).
    bool attachTarget(unsigned id, scsi::VirtualScsiDevice* t) noexcept
    {
        if (id >= 8 || id == kHostId || t == nullptr) return false;
        m_targets[id] = t;
        return true;
    }

    // ---- chip reset -------------------------------------------------------
    void reset() noexcept
    {
        m_reg.fill(0);
        m_reg[kISTAT] = 0;
        m_reg[kDSTAT] = 0x80;              // DFE: DMA FIFO empty
        m_reg[kCTEST0] = 0;
        m_reg[kSSTAT0] = 0;
        m_dstatPend = 0; m_sist0Pend = 0; m_sist1Pend = 0;
        m_running = false;
        m_conn = Conn{};
        updateIrq();
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
        if (reg == 0x10 || reg == 0x14) {              // BAR0 (I/O) / BAR1 (mem)
            const bool     isMem    = (reg == 0x14);
            const uint32_t typeBits = isMem ? 0x00u : 0x01u;
            const uint32_t addrMask = ~(kBarWindow - 1u);
            if (value == 0xFFFFFFFFu) {                // size probe
                storeCfgLE(reg, (addrMask & ~0xFu) | typeBits);
                return;
            }
            storeCfgLE(reg, (value & addrMask) | typeBits);
            programBar(isMem, value & addrMask);
            return;
        }
        if (!cfgWritable(reg)) return;
        storeCfgLE(reg, value, width);
    }

    // ========================================================================
    // IIoPortHandler -- CSR file (BAR-relative offset 0x00-0x5F; the Pchip
    // mem-claim path rebases, the io-port path passes the raw port, so mask
    // to the 0x7F window either way).
    // ========================================================================
    uint64_t ioRead(uint16_t off, uint8_t width) noexcept override
    {
        uint64_t v = 0;
        for (uint8_t b = 0; b < width; ++b)
            v |= static_cast<uint64_t>(regRead8((off + b) & 0x7F)) << (8u * b);
        return v;
    }
    void ioWrite(uint16_t off, uint64_t value, uint8_t width) noexcept override
    {
        for (uint8_t b = 0; b < width; ++b)
            regWrite8((off + b) & 0x7F, static_cast<uint8_t>(value >> (8u * b)));
        // Long DSP writes start SCRIPTS after the last byte lands (pke:
        // wl(dsp, phys(script)) with DMODE<MAN>=0 auto-starts).
        if (((off & 0x7F) <= kDSP + 3) && ((off & 0x7F) + width) > kDSP + 3)
            startScripts("DSP write");
    }

private:
    // ---- register offsets (n810_def.h struct n810_csr) --------------------
    static constexpr uint8_t kSCNTL0 = 0x00, kSCNTL1 = 0x01, kSCNTL2 = 0x02,
                             kSCNTL3 = 0x03, kSCID   = 0x04, kSXFER  = 0x05,
                             kSDID   = 0x06, kGPREG  = 0x07, kSFBR   = 0x08,
                             kSOCL   = 0x09, kSSID   = 0x0A, kSBCL   = 0x0B,
                             kDSTAT  = 0x0C, kSSTAT0 = 0x0D, kSSTAT1 = 0x0E,
                             kSSTAT2 = 0x0F, kDSA    = 0x10, kISTAT  = 0x14,
                             kCTEST0 = 0x18, kCTEST1 = 0x19, kCTEST2 = 0x1A,
                             kCTEST3 = 0x1B, kTEMP   = 0x1C, kDFIFO  = 0x20,
                             kCTEST4 = 0x21, kCTEST5 = 0x22, kCTEST6 = 0x23,
                             kDBC    = 0x24, kDCMD   = 0x27, kDNAD   = 0x28,
                             kDSP    = 0x2C, kDSPS   = 0x30, kSCRATCHA = 0x34,
                             kDMODE  = 0x38, kDIEN   = 0x39, kDWT    = 0x3A,
                             kDCNTL  = 0x3B, kADDER  = 0x3C, kSIEN0  = 0x40,
                             kSIEN1  = 0x41, kSIST0  = 0x42, kSIST1  = 0x43,
                             kMACNTL = 0x46, kGPCNTL = 0x47, kSTIME0 = 0x48,
                             kSTIME1 = 0x49, kRESPID = 0x4A, kSTEST0 = 0x4C,
                             kSTEST3 = 0x4F, kSIDL   = 0x50, kSODL   = 0x54,
                             kSBDL   = 0x58, kSCRATCHB = 0x5C;

    // ISTAT bits (53C810): ABRT 0x80, RST 0x40, SIGP 0x20, SEM 0x10,
    //                      CON 0x08, INTF 0x04, SIP 0x02, DIP 0x01
    static constexpr uint8_t kIstatRst = 0x40, kIstatSip = 0x02, kIstatDip = 0x01;
    // DSTAT: DFE 0x80, MDPE 0x40, BF 0x20, ABRT 0x10, SSI 0x08, SIR 0x04, IID 0x01
    static constexpr uint8_t kDstatDfe = 0x80, kDstatSsi = 0x08,
                             kDstatSir = 0x04, kDstatIid = 0x01;
    // SIST0: MA 0x80, FC 0x40, SEL 0x20, RSL 0x10, SGE 0x08, UDC 0x04,
    //        RST 0x02, PAR 0x01;  SIST1: STO 0x04, GEN 0x02, HTH 0x01
    static constexpr uint8_t kSist0Rst = 0x02, kSist1Sto = 0x04;
    // DCNTL: SSM 0x10, IRQM 0x08, STD 0x04, SA 0x02, COM 0x01
    static constexpr uint8_t kDcntlSsm = 0x10, kDcntlStd = 0x04;
    // SCNTL1: EXC 0x80? (n810_scntl1_exc), RST 0x08 (assert SCSI RST)
    static constexpr uint8_t kScntl1Rst = 0x08;

    static constexpr unsigned kHostId    = 7;
    static constexpr uint32_t kBarWindow = 0x100;

    // ---- SCSI phases (n810_def.h) -----------------------------------------
    enum : uint8_t { kPhDatOut = 0, kPhDatIn = 1, kPhCmd = 2, kPhSts = 3,
                     kPhMsgOut = 6, kPhMsgIn = 7 };

    // ---- SCRIPTS INT vectors (n810_def.h n810_k_*) ------------------------
    // ok=0, reselected=4, got_msg_in=7, dat_out_done=8, dat_in_done=9;
    // errNNN vectors are literal (101..112).  Values live in guest DSPS.

    // ========================================================================
    // Register byte access with side effects
    // ========================================================================
    uint8_t regRead8(uint8_t off) noexcept
    {
        switch (off) {
        case kISTAT: {
            uint8_t v = 0;
            if (m_dstatPend)               v |= kIstatDip;
            if (m_sist0Pend || m_sist1Pend) v |= kIstatSip;
            if (m_conn.active)             v |= 0x08;      // CON
            return v;
        }
        case kDSTAT: {                       // clear-on-read
            uint8_t const v = static_cast<uint8_t>(m_dstatPend | kDstatDfe);
            m_dstatPend = 0;
            updateIrq();
            return v;
        }
        case kSIST0: {                       // clear-on-read
            uint8_t const v = m_sist0Pend;
            m_sist0Pend = 0;
            updateIrq();
            return v;
        }
        case kSIST1: {
            uint8_t const v = m_sist1Pend;
            m_sist1Pend = 0;
            updateIrq();
            return v;
        }
        case kCTEST1: return 0xF0;           // FIFOs empty (FMT=1111, FFL=0)
        case kDFIFO: return 0;               // no residue modeled (D1)
        default:      return m_reg[off];
        }
    }

    void regWrite8(uint8_t off, uint8_t v) noexcept
    {
        switch (off) {
        case kISTAT:
            if (v & kIstatRst) { trace("ISTAT soft reset"); reset(); return; }
            m_reg[off] = v & ~(kIstatSip | kIstatDip);   // status bits RO
            return;
        case kSCNTL1:
            m_reg[off] = v;
            if (v & kScntl1Rst) {             // SCSI bus reset -> SIST0<RST>
                m_sist0Pend |= kSist0Rst;
                m_conn = Conn{};
                updateIrq();
            }
            return;
        case kDSTAT: case kSIST0: case kSIST1:
            return;                           // read-only status
        case kCTEST3:
            return;                           // CLF/FLF: FIFOs not modeled
        case kDCNTL:
            m_reg[off] = static_cast<uint8_t>(v & ~kDcntlStd);  // STD self-clears
            if (v & kDcntlStd) startScripts("DCNTL<STD>");
            return;
        default:
            m_reg[off] = v;
            return;
        }
    }

    uint32_t reg32(uint8_t off) const noexcept
    {
        return  static_cast<uint32_t>(m_reg[off])
             | (static_cast<uint32_t>(m_reg[off + 1]) << 8)
             | (static_cast<uint32_t>(m_reg[off + 2]) << 16)
             | (static_cast<uint32_t>(m_reg[off + 3]) << 24);
    }
    void setReg32(uint8_t off, uint32_t v) noexcept
    {
        m_reg[off]     = static_cast<uint8_t>(v);
        m_reg[off + 1] = static_cast<uint8_t>(v >> 8);
        m_reg[off + 2] = static_cast<uint8_t>(v >> 16);
        m_reg[off + 3] = static_cast<uint8_t>(v >> 24);
    }

    // ========================================================================
    // Interrupt plumbing
    // ========================================================================
    void raiseDma(uint8_t bit, uint32_t dsps) noexcept
    {
        setReg32(kDSPS, dsps);
        m_dstatPend |= bit;
        m_running = false;
        updateIrq();
    }
    void updateIrq() noexcept
    {
        bool const dip = (m_dstatPend & m_reg[kDIEN]) != 0;
        bool const sip = (m_sist0Pend & m_reg[kSIEN0]) != 0
                      || (m_sist1Pend & m_reg[kSIEN1]) != 0;
        bool const level = dip || sip;
        if (level == m_irq) return;
        m_irq = level;
        if (m_intr) m_intr(level);
    }

    // ========================================================================
    // SCRIPTS engine
    // ========================================================================
    void startScripts(char const* why) noexcept
    {
        if (!m_dmaRead) {
            // MSVC /std:c++20 pipeline: no std::println -- fprintf per house style.
            std::fprintf(stderr, "Ncr53C810: SCRIPTS start (%s) with no DMA "
                                 "seam wired -- ignored\n", why);
            return;
        }
        trace2("SCRIPTS start", reg32(kDSP));
        m_running = true;
        int guard = 100000;                       // runaway backstop
        while (m_running && guard-- > 0) {
            stepScripts();
            if ((m_reg[kDCNTL] & kDcntlSsm) && m_running) {
                m_dstatPend |= kDstatSsi;         // single-step (D3)
                m_running = false;
                updateIrq();
            }
        }
        if (guard <= 0)
            std::fprintf(stderr, "Ncr53C810: SCRIPTS runaway (100k instrs) -- "
                                 "stopped at DSP=0x%08X\n", reg32(kDSP));
    }

    void stepScripts() noexcept
    {
        uint32_t const dsp = reg32(kDSP);
        uint8_t insn[8];
        m_dmaRead(dsp, insn, 8);
        uint32_t const w0 = le32(&insn[0]);
        uint32_t const w1 = le32(&insn[4]);
        setReg32(kDBC, w0);                       // DCMD:DBC mirror
        setReg32(kDNAD, w1);
        setReg32(kDSP, dsp + 8);
        scriptsTrace(dsp, w0, w1);

        switch (w0 >> 30) {                       // type
        case 0: execBlockMove(w0, w1);   return;
        case 1: execIoOrRw(w0, w1);      return;
        case 2: execTransferCtl(w0, w1); return;
        default:
            std::fprintf(stderr, "Ncr53C810: SCRIPTS memory-move (type 3) "
                                 "UNSUPPORTED at 0x%08X\n", dsp);
            raiseDma(kDstatIid, 0);
            return;
        }
    }

    // ---- type 0: block move ----------------------------------------------
    void execBlockMove(uint32_t w0, uint32_t w1) noexcept
    {
        uint8_t  const phase = (w0 >> 24) & 0x7;
        bool     const tab   = (w0 >> 28) & 0x1;
        uint32_t count = w0 & 0x00FFFFFF;
        uint32_t addr  = w1;
        if (tab) {                                // table indirect off DSA
            uint8_t ent[8];
            m_dmaRead(reg32(kDSA) + (w1 & 0x00FFFFFF), ent, 8);
            count = le32(&ent[0]) & 0x00FFFFFF;
            addr  = le32(&ent[4]);
        }
        if (!m_conn.active) {
            std::fprintf(stderr, "Ncr53C810: block move (phase %u) with no "
                                 "connection -- IID\n", phase);
            raiseDma(kDstatIid, 0);
            return;
        }
        if (phase != m_conn.phase) {
            // The pke script guards every move with phase-conditional jumps,
            // so a mismatch here is a model bug, not guest behavior.  Loud.
            std::fprintf(stderr, "Ncr53C810: PHASE MISMATCH move=%u bus=%u "
                                 "(MA residue path unmodeled, D1)\n",
                         phase, m_conn.phase);
            m_sist0Pend |= 0x80;                  // MA
            m_running = false;
            updateIrq();
            return;
        }
        if (count == 0) return;
        moveData(phase, addr, count);
    }

    // ---- type 1: I/O class or read/write-register class -------------------
    void execIoOrRw(uint32_t w0, uint32_t w1) noexcept
    {
        uint8_t const opcode = (w0 >> 27) & 0x7;
        if (opcode >= 5) { execRw(w0); return; }  // 5/6/7 = rw class
        switch (opcode) {
        case 0: execSelect(w0, w1);  return;      // SELECT (ATN via bit 24)
        case 1:                                    // WAIT DISCONNECT
            m_conn = Conn{};                      // D2: immediate bus-free
            return;
        case 2:                                    // WAIT RESELECT
            // D2: no target ever reselects; jump to alternate address per
            // chip semantics would wait forever -- signal STO instead so a
            // driver that lands here recovers.
            std::fprintf(stderr, "Ncr53C810: WAIT RESELECT reached -- no "
                                 "reselection modeled (D2); raising STO\n");
            m_sist1Pend |= kSist1Sto;
            m_running = false;
            updateIrq();
            return;
        case 3:                                    // SET ATN/ACK
        case 4:                                    // CLEAR ATN/ACK
            // Bus signal bookkeeping only; the phase engine sequences on
            // moves, so ATN/ACK need no side effects here.
            return;
        default:
            raiseDma(kDstatIid, 0);
            return;
        }
    }

    void execSelect(uint32_t w0, uint32_t w1) noexcept
    {
        (void) w1;                                 // alternate addr (resel) unused (D2)
        unsigned const id = (w0 >> 16) & 0xF;
        scsi::VirtualScsiDevice* t = (id < 8) ? m_targets[id] : nullptr;
        if (t == nullptr) {
            // Selection timeout -- how the pk driver learns an ID is empty.
            m_sist1Pend |= kSist1Sto;
            m_running = false;
            updateIrq();
            return;
        }
        m_conn = Conn{};
        m_conn.active   = true;
        m_conn.targetId = static_cast<uint8_t>(id);
        m_conn.phase    = kPhMsgOut;               // selected with ATN
        return;
    }

    void execRw(uint32_t w0) noexcept
    {
        uint8_t const op   = (w0 >> 24) & 0x7;     // copy 0 / or 2 / and 4 / add 6
        uint8_t const rega = (w0 >> 16) & 0xFF;
        uint8_t const data = (w0 >> 8) & 0xFF;
        uint8_t const opc  = (w0 >> 27) & 0x7;     // 5 write, 6 read, 7 modify
        uint8_t const cur  = m_reg[rega & 0x7F];
        uint8_t res = cur;
        switch (op & 0x6) {
        case 0: res = data;               break;   // copy (move data to reg)
        case 2: res = cur | data;         break;   // or
        case 4: res = cur & data;         break;   // and  (bic emits and-mask)
        case 6: res = static_cast<uint8_t>(cur + data); break;
        }
        // opc semantics: 7 read-modify-write reg; 5 write SFBR->reg forms;
        // 6 read reg->SFBR.  The pke script uses only bic/bis (opc 7).
        if (opc == 6) m_reg[kSFBR] = res;
        else          m_reg[rega & 0x7F] = res;
    }

    // ---- type 2: transfer control -----------------------------------------
    void execTransferCtl(uint32_t w0, uint32_t w1) noexcept
    {
        uint8_t const opcode  = (w0 >> 27) & 0x7;  // jmp 0 / call 1 / ret 2 / int 3
        bool const cmpPhase   = (w0 >> 17) & 1;
        bool const cmpData    = (w0 >> 18) & 1;
        bool const jmpIfTrue  = (w0 >> 19) & 1;
        uint8_t const phase   = (w0 >> 24) & 0x7;
        uint8_t const data    = w0 & 0xFF;
        uint8_t const mask    = (w0 >> 8) & 0xFF;

        bool cond = true;
        if (cmpPhase) cond = cond && (m_conn.active && m_conn.phase == phase);
        if (cmpData)  cond = cond && (((m_reg[kSFBR] ^ data) & ~mask) == 0);
        if (cond != jmpIfTrue) return;             // condition failed -> fall through

        switch (opcode) {
        case 0: setReg32(kDSP, w1);  return;       // JUMP (absolute; pke relocates)
        case 1:                                    // CALL
            setReg32(kTEMP, reg32(kDSP));
            setReg32(kDSP, w1);
            return;
        case 2: setReg32(kDSP, reg32(kTEMP)); return;   // RETURN
        case 3: raiseDma(kDstatSir, w1); return;   // INT: vector in DSPS
        default: raiseDma(kDstatIid, 0); return;
        }
    }

    // ========================================================================
    // Phase engine (ScsiBus-lite target side)
    // ========================================================================
    struct Conn {
        bool     active   = false;
        uint8_t  targetId = 0;
        uint8_t  phase    = kPhMsgOut;
        uint8_t  lun      = 0;
        // command assembly + data
        uint8_t  cdb[16]  = {};
        uint8_t  cdbLen   = 0;
        std::vector<uint8_t> data;     // data-in (from target) or data-out (to target)
        uint32_t dataPos  = 0;
        uint32_t expectOut = 0;        // bytes the target expects in DATA OUT
        uint8_t  status   = 0;
        uint8_t  msgIn    = 0x00;      // COMMAND COMPLETE
        bool     msgInDone = false;
    };

    void moveData(uint8_t phase, uint32_t addr, uint32_t count) noexcept
    {
        switch (phase) {
        case kPhMsgOut: {
            std::vector<uint8_t> buf(count);
            m_dmaRead(addr, buf.data(), count);
            // IDENTIFY (0x80|dis|lun) is byte 0; SDTR etc may follow -- LUN
            // only is consumed today (D2: disconnect priv ignored).
            if (count > 0 && (buf[0] & 0x80)) m_conn.lun = buf[0] & 0x07;
            m_reg[kSFBR] = buf[0];
            m_conn.phase = kPhCmd;
            return;
        }
        case kPhCmd: {
            uint32_t const n = count <= sizeof(m_conn.cdb)
                             ? count : static_cast<uint32_t>(sizeof(m_conn.cdb));
            m_dmaRead(addr, m_conn.cdb, n);
            m_conn.cdbLen = static_cast<uint8_t>(n);
            executeCommand();
            return;
        }
        case kPhDatIn: {
            uint32_t const have = static_cast<uint32_t>(m_conn.data.size()) - m_conn.dataPos;
            if (count > have) {
                // D1: pad with zeros instead of a mid-move phase change.
                m_conn.data.resize(m_conn.data.size() + (count - have), 0);
                std::fprintf(stderr, "Ncr53C810: data-in move %u > available "
                                     "%u -- padded (D1)\n", count, have);
            }
            // JRN-SCSI-027 tiling probe: the last unverified I/O hop is
            // HBA buffer -> guest RAM.  Target-side payloads are proven
            // byte-exact (scsi_read_diff, 68/68), so if the image still does
            // not parse the suspect is PLACEMENT: do the MOVEs for one command
            // tile the guest buffer exactly once (no gap, overlap, repeat), and
            // is `addr` a direct guest PHYSICAL address (m_dmaWrite pokes PA --
            // there is no bus-master window translation in this model, a scoped
            // gap now on the ledger with evidence attached)?
#if defined(EMULATR_BRINGUP_PROBES)
            // Two-tier per house rule (the ITBPROBE/DTBPROBE shape): compile
            // guard outside so release builds carry zero cost AND zero strings;
            // runtime env key inside so a diag build can flip probe configs
            // without a rebuild.  Keyed to EMULATR_SCSI_MOVE_PROBE (falls back
            // to the file's EMULATR_SCSI_TRACE so one env var lights both).
            if (movesProbeOn()) {
                std::fprintf(stderr,
                    "N810-MOVE in  pa=0x%016llx count=%u dataPos=%u size=%zu\n",
                    static_cast<unsigned long long>(addr), count,
                    m_conn.dataPos, m_conn.data.size());
            }
#endif
            m_dmaWrite(addr, m_conn.data.data() + m_conn.dataPos, count);
            m_reg[kSFBR] = m_conn.data[m_conn.dataPos];
            m_conn.dataPos += count;
            if (m_conn.dataPos >= m_conn.data.size())
                m_conn.phase = kPhSts;
            return;
        }
        case kPhDatOut: {
            size_t const base = m_conn.data.size();
            m_conn.data.resize(base + count);
            m_dmaRead(addr, m_conn.data.data() + base, count);
            if (m_conn.data.size() >= m_conn.expectOut) {
                executeWriteCommand();
                m_conn.phase = kPhSts;
            }
            return;
        }
        case kPhSts: {
            m_dmaWrite(addr, &m_conn.status, 1);
            m_reg[kSFBR] = m_conn.status;
            m_conn.phase = kPhMsgIn;
            return;
        }
        case kPhMsgIn: {
            m_dmaWrite(addr, &m_conn.msgIn, 1);
            m_reg[kSFBR] = m_conn.msgIn;
            m_conn.msgInDone = true;
            return;
        }
        default:
            raiseDma(kDstatIid, 0);
            return;
        }
    }

    static bool isWriteOpcode(uint8_t op) noexcept
    {
        return op == 0x0A || op == 0x2A || op == 0x15 /*MODE SELECT6*/
            || op == 0x55 /*MODE SELECT10*/ || op == 0x3F /*WRITE LONG*/;
    }
    static uint32_t writeLengthFromCdb(uint8_t const* cdb) noexcept
    {
        switch (cdb[0]) {
        case 0x0A: return (cdb[4] ? cdb[4] : 256u) * 512u;
        case 0x2A: return ((uint32_t(cdb[7]) << 8) | cdb[8]) * 512u;
        case 0x15: return cdb[4];
        case 0x55: return (uint32_t(cdb[7]) << 8) | cdb[8];
        default:   return 0;
        }
    }

    void executeCommand() noexcept
    {
        scsi::VirtualScsiDevice* t = m_targets[m_conn.targetId];
        if (t == nullptr) { raiseDma(kDstatIid, 0); return; }
        if (isWriteOpcode(m_conn.cdb[0])) {
            m_conn.expectOut = writeLengthFromCdb(m_conn.cdb);
            m_conn.data.clear();
            m_conn.dataPos = 0;
            if (m_conn.expectOut > 0) { m_conn.phase = kPhDatOut; return; }
        }
        // Read-class / no-data command: execute now into the data buffer.
        m_conn.data.assign(kMaxDataIn, 0);
        scsi::ScsiCommand cmd;
        cmd.cdb              = m_conn.cdb;
        cmd.cdbLength        = m_conn.cdbLen;
        cmd.lun              = m_conn.lun;
        cmd.dataDirection    = scsi::ScsiDataDirection::DeviceToHost;
        cmd.dataBuffer       = m_conn.data.data();
        cmd.dataBufferLength = kMaxDataIn;
        t->handleCommand(cmd);
        m_conn.data.resize(cmd.dataTransferred);
        m_conn.dataPos = 0;
        m_conn.status  = static_cast<uint8_t>(cmd.status);
        m_conn.phase   = cmd.dataTransferred > 0 ? kPhDatIn : kPhSts;
        cmdTrace(cmd);
    }

    void executeWriteCommand() noexcept
    {
        scsi::VirtualScsiDevice* t = m_targets[m_conn.targetId];
        if (t == nullptr) { raiseDma(kDstatIid, 0); return; }
        scsi::ScsiCommand cmd;
        cmd.cdb              = m_conn.cdb;
        cmd.cdbLength        = m_conn.cdbLen;
        cmd.lun              = m_conn.lun;
        cmd.dataDirection    = scsi::ScsiDataDirection::HostToDevice;
        cmd.dataBuffer       = m_conn.data.data();
        cmd.dataBufferLength = static_cast<uint32_t>(m_conn.data.size());
        t->handleCommand(cmd);
        m_conn.status = static_cast<uint8_t>(cmd.status);
        cmdTrace(cmd);
    }

    // ========================================================================
    // Config space + BAR plumbing (tulip pattern)
    // ========================================================================
    void initConfig() noexcept
    {
        m_cfg.fill(0);
        storeCfgLE(0x00, 0x00011000u);  // vendor 0x1000 NCR / device 0x0001 53C810
        storeCfgLE(0x08, 0x01000002u);  // class 0x010000 (SCSI), rev 0x02
        m_cfg[0x10] = 0x01;             // BAR0 = I/O
        m_cfg[0x3D] = 0x01;             // interrupt pin INTA
    }
    static bool cfgWritable(uint8_t reg) noexcept
    {
        return reg == 0x04 || reg == 0x05 || reg == 0x0C ||
               reg == 0x0D || reg == 0x0F || reg == 0x3C;
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
        if (cur != 0 && m_unregister) m_unregister(cur, kBarWindow, isMem, this);
        cur = base;
        if (base != 0 && m_register) m_register(base, kBarWindow, isMem, this);
    }

    // ---- traces (EMULATR_SCSI_TRACE) --------------------------------------
    static bool traceOn() noexcept
    {
        static bool const on = (std::getenv("EMULATR_SCSI_TRACE") != nullptr);
        return on;
    }
#if defined(EMULATR_BRINGUP_PROBES)
    // JRN-SCSI-027 DMA-tiling probe key (two-tier: this whole facility is
    // compiled out of release; inside a diag build the env var selects it).
    // HOUSEKEEPING (filed, not done here): this file's OLDER traces --
    // traceOn()/trace()/cmdTrace() -- are runtime-gated ONLY and predate the
    // compile-guard convention.  Bringing them into conformance is a separate
    // sweep on purpose: mixing it into a diagnostics landing would muddy both.
    static bool movesProbeOn() noexcept
    {
        static bool const on =
            (std::getenv("EMULATR_SCSI_MOVE_PROBE") != nullptr) || traceOn();
        return on;
    }
#endif
    static void trace(char const* msg) noexcept
    {
        if (!traceOn()) return;
        std::fprintf(stderr, "N810: %s\n", msg);
    }
    static void trace2(char const* msg, uint32_t v) noexcept
    {
        if (!traceOn()) return;
        std::fprintf(stderr, "N810: %s 0x%08X\n", msg, v);
    }
    void scriptsTrace(uint32_t dsp, uint32_t w0, uint32_t w1) noexcept
    {
        if (!traceOn()) return;
        static int n = 0;
        if (n++ < 4000)
            std::fprintf(stderr, "N810-SCRIPT dsp=0x%08X w0=0x%08X w1=0x%08X "
                                 "phase=%u\n", dsp, w0, w1, m_conn.phase);
    }
    // JRN-SCSI-027: the trace now carries the LBA/length the CDB asked for and
    // a checksum of the payload the target actually returned, so a run log can
    // be byte-diffed against the backing file host-side without a debugger.
    // The %LOADER-E-BADIMGOFF wall says the bytes ARRIVE and do not parse, so
    // the question is not "did the command succeed" (status/xfer already said
    // yes) but "are these the RIGHT bytes for this LBA".
    //   fnv = FNV-1a over the returned payload; first/last = the payload's
    //   first and last 8 bytes, which localize a shift without dumping MB.
    void cmdTrace(scsi::ScsiCommand const& cmd) noexcept
    {
        if (!traceOn()) return;
        uint8_t const op = cmd.opcode();
        // Decode LBA/count for the read/write family; -1 for everything else.
        long long lba = -1; long long cnt = -1;
        if (op == 0x28 || op == 0x2A) {            // READ(10) / WRITE(10)
            lba = (long long)(((uint32_t)m_conn.cdb[2] << 24)
                            | ((uint32_t)m_conn.cdb[3] << 16)
                            | ((uint32_t)m_conn.cdb[4] << 8)
                            |  (uint32_t)m_conn.cdb[5]);
            cnt = (long long)(((uint32_t)m_conn.cdb[7] << 8) | m_conn.cdb[8]);
        } else if (op == 0x08 || op == 0x0A) {     // READ(6) / WRITE(6)
            lba = (long long)((((uint32_t)m_conn.cdb[1] & 0x1F) << 16)
                            | ((uint32_t)m_conn.cdb[2] << 8)
                            |  (uint32_t)m_conn.cdb[3]);
            cnt = m_conn.cdb[4] ? m_conn.cdb[4] : 256;
        }
        uint64_t fnv = 1469598103934665603ull;     // FNV-1a 64 offset basis
        // Data-OUT commands (MODE SELECT, WRITE) never set dataTransferred --
        // that field is the target's data-IN output -- so fall back to the
        // buffer the initiator actually delivered.  Without this the MODE
        // SELECT parameter list logged as "-" and its block descriptor could
        // not be read back from the run log.
        uint32_t const n = cmd.dataTransferred
                         ? cmd.dataTransferred
                         : static_cast<uint32_t>(m_conn.data.size());
        for (uint32_t i = 0; i < n && i < m_conn.data.size(); ++i) {
            fnv ^= m_conn.data[i];
            fnv *= 1099511628211ull;
        }
        char head[24] = "-", tail[24] = "-";
        if (n >= 8 && m_conn.data.size() >= 8) {
            std::snprintf(head, sizeof(head), "%02X%02X%02X%02X%02X%02X%02X%02X",
                m_conn.data[0], m_conn.data[1], m_conn.data[2], m_conn.data[3],
                m_conn.data[4], m_conn.data[5], m_conn.data[6], m_conn.data[7]);
            size_t const e = (n <= m_conn.data.size() ? n : m_conn.data.size()) - 8;
            std::snprintf(tail, sizeof(tail), "%02X%02X%02X%02X%02X%02X%02X%02X",
                m_conn.data[e+0], m_conn.data[e+1], m_conn.data[e+2],
                m_conn.data[e+3], m_conn.data[e+4], m_conn.data[e+5],
                m_conn.data[e+6], m_conn.data[e+7]);
        }
        // Full CDB bytes: documents what the driver actually ASKED for (e.g.
        // the MODE SELECT parameter-list length in byte 4) so implementations
        // are probe-driven rather than guessed.
        char cdbHex[3 * 16 + 1] = {};
        for (uint8_t i = 0; i < cmd.cdbLength && i < 16; ++i)
            std::snprintf(cdbHex + i * 3, 4, "%02X ", m_conn.cdb[i]);
        std::fprintf(stderr,
            "N810-CMD id=%u lun=%u op=0x%02X len=%u lba=%lld cnt=%lld -> "
            "status=%u xfer=%u fnv=0x%016llx head=%s tail=%s cdb=[%s]\n",
            m_conn.targetId, cmd.lun, op, cmd.cdbLength, lba, cnt,
            static_cast<unsigned>(cmd.status), cmd.dataTransferred,
            static_cast<unsigned long long>(fnv), head, tail, cdbHex);
    }

    static uint32_t le32(uint8_t const* p) noexcept
    {
        return  static_cast<uint32_t>(p[0])
             | (static_cast<uint32_t>(p[1]) << 8)
             | (static_cast<uint32_t>(p[2]) << 16)
             | (static_cast<uint32_t>(p[3]) << 24);
    }

    static constexpr uint32_t kMaxDataIn = 1u << 20;   // 1 MiB per-command cap

    // ---- state ------------------------------------------------------------
    std::array<uint8_t, 256>  m_cfg{};
    std::array<uint8_t, 0x80> m_reg{};
    uint8_t  m_dstatPend = 0, m_sist0Pend = 0, m_sist1Pend = 0;
    bool     m_running = false;
    bool     m_irq = false;
    Conn     m_conn{};
    std::array<scsi::VirtualScsiDevice*, 8> m_targets{};

    uint64_t m_ioBase = 0, m_memBase = 0;
    RangeFn  m_register, m_unregister;
    DmaRdFn  m_dmaRead;
    DmaWrFn  m_dmaWrite;
    IntrFn   m_intr;
};

} // namespace deviceLib

#endif // DEVICELIB_TSUNAMI_NCR53C810_H
