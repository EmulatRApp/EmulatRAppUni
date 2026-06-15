# EmulatR V4 -- Roadmap after the SRM `>>>` prompt (2026-06-05)

Status anchor: clean cold boot reaches the SRM console `>>>` prompt,
fully interactive over serial (`show version` round-trip confirmed).
Flash carries a valid srm_fw 7.3-1 rev block (LFU `update srm`).
predig snapshots banked for both the UPD> and `>>>` states (v2 format,
live interrupt chain).

This roadmap enumerates the workstreams as understood now. It is a
planning draft for refinement, not a commitment. Ordering within a
stream is dependency-driven; ordering ACROSS streams is the open
question for Tim.

LIVE CONSOLE EVIDENCE (2026-06-05, at `>>>`): `show config`,
`show memory`, `show pal`, `show version` all clean and correct.
`show config` PCI Hose 00 shows ONLY the Cypress 82C693 bridge (Slot
05/0) -- no NIC, no SCSI HBA enumerated (confirms section 2/3: the
on-board devices are unmodeled / not walked). `show device` lists ONLY
`dva0.0.0.0.0 DVA0` (the floppy) -- there is NO `dka*` disk to boot
from, which is the concrete gate on OS boot (section 4 needs G6 SCSI +
media). Two cosmetic findings: `show device` prints its line TWICE
(console line-doubling, also seen at UPD>), and `display`/`show config`
SROM Revision shows one garbled byte ("P<?>").

--------------------------------------------------------------------
## 0. Immediate hygiene (low-risk, do-before-new-work)

- **Commits.** Three changesets uncommitted Windows-side: (1) LDQP/
  STQP/VPTB `_vms` fix + fBox stub repair, (2) snapshot
  kChipsetVersion 2, (3) boot profiler. Land as three commits while
  the tree is green.
- **Flash + snapshot cwd pinning.** Backing `ds10_flash.rom` and the
  `snapshots/` dir both resolve against launcher CWD, so VS launches
  (build dir) and bash launches (project dir) diverge -- the updated
  flash lives in the project dir; the build-dir copy is stale and a
  VS launch would boot back into LFU. Add `--snapshot-dir` /
  `--flash-path` CLI (or `EMULATR_*` env), same pattern as the trace
  dir. Until then: manually `cp` the updated flash into the build dir.
- **TEMP diagnostic removal.** PICTRACE, UARTBP#9/#10/#9d/#10d,
  DIVERT-REI ledger, C970/FCLOSE/FPIP watches, the PipelineDriver
  lexer probe (EMULATR_MEMDIAG-gated). Strip now that the chain works.
- **Codegen orphan guard.** genGrains.py should die/warn when a
  handwritten.tsv entry matches no derived leaf name. Three silent-
  stub bugs to date (SCBB, LDQP/STQP, VPTB). ~10 lines.
- **Console transport defaults.** SRMConsoleDevice::Config defaults
  port 23 + echo on; canonical is 10023 + echo off (guest echoes) +
  IAC guard. Re-default and document.

--------------------------------------------------------------------
## 1. SRM ENV interface + persistence across reset

Goal: `set <var> <val>` at `>>>` persists across a reset / cold boot,
like real NVRAM.

Current state -- TWO env mechanisms, not yet reconciled:
- **V4 SRMEnvStore** (deviceLib/SRMEnvStore.{h,cpp}): a "toy" JSON
  store with sensible SRM defaults, auto-load on construct / auto-save
  on destruct, wired through SRMConsole + a CSERVE path. This is
  V4-side state.
- **Guest firmware NVRAM / "eerom"**: the shipped SRM manages its own
  environment in an NVRAM region and tried to open an "eerom" backing
  file -- V4 reported "file open failed for eerom" (non-fatal). The
  firmware's `set`/`show` at `>>>` operate on THIS, not necessarily on
  SRMEnvStore.

Open questions to resolve first (verify against live console, do not
assume):
1. When you type `set foo bar` then `show foo` at `>>>`, does it
   round-trip within a session? (tests the firmware's in-RAM env)
2. Does it survive a reset (LFU exit) and a cold boot? (tests
   persistence -- expected NO today, since eerom has no backing)
3. Is SRMEnvStore actually on the firmware's path, or a parallel V4
   abstraction the firmware never consults?

Scope (pending answers): back the firmware's NVRAM/eerom region with a
persistent file (mirrors the FlashRom backing-file pattern), so env
writes land and reload on next boot. Decide whether SRMEnvStore
becomes that backing or is retired in favor of a raw NVRAM image.
Persistence must also survive snapshot save/restore (env region is
guest memory or a device -- confirm which).

Related history: [[project_srm_env_fow_frontier]] (the FOW crash was a
FETCH-FIXUP artifact, since removed; env surfaced gracefully after).

--------------------------------------------------------------------
## 2. Device discovery / PCI enumeration (gap G4)

Goal: a real PCI bus walk so on-board devices are discovered and get
sane BARs, instead of the firmware computing garbage all-ones bases.

Current state: G1 (PCF8584 IIC stub) and G3-lite (fixed-range PCI-mem
claim seam) landed -- the 0xFFFF0000 IIC writes are handled, no longer
a blocker. The residual `UNHANDLED OUTER WRITE` noise and `show
device` behavior at `>>>` are the next observable.

Scope: implement PCI config-space enumeration + BAR machinery
(USPO-PCI ch. 6) in TsunamiPchip. Fixed-range claims become
BAR-tracked claims. Prerequisite for NIC and SCSI device models.
Non-fatal today, so sequence is a choice not a block.

Authority: journals/V4_IO_Machinery_Map.txt (gap list), USPO-PCI ch.6.
Related: [[reference_pc264_pci_irq_and_bridge_bdf]].

--------------------------------------------------------------------
## 3. Adding devices (after the PCI walk)

- **G5 -- DE500 / DECchip 21143 "tulip" Ethernet.** The on-board NIC.
  Needs G4 BARs. The original tulip hypothesis for 0xFFFF0000 was
  refuted (that was IIC), so the NIC is genuinely unmodeled. Enables
  network presence + later netboot.
- **G6 -- SCSI fabric port from V1 scsiCoreLib.** The boot-device
  path. V1 has a developed-but-unimplemented SCSI core
  ([[reference_v1_scsi_core]]); port it, plug via the PCI-mem claim
  seam once BARs are assigned. This is the storage backend for OS
  boot.
- **G7 -- 8042 keyboard** (only if serial console proves insufficient;
  SRM falls back to COM1, so likely skip).
- **G8 -- TIGbus control registers** (reset = restart/noop, CPU-start
  = absorb for single-CPU). Downstream symptom-silencer.

--------------------------------------------------------------------
## 4. OS boot arc (the new frontier)

- **Halt-button / AUTO_ACTION.** Banner reads "Halt Button is IN,
  AUTO_ACTION ignored" -- our halt-button bit reads pressed, which is
  why it drops to console instead of auto-booting. Flipping it (a
  platform/TIG input bit) is the doorway to `AUTO_ACTION boot`.
- **Boot device + media.** Needs G6 SCSI + a disk image. `boot dka0`
  then drives the OS PALcode handoff (the HWRPB builder we already
  have for OS handoff finally gets exercised for real).
- **OS PALcode takeover** at scale -- partially crossed already
  (palBase 0x600000 -> 0x8000); the OS load path is the full exercise.

--------------------------------------------------------------------
## 5. Snapshots productization (now unblocked)

The snapshots work was explicitly deferred until `>>>`. Now reached.
v2 format serializes the interrupt chain; predig console snapshot
banked. Remaining: prune-policy tuning (219 autos accumulated, ~15 GB
-- prune is opt-in), the cwd-pinning from section 0, and deciding the
canonical "restore to prompt" entry point for fast iteration. Design
notes: journals/Snapshots_Design_Notes.md.

--------------------------------------------------------------------
## 6. Boot profiler analysis (data now available)

The clean cold-boot-to-`>>>` is the first fully profileable run.
Resolve the run_end / poke dump with tools/profile_resolve.py to
attribute the silent stretches: genuine device discovery vs tick-bound
delay loops (fast-forward candidates) vs modeling artifacts. Feeds the
boot-time optimization decisions. Ticket:
[[project_ticket_boot_profiler]].

--------------------------------------------------------------------
## 7. Secondary / eventual

- SROM Revision string shows one garbled byte ("P<?>") in `display` --
  cosmetic, someday.
- EV5 (21164) emulator profile -- parallel coreLib/Ev5EntryVectors.h.
- S_PalLinux codegen extension (~10 lines, genGrains.py personality
  loop).
- Host-native decompression alternate boot path (intercept-hybrid;
  trusted oracle exists). Cold-boot/determinism value; sequence after
  the above.
