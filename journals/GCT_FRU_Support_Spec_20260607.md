# GCT / FRU Configuration-Tree Support -- Specification and Architectural Design
# EmulatR V4 -- 2026-06-07
# Author: Claude (Anthropic), with T. Peer (project architect)
# Status: DESIGN. No code rides with this doc (house rule: discuss before code).
# Scope: enable the guest SRM to build a complete GCT FRU branch so the
#        `set sys_serial_num' / `show fru' / `show error' / `clear_error all'
#        command family terminates instead of hanging.

--------------------------------------------------------------------------------
## 0. One-paragraph summary

The `set sys_serial_num' hang is NOT an EEPROM-content problem and is NOT a bug
in any GCT walker EmulatR owns. The guest firmware builds its own configuration
tree; for the DS10 (PC264 family) that build runs in gct_init$pc264_hw(), a
strictly sequential builder that EARLY-RETURNS on the first sub-step that fails
and whose return value gct() then IGNORES -- so any failure silently truncates
the tree. The FRU_ROOT node (type 0x14) is created at sub-step 6 of that builder.
Sub-step 5, build_power_hw(), requires a readable byte from IIC system-status
registers iic_system0 (0x70) and iic_system1 (0x72); EmulatR models neither, so
build_power_hw() returns failure and pc264_hw() truncates BEFORE FRU_ROOT is
created. With no FRU_ROOT, gct$find_node(NODE_FRU_ROOT) returns handle 0 and the
serial/show-fru walk loops forever. The minimal faithful fix is to model the two
IIC system-status registers; the complete fix is to make every pc264_hw sub-step
find a faithful environment.

--------------------------------------------------------------------------------
## 1. Confirmed root-cause chain (source-traced 2026-06-07)

Files: apisrm galaxy_config_tree.c (uploaded), galaxy_pc264.c, dp264_fru.c,
iic_driver.c (Processor Support ref tree).

1.1  The hang loop (dp264_fru.c gct_sys_serial_write, identical in show fru /
     show error / clear_error all):

         gct$find_node(FIND_BY_TYPE, NODE_FRU_ROOT);   // type 0x14
         pNode = _GCT_MAKE_ADDRESS(fru_root);          // 0 -> GalaxyRoot if NOT found
         while (pNext && j < 2) {                       // "until FRU_ROOT seen twice"
             gct$find_node(FIND_ANY, pNext);
             if (pNext->type == 0x14) j++;
         }

     With no type-0x14 node in the tree the visit counter never reaches 2 and the
     walk restarts from root forever. The cyclic appearance is the search
     restarting, not a malformed link (earlier "cyclic GCT" reads are SUPERSEDED).

1.2  The DS10 build path (galaxy_config_tree.c gct(), #if PC264):

         gct_init$init_tree(...)
         gct_init$populate_tree(regatta_tree)   // static HW skeleton from outline
         gct_init$pc264_hw()                     // <-- return value IGNORED (line 396)
         gct_init$configure_galaxy(...)          // SW community/partition
         gct_init$pc264_sw()

     Because pc264_hw()'s return is discarded, a failure inside it does not stop
     gct(); the SW nodes (Community, Partition) still get built afterward. That is
     why the observed tree HAS those SW nodes yet is missing the FRU branch -- the
     presence of SW nodes does NOT prove pc264_hw() completed.

1.3  gct_init$pc264_hw() (galaxy_pc264.c:158) is sequential with early-return on
     every sub-step; the relevant ordering is:

         step 1  build_smb_hw(hw_root)              SMB HW node
         step 2  build_cpu_hw(hw_root, i)  per CPU  reads iic_cpu%d (HARD: returns 0
                                                    if fopen fails), HWRPB SLOT
         step 3  build_iop_hw(hw_root, i)  per IOP  count = (CSR_CSC>>14 & 3)+1;
                                                    reads PCHIPn_* CSRs
         step 4  build_memory_sub_hw(hw_root)       MEMORY_SUB HW node
         step 5  build_power_hw(hw_root)            iff (SYSVAR>>10)!=1; reads
                                                    iic_system0 (0x70) + iic_system1
                                                    (0x72); returns failure if neither
                                                    read yields a byte  <== TRUNCATION
         step 6  build_fru_root(hw_root)            CREATES FRU_ROOT (type 0x14) +
                                                    base FRU_DESC, UNCONDITIONALLY
                                                    from HWRPB/DSRDB + ev sys_serial_num
                                                    + fw_rev -- NO device read
         step 7  build_smb_fru(...)                 FRU_DESC; reads iic_smb0 (0xA2)
                                                    INSIDE `if(fopen!=0)' -- TOLERANT,
                                                    returns SUCCESS even if absent
         step 8+ build_cpu_fru / build_memory_fru / build_pci_fru / build_power_fru

1.4  Observed tree (prior STOREWATCH): GalaxyRoot, HwRoot, SwRoot, SMB, CPU, IOP,
     Hose, Bus, IoCtrl, MemSub, MemCtrl, MemDesc, Community, Partition -- i.e.
     steps 1-4 SUCCEEDED, NO power node, NO FRU_ROOT. That pattern is exactly
     consistent with truncation at step 5 (build_power_hw), before step 6.

1.5  Why the EEPROM theory was wrong (DEFINITIVE):
     - build_fru_root (step 6, the FRU_ROOT carrier) never reads the IIC bus.
     - build_smb_fru (step 7) wraps its iic_smb0 read in `if(fopen!=0)' and
       returns SUCCESS regardless -- absent EEPROM just yields empty TLVs.
     Therefore seeding the 0xA0..0xAE JEDEC bank cannot create FRU_ROOT. The
     EMULATR_FRU_EEPROM bank is descriptor enrichment for `show fru' CONTENT, a
     LATER concern, not the unblock. (It is also currently un-wired: TsunamiChipset
     declares `IicPcf8584 m_iic;' default-constructed enableFruBank=false, and the
     env gate is read only in test_iicpcf8584.cpp.)

--------------------------------------------------------------------------------
## 2. The build_power_hw failure, exactly

galaxy_pc264.c:1574-1583:

    if ((fp = fopen("iic_system0","sr")) != 0) { status = fread(&fail_register,1,1,fp); fclose(fp); }
    if ((fp = fopen("iic_system1","sr")) != 0) { status = fread(&func_register,1,1,fp); fclose(fp); }
    if (!status) return(status);          // status==0 (no byte read) -> FAILURE

`status` is the fread byte-count; it is set ONLY inside the fopen-guarded blocks.
If EmulatR does not answer 0x70/0x72 with a readable byte, status stays 0 and
build_power_hw returns 0. Note it has already inserted a partial POWER_ENVIR node
(line 1546) before returning, so the node accounting (pRoot->first_free /
available) is left mid-update -- a second reason to fix the cause, not paper over it.

iic_driver.c iic_node_list (#if PC264) defines these as IIC_LED_TYPE, size 1:
    "iic_system0", IIC_LED_TYPE, 1, 1, 0x70, 0, 0, 0
    "iic_system1", IIC_LED_TYPE, 1, 1, 0x72, 0, 0, 0
They are NOT gated by WEBBRICK, so the DS10 firmware always probes them.

EmulatR IicPcf8584 today: eepromIndex() recognizes only the FRU bank 0xA0..0xAE
(when enabled) and the RCM NVRAM bank 0xC0..0xCE. Addresses 0x70/0x72 fall through
to -1 -> NAK -> the guest's iic transaction fails -> fopen/fread fail -> status 0.

Semantics of the two bytes (from the bit tests below the read):
    fail_register (0x70):  bit ps_present[0]=7, fan_ok={5,1}, bit0 = temp-sensor present
    func_register (0x72):  bit ps_ok[0]=5, bit1 = fan func
A byte pair indicating "power present and OK, fans OK, no thermal fault" lets the
subpacket builders run without asserting faults. A pragmatic faithful seed is
fail_register = 0x00 (nothing failed/all present per active-low reading) and
func_register = 0x00, then refine against the bit tests (Section 5.2) so the
sensor subpackets that DO get built are self-consistent. Exact polarity must be
confirmed against the build_power_hw bit logic and, ideally, a real DS10 dump.

--------------------------------------------------------------------------------
## 3. What "full support" means -- two strategies

### Strategy A -- Faithful environment (RECOMMENDED)
Make every pc264_hw sub-step find the state it reads, so the guest firmware builds
the entire FRU branch itself, byte-for-byte as on real hardware. This is the only
approach that also fixes `show fru', `show error', `show power', `clear_error all'
and is correct under SMP.

Dependency surface that pc264_hw reads from EmulatR (the full list to satisfy):

    sub-step            EmulatR dependency                        status in V4
    -----------------   ---------------------------------------   ----------------
    build_smb_hw        intig(0xE00006) TIG rev; CSR_MISC, CSR_   CSRs modeled;
                        DREV, PCHIP0/1_PCTL                       verify TIG path
    build_cpu_hw        iic_cpu%d EEPROM (HARD fopen!=0 req'd);   GAP if firmware
                        HWRPB SLOT_OFFSET data                    is non-WEBBRICK
    build_iop_hw        (CSR_CSC>>14 & 3)+1 IOP count; PCHIPn_*   CSRs modeled;
                        WSBA/WSM/TBA/PCTL/PERRMASK CSRs           verify CSC field
    build_memory_sub_hw MEMORY_SUB node (no device read)         OK
    build_power_hw      iic_system0 (0x70), iic_system1 (0x72)    GAP -- THE BLOCKER
    build_fru_root      HWRPB DSRDB NAME_OFFSET; ev_read(sys_     OK (HWRPB + env
                        serial_num); fw_rev (v_variant..seq)      present)
    build_smb_fru       iic_smb0 (0xA2) -- TOLERANT               optional content
    build_cpu_fru       iic_cpu%d -- per CPU                      optional content
    build_memory_fru    DIMM / SPD presence                      verify
    build_pci_fru       get_matching_pb() PCI bus walk (skips    GAP -- depends on
                        hose0/bus0 slot5 ISA + slot6 SCSI)        PCI enumeration

Order of EmulatR work implied by the table: 0x70/0x72 is the single gap on the
critical path to FRU_ROOT. build_cpu_hw's hard iic_cpu%d requirement is the next
gate IF the shipped image is non-WEBBRICK (the cold-boot IIC trace settles this;
see Section 6). build_pci_fru depends on the deferred PCI-enumeration work but is
AFTER FRU_ROOT, so it does not block the hang fix -- a build_pci_fru failure only
truncates the per-option FRU descriptors, not FRU_ROOT itself.

### Strategy B -- Emulator-side injection (FALLBACK, not recommended as primary)
After gct() returns, EmulatR post-processes the tree to splice in a synthetic
FRU_ROOT + minimal FRU_DESC, or intercepts gct$find_node(NODE_FRU_ROOT) to hand
back a fabricated handle. Unblocks `set sys_serial_num' quickly but is not
faithful, must track the GCT node layout by hand, and risks divergence from guest
expectations (the walk also reads FRU_DESC TLVs). Keep only as a stopgap if a
faithful step proves unexpectedly deep.

RECOMMENDATION: Strategy A, beginning with the 0x70/0x72 model. It is small,
on the critical path, faithful, and SMP-safe.

--------------------------------------------------------------------------------
## 4. Architectural design -- IIC system-status register model

Place in deviceLib/Tsunami/IicPcf8584.h (the existing PCF8584 model), consistent
with the existing FRU/RCM bank handling. These are IIC_LED_TYPE devices (1 byte,
read-mostly status), NOT 256-byte EEPROMs, so they need a distinct, tiny code path
rather than an entry in the 256-byte m_eeprom bank.

4.1  Address recognition
     Extend the transaction decode so even node addresses 0x70 and 0x72 are
     recognized as present (ACK) and return a single status byte on read. Keep
     them ALWAYS present (no enable gate): they are board-level status registers,
     not optional modules, and build_power_hw needs them on every cold boot.

4.2  State
     Two bytes, m_sysFail (0x70) and m_sysFunc (0x72), initialized to a
     "healthy" seed (Section 5.2). Read returns the byte; the pipelined-receiver
     P2 dummy-first-read contract already implemented for the EEPROM path applies
     identically (first S0 read after address is the dummy, second yields the
     byte). Writes (if any) are absorbed.

4.3  Determinism
     Constant bytes, no host coupling, no cycle source -- same determinism note as
     the rest of IicPcf8584. The values are part of the emulated board identity.

4.4  Snapshot
     If the bytes remain constant they need no serialization (guest re-reads each
     boot). If later made writable (e.g. fault injection), bump kChipsetVersion and
     add them to the IicPcf8584 image alongside the FRU bank.

4.5  Trace
     Reuse the existing EMULATR_IIC_TRACE gate: log 0x70/0x72 transactions in the
     same IIC-TXN / IIC-RD format so a cold boot shows build_power_hw reading them.

--------------------------------------------------------------------------------
## 5. Implementation plan (phased)

PHASE 0 -- Confirm the failing step (NO code; uses already-built gates)
  Run one cold boot with EMULATR_IIC_TRACE=1 + EMULATR_GCT_WATCH=1 (--no-autoload).
  Expected if this analysis is correct:
    - IIC-TXN shows the firmware probing 0x70 and 0x72 and getting NAK.
    - GCT-STOREWATCH shows NO type-0x14 store (FRU_ROOT never created).
    - (Bonus) IIC-TXN shows whether iic_cpu0 (0xA4) is probed+ACKed -- decides
      WEBBRICK vs non-WEBBRICK and whether build_cpu_hw is a second gate.
  This run is the gate for committing Phase 1. If 0x70/0x72 are NOT probed, the
  truncation is elsewhere (build_fru_root internal, or build_power_hw skipped via
  SYSVAR member-id==1) and Phase 1 changes accordingly.

PHASE 1 -- Model iic_system0/1 (the minimal faithful unblock)
  Implement Section 4. Seed bytes per 5.2. Rebuild, run doctests, then a cold boot:
  expect build_power_hw to succeed, a POWER_ENVIR node to appear, pc264_hw to reach
  build_fru_root, and a type-0x14 FRU_ROOT store to land. Re-mint the coldgct + >>>
  snapshots after this lands (the tree shape changes).

PHASE 2 -- Validate the command family from the >>> snapshot
  Restore the >>> snapshot, EMULATR_GCT_WATCH=1, inject `set sys_serial_num TEST123'
  via plink. Expect termination (no runaway walk) and the serial to take. Repeat for
  `show fru', `show error', `clear_error all'.

PHASE 3 -- Descriptor content (optional, faithful enrichment)
  Wire the EMULATR_FRU_EEPROM gate properly (pass an enable flag into m_iic from
  TsunamiChipset, reading the env once) so build_smb_fru / build_cpu_fru populate
  real TLVs and `show fru' shows manufacturer/model/serial. This is cosmetic for
  the hang but completes fidelity. Also revisit build_cpu_hw's hard iic_cpu%d
  requirement if Phase 0 shows it is a second gate.

PHASE 4 -- PCI option FRU descriptors (defer; depends on PCI enumeration)
  build_pci_fru walks get_matching_pb over hose/bus/slot/func. This rides the
  separate deferred PCI-enumeration + on-board-device work. Not on the hang path
  (it is after FRU_ROOT); a build_pci_fru failure only drops per-option descriptors.

--------------------------------------------------------------------------------
## 5.2 Seed-byte derivation (build_power_hw bit logic)

From galaxy_pc264.c:1534-1631:
    ps_present[] = {7,4};  ps_ok[] = {5,6};  fan_ok[] = {5,1};
    DC-power subpkt built when:  !(fail_register & (1<<7)) && !(func_register & (1<<5))
    Fan subpkt (i=0,1) built when: (func_register & (1<<1)) || (fail_register & (1<<fan_ok[i]))
    Temp subpkt built when:        fail_register & (1<<0)
Interpretation: bits are a mix of "present"/"ok"/"fail" in fail_register and
"ok/func" in func_register. The build does NOT fault on any value -- it only
chooses which sensor subpackets to emit -- so the ONLY hard requirement is that at
least one fread returns a byte (status!=0). Therefore the safe initial seed is any
defined byte pair; fail_register=0x00, func_register=0x00 is the simplest and
yields a power node with zero or minimal sensor subpackets, which is sufficient to
pass step 5 and reach FRU_ROOT. Refine later for a realistic `show power'. CONFIRM
polarity against a real DS10 EEPROM/register dump before claiming fidelity.

--------------------------------------------------------------------------------
## 6. Open questions / confirmations needed

Q1  WEBBRICK or not?  iic_node_list gates iic_cpu0(0xA4)/iic_cpu1(0xAC) under
    `#if !WEBBRICK'. If the shipped ds10_v7_3 is WEBBRICK, build_cpu_hw's
    fopen("iic_cpu0") would fail and truncate at step 2 -- but the observed tree
    HAS a CPU node, implying step 2 succeeded, implying either non-WEBBRICK or
    that the CPU node seen comes from populate_tree(regatta_tree) not pc264_hw.
    Phase-0 IIC trace resolves this directly.

Q2  SYSVAR member id.  build_power_hw runs only if (hwrpb->SYSVAR[0]>>10)!=1. If
    EmulatR's HWRPB SYSVAR encodes member-id 1, step 5 is SKIPPED and the
    truncation must be at step 6 (build_fru_root internal) -- a different fix.
    Confirm the SYSVAR value EmulatR presents.

Q3  populate_tree vs pc264_hw duplication.  Both run for PC264. Need to confirm
    which branch each observed node belongs to so we do not double-build. Likely
    populate_tree lays the SW/galaxy skeleton and pc264_hw the HW resource +
    FRU branch; verify against the GCT-STOREWATCH node-creation order.

--------------------------------------------------------------------------------
## 7. Verification matrix

    check                            method                              pass
    ------------------------------   ---------------------------------   --------
    0x70/0x72 probed at cold boot    EMULATR_IIC_TRACE IIC-TXN lines     ACK seen
    FRU_ROOT created                 EMULATR_GCT_WATCH type-0x14 store   present
    pc264_hw reaches build_fru_root  POWER_ENVIR node + FRU_ROOT exist   both
    set sys_serial_num terminates    >>> snapshot + plink inject         no hang
    show fru / show error            >>> snapshot + plink inject         no hang
    doctest suite                    ctest                               all green
    no boot regression               cold boot to >>>                    reaches >>>

--------------------------------------------------------------------------------
## 8. Risk register

R1  Polarity of the 0x70/0x72 status bytes wrong -> build_power_hw builds bogus
    sensor subpackets but still returns SUCCESS (non-fatal); refine in Phase 3.
R2  A SECOND upstream gate (build_cpu_hw iic_cpu%d) also fails -> Phase 1 alone
    will not reach FRU_ROOT; Phase 0 trace surfaces this before we commit.
R3  Truncation actually at build_fru_root (member-id==1 path) -> different fix;
    Phase 0 trace + Q2 confirmation prevents building the wrong thing.
R4  Re-mint cost: changing the HW tree invalidates the coldgct/>>> snapshots;
    budget the multi-hour cold re-mint after Phase 1.
R5  Strategy-B temptation: do not splice a synthetic FRU branch unless a faithful
    step proves intractable; injection diverges from the guest's own walk reads.

--------------------------------------------------------------------------------
## 9. Bottom line

The faithful unblock is small and specific: model two IIC system-status registers
(0x70/0x72) returning a defined status byte, gated behind a Phase-0 confirmation
run that already needs no new code. That lets the guest's own gct_init$pc264_hw()
run to completion, build FRU_ROOT, and make the entire `set/show fru/show error'
family work -- without EmulatR ever owning the GCT layout. The EEPROM bank is
demoted to optional descriptor content (Phase 3), and PCI option FRUs ride the
separate, already-deferred PCI-enumeration work (Phase 4).
