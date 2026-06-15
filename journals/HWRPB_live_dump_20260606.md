# Live SRM HWRPB — complete annotated dump  (2026-06-06)

Source: predig_coldgct_v3 snapshot · HWRPB base = guest PA 0x2000 (from PAL$HWRPB_BASE)

## Header
```
  +0x000  PHYS_ADDRESS (self)      = 0x0000000000002000
  +0x008  VALID 'HWRPB' magic      = 0x0000004250525748  ascii='........'
  +0x010  REVISION                 = 0x000000000000000e
  +0x018  HWRPB SIZE (bytes)       = 0x0000000000000900
  +0x020  PRIMARY_CPU_ID           = 0x0000000000000000
  +0x028  PAGE_SIZE                = 0x0000000000002000  (8192 = 8 KiB)
  +0x030  PA_SIZE (bits)           = 0x000000000000002c
  +0x038  MAX_ASN                  = 0x00000000000000ff
  +0x040  SYS_SERIALNUM [0]        = 0x0000000000000000   <<< ZERO (unbuilt)
  +0x048  SYS_SERIALNUM [1]        = 0x0000000000000000   <<< ZERO (unbuilt)
  +0x050  SYS_TYPE                 = 0x0000000000000022
  +0x058  SYS_VARIATION            = 0x0000000000001c05
  +0x060  SYS_REVISION             = 0x0000000000000000
  +0x068  INTCLK_FREQ              = 0x0000000000400000
  +0x070  CYCLE_CNT_FREQ (Hz)      = 0x000000000fe502aa  (266.667 MHz)
  +0x078  VPTB                     = 0x0000000200000000
  +0x080  reserved                 = 0x0000000000000000
  +0x088  TBHB_OFFSET              = 0x0000000000000140
  +0x090  NUM_CPU_SLOTS            = 0x0000000000000001
  +0x098  CPU_SLOT_SIZE            = 0x0000000000000280
  +0x0a0  CPU_SLOT_OFFSET          = 0x0000000000000180
  +0x0a8  NUM_CTB                  = 0x0000000000000001
  +0x0b0  CTB_SIZE                 = 0x0000000000000160
  +0x0b8  CTB_OFFSET               = 0x0000000000000400
  +0x0c0  CRB_OFFSET               = 0x0000000000000560
  +0x0c8  MEMDSC_OFFSET            = 0x00000000000005c0
  +0x0d0  CONFIG_OFFSET            = 0x0000000000036740
  +0x0d8  FRU_OFFSET               = 0x0000000000000000   <<< ZERO (unbuilt)
  +0x0e0  SAVE_TERM rtn            = 0x0000000000000000
  +0x0e8  SAVE_TERM arg            = 0x0000000000000000
  +0x0f0  RESTORE_TERM rtn         = 0x0000000000000000
  +0x0f8  RESTORE_TERM arg         = 0x0000000000000000
  +0x100  REST_TERM_RTN            = 0x0000000000000000
  +0x108  REST_TERM_VAL            = 0x0000000000000000
  +0x110  RESERVED_OS              = 0x0000000000000000
  +0x118  RESERVED_HW              = 0x0000000000000000
  +0x120  CHECKSUM (+0..+0x118)    = 0x00000044607b3d54
  +0x128  RX_RDY                   = 0x0000000000000000
  +0x130  TX_RDY                   = 0x0000000000000000
  +0x138  DSRDB_OFFSET             = 0x0000000000000840
  +0x140  reserved                 = 0x0000000000080000
```

**Checksum:** computed(sum +0..+0x118)=0x00000044607b3d54  stored=0x00000044607b3d54  -> MATCH ✓

## Per-CPU SLOT @0x2180 (size 0x280)
```
  +0x000 = 0x0000000000000000
  +0x008 = 0x0000000000000000
  +0x010 = 0x0000000000000000
  +0x018 = 0x0000000000000000
  +0x020 = 0x0000000000000000
  +0x028 = 0x0000000000000000
  +0x030 = 0x0000000000000000
  +0x038 = 0x0000000000000000
  +0x040 = 0x0000000000000000
  +0x048 = 0x0000000000000000
  +0x050 = 0x0000000000000000
  +0x058 = 0x0000000000000000
  +0x060 = 0x0000000000000000
  +0x068 = 0x0000000000000000
  +0x070 = 0x0000000000000000
  +0x078 = 0x0000000000000000
```

## CTB (Console Terminal Block) @0x2400 (size 0x160)
```
  +0x000 = 0x0000000000000004
  +0x008 = 0x0000000000000000
  +0x010 = 0x0000000000000000
  +0x018 = 0x0000000000000160
  +0x020 = 0x0000000000000015
  +0x028 = 0x0000000000000000
  +0x030 = 0x0000000000000000
  +0x038 = 0x0000000000000002
  +0x040 = 0x0000000000000000
  +0x048 = 0x0000000000000000
  +0x050 = 0x0000000000000000
  +0x058 = 0x0000000000000000
```

## CRB (Console Routine Block) @0x2560
```
  +0x000 = 0x0000000000000000
  +0x008 = 0x00000000001c5900
  +0x010 = 0x0000000000000000
  +0x018 = 0x00000000001c5910
  +0x020 = 0x0000000000000002
  +0x028 = 0x0000000000000366
  +0x030 = 0x0000000010000000
  +0x038 = 0x0000000000002000
```

## MEMDSC (Memory Descriptors) @0x25c0
```
  +0x000 checksum   = 0x00000000000002ed
  +0x008 impure_pa  = 0x0000000000000000
  +0x010 cluster_cnt= 3
   cluster[0] PFN=0x0 count=0x2e9 tested=0x0 usage=0x0
   cluster[1] PFN=0x0 count=0x1 tested=0x2e9 usage=0x1c99
   cluster[2] PFN=0x0 count=0x3f16000 tested=0x1ffff8d usage=0x1f82
```

## CONFIG Data Table @0x38740 (CONFIG_OFFSET=0x36740) — first 0x40 bytes
```
  +0x000 = 0x0000000000000000   ascii='CNFGTBL.'
  +0x008 = 0x0000000000000000   ascii='........'
  +0x010 = 0x0000000000000000   ascii='.... ...'
  +0x018 = 0x0000000000000000   ascii='........'
  +0x020 = 0x0000000000000000   ascii='.......n'
  +0x028 = 0x0000000000000000   ascii='....y...'
  +0x030 = 0x0000000000000000   ascii='......k.'
  +0x038 = 0x0000000000000000   ascii='....l...'
```

## Verdict
- HWRPB header self-consistent; boot-critical offsets (SLOT/CTB/CRB/MEMDSC) all point to populated structures.
- SYS_SERIALNUM, FRU_OFFSET, DSRDB_OFFSET are zero — the FRU/DSR/serial cluster is unbuilt (no SMBus FRU EEPROM data).

## CORRECTIONS (after full sub-structure decode)
- **Checksum (+0x120) MATCHES** the firmware-stored value -> header provably valid.
- **DSRDB is PRESENT** (DSRDB_OFFSET @+0x138 = 0x840, DSRDB @PA 0x2840):
  SMM=0x72f, model name "AlphaServer DS10 266 MHz" at PA 0x28b0. (Earlier
  "DSRDB=0" was a mis-read of a reserved zero.)
- **Per-CPU SLOT @0x2180 is POPULATED** (proc type/variation/PAL-rev at +0xa8/
  +0x1d0/+0x1e0 = 0x0001005300010162 etc.); the first 0x80 bytes are the
  bootstrap/halt fields and are legitimately zero pre-boot.
- Model-name TEMPLATE strings live in the firmware image at PA 0x1a0a38
  ("AlphaServer DS10 %3d MHz") / 0x1a0a78 (DS10L) -- formatted into the DSRDB.
- **NET GAP = exactly two zeroed fields: SYS_SERIALNUM (+0x40) and FRU_OFFSET
  (+0xd8).** Everything else (slot, CTB, CRB, MEMDSC, DSRDB, CONFIG) is built
  and sound. The fix is just a store target for the serial (FRU table / serial
  EEPROM); `set sys_serial_num' fails only because that target doesn't exist.
