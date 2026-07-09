<!--
Title:  Device-model HRM/datasheet faithfulness audit -- 16550 UART, PCF8584 IIC,
        SMC37C669 SuperIO. Register-level vs authoritative datasheets + apisrm driver.
Date:   2026-07-07
Author: Timothy Peer (architect) / Claude (audit synthesis, Cowork; two parallel
        source sweeps vs the authoritative datasheets Tim provided).
Status: LEDGER. Companion to 20260707_tsunami_typhoon_hrm_faithfulness_audit.md
        (chipset). Sources now indexed in Processor Support/REFERENCE_INDEX.md.
Method: ASCII(128) only. Datasheet cites by section; V4 cites by file:line.
        Authoritative parts: PC16550D (UART), Philips PCF8584 (IIC), SMC37C669
        (SuperIO -- 37C935 is a PROXY, not the part).
-->

# Device-Model Faithfulness Audit (2026-07-07)

## 0. Verdict

- Uart16550.h vs PC16550D: FAITHFUL along the polled console path the DEC SRM uses;
  reset values match datasheet Table 3. All gaps are unexercised interrupt legs -- LOW.
- IicPcf8584.h vs PCF8584: FAITHFUL along the polled master path; reset matches sec 6.10
  (PIN=1, BB=1, S0'=S3=0). All gaps LOW / slave-mode. The live "DS20 badge" failure is a
  poll-SCHEDULING race, NOT a device-model gap.
- Smc37c669SuperIo.h vs SMC37C669: minimal-detect-stub with a faithful flat CR read-back.
  TWO REAL fixes: (1) 37C935 contamination on CR20/CR21; (2) registered on ALi (ES40/ES45)
  boxes where the 37C669 does not exist. Decode side-effects (base/IRQ/DRQ) are inert.

## 1. 16550 UART vs PC16550D (deviceLib/Tsunami/Uart16550.h)

FAITHFUL: RBR/THR + DLAB steering; IER (ERBFI/ETBEI incl the enable-while-empty THRE
kick, matching combo_driver.c re-arm); IIR RDA>THRE priority + read-clears; LSR
(DR/OE/THRE/TEMT, clear-on-read, overrun-drop, reset 0x60); SCR; DLL/DLM store/readback;
the two-source interrupt latch. Reset values all match Table 3.

Gap list (all LOW, none boot-critical -- TODO-documented at Uart16550.h:84-97):
- Modem-status delta bits (MSR<3:0>) + IIR 0x00 leg never fire (Uart16550.h:910-930) ->
  no modem-status interrupt. Wire only if console goes interrupt-driven.
- Receiver-line-status interrupt (IIR 0x06 / ELSI): LSR error bits store, raise nothing.
- FIFO RX trigger level + char-timeout (IIR 0x0C): trigger stored but RX-avail at >=1 byte.
- Local loopback (MCR bit4): stored, datapath absent.
- Framing/parity/break (LCR effects, LSR PE/FE/BI): intentional register-transparent model.

## 2. PCF8584 IIC vs Philips datasheet (deviceLib/Tsunami/IicPcf8584.h)

FAITHFUL: S1 control/status bit maps bit-exact to datasheet Table 4 + iic_def.h; the
register-select ES latch (S0/S0'/S2/S3) precedence; START/repeated-START/STOP; ACK/NAK/LRB
(incl the mid-read IIC_NACK -> LRB=1 that lets the driver's 0x08 count-complete test pass);
BB polarity (1=free) + transitions; reset (PIN=1,BB=1,S0'=S3=0); master write/read
sequencing; the pipelined-receiver dummy-first-read (the most carefully matched mechanism).

Gap list (all LOW):
- PIN handshake simplified: V4 holds PIN=0 through a transaction rather than toggling PIN=1
  per S0 access; the dummy-read contract reproduces the exact firmware poll/service cadence.
- Slave-mode flags AAS/LAB/STS/BER never set; S0' compare / one-bit offset not modeled
  (V4 is master-only). Needed only for slave / multi-master / bus-error modeling.
- ENI/ESO gating + interrupt delivery inert (correct formula, POLLED platform never uses it).
- Architectural: EEPROM offset/auto-increment folded into the controller vs a separate slave
  device. Behavior exact; refactor only if heterogeneous slaves must coexist.

## 3. SMC37C669 SuperIO vs apisrm driver (deviceLib/Tsunami/Smc37c669SuperIo.h)

Classification: minimal-detect-stub. detect() + config read-back FAITHFUL (CR0D=0x03 seeded;
enter 0x55x2 / exit 0xAA at 0x3F0/0x3F1 modeled); every base/IRQ/DRQ EFFECT unmodeled --
works only because the firmware programs the hard-wired defaults (COM1 0x3F8 / COM2 0x2F8 /
FDC 0x3F0).

37C669 (AUTHORITATIVE) vs 37C935 (PROXY) -- do NOT model the wrong part:
  Register model : 669 = FLAT CR00-CR29        | 935 = ISA-PnP LDN (index 0x07 select)
  Device ID      : 669 = CR0D = 0x03           | 935 = index 0x20 = 0x02
  Index 0x20/0x21: 669 = FDC base / IDE base   | 935 = ID / rev
  Keyboard, RTC  : 669 = NONE (external 8042 + MC146818) | 935 = KBC + RTC LDNs
V4 is correctly FLAT (669), 8042/RTC correctly external -- but CR20=0x02/CR21=0x01 are 935
ID/rev leaked onto the 669 FDC/IDE base regs.

Gap tasks:
- T-SIO1 [WRONG-part, do first]: remove 935 seeds (Smc37c669SuperIo.h:64-65); set true 669
  resets CR20=0x3C, CR21=0x3C, CR22=0x3D, CR0E=0x02 (smcc669_def.h:461/473/577/593/609).
- T-SIO2 [southbridge gating]: the 37C669 is Cypress-box (DS10/DS20) specific; ES40/ES45 use
  the ALi M1543C which integrates the SuperIO functions. Move the m_superio registration
  (TsunamiChipset.h:672-678) INSIDE the non-ALi branch of wireDevices so the 0x3F0 config
  window is not claimed on ALi boxes. Gate on PlatCap::SbCypress (southbridge axis).
- T-SIO3 [decode side-effects]: honor programmed CR base/IRQ/DRQ (relocate/enable the FDC/
  UART windows) instead of inert RAM (self-noted Smc37c669SuperIo.h:35-36).
- T-SIO4 [reset defaults, low]: seed the full 669 power-on CR set for defensive read-back.
- Low: config-enter abort on any intervening cycle; register the 0x370 secondary window.
NON-gaps (do NOT add): keyboard-controller LDN, RTC LDN -- the 669 has neither.

## 4. Cross-cutting -- confirms the gating model

The device layer splits cleanly on the axes (see chipset audit journal sec 11 for the
DECIDED gating):
- 16550 UART + PCF8584 IIC = Tier-0 UNIVERSAL device models (same silicon across DS and ES);
  no gate. They are already faithful.
- SuperIO / ISA-bridge = SOUTHBRIDGE axis -- Cypress (DS10/DS20) vs ALi M1543C (ES40/ES45).
  Gate on PlatCap::SbCypress/SbAli (already in the inert PlatformCapabilities). T-SIO2 is the
  first real consumer of that capability -- exactly the case the primitive was written for.
