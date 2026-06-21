<!--
EmulatR V4 -- pc264.c SRM-init / SMP call-stack pseudo-notes
Project: EmulatR (Alpha 21264 / EV6-EV67 emulator), V4 active tree
Architect: Timothy Peer.  AI collaboration: Claude / Anthropic (claude.ai web).
Source analyzed: Digital SRM Console Firmware, pc264.c (DP264/DS20/DS20E/XP2000
platform module, Tsunami).  These are transformed control-flow notes, not a
copy of the source.  ASCII(128) only.  Implementation happens in Cowork against
the live tree; PAL$ symbol values and line numbers are CONFIRM-against-tree.
-->

# pc264.c -- SRM init / SMP call-stack notes

## What this file is

`pc264.c` is the platform leaf for the Tsunami EV6/EV67 desktop-server family
(AlphaPC 264DP, DS20, DS20E "Goldrack", XP2000, CS20D, plus SHARK/SWORDFISH
variants under compile guards). It is a library of hooks; the shared console
kernel (NOT in this file) drives the boot sequence and calls down into these
routines. Callers outside this file are marked [kernel].

PAL$* names below are link-time symbols resolving to fixed PAs in the build.
Resolve their values against EV6_DEFS / the live tree before wiring -- marked
[CONFIRM].

---

## Phase A -- platform + system identity

[kernel] system-init
  -> get_sysvar()                          // member id into SYSVAR<15:10>
       sysvar = HWRPB$_SYSVAR base
       IF compile==SHARK:   sysvar |= 5<<10   (CLIPPER)
       ELIF compile==SWORDFISH: sysvar |= 1<<10 (DP264)
       ELSE (real DP264/DS20 family):
           IF fopen("iic_rcm_temp") succeeds:        // server-mgmt card present
               IF fopen("iic_8574_ocp") succeeds:
                   sysvar |= 8<<10                    // DS20E / Goldrack
               ELSE:
                   sysvar |= 6<<10                    // DS20 / Goldrush
           ELSE:
               sysvar |= 1<<10                        // DP264 (no SMM card)
       return sysvar
       // V4 GATE: the IIC devices iic_rcm_temp / iic_8574_ocp decide DS20 vs
       // DP264. If neither IIC device is modeled, sysvar falls to member 1
       // (DP264) and the box is treated as a single-CPU desktop. To present
       // a DS20 you must make iic_rcm_temp openable (and leave iic_8574_ocp
       // absent for plain DS20, present for DS20E).

  -> platform()                            // emulator vs real-hw self-flag
       IF *(int*)0xBFFC == 0xCAFEBEEF: return ISP_MODEL   // ISP harness marker
       ELSE: return REAL_HW
       // V4 NOTE: a magic word at PA 0xBFFC selects an ISP (in-system-probe)
       // model path. Leaving it unset (REAL_HW) is the normal console path.

---

## Phase B -- CPU identity + primary selection

  -> whoami()
       return mfpr_whami()                 // HW_MFPR WHAMI (IPR), NOT cserve$whami
       // V4 GATE: HW_MFPR WHAMI must return the per-CPU id. V4 is currently
       // hardwired UP (returns 0). Two CPUs returning 0 collapses all per-CPU
       // indexing below.

  -> primary()
       return whoami() == *(uint32*)PAL$PRIMARY   [CONFIRM PAL$PRIMARY]
       // V4 GATE: PAL$PRIMARY must hold the elected primary id. primary() is
       // the gate the kernel uses to fork primary-only vs secondary-only work.

  -> dualCPU()                             // "is there a secondary present?"
       impure = PAL$IMPURE_BASE + COMMON_SIZE + whoami()*SPECIFIC_SIZE
       signature = *impure.cns$srom_signature
       IF (signature >> 20) == 0xDEC:                 // SROM signature valid
           proc_mask = *impure.cns$srom_proc_mask
           proc_mask &= ~(1 << whoami())              // drop self
           IF proc_mask != 0: return 1                // another CPU exists
       return 0
       // V4 GATE (DS20 crux #1): dual detection reads the SROM-seeded
       // cns$srom_proc_mask out of the per-CPU impure region. For DS20 to be
       // recognized as dual, the SROM-emulation must seed signature>>20==0xDEC
       // and proc_mask with BOTH cpu bits. If proc_mask only has bit 0, the
       // console concludes single-CPU and never enters secondary bring-up.

---

## Phase C -- HWRPB system-recognition + per-CPU descriptors

  -> build_dsrdb(hwrpb, offset)            // dynamic system recognition block
       cpu_speed = krn$_timer_get_cycle_time(primary_cpu) / 1e6
       SWITCH hwrpb->SYSVAR[0] >> 10:               // member id from Phase A
           case 1:  SysType = 0                      // DP264
           case 6:  SysType = dualCPU() ? 2 : 1      // DS20 dual : single
           case 8:                                   // DS20E / Goldrack
               dual = dualCPU()
               read byte@0x11 of "iic_rcm_nvram0" -> xp2000 = (byte & 1)
               cpu_type = impure CPU_TYPE field
               IF cpu_type == 11 (EV67):
                   SysType = xp2000 ? (dual?10:9) : (dual?6:5)
               ELSE (EV6):
                   SysType = xp2000 ? (dual?8:7)  : (dual?4:3)
           case 11: SysType = 11                     // CS20D
           default: print "Error determining system type"; SysType = 0
       write DSRDB at hwrpb+offset:
           SMM    = dsrdb_n[SysType].smm
           LURT[] = dsrdb_n[SysType].lurt   (9 cols)
           NAME   = sprintf(dsrdb_n[SysType].name, cpu_speed)
       hwrpb->DSRDB_OFFSET[0] = offset
       return size
       // NOTE: this is where the model-name string and SMM/LURT tables are
       // chosen. DS20 dual == SysType 2 ("AlphaServer DS20 %3d MHz"). Drives
       // the banner identity, not the prompt format, but depends on the same
       // dualCPU()/sysvar inputs as the SMP path -- get those right once.

  -> get_bcache_info(hwrpb, cpu, *size)    // per-CPU Bcache size for HWRPB slot
       impure = PAL$IMPURE_BASE + COMMON + cpu*SPECIFIC
       write_many = *impure.cns$write_many
       bc_enable = (write_many >> 30) & 1
       IF bc_enable:
           code = (write_many >> 32) & 0xF
           bc_size = {1->2, 3->4, 7->8, else->0}     // MB
       *size = bc_size * 1024
       // V4 NOTE: reads the SROM-staged WRITE_MANY CSR shadow from impure.
       // Seed cns$write_many or *size comes back 0 (no Bcache reported).

  -> get_srom_revision(srev, id)           // cosmetic: "show cpu" srom rev string
       impure = ... id ...
       IF (*impure.cns$srom_signature >> 16) == 0xDECB:
           parse rev bytes -> "X" | <letter> | "?" then dotted numeric levels
       // Off the boot-to-prompt path (display only).

---

## Phase D -- SMP secondary bring-up  (DS20 crux)

[kernel, primary-only] after HWRPB built and per-CPU slots initialized
  -> start_secondaries()
       ev_read("cpu_enabled", &evp, 0)               // env-var bitmask
       FOR i in 0 .. MAX_PROCESSOR_ID-1:
           IF i != primary_cpu  AND  (cpu_enabled & (1<<i)):
               start_secondary(i, PAL$PAL_BASE + 1)  // +1 == enter in PAL mode
       // V4 GATE (DS20 crux #2): the SET of CPUs started is gated by the
       // "cpu_enabled" environment variable. The secondary entry target is
       // PAL$PAL_BASE | 1 -- the low bit is the PALmode flag (ties directly to
       // the PC<0> fidelity work; the secondary must begin in PAL mode).

  -> start_secondary(id, address)
       ((uint64*)PAL$CPU0_START_BASE)[id] = address  // per-CPU start vector
       mb()                                          // barrier before poke
       outtig(NULL, 0xC00028 + id, 0)                // TIG write == WAKEUP id
       krn$_sleep(1000)                              // 1s settle, no poll-back
       // V4 GATE (DS20 crux #3): the actual secondary wakeup is one MMIO write
       // through the TIG (outtig) at offset 0xC00028 + id. To start CPU 1 the
       // Tsunami/TIG decode must:
       //   1. accept the write at 0xC00028+id,
       //   2. release CPU id from its console spin,
       //   3. have it begin fetch at ((uint64*)PAL$CPU0_START_BASE)[id] | palmode.
       // Note there is NO handshake-back here: the primary writes, sleeps 1s,
       // and moves on. So a non-responding secondary does NOT hang start_
       // secondary itself -- any hang is later, in the kernel rendezvous that
       // waits on the secondary's per-CPU slot flags (CV/BIP/PV) being set by
       // the secondary console after it wakes. Localize the DS20 stall by
       // whether CPU1 ever takes its wakeup and runs PAL$CPU0_START_BASE[1].

  -> node_halt(id)                          // inter-processor halt
       IF !(in_console & (1<<id)):
           cserve(CSERVE$MP_WORK_REQUEST, id, MP$HALT)   // CSERVE 0x65
           krn$_sleep(5)
       // V4 NOTE: MP halt routes through CSERVE$MP_WORK_REQUEST (0x65), which
       // V4 execCserve currently no-ops (default arm). Fine for reaching the
       // prompt; needed later for "halt <cpu>" console commands.

---

## Phase E -- restart + per-CPU error clear

  -> console_restart()
       pprintf("Initializing...")
       krn$_micro_delay(10000)
       IF compile==SHARK:      MB8574 reset-request toggle
       ELIF compile==SWORDFISH:
           IF SwordFish(): outtig(NULL, 0xE00004, 1)
           ELSE:           MB8574 reset-request toggle
       ELSE:                   outtig(NULL, 0xE00004, 1)   // TIG system reset
       // V4 NOTE: default-platform reset is a single TIG write at 0xE00004=1.

  -> reset_cpu(id) -> clear_cpu_errors(id)
       misc = ReadTsunamiCSR(CSR_MISC); misc |= (1<<28)   // clear NXM
       WriteTsunamiCSR(CSR_MISC, misc)
       WriteTsunamiCSR(PCHIP0_PERROR, ReadTsunamiCSR(PCHIP0_PERROR))  // W1C
       WriteTsunamiCSR(PCHIP1_PERROR, ReadTsunamiCSR(PCHIP1_PERROR))  // W1C
       // V4 NOTE: touches Tsunami CSR_MISC and both Pchip PERROR (write-1-clear).

  -> halt_switch_in()
       s = *(int*)PAL$HALT_SWITCH_IN; *(int*)PAL$HALT_SWITCH_IN = 0; return s
       // Reads-and-clears the front-panel halt latch.

---

## Off-path routines (summarized, not boot-to-prompt)

- arc_to_srm / jump_to_arc / load_arc_fw / arc / rcu / jtopal:
    ARC (AlphaBIOS / NT) firmware switching and SCB load. Only exercised when
    the user switches console personality; not on the SRM cold-boot-to-prompt
    path. Skip for DS20 prompt work.
- show_power / print_environ_data / ev_*_shutdown_temp / validate_temp_value:
    RMC EEPROM + temperature/fan environmental display and shutdown-temp env
    vars (DS20/DS20E server-management card). Display/maintenance only.
- show_cpu: "show cpu" command formatter (uses get_srom_revision). Display only.
- con$checkchar/getchar/putchar (XDELTA build): poll 16550 UART LSR@0x3fd bit0,
    data@0x3f8. The character path for the console terminal, but only compiled
    under XDELTA_ON here; the live console I/O path is in the shared kernel.
- xdelta / fake_bpt / smp$intall_bit_acq / ini$writable / ini$rdonly:
    Debugger / SCB plumbing. fake_bpt is the no-XDELTA breakpoint stub.

---

## V4 modeling checklist distilled from this file (DS20 -> P00>>>)

Ordered by what blocks reaching the prompt first:

1. mfpr_whami() per-CPU      -- HW_MFPR WHAMI returns real id, not 0.   (Phase B)
2. PAL$PRIMARY seeded        -- so primary()/secondary() fork correctly.[CONFIRM]
3. SROM proc_mask seeded     -- impure cns$srom_proc_mask has both CPU bits,
                                signature>>20==0xDEC, so dualCPU()==1.   (Phase B)
4. sysvar member id          -- iic_rcm_temp openable -> DS20 (member 6),
                                else falls back to DP264 single-CPU.     (Phase A)
5. "cpu_enabled" env var     -- secondary bit set so start_secondaries
                                actually iterates CPU 1.                 (Phase D)
6. TIG wakeup decode         -- outtig 0xC00028+id releases CPU id at
                                PAL$CPU0_START_BASE[id] | palmode.       (Phase D)
7. PAL$CPU0_START_BASE       -- writable per-CPU start-vector region.   [CONFIRM]
8. per-CPU slot flag dance   -- secondary console (in shared kernel) sets
                                CV/BIP/PV in its HWRPB slot; primary rendezvous
                                waits on these. This is where a half-modeled
                                secondary actually hangs (start_secondary itself
                                does not block).

Fast short-circuit to P00>>> without real SMP:
  Force dualCPU()==0 by seeding proc_mask with only the primary bit (item 3),
  or clear the secondary bit in "cpu_enabled" (item 5). The console then skips
  Phase D entirely and still prints the MP-format P00>>> prompt (prompt format
  is a console-build property, not a runtime CPU count). Standing up items 6-8
  is the real two-CPU milestone, separable from reaching the prompt.

Diagnostic that localizes the current DS20 stall:
  Does CPU 1 ever fetch from PAL$CPU0_START_BASE[1]?
    - never written / never woken  -> stall is upstream (items 3/4/5: console
      decided single-CPU or cpu_enabled lacks the bit, OR outtig 0xC00028+1
      is not decoded as a wakeup).
    - written + woken, then stalls  -> secondary console runs but the slot-flag
      rendezvous (item 8) never completes -> look in the shared kernel
      secondary path, not pc264.c.
