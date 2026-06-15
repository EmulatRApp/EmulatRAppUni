# FRU EEPROM Fix Plan -- `set sys_serial_num` / `show fru` family (2026-06-06)

Status: DESIGN. No code rides with this doc (house rule: discuss before code).
Diagnosis is COMPLETE and triangulated (see project_gct_cyclic_link_set_hang
memory + HWRPB_live_dump_20260606.md). This plan is the agreed scope for the
implementation, which is a NEXT-SESSION task (needs address confirmation + a
multi-hour cold-boot test).

## 1. Root cause (confirmed)

`gct_sys_serial_write` (apisrm dp264_fru.c), and identically `show fru`,
`show error`, `clear_error all`, all run:

    gct$find_node(FIND_BY_TYPE, NODE_FRU_ROOT);   // type 0x14
    pNode = _GCT_MAKE_ADDRESS(fru_root);          // 0 -> root when NOT FOUND
    pNext = pNode; j = 0;
    while (pNext && (j < 2)) {                     // "until FRU_ROOT seen twice"
        gct$find_node(FIND_ANY, pNext);
        if (pNext->type == 0x14) j++;             // FRU_ROOT visit counter
    }

The GCT has NO FRU_ROOT(0x14)/FRU_DESC(0x15) nodes, so find_node returns
handle 0 -> _GCT_MAKE_ADDRESS(0) = the GalaxyRoot, and the visit counter never
reaches 2 -> infinite walk. The cyclic traversal is BY DESIGN (the loop bounds
itself by counting FRU_ROOT visits); it is starved because the FRU subtree was
never built. HWRPB confirms from the other side: FRU_OFFSET=0, SERIALNUM=0
(DSRDB is fine). This is a whole command family, not serial-specific.

## 2. Why the FRU subtree is empty

The subtree is built by gct()/build_fru reading three I2C JEDEC FRU EEPROMs.
V4's IicPcf8584 models the PCF8584 + the RCM NVRAM bank (0xC0..0xCE) but
the FRU "module" EEPROMs (0xA0..0xAC) deliberately NAK = "module absent", so
read_jedec's fopen() fails and no FRU nodes are created.

DS10/WEBBRICK FRU devices (apisrm iic_driver.c iic_node_list, WEBBRICK rows --
ADDRESSES NEED EMPIRICAL CONFIRMATION against the shipped image; source drifts):
    iic_smb0  ~ I2C 0xA2   (system motherboard; the FRU_ROOT carrier)
    iic_cpu0  ~ I2C 0xA4
    iic_cpu1  ~ I2C 0xAC
Confirm by store-watching the FRU probe on a cold boot (same method used to
pin the PCF8584 base 0xFFFF0000).

## 3. JEDEC 256-byte EEPROM layout (authoritative: apisrm jedec_def.sdl)

Offsets (byte), struct _JEDEC; three checksum segments (JEDEC-21C = sum of the
covered bytes, keep low byte):

    0x00..0x3E  fru_spec[63]              (segment 1 data)
    0x3F        checksum0to62             (sum 0x00..0x3E)
    0x40..0x47  manuf_JEDEC_ID[8]
    0x48        manuf_location            (1=NI Salem,2=AY Ayr,3=ZG,4=S3)
    0x49..0x4B  manuf_part_class[3]
    0x4C..0x51  manuf_part_base[6]
    0x52..0x54  manuf_part_variation[3]
    0x55..0x58  manuf_part_revision[4]
    0x59..0x5A  manuf_part_space[2]
    0x5B..0x5C  revision_code[2]
    0x5D        manuf_date_y
    0x5E        manuf_date_m
    0x5F..0x62  assembly_serialnum[4]
    0x63..0x72  manuf_spec_alias[16]
    0x73..0x7C  manuf_spec_model[10]
    0x7D        manuf_spec_dec_JEDEC_ID   (0xB5 = DEC, 0x4A = CPQ)
    0x7E        revision_ro_data          (0x20 = rev V2.0)
    0x7F        checksum64to126           (sum 0x40..0x7E)
    0x80..0x8F  sys_serialnumb[16]        <-- set sys_serial_num target
    0x90..0x93  tdd_log_head[4]
    0x94..0xA3  tdd_log_data[16]
    0xA4        sdd_log_ctrl
    0xA5..0xD0  sdd_log_block0[44]
    0xD1..0xFC  sdd_log_block1[44]
    0xFD        dec_flag_id               (0x4A)
    0xFE        rev_rw_area               (0x21 = rev V2.1)
    0xFF        checksum128to254          (sum 0x80..0xFE)

A minimum VALID FRU image: zero-fill, set manuf_location, a part class string,
manuf_spec_dec_JEDEC_ID=0xB5, the two revision bytes, recompute the three
checksums. (Mirror what `buildfru` writes; build_fru.c is the reference.)

## 4. V4 implementation (extends IicPcf8584)

Attach point: deviceLib/Tsunami/IicPcf8584.h -- the EEPROM bank
(kEepromNodeBase=0xC0, eepromIndex(), m_eeprom[], eepromData()).

Plan:
  a. Add a SECOND EEPROM region for the FRU module addresses 0xA0..0xAC (or
     generalize the bank to a node->index map covering both 0xA0.. and 0xC0..).
     Each 256-byte, byte-addressed, R/W, persists offset between transactions
     (existing state machine already does this for the 0xC0 bank).
  b. Pre-seed iic_smb0 (the FRU_ROOT carrier) with a valid JEDEC image via the
     existing eepromData() accessor at construction; cpu0/cpu1 optional in the
     minimal tier (NAK -> "CPU FRU absent", non-fatal).
  c. NO new chipset wiring needed -- the PCF8584 is already on the PCI-mem seam
     (0xFFFF0000); we are only widening which slave addresses ACK.

Tiers:
  MINIMAL  -- iic_smb0 only, valid JEDEC. gct() builds FRU_ROOT + one FRU_DESC;
              the find-FRU_ROOT loops terminate; `show fru`/`set sys_serial_num`
              stop hanging. Smallest unhang.
  FULL     -- smb0 + cpu0 + cpu1, writable AND persisted so `set`/`buildfru`
              survive reboot. Persistence can ride the flash-snapshot pattern
              landed this session (serialize the FRU EEPROM bank), or a sidecar
              file, or a buildfru-equivalent re-seed at boot.

## 5. Verification

  1. Cold boot with EMULATR_GCT_WATCH=1 (range already 0x3f32000..0x3f33fff):
     confirm FRU_ROOT(0x14) + FRU_DESC(0x15) nodes now appear in the replayed
     tree (they were absent before).
  2. Restore the post-build snapshot, plink `set sys_serial_num TEST123`:
     confirm the gct$find_node loop TERMINATES (j reaches 2) -- no spin.
  3. `show fru` renders a row; `show config` displays the serial (HWRPB
     SERIALNUM now populated from the FRU via gct()).
  4. Doctest: extend test_iicpcf8584.cpp with a FRU-address ACK + JEDEC
     readback + checksum-valid case (CHECK only).

## 6. Open items before coding
  - Confirm the three FRU I2C addresses empirically (store-watch; source drifts).
  - Decide MINIMAL vs FULL for the first landing (recommend MINIMAL first).
  - Decide persistence mechanism for FULL (flash-snapshot serialize vs sidecar).
  - Pin how gct() parses a JEDEC EEPROM into a FRU_DESC (boot-time builder;
    build_fru.c is the writer reference, the reader is in gct()/galaxy_config_tree).
