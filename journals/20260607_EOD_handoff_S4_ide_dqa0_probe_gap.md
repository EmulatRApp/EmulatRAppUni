# EOD Handoff — 2026-06-07 — S4 IDE Wiring Complete; dqa0 Probe Gap Found

## Status: S4 (IDE scaffold) DONE on the wiring/enumeration axis; one probe-protocol gap remains before dqa0 enumerates.

NOTE: Findings below were partly RECOVERED from a crashed session
(local_3eacc01a, "IicPcf8584 trace memory update") whose results were
never persisted before the crash. Recovered via session transcript +
re-confirmed live at the `>>>` prompt tonight.

---

## What landed

### Cy82C693Ide func1 wired into TsunamiChipset.h (3 edits)
1. **Includes** (after the IicPcf8584 include):
   `#include "deviceLib/Tsunami/Cy82C693Ide.h"`
   `#include "deviceLib/scsi/VirtualIsoDevice.h"`
2. **Registration** in `wireDevices()` (~line 547, after the COM2 line):
   - `m_ide.attachDevice(0, 0, &m_cdrom);`            // primary master = ATAPI CD
   - `m_pchip.registerPciDevice(0, 5, 1, &m_ide);`     // func 1 config space
   - `m_pchip.registerIoPortRange(0x1F0, 0x1F8, &m_ide);`  // primary command block
   - `m_pchip.registerIoPortRange(0x170, 0x178, &m_ide);`  // secondary command block
   - `m_pchip.registerIoPortRange(0x3F6, 0x3F7, &m_ide);`  // primary alt-status/control
   - `m_pchip.registerIoPortRange(0x376, 0x377, &m_ide);`  // secondary alt-status/control
3. **Members** (after `m_com2`; order matters — both after `m_pchip`,
   and `m_cdrom` before `m_ide` since `m_ide` holds a raw ptr to it):
   - `scsi::VirtualIsoDevice m_cdrom;`   // no-media ATAPI CD
   - `Cy82C693Ide            m_ide;`     // CY82C693 IDE controller (func 1)

### Verification
- `test_ide_wiring`: ALL 3 cases green — func1 enumerates (vendor 0x1080 /
  device 0xC693 at bus0/dev5/func1) AND legacy ports 0x1F0/0x170/0x3F6/0x376
  route to `m_ide`. No Cypress `setIoPortHandler` catch-all shadowing of
  0x1F0 → no dispatch-precedence fix needed.
- Full suite: **407 tests / 1679 assertions, SUCCESS.**
- Test had a self-bug (fixed): class-code byte shifts at reg 0x08 4-byte
  read — subclass is `>>16`, base class `>>24` (had `>>8`/`>>16`).

### Live at `>>>`
- Reached the prompt via `update srm` → AlphaServer DS10 Console **V7.3-2**.
- `sys_serial_num test123` **persisted across a full reboot** → the
  2026-06-06 GCT/FRU cyclic-link fix is holding in flash NVRAM.
- `set oem_string snapshot` accepted; `show oem_string` reads back `snapshot`.

---

## Two negative results (tonight)

### 1. `show device` lists ONLY `dva0` — dqa0 did NOT enumerate
The CD is wired and unit-testable, but the firmware probe does not discover
it. The unit test proved **config-space enumeration + legacy-port routing**;
it did NOT exercise the **ATA/ATAPI probe protocol**: soft reset, the ATAPI
signature handshake (0x14 / 0xEB in cyl-low / cyl-high after reset), and
`IDENTIFY PACKET DEVICE`. `dq_driver` only registers `dqa0` if that handshake
returns correctly; dva0-only means it read `0xFF` status or the wrong
signature and skipped the channel.

**NEXT (read-only diagnostic):** I/O trace on 0x1F0–0x1F7 during the
cold-boot probe window — capture what `dq_driver` READS BACK.
- status == 0xFF → `Cy82C693Ide` never clears BSY / asserts DRDY (no ready
  taskfile) — fix in the controller.
- status OK but cyl bytes != 0x14/0xEB → ATAPI signature not set on reset —
  fix in `VirtualIsoDevice`.

### 2. No `predig_oemsnap` snapshot created
`set oem_string snapshot` produced no snapshot file. LIKELY run-config, not a
code bug: the trigger keys off `EMULATR_CONSOLE_SNAPSHOT=1`, present in the
scripted cold-boot cmd but probably absent on the interactive plink run.
Confirm by grepping the run log for `console-snapshot: marker matched`; only a
wiring bug if that line never fired WITH the env var set.

---

## Resume next session
1. dqa0 probe-gap I/O trace (read-only) → split controller vs signature fix.
2. Confirm the snapshot env-var theory (grep the log) before touching code.
3. Boot-time delta vs the old ~45–60 min dva0 stretch — still unmeasured;
   the CD-search loop break only pays off once dqa0 actually enumerates.

Cold-boot run cmd (fresh cold boot, NOT a coldgct_v5 restore — dq_driver poll
runs before the 0x44518 snapshot point):
`--snapshot-on-pc 0x44518 --snapshot-name-tag coldgct_ide`

Discuss-before-code per house rules.
