<!--
EmulatR V5 -- Session Journal / Design Briefing JRN-VMB-006
Project: EmulatR (Alpha 21264 / EV6 emulator), V5 active hive (emulatrappuniv5)
Architect: Timothy Peer.  AI collaboration: Claude (Anthropic).
Copyright (C) 2025, 2026 eNVy Systems, Inc.  All rights reserved.
Licensed under eNVy Systems Non-Commercial License v1.1.
Per docs/notes/ADR-0001-source-file-headers.md (Markdown header as HTML comment).
ASCII(128) only.  Hex radix.
-->

# JRN-VMB-006 -- Keyboard (8042) + VGA interface: DESIGN BRIEFING (decision reversed; implement across all platforms)

    Doc id      : JRN-VMB-006
    Status      : OPEN -- design plan for review. NO source edits yet
                  (per V5 "discuss before code"). Implementation follows
                  approval, seam by seam.
    Date        : 2026-07-18
    Model       : claude-opus-4-8 (Cowork), device bridge to tim-hpz640,
                  hive D:\EmulatR\emulatrappuniv5.
    Supersedes  : JRN-VMB-005 "Decision" block (keyboard + VGA NOT to be
                  implemented). That decision is REVERSED (Sec 1).
    Relates to  : JRN-VMB-001..005. Corrects the JRN-VMB-005 primary finding
                  (Sec 2 -- the b_irq<1> storm is NOT the keyboard).
    Encoding    : ASCII-128.  Hex radix.

---

## 1. Decision reversed

JRN-VMB-005 recorded: "Keyboard (i8042) and graphics (VGA) are NOT to be
implemented." That is REVERSED by architect decision on 2026-07-18. Both
device INTERFACES are to be emulated, across ALL modeled platforms, because
there is an operational dependency on their presence.

Scope, as set by the architect:
  - The device INTERFACES must be present and behave correctly. We do NOT
    attach a virtual keyboard (no host keystroke source) and we do NOT open
    a graphics window for interactive use.
  - Coverage: ALL modeled platforms (DS10, DS20, DS25, ES40, ES45).
  - Authority: the HRM / vendor datasheets are authoritative. AXPBox is a
    secondary cross-check only.

The consequence of "interface present, nothing attached" is the clean
target state: a keyboard CONTROLLER that is register- and IRQ-correct with
an EMPTY keyboard (so IRQ1 never asserts), and a VGA device that CLAIMS its
aperture and register file and stores what the firmware writes (so the
0xB8000 console writes land instead of faulting). No host window, no host
input, deterministic and single-threaded (V5 framing preserved).

## 2. Correction to JRN-VMB-005: the current b_irq<1> storm is NOT the keyboard

JRN-VMB-005 Sec 2 attributed the divert storm to "IRQ1 (keyboard) never
deasserts." Reading the live V5 code, that attribution is WRONG and must be
corrected so we do not "fix" the storm by accident and mis-learn:

  - The CPU line the firmware takes is b_irq<1>, which V5 computes as the
    DEVICE-CLASS aggregate: pendingIrq1 = DRIR<55:0> & DIM & kDeviceClassMask
    (chipsetLib/TsunamiCchip.h:652). It is NOT ISA interrupt line 1.
  - ALL legacy ISA interrupts funnel through ONE 8259 pair (m_pic) whose
    single output drives ONE Cchip bit, DRIR<55> = kIsaBridgeDrirBit
    (chipsetLib/TsunamiChipset.h:322). Keyboard, COM, FDC all share it.
  - The V5 keyboard stub is POLL-ONLY and never touches an interrupt line
    (deviceLib/Tsunami/MinimalIsaStub.h header note :40-44; no interrupt
    call anywhere in the class at :112). And evalDeviceIrqs has NO
    setIrqInput(1, ...) -- keyboard line 1 is absent between the COM and FDC
    feeds (chipsetLib/TsunamiChipset.h:351-353).

Therefore the keyboard CANNOT be the source of the current storm -- it is
not wired to any interrupt at all. The storm on b_irq<1> is driven by
whatever holds DRIR<55> (a shared-PIC input from COM/FDC, or a stuck PIC
output), and the fixed non-vector picVector=0x916f3858 points at the SCB /
PIC-dispatch computation. That remains a SEPARATE open track (Sec 9); it is
NOT solved, or caused, by this keyboard/VGA work. This briefing implements
the interfaces for the operational dependency; it does not claim to clear
the storm.

## 3. Device homes per platform (fidelity placement)

The 8042 exists in TWO faithful homes depending on the south bridge, and
V5 already selects the bridge per model (southBridgeFromModel,
chipsetLib/TsunamiVariant.h:207-212; m_cypress at TsunamiChipset.h:898,
m_ali at :899):

  - DS10 / DS20 -- Cypress CY82C693 bridge. The keyboard controller is the
    SMC FDC37C93x Super I/O's integrated "Universal Keyboard Controller"
    (8042 core; KIRQ=keyboard IRQ1, MIRQ=mouse IRQ12; host interface at
    0x60/0x64, per the SMC FDC37C935 datasheet, "8042 KEYBOARD CONTROLLER"
    functional section and pin table). V5 already models this Super I/O as
    m_superio (deviceLib/Tsunami/Smc37c669SuperIo.h), which is also the FDC
    host and already feeds IRQ6 (fdcInterruptPending, Smc37c669SuperIo.h:115
    -> TsunamiChipset.h:353). NOTE: the Cypress part ALSO integrates an 8042
    (Cypress datasheet KBC section) and the SRM has a "wake up on Cypress
    CY82C693" comment (kbd_driver.c:1956-1958); on the modeled DS20 the
    Super I/O is the concrete host we already own, so the KBC lives there
    with KIRQ -> IRQ1.
  - DS25 / ES40 / ES45 -- ALi M1543C bridge. The 8042 is integrated in the
    M1543C (ALi datasheet KBC section) with the authoritative IRQ1 latch
    control in config register 0x41 bit7 (ALi datasheet :5567-5573).

V5 architecture makes this ONE shared implementation regardless of bridge:
the keyboard, RTC, PIC, and Super I/O are standalone IIoPortHandler objects
registered once in the shared wireDevices() (chipsetLib/TsunamiChipset.h:643,
registrations :655-705), and keyboard IRQ1 lands on the shared m_pic whose
output already drives DRIR<55> for both bridges. So "all platforms" = ONE
code path, NOT five. The bridge difference is confined to the IRQ1 deassert
RULE (Sec 4.3).

## 4. Interface spec 1 -- the 8042 keyboard controller

Authority: SRM PALcode kbd driver (the firmware's own expectations) plus
the SMC / ALi / Cypress datasheets. AXPBox CKeyboard cross-checks the
lifecycle.

### 4.1 Port + status + command model (must match exactly)

  - Ports: 0x60 data, 0x64 command(write)/status(read)
    (kbd.h:228-230; 8242_def.h:244-245; SMC Table 52).
  - Status @0x64 bits (kbd.h:232-239, 253-259): OBF 0x01, IBF 0x02,
    SYS 0x04, CD 0x08, KBEN 0x10, ODS/AuxOBF 0x20 (1=mouse byte,
    0=keyboard byte), GTO 0x40, PERR 0x80.
  - Controller commands @0x64 (kbd.h:241-249): 0x20 read mode byte,
    0x60 write mode byte, 0xAA self-test (returns 0x55), 0xAB interface
    test (returns 0x00), 0xAE enable kbd, 0xA7/0xA8 mouse dis/enable,
    0xD0/0xD1 read/write output port, 0xD4 write-to-aux, 0xFF reset.
  - Mode/command byte (KCCB) bits (kbd.h:261-267): EKI 0x01 (enable kbd
    IRQ1), EMI 0x02 (enable mouse IRQ12), SYS 0x04, DKB 0x10, DMS 0x20,
    KCC 0x40 (scancode translate).
  - Handshake: writer spins on IBF clear before writing (kbd_put,
    kbd_driver.c:550-564); reader reads 0x60 only when OBF set (kbd_get,
    :456-475, and it REJECTS a byte if ODS/PERR/GTO set, :471).

### 4.2 What the SRM actually does (the load-bearing fact)

The SRM console keyboard is POLLED, not interrupt-driven. The ISR only
bumps a counter and masks the vector; it NEVER reads 0x60
(keyboard_interrupt, kbd_driver.c:1011-1024). All data movement is in the
polled kbd_get path (:456-475, :890-913). Init sequence init_raw_kbd
(:1760-1899): self-test 0xAA->0x55 (:1793-1806), iface test 0xAB->0x00
(:1808-1812), enable 0xAE (:1813-1816), keyboard reset 0xFF expecting ACK
0xFA (:1820-1826), then program mode byte to (old & ~(SYS|DKB|DMS|KCC)) |
(EKI|EMI) -- i.e. it SETS EKI+EMI (:1833-1839). All controller-command
responses are consumed by POLLING and this happens BEFORE EKI is set.

### 4.3 IRQ1 lifecycle (the correctness core)

  - ASSERT IRQ1 only when the output buffer holds a KEYBOARD-sourced byte
    (OBF=1, ODS=0) AND EKI is set. Controller-command responses (0x55,
    0x00, mode byte, reset ACK) set OBF but MUST NOT pulse IRQ1 -- the
    firmware reads them by polling before EKI exists.
  - DEASSERT IRQ1 on a read of port 0x60 (the firmware ISR never does this;
    the polled kbd_get read is the only de-latch, kbd_driver.c:461-463).
  - Bridge-specific rule:
      * ALi (DS25/ES40/ES45): register 0x41 bit7 selects PS/2 semantics --
        "IRQ1 latches when it goes high and is released when read Port 60H"
        (ALi datasheet :5567-5573). Model that latch exactly.
      * Cypress/SMC (DS10/DS20): AT-legacy edge-per-byte via the shared
        8259; clear OBF and drop the IRQ input on the 0x60 read.
  - IDLE (our actual runtime): with no keyboard attached, no keyboard byte
    is ever produced, so IRQ1 stays deasserted for the entire boot
    regardless of EKI. This is the storm-free target.

### 4.4 No-keyboard behavior (what "interface present, nothing attached" means)

  - Respond to CONTROLLER commands on 0x64 so init proceeds: 0xAA->0x55,
    0xAB->0x00, 0x20 returns the stored mode byte, 0x60 stores it. These
    use OBF + polling, never IRQ1.
  - Return NO response to KEYBOARD-DEVICE commands written to 0x60 (0xFF
    reset, 0xF4 enable): with no ACK, the SRM kbd_attatched reset loop
    exhausts and concludes "keyboard not plugged in", init_raw_kbd returns
    0, and the keyboard-interrupt path is never established
    (kbd_driver.c:1769-1770, 1990-2001). Deterministic, storm-free.
  - 0x64 status at rest: OBF=0 (set only transiently while a controller
    response is pending), IBF=0, ODS=0, PERR=0, GTO=0.

### 4.5 AXPBox cross-check (reference only)

CKeyboard is a standalone bus device at 0x60/0x64 (Keyboard.cpp:54-55).
periodic() self-clears irq1_requested each tick and re-sets it only on a
NEW buffer->OBF transfer (:1524-1564); read_60 clears irq1_requested on the
0x60 read (:365-441, and note the explicit "lower PIC line on 0x60 read" is
COMMENTED OUT at :425 -- AXPBox leans on 8259 latch + EOI). command-byte
bit0/bit1 store allow_irq1/allow_irq12 (:588-595). Routed via
AliM1543C::pic_interrupt(0,1), which is idempotent (dedups on already-
asserted, AliM1543C.cpp:1075-1080) -> System interrupt(55) -> DRIR<55> ->
CPU (System.cpp:1760-1790). Teardown is guest EOI at the PIC, not the 0x60
read. This matches our plan; V5 already has the 8259 ISR/EOI + edge-mirror
to DRIR<55> (Sec 5), so we only add the IRQ1 INPUT.

## 5. Interface spec 2 -- the VGA device

### 5.1 What must be claimed

  - Framebuffer aperture 0xA0000-0xBFFFF in Pchip PCI-memory (OUTER) space,
    including the color text buffer 0xB8000-0xBFFFF. This is exactly where
    the firmware's console writes currently fall through as UNHANDLED OUTER
    WRITE (TsunamiPchip.h write registry consulted :608-615, UNHANDLED log
    :671; read float :547).
  - Legacy VGA I/O 0x3B0-0x3DF (CRTC 0x3B4/0x3D4, attribute 0x3C0, DAC
    0x3C8/0x3C9, feature/status 0x3BA/0x3DA, sequencer 0x3C4). AXPBox
    claims the subset 0x3B4-0x3B5, 0x3BA, 0x3C0-0x3CF, 0x3D4-0x3D5, 0x3DA
    (S3Trio64.cpp:270-278); we register the contiguous 0x3B0-0x3E0 range and
    decode within.

### 5.2 Behavior

  - Store the text buffer (character + attribute planes) so 0xB8000 writes
    are retained and the "UNHANDLED" log stops. Track the CRTC start
    address, cursor position, and mode registers enough that the stored
    contents are interpretable. AXPBox stores into 4 planes gated by the
    sequencer map-mask; text mode is char->plane0, attr->plane1
    (S3Trio64.cpp:3839-3861), with the active text window selected by
    graphics_ctrl.memory_mapping (case 3 = 0xB8000, :3444-3448).
  - Minimum viable for the operational dependency: a faithful register file
    + a retained text framebuffer that satisfies the firmware's VGA console
    init and absorbs writes. Full graphics-mode rasterization (CRTC timing,
    planar 4-plane graphics, DAC palette to pixels) is a LATER tier, gated
    by a TODO (Sec 8), only if a guest OS graphics head is later required.

### 5.3 Presentation model -- DECISION TO CONFIRM (answers "how does AXPBox present these?")

How AXPBox presents them: AXPBox drives a host WINDOW. The VGA card owns a
worker thread (CS3Trio64::start_threads -> run, S3Trio64.cpp:344-349, 81-108)
that opens an SDL 1.2 window (SDL_Init/SDL_SetVideoMode, gui/sdl.cpp:125-166,
1096-1131), polls host input ~100 Hz and repaints ~10 Hz (SDL_UpdateRect,
sdl.cpp:1041-1045). Host key events flow SDL_KEYDOWN -> sdl_sym_to_bx_key ->
CKeyboard::gen_scancode -> internal buffer -> keyboard thread periodic() ->
OBF + IRQ1 (sdl.cpp:962-994; Keyboard.cpp:333-360, 1524-1564). There is no
true headless VGA: "headless" in AXPBox just means "do not declare a GUI
device", in which case the VGA cannot instantiate at all (Configurator.cpp:
600-603) and the console is serial only.

Why we should NOT copy that here: AXPBox's model needs a windowing library,
a per-card display thread, and a host-input pump -- all of which conflict
with (a) the architect's "no virtual keyboard attached / no interactive
window" scope, (b) the V5 single-threaded deterministic framing, and (c)
the project rule that the Qt surface stays minimal.

RECOMMENDED presentation model for EmulatR (confirm before building):
  - Keyboard: NO host-input source. The controller is register/IRQ correct
    but its keyboard channel is permanently empty -> IRQ1 idle. Deterministic.
  - VGA: render to an IN-MEMORY text/framebuffer only. No window, no thread.
    Provide a gated snapshot exporter (EMULATR_VGA_DUMP -> write the decoded
    text screen to <run>/logs, ASCII-128) so the firmware's console output
    is observable on demand without a GUI. This satisfies "the interface is
    present and correct" with zero threading and zero Qt surface.
  - LATER / optional tier (separate approval): a minimal Qt read-only view
    of the text framebuffer, refreshed off the existing single-threaded
    step (no new device thread), if a live view is ever wanted. Gated,
    additive, not in this plan's default.

## 6. Seam-by-seam change list (all platforms = one shared set)

Style per V5 convention (FILE N / FUNCTION / CHANGE). NO edits applied yet.

  FILE 1: deviceLib/Tsunami/Smc37c669SuperIo.h  (DS10/DS20 KBC home)
    FUNCTION: new integrated 8042 channel (or fold the upgraded
      Kbd8042Stub in as a member the Super I/O owns).
    CHANGE: implement the 4.1-4.4 controller: 0x60/0x64 decode, status
      bits, controller-command responses via OBF+poll, mode byte with
      EKI/EMI, and a LEVEL accessor keyboardIrq1Pending() that is true only
      while OBF holds a keyboard-sourced byte with EKI set (idle -> false).
      Mirror the existing fdcInterruptPending() shape (:115).

  FILE 2: deviceLib/Tsunami/MinimalIsaStub.h  (the current Kbd8042Stub)
    FUNCTION: Kbd8042Stub (class :112; status :64; self-test queue :186;
      OBF flag m_obfPending :216; 0x60/0x64 handlers :135-155).
    CHANGE: promote from poll-only to interrupt-correct per 4.3-4.4; add
      irq1Pending(); ensure a read of 0x60 clears OBF and drops the IRQ
      input; NO IRQ on controller-command responses. If FILE 1 becomes the
      canonical home, this class is either retired or becomes the shared
      8042 core both bridges reference (decide at implementation).

  FILE 3: chipsetLib/TsunamiChipset.h  (interrupt wiring)
    FUNCTION: evalDeviceIrqs (:331; COM/FDC feeds :351-353; PIC->DRIR<55>
      mirror :355-360).
    CHANGE: add exactly one line after :353 --
      m_pic.setIrqInput(1, <kbd>.irq1Pending());   // keyboard IRQ1
    The existing 8259 level model + edge-mirror to DRIR<55> and the guest
    EOI path then provide assert AND deassert for free (Pic8259Pair.h:132,
    150, 166). No new Cchip DRIR bit.

  FILE 4: deviceLib/Tsunami/VgaTextConsole.h  (NEW device; ASCII-128,
      include guard DEVICELIB_TSUNAMI_VGATEXTCONSOLE_H, ADR-0001 header)
    FUNCTION: IIoPortHandler for I/O 0x3B0-0x3DF and a memory handler for
      the 0xB8000 text aperture; retained text/attr framebuffer + CRTC/
      cursor/mode registers per 5.1-5.2; gated snapshot exporter per 5.3.

  FILE 5: chipsetLib/TsunamiChipset.h  (device registration + members)
    FUNCTION: wireDevices (:643; registrations :655-705; IIC PCI-mem
      registration pattern to copy :753); member decls near :917
      (respect construction-order note :911-915).
    CHANGE: register VGA I/O via
      m_pchip.registerIoPortRange(0x3B0, 0x3E0, &m_vgaIo);  and the text
      aperture via m_pchip.registerPciMemRange(0xB8000, 0xC0000, &m_vga);
      add m_vga / m_vgaIo members.

  FILE 6: chipsetLib/TsunamiPchip.h  (aperture-span cap -- CONDITIONAL)
    FUNCTION: registerPciMemRange (:372-390; span>0x10000 rejected :376;
      I/O rebase to uint16_t port :493-527).
    CHANGE: the 32 KB text window 0xB8000-0xC0000 fits the 64 KB cap and
      needs NO change. The FULL 128 KB graphics aperture 0xA0000-0xC0000
      does NOT fit one registration -> either split into 0xA0000-0xB0000 +
      0xB0000-0xC0000, or raise the cap and widen the rebase. Default plan
      registers only the text window; full aperture is deferred with the
      graphics tier (Sec 8).

  NOT TOUCHED: the five *_platform.json manifests (they enumerate only PCI
  devices; keyboard/VGA/console are not listed). No per-platform edits.

## 7. Test / assertion plan (doctest CHECK-only; exceptions disabled)

  T1 (keyboard idle invariant): boot with no keyboard; assert IRQ1 input to
     m_pic stays 0 across the whole run; assert 0x64 status OBF=0 at rest.
  T2 (controller commands do not pulse IRQ1): drive 0xAA/0xAB/0x20/0x60 and
     CHECK the responses arrive via OBF polling with the IRQ1 input never
     asserting. CHECK 0xAA->0x55, 0xAB->0x00.
  T3 (no-keyboard resolution): CHECK that keyboard-device 0x60 commands
     (0xFF, 0xF4) get no ACK, so the SRM concludes "not plugged in" (observe
     via checkpoint/console rather than reaching into firmware state).
  T4 (IRQ1 de-latch, ALi path): with a synthetic injected keyboard byte
     (test-only), CHECK IRQ1 asserts, then CHECK it deasserts on the 0x60
     read; ALi reg-0x41 bit7 latch honored.
  T5 (VGA absorbs writes): write the 0xB8000 fill pattern (0x20/0x4f) and
     CHECK no UNHANDLED OUTER WRITE is logged and the text buffer holds the
     bytes; CHECK the snapshot exporter reproduces the screen.
  T6 (full-boot regression): EMULATR_CHECKPOINTS run; CHECK the 0xB8000
     UNHANDLED storm is gone from the log and the boot advances at least to
     the same point, with the b_irq<1> device-class storm behavior recorded
     for the SEPARATE track (Sec 9) -- this plan must not regress it either
     way.

## 8. TODO table (named, greppable; entry + call-site removed together on wiring)

    TAG                     SUMMARY
    kbd-aux-mouse           Mouse/aux (IRQ12) channel deferred; controller
                            answers 0xA7/0xA8/0xD4 minimally, no mouse data.
    vga-graphics-tier       Full graphics-mode rasterization (planar 4-plane,
                            CRTC timing, DAC palette) deferred; text retained
                            only. Needed only for a guest-OS graphics head.
    vga-full-aperture       0xA0000-0xB0000 non-text aperture registration
                            (needs the Pchip 64 KB cap split, FILE 6).
    vga-qt-view             Optional read-only Qt view of the text framebuffer
                            (single-threaded, gated), if a live view is wanted.

## 9. Out of scope (do NOT chase in this work)

  - The b_irq<1> device-class storm and the fixed picVector=0x916f3858 are a
    SEPARATE track (Sec 2). Root cause lives in the shared PIC output / SCB-
    PIC dispatch, not the keyboard. Do not attempt to "fix the storm" via the
    keyboard.
  - The 0x60222c firmware panic / 0x20000000 load-base question
    (JRN-VMB-003..005) is unrelated and stays deferred.

## 10. Citations

  V5 hive (D:\EmulatR\emulatrappuniv5):
    chipsetLib/TsunamiCchip.h:652        pendingIrq1 = DRIR & DIM & classmask
    chipsetLib/TsunamiCchip.h:437,448    assertInterrupt / deassertInterrupt
    chipsetLib/TsunamiChipset.h:322      kIsaBridgeDrirBit = DRIR<55>
    chipsetLib/TsunamiChipset.h:331,351-360  evalDeviceIrqs (COM/FDC feeds,
                                         PIC->DRIR<55> edge mirror)
    chipsetLib/TsunamiChipset.h:643,655-705,753,911-917  wireDevices,
                                         registrations, IIC PCI-mem pattern,
                                         member decls / construction order
    chipsetLib/Pic8259Pair.h:132,150,166 edge latch / output / acknowledge
    chipsetLib/TsunamiPchip.h:372-390,493-527,536-547,608-615,671  registry
                                         + rebase + UNHANDLED float/log
    chipsetLib/TsunamiVariant.h:151,207-212  variant + southBridgeFromModel
    deviceLib/Tsunami/MinimalIsaStub.h:40-44,64,112,135-155,186,216  Kbd stub
    deviceLib/Tsunami/Smc37c669SuperIo.h:115  fdcInterruptPending pattern
    systemLib/Machine.cpp:359,302-352,321,423  single TsunamiChipset; console
    (NEW) deviceLib/Tsunami/VgaTextConsole.h   VGA device (to create)

  Authoritative refs (Processor Support; read-only):
    Palcode/.../ref/kbd.h:228-278            ports/status/cmd/mode bits
    Palcode/.../ref/kbd_driver.c:456-475,550-564,1011-1024,1760-1899,
      1956-2001                              polled get/put, ISR, init, "not
                                             plugged in", Cypress wake comment
    Palcode/.../ref/8242_def.h:244-257       port + status mirror
    ALi M1543_Datasheet.txt:5567-5580,1995-1996  reg 0x41 bit7/6 IRQ1/12
                                             latch release-on-0x60; IRQ1I mux
    Cypress ISA Bridge cy82c693 .txt:837-851,1039-1042,1099-1109,1172-1174
                                             KBC integrate, host access,
                                             internal int connect, level/edge
    SMC FDC37C935 datasheet (uploaded)       8042 Universal Keyboard
                                             Controller; KIRQ/MIRQ; 0x60/0x64
                                             host interface (Table 52)

  Cross-check (axpbox/src; read-only):
    Keyboard.cpp:54-55,365-441,588-595,1524-1564  ports, read_60 clear,
                                             cmd-byte irq enables, periodic
    AliM1543C.cpp:1058-1094,986-1015         pic_interrupt, EOI/deassert
    System.cpp:1760-1790                      interrupt(55) -> DRIR<55> -> CPU
    S3Trio64.cpp:270-303,344-349,3444-3448,3839-3861  IO/mem map, thread,
                                             text window, plane store
    gui/sdl.cpp:125-166,962-994,1041-1045    window, key events, flush
    Configurator.cpp:600-603                 no-GUI => VGA cannot instantiate

## 11. Standing rules in force (JRN-VMB-001..005)

  ASCII-128 only; include guards not #pragma once; hex dispatch labels;
  doctest CHECK only; surgical Edit over whole-file rewrites; treat V0/V1/V2,
  Processor Support, and axpbox as read-only; header + inline doc on every
  change; TODO discipline (Sec 8); multi-GB traces bounded-tail only; discuss
  before code (this briefing IS that discussion -- no edits until approved).
