<!--
EmulatR V5 -- Session Journal JRN-VMB-019
Project: EmulatR (Alpha 21264 / EV6), V5 active tree (emulatrappuniv5).
Architect: Timothy Peer.  AI collaboration: Claude (Anthropic, Fable session, Mac).
Copyright (C) 2025, 2026 eNVy Systems, Inc.  Licensed eNVy Non-Commercial v1.1.
ASCII(128) only.  Hex radix.
NOTE: authored on the Mac while the PC share was unreachable (SMB timeout
during the PC reload); copied to emulatrappuniv5/journals/ (the directory
of record) 2026-07-24 once the share returned.  This repo copy is canonical.
-->

# JRN-VMB-019 -- NOIOVEC part 3: BOOT_DEV string EXONERATED (it is the
#                complete canonical 19-char form); failure isolated to APB's
#                pattern-VM walk; PCI-completion plan (#41) scoped from HRM

    Doc id   : JRN-VMB-019
    Date     : 2026-07-24
    Status   : INVESTIGATION RECORD + WORK PLAN.  No code changed.
               Trace: out/build/relwithdebinfo/traces/20260724-185950_srm.trc
               (42.6 MB, final 261,612 retires, ends HALT @0x20003a38).
    Relates  : JRN-VMB-018 (both files; this EXECUTES its Next Step 1 + 3 and
               REVISES its Sec 2 hypothesis), JRN-VMB-018_P2 (static APB),
               tasks_20260612_boot_pci_deploy.md (#37-#42).
    Method   : Trace forensics on 20260724-185950_srm.trc + static disasm of
               APB (tools/alpha_disasm.py) + apisrm SRM console SOURCE as the
               authority for the console-side string format.

--------------------------------------------------------------------------------
## 0. Executive summary

1. The JRN-018 confirmation question is ANSWERED, and the answer REFUTES the
   working hypothesis: during the failing IOVEC search APB makes ZERO reads
   from the GCT region (PA 0x3ff32xxx) or ANY console memory.  All loads are
   APB's own data/literals/stack.  The GCT tree is not walked at failure time.

2. The BOOT_DEV string is EXONERATED.  Byte-store reconstruction of the
   GETENV(func 0x22, id 0x2 = BOOT_DEV) copy loop (trace pc=0x101ab4cc)
   proves APB received the COMPLETE canonical topology string:

       "IDE 0 105 0 0 0 0 0"   (19 chars: protocol + 7 numeric fields)

   This is EXACTLY what apisrm dictates: filesys.c file2dev() emits
   sprintf("%s %d %d %d %d %d%s", dev, n4..n0, suffix) with fd_table entry
   {"dq","dq",0,"IDE"," 0 0"} (filesys.c:2772) -> 5 fields + " 0 0" suffix.
   105 = 100*function + device = Cy82C693 IDE at dev 5 func 1.  The console
   side of the handoff is PERFECT.  The earlier "content gap / truncated
   string" theories are DEAD.

3. The failure is now isolated to APB's INTERNAL pattern-matching VM:
   - %APB-F-NOIOVEC status 0x158284 is manufactured at EXACTLY ONE site:
     CMOVEQ @0x20096e58 ("result slot [fp+0x28] still 0 -> 0x158284").
   - The success stores for that slot (0x20097e30/40/60/90) are in a region
     (0x20097200-0x20097fff) that NEVER EXECUTES in the window.
   - The walk: request code 0xf8 dispatch (ladder 0x96290-0x96490 XORs vs
     0xe7,0xe8..0xf8) follows a self-relative word link at [0x2009921e]
     (STATIC data inside the APB image), recurses (bsr 0x200964fc,
     sub-request r25=4) on child 0x2009922c, and both top-level invocations
     (ret-addr 0x2000e978) fail identically.
   - The "failed compare" candidates chased earlier are benign: the reject
     at 0x20097184-98 is the tokenizer's is-identifier-char test (isalpha +
     '$' 0x24 + '_' 0x5f) correctly ending the "IDE" token at the space.

4. APB image facts (for whoever picks this up):
   - Boot image = vdisk lbn 283169 x 1226 blocks; byte-identical to
     "Processor Support/OpenVMS/APB.EXE;1" minus its 512-byte EIHD block.
     File offset = VA - 0x20000000 (verified by instruction match).
   - Header ident A13-03; EW5700 + FibreChannel-aware => 7.3-2/8.x era;
     IDE boot IS supported by this vintage.
   - No protocol-keyword table: driver names are template-built
     ("SYS$P DRIVER.EXE"/"SYS$D DRIVER.EXE" with 2-char mnemonic patch-in);
     the pattern bytecode's only literal keyword is "wwid" (encoded
     char,0x04 pairs @0x99278).  Absence of "IDE" bytes in the image is
     NORMAL, not a defect.

5. CONSEQUENCE FOR THE PLAN: completing PCI (#41) remains REQUIRED for
   SYSBOOT (#39) and is the right next build target -- but today's evidence
   WEAKENS "PCI completion fixes NOIOVEC".  The console already produced a
   perfect boot path with the CURRENT PCI substrate.  Treat NOIOVEC as a
   possibly-separate defect until proven otherwise.  Both tracks below.

--------------------------------------------------------------------------------
## 1. Evidence chain (trace 20260724-185950_srm.trc line numbers)

  E1. Zero GCT reads: grep pa=0x3ff32/0x3ff33 -> 0 hits in 261,619 lines.
      Module loads only: va 0x2006xxxx (APB data, pa 0x62xxxx), 0x2009xxxx
      (image literals, pa 0x65xxxx), 0x200dxxxx (stack, pa 0x69xxxx).
  E2. GETENV setup for the failing call: lines ~4415-4435 --
      pc 0x2000e7a4-0x2000e7d0: R16=0x22 (GETENV), R17=0x2 (BOOT_DEV),
      R18=0x200dfd40 (dest), dispatch via CRB [HWRPB+0x7E0] -> 0x101aac60.
  E3. String reconstruction (byte stores at pc=0x101ab4cc, two copies:
      dest 0x2006aab8 then dest 0x200dfd40):
        qword1 "IDE 0 10" + qword2 "5 0 0 0 " + 3 bytes "0 0"
        = "IDE 0 105 0 0 0 0 0" (19 chars).  COMPLETE.
  E4. Both parser invocations: entries line 307 (SP 0x200dfd70) and 4800
      (SP 0x200dfc70), both from BSR @0x2000e974; recursions lines 955,
      5448; both return 0x158284.
  E5. Status births: ONLY pc=0x20096e58 CMOVEQ (lines 3178, 3587, 7647,
      8056) + propagation (0x2000e844 zapnot line 8081 = the JRN-018 WREG
      site).  Result slot [0x200dfc98] written ONLY by the two prologue
      zeroings (lines 4859 @0x20095a68, 5248 @0x20096ae0) -- read back 0 at
      line 8051.  No success store ever fires (pc range 0x200978xx-0x97fxx:
      0 retires).
  E6. Message emission: counted string @VA 0x2006c080 (len 0x26=38), chunk
      reads lines 8130-242608, ~6,340 lines per 8-byte chunk (console PUTS
      round trip), HALT line 261619.
  E7. apisrm authority: filesys.c:2746 fd_table, :2894 sprintf (5 fields +
      suffix); boot.c:1252 ev_write("booted_dev", dname).  Sample in source
      comment: "dkb200.2.0.1.0 <--> SCSI 0 1 0 2 200 0 0" (7 fields).

--------------------------------------------------------------------------------
## 2. What needs to be done -- TRACK A: NOIOVEC root cause (finish the hunt)

The remaining suspects, in probe order (cheapest, most-discriminating first):

  A1. STATE-vs-IMAGE DIFF (cheap, decisive).  The pattern walk consults
      state/jump tables in APB DATA: 0x2006a0e0 (table ptr from literal
      [0x200652e8]), 0x2006a12c/0x2006a174 (S4ADDQ-indexed dispatch),
      parse ctx 0x2006aa64-0x2006aab8.  These live at VA 0x2006xxxx = file
      offset 0x6xxxx IF statically initialized.  DIFF guest memory against
      the image bytes (SRM examine after the halt, or an EmulatR memory
      dump) for 0x2006a0c0-0x2006a200.  DIVERGENT -> earlier runtime
      corruption/mis-seeding (chase the writer); IDENTICAL -> suspicion
      shifts squarely to A2.
  A2. CPU-EXECUTION AUDIT of the parser's hot instruction set.  The
      tokenizer/VM leans on unaligned LDQ_U/EXTWL/EXTWH/EXTLL/EXTLH/EXTBL/
      INSLH/INSLL/MSKLH/MSKLL chains, CMPULE ladders, S4ADDQ jump math,
      CMOVNE/CMOVEQ, and a nibble-table helper @0x20074100 reading packed
      0x6554433243322110 @0x20099150.  One wrong result derails the match.
      Cross-check EmulatR's byte-manipulation leaves against the AARM for
      the EXACT operand patterns in the trace (values are all captured).
      A targeted differential: hand-execute 20-30 instructions around each
      branch that turns back (0x95e80 dispatch, 0x96060-0x961xx, the 0xf8
      handler tail 0x964a0-0x964fc) against the trace's register values.
  A3. DESCRIPTOR PROVENANCE.  The parsed descriptor (0x2006a308/0x2006a430,
      JRN-018 finding 4, "-1 sentinel") is built BEFORE this window.  If A1
      exonerates the static tables, capture a window over the descriptor
      build ("Determining boot device type" phase) and verify the fields
      the 0xf8 walk consumes.
  A4. ORACLE RUN (if A1-A3 stall).  AXPBox ES40 boots VMS successfully;
      running the SAME VMS media there and windowing the same module
      (image offsets transfer: entry 0x95840) gives a known-good execution
      to diff against ours instruction-by-instruction.  (EmulatR remains
      the PRIMARY Oracle; AXPBox is corroboration only.)
  A5. SYMBOLS (nice-to-have).  APB.EXE;1 EIHD is intact -- decode EIHD/EIHS
      to name 0x20095840/0x2000def0/0x2000e770 and pull the message section
      mapping for 0x158284/0x15828C.  Strengthens the record.

--------------------------------------------------------------------------------
## 3. What needs to be done -- TRACK B: PCI interface completion (#41)

HRM references (tsunami_typhoon_21272_hrm.txt): Sec 10.1.3.3 linear config
translation (fig 10-3/10-4, Table 10-3 IDSEL one-hot, Table 10-4 byte
enables), Sec 8.5 type-0 vs type-1 rules ("Bus#0 -> Type 0, else Type 1
forward", line 14885), Ch.10 Pchip CSRs @10-45.  No-DEVSEL config reads
return all-1s WITHOUT error (line 16660) -- keep that exact behavior on
misses.

Current state (audited 2026-07-24 via code survey; files/lines):
  DONE:  type-0 BDF decode + dispatch (TsunamiPchip.h:1158-1186), 256-byte
         config images + width handling, BAR size-probe handshake
         (ManifestPciDevice.h:59-81, PciConfigSpace.h), EMULATR_PCI_CFG_TRACE
         (TsunamiPchip.h:1116-1127), manifest-driven registration
         (Machine.cpp:517-543), Cypress func0/func1 (Cy82C693Ide.h,
         Cypress_CY82C693ISABridge.h), tulip phase 1 (Dec21143Tulip.h),
         INTx->DRIR routing helpers (TsunamiChipset.h:430-444).
  GAPS (the #41 work, priority order):
  B1. BAR write -> decode REBIND.  Devices register I/O/mem ranges at
      construction; BAR writes are stored but never re-route.  Tulip
      already has setRangeCallbacks/programBar hooks (Dec21143Tulip.h:55-59,
      356-362) that nothing invokes.  Design: on pciConfigWrite to a BAR,
      device (or a shared PciConfigSpace helper) computes the new range and
      calls Pchip register/unregister (io-port registry for io-BARs,
      registerPciMemRange for mem-BARs).  Acceptance: doctest -- write BAR,
      observe decode move; SRM's own assignment sequence replays clean
      under EMULATR_PCI_CFG_TRACE.
  B2. IDSEL faithfulness: dev# >20 (>1.0100 per Table 10-3) must
      master-abort (all-1s) -- verify the current miss path handles the
      boundary exactly.
  B3. Sparse I/O + sparse mem windows (currently all-1s/absent): implement
      per HRM 10.1.3.1/10.1.3.2 address folding (needed by any driver using
      sparse space; SRM used dense so far).
  B4. SG DMA window: translateDmaToPa (TsunamiPchip.h:401-420) handles
      direct-map only; SG=1 needs the TBA-based PTE fetch + TLB per HRM
      Ch.10 window regs.  Needed for tulip DMA and any HBA; NOT needed for
      the PIO dq boot path (JRN-018 authority check: dq_driver.c is pure
      inportb/w, zero DMA).
  B5. Pchip1 hose (0x802/0x803): presently a stub; DS20 has a second hose.
      Low urgency until a device lives there (keep returning proper
      presence bits).
  B6. PERROR wiring once B1-B4 create real error sources.
Sequencing: B1+B2 first (SRM/OS-visible, cheap); B3/B4 gate later OS driver
phases; B5/B6 close the fidelity gap.  SCSI HBA modeling and DMA are NOT
prerequisites for getting past bootstrap (user question 2026-07-24):
IDE boot is supported by this APB, the boot-time dq path is PIO, and the
console already enumerates dqa0 correctly.

--------------------------------------------------------------------------------
## 4. Corrections to prior sessions' records

  - JRN-018 Sec 2 hypothesis ("GCT/FRU content gap, fix = console-side GCT
    content") is NOT confirmed by the confirmation run it requested: no GCT
    reads occur in the failing window and the boot-path string is complete
    and canonical.  The GCT may still matter elsewhere, but NOT via
    BOOT_DEV, and not in this module's inputs.
  - This session's own interim theories, killed in order: "exit console
    scan" (fixed, JRN-017), "APB lacks IDE support" (disproven -- template
    driver names + era), "topology string truncated" (disproven -- E3).
  - memory.md line ~45 ("Hypothesis: the KNOWN PCI gate") should be updated
    to point here when memory.md is next edited.

--------------------------------------------------------------------------------
## 5. Key addresses (adds to JRN-018 Sec 4)

  NOIOVEC status birth        0x20096e58  (CMOVEQ; only site)
  result slot                 fp+0x28 (call#2: 0x200dfc98; zeroed @0x20095a68,
                                       0x20096ae0; success stores 0x20097e30+)
  request-code ladder         0x96290 (0xe7) .. 0x96490 (0xf8 handler)
  0xf8 chain root             [0x2009921e] static link -> child 0x2009922c
  recursion                   0x200964fc (sub-request r25=4)
  tokenizer char-class leaf   0x97100-0x971b0 (isalpha + '$'/'_')
  nibble-helper + table       0x20074100 / 0x20099150 (0x6554433243322110)
  state tables (APB data)     0x2006a0e0 / 0x2006a12c / 0x2006a174
  parse context               0x2006aa64-0x2006aab8 (token type 0x13 = ident)
  BOOT_DEV string copies      0x2006aab8 (call#1), 0x200dfd40 (call#2)
  GETENV handler copy loop    0x101ab4cc (byte stores)
  apisrm authority            filesys.c:2746 (fd_table), :2894 (sprintf),
                              :2812 (file2dev); boot.c:1252 (ev_write)

Artifacts: Mac scratchpad apb.img/apb_exe.bin (transient); re-derive: dd
vdisk lbn 283169 x 1226, or strip 512-byte header from
"Processor Support/OpenVMS/APB.EXE;1".

Standing rules: P-1 faithful; ASCII/hex; surgical Edit; discuss-first;
V5 only write target.  EmulatR is the PRIMARY Oracle.
