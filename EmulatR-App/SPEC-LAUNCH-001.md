
# SPEC-LAUNCH-001 -- EmulatR Launcher Application (EmulatrLaunch)

    Project   : EmulatR -- Alpha AXP (EV6/21264) hardware emulator
    Artifact  : SPEC-LAUNCH-001.md
    Status    : DRAFT Rev D.2 -- awaiting sign-off (see Section 13,
                Gates). D.1: run-as-a-service deferred with design-ahead
                constraints; G2a constrained to headless-capable
                mechanisms. D.2: network console formalized -- console
                PORT is per-system state, Console group + Open Console
                added to v1, Service group reserved as adjacent layout.
    Date      : 2026-07-29
    Revisions : A initial; B start/stop semantics, PlatEd delegation,
                storage creation; C environment variable panel;
                D recomposed around the NAMED SYSTEM model -- the System
                List is the single reference structure, QSettings is the
                system registry, and the UI is the two-tab layout of the
                QML wireframe (docs/launcher_wireframe.qml, normative).
    Author    : Architect (Claude) from design sessions with Tim /
                eNVy Systems
    Root      : D:\EmulatR\EmulatR-App
    Audience  : Cowork (implementer), Tim (owner / sign-off)

--------------------------------------------------------------------------

## 1. Purpose

EmulatrLaunch is a standalone Qt6 Widgets desktop application that
manages NAMED SYSTEMS -- each a virtual Alpha machine bound to one run
directory and one platform -- and launches Emulatr.exe against the
selected system. It is the tester-facing front door for the beta
program. In scope for v1:

* Own the SYSTEM concept: create, name, validate, remember, select.
  One system = one run directory = one platform (DS10 / DS20 / ES40),
  bound at creation. The System List is the ONLY selector in the
  application; every action operates on the selected system.
* Start the emulator as a managed subprocess (QProcess or equivalent)
  with the system's run directory as working directory; stop it via a
  CLEAN SHUTDOWN that preserves persisted state (flash .rom), never a
  bare kill except as a logged, user-confirmed last resort.
* Expose a deliberately small per-system configuration surface
  (firmware, console binding, runtime environment variables); delegate
  DEVICE configuration to PlatEd (SPEC-PLATED-001) rather than
  duplicating any of its surface.
* Create virtual storage (vDisk container images) into the selected
  system's run directory, with prompted geometry and naming backed by
  the verified DEC drive geometry table.
* Give one-click access to the logs a tester needs to send back.

It replaces the "shortcut with Start-in field" launch model and removes
two structural failure modes of the installed deployment: Emulatr.exe
running with cwd inside C:\Program Files (read-only; VirtualStore
shadow-copy hazard for the flash .rom), and testers hand-editing
Emulatr.ini into unparseable or silently-wrong states.

## 2. Position in the product

EmulatrLaunch is a PERIPHERAL application in the established sense of
the Qt-free-core mandate: the emulator core never links Qt; the launcher
is a separate executable that communicates with the emulator only
through the public invocation contract (command line, cwd, Emulatr.ini,
the shutdown channel of Section 8, exit code). It is a sibling of, not a
mode of, Emulatr.exe.

Relationship to existing specs:

* SPEC-PLATED-001 (PlatEd). Device configuration is DELEGATED: the
  launcher's details tab carries an "Open in PlatEd" affordance that
  invokes PlatEd (separate process) on the selected system's platform
  manifest. PlatEd remains the sole owner of device topology, manifest
  authoring, and the SPEC-SCSIH-001 vDisk hierarchy; the launcher never
  writes a manifest. Embedding PlatEd as an in-process widget is
  deferred (Section 15). With PlatEd absent from the machine, only that
  affordance disables -- the launcher must ship and function before
  PlatEd does.
* SPEC-SCSIH-001 (SCSI vDisk hierarchy). Storage creation (Section 9)
  produces container images whose FORMAT is owned by SPEC-SCSIH-001;
  this spec consumes the format decision as gate G3 input.
* JRN-VMB-001/002 (bootstrap halt investigation). EmulatrLaunch touches
  nothing on that critical path. It is parallel work by design.

## 3. Directory root and layout

Project root: D:\EmulatR\EmulatR-App

    EmulatR-App/
        CMakeLists.txt          top-level build (Qt6 Widgets, C++20, MSVC)
        src/
            main.cpp
            LauncherWindow.h/.cpp       main window: tabs, global actions
            SystemModel.h/.cpp          QAbstractListModel of named
                                        systems; QSettings-backed registry
                                        (Section 4); THE reference
                                        structure of the application
            SystemRecord.h              the system value type
            RunDirSkeleton.h/.cpp       skeleton creation + validation of
                                        a run directory (Section 5)
            IniOverlay.h/.cpp           whitelisted ini read/modify/write
            EmulatorProcess.h/.cpp      subprocess wrapper: start, clean
                                        shutdown, escalation, exit codes
            FirmwareCheck.h/.cpp        firmware presence validation
                                        against the system's platform
            EnvVarModel.h/.cpp          registry-backed model of runtime
                                        env vars: state, tiers, denylist
            EnvVarPanel.h/.cpp          the checkable table view over it
            DiskImageFactory.h/.cpp     vDisk container creation logic
                                        (UI-free)
            StorageCreateDialog.h/.cpp  geometry/name prompt over the
                                        factory
            PlatEdBridge.h/.cpp         PlatEd discovery + invocation
            TerminalBridge.h/.cpp       PuTTY/telnet-client discovery +
                                        Open Console invocation (W3)
        resources/
            emulatr_launch.qrc
            templates/
                Emulatr.ini.default     seed config for new systems
                firmware_readme.txt     "place your SRM image here" text
            data/
                dec_drive_geometries.tsv    the verified 28-drive table
                emulatr_env_registry.tsv    curated EMULATR_* env var
                                            registry (Section 7, W6)
        tools/
            mkdisk.py               OPTIONAL dev-side scriptable twin of
                                    DiskImageFactory; not a tester-path
                                    deliverable (Section 9, S4)
        docs/
            SPEC-LAUNCH-001.md          this spec
            launcher_wireframe.qml      Tim's QML mock -- NORMATIVE
                                        wireframe for Section 10 (QML is
                                        the design medium only; the
                                        implementation is Qt Widgets)
        out/build/<config>/         CMake build output (core convention)

Conventions carried over from the core tree, binding here: ASCII-128-
only source; ADR-style headers with version/date attribution in every
file; version lives in headers and the tree, never in filenames;
discuss-before-code with explicit sign-off gates (Section 13); this
project's scripts live in EmulatR-App/tools/ and the core tree's tools/
is not modified.

Deliverable executable: EmulatrLaunch.exe.

## 4. The named system model (the single reference structure)

A SYSTEM is the unit everything else hangs from:

    SystemRecord {
        id        : GUID, assigned at creation, immutable. The stable
                    key for all per-system persisted state.
        name      : display name, e.g. "AlphaServer DS20 (ds20_run)".
                    User-editable at any time; uniqueness enforced
                    case-insensitively.
        platform  : DS10 | DS20 | ES40. Bound at creation, IMMUTABLE
                    in v1 (Section 15 records "change platform" as
                    deferred; the v1 migration path is: create a new
                    system, move media deliberately).
        runDir    : absolute path, canonicalized. One-to-one with the
                    system: registering two systems on the same
                    canonical run dir is refused.
    }

* M1. REGISTRY. SystemModel persists the system set in the launcher's
  QSettings (HKCU scope). Schema:

        systems/<id>/name
        systems/<id>/platform
        systems/<id>/runDir
        systems/<id>/env/<VAR>          checked state + value (W6, V4)
        ui/lastSelectedSystem = <id>

  All per-system launcher state keys off the GUID, so renames and
  run-dir moves never orphan settings.
* M2. SOLE SELECTOR. The System List (Tab 0) is the only place a
  system -- and therefore a run dir, a platform, a configuration -- is
  chosen. There is no separate run-directory combo; the selected
  system's runDir is displayed read-only in the details tab. Start,
  Stop, Make Disk, Configure/PlatEd, and log actions all bind to the
  current selection.
* M3. RECOVERABILITY. QSettings is a registry of pointers, not the
  source of truth. The run dir itself remains self-describing
  (Emulatr.ini + platform manifest + skeleton), so a lost or fresh
  QSettings hive is recovered via "Add existing..." (M5) without data
  loss. Nothing launcher-critical lives ONLY in QSettings.
* M4. NEW SYSTEM. The "New System..." flow prompts: name, platform
  (DS10/DS20/ES40), and location (default
  %USERPROFILE%\Documents\EmulatR\<name>; any writable path accepted).
  It creates the run-dir skeleton (Section 5), seeds the
  platform-matched defaults, registers the record, and selects it.
* M5. ADD EXISTING. "Add Existing..." browses to a run dir, validates
  it (Section 5 checks), INFERS the platform from the dir's contents
  (manifest present; firmware identity as corroboration), asks for a
  name, and registers it. If the platform cannot be inferred
  unambiguously, the user is asked to state it and the choice is
  validated against the contents before registration.
* M6. REMOVE. Removing a system deletes the registration and its
  per-system settings only -- NEVER the run directory or its contents.
  The dialog says so explicitly.

## 5. The run-directory contract

Each system's run directory:

    {run-dir}/
        Emulatr.ini             runtime configuration
        firmware/               tester-supplied SRM image(s); NOT shipped
        disks/                  vDisk container images (Section 9)
        logs/                   console/run logs   (created if absent)
        traces/                 trace output       (created if absent)
        *.rom                   persisted flash    (created by the
                                emulator; platform-specific state)

Contract terms:

* R1. The launcher ALWAYS sets the child process working directory to
  the selected system's run dir. Emulatr.exe is invoked by absolute
  path from wherever it is installed; it never runs with cwd inside its
  install directory.
* R2. The launcher refuses a run dir under C:\Program Files, and any
  directory failing a writability probe (create/delete of a temp file
  at registration time).
* R3. Skeleton creation (M4) seeds from resources/templates/:
  Emulatr.ini.default copied to Emulatr.ini with the platform selection
  applied; firmware/ containing firmware_readme.txt; empty disks/,
  logs/, traces/.
* R4. The launcher stores no launcher state inside the run dir beyond
  what Emulatr.ini already carries (per-system launcher state lives in
  QSettings keyed by GUID, M1).

GATING ASSUMPTION (G1, Section 13): this contract presumes the
emulator's config loader resolves Emulatr.ini, firmware paths, disk
image paths, and the flash .rom relative to CWD, not relative to the
executable directory. Audited and signed off in the core tree BEFORE
implementation starts. An exe-relative resolution anywhere is a
core-tree defect to fix first; the launcher does not work around it.

## 6. Discovery of sibling executables

The launcher locates Emulatr.exe in this order, first hit wins, result
shown (and overridable) in Settings:

1. Explicit path stored in launcher settings (user override).
2. Installed location: C:\Program Files\eNVy Systems, Inc\asa-emulatR\
   Emulatr.exe (the Setup Factory payload).
3. Dev-tree probe: D:\EmulatR\EmulatRAppUniV5\out\build\<config>\
   Emulatr.exe, newest config first (dev-machine convenience; absent on
   tester machines, harmless).

PlatEd is discovered by the same three-step pattern via PlatEdBridge.
If Emulatr.exe does not resolve, Start is disabled with an explanatory
status line; if PlatEd does not resolve, only the PlatEd affordance is
disabled. Never a modal error at startup for either.

## 7. Per-system configuration surface (the whitelist)

The details tab (Tab 1) presents the selected system's configuration.
W2-W4 map to named Emulatr.ini keys via IniOverlay -- a read-modify-
write overlay that touches only whitelisted keys and preserves every
other line, ordering, and comment verbatim.

* W1. PLATFORM -- displayed read-only (bound at creation, Section 4).
  The platform "selector" of this application is the System List
  itself: choosing among DS10/DS20/ES40 machines is choosing among
  systems.
* W2. Firmware image: pick from files present in {run-dir}/firmware/,
  validated by FirmwareCheck against the system's platform. Never a
  free-text path outside the run dir.
* W3. SERIAL CONSOLE BINDING -- the network console contract. The
  emulator's serial console (the path to P00>>>) is reached over a TCP
  listener; a terminal client latches to the PORT, not the process,
  which keeps this surface identical whether the emulator runs as a
  launcher child today or under the deferred service host later.
  - The console PORT is per-system state, held in the whitelisted ini
    keys (exact key list and listener semantics enumerated at gate G2)
    and shown in the Console group. Preflight cross-checks the port
    against every OTHER registered system and refuses duplicates by
    name (E10).
  - Bind address defaults to 127.0.0.1. Exposing the listener beyond
    localhost is an explicit per-system opt-in carrying a one-line
    caution -- a console reachable from the LAN must never be an
    accident.
  - OPEN CONSOLE: a Console-group button that spawns the terminal
    client against the system's port (putty.exe -telnet 127.0.0.1
    <port>). PuTTY is discovered by the Section 6 three-step pattern
    (settings override, common install paths, PATH), falling back to
    the OS telnet handler; with neither found, the button disables and
    the status line shows the host:port to connect manually.
* W4. Execution mode: only if a supported, tester-safe toggle exists in
  the ini. Anything compile-gated (EMULATR_EV6_BPRED,
  EMULATR_PCI_CFG_TRACE, quarantined EMULATR_RSCCWARP) is OUT of scope
  and must not appear in the UI.
* W5. Devices -- DELEGATED. The "Open in PlatEd" affordance invokes
  PlatEd (separate process) on the selected system's platform manifest.
  On PlatEd exit, the launcher re-runs preflight so manifest changes
  are revalidated before Start. The launcher reads the manifest only as
  much as preflight requires and never writes it.
* W6. ENVIRONMENT VARIABLES -- a checkable table of the EMULATR_*
  runtime variables the emulator consults via getenv. Checked rows are
  injected into the child environment at L2; unchecked rows are ABSENT
  from the child environment (not set-empty -- absence and empty are
  different states to getenv consumers). Runtime variables only;
  compile guards remain out of scope exactly as W4 states.

### 7.1 The environment variable panel

* V1. REGISTRY-BACKED. Rows come from
  resources/data/emulatr_env_registry.tsv -- columns: name, value kind
  (flag / path / integer / enum), default value, one-line description,
  tier, and the core-tree anchor (file:line of the getenv). The
  registry's CONTENT is produced by an evidence census of the core tree
  (gate G2b), not invented in this app.
* V2. TIERS. safe -- visible to everyone (e.g. trace-output knobs whose
  worst case is disk usage). dev -- behind a "Show developer variables"
  toggle, default off, one-line caution when on. denied -- never
  displayed, never settable, and stripped from the child environment
  even if present in the launcher's own inherited environment;
  EMULATR_RSCCWARP is the founding member (confirmed boot corruption).
  The denylist ships in the registry, so quarantining a variable is a
  data change, not a code change.
* V3. TABLE UI. Columns: [checkbox] | name | value | description.
  Value editable per kind (path picker for paths, spin/enum where
  typed); pure on/off flags hide the value cell. No free-form "add
  variable" row in v1 -- an unknown variable is a registry (and census)
  update, which keeps the denylist meaningful (deferred, Section 15).
* V4. PERSISTENCE. Selections persist per SYSTEM under
  systems/<id>/env/ in QSettings (M1), so they follow the system
  through renames and run-dir moves.
* V5. TRIAGEABILITY. Every launch appends the effective EMULATR_*
  environment (names and values of checked rows) to the head of the
  mirror log, so a tester's log always answers "what diagnostics were
  on for this run". Path-kind variables that point at trace output
  default into {run-dir}/traces/ per house convention.

## 8. Launch and lifecycle: start, clean shutdown, escalation

* L1. Preflight (Start enablement; re-evaluated on selection change and
  via a filesystem watcher over the selected system's run dir):
  - run dir exists and passes R2 writability;
  - Emulatr.ini present and parseable by IniOverlay;
  - at least one platform-valid firmware image present (W2 has a
    value);
  - Emulatr.exe resolved (Section 6).
  Each failed check is a plain-language status line with a "Fix" action
  where one exists (e.g. "Open firmware folder").
* L2. START: managed subprocess -- QProcess or a Win32-equivalent
  wrapper where QProcess semantics fall short -- with workingDirectory
  = the system's run dir, program = resolved Emulatr.exe, arguments per
  the invocation contract (enumerated at G2). Environment = launcher's
  inherited environment, minus every denied-tier variable (V2), plus
  the system's checked W6 rows. stdout/stderr captured to
  {run-dir}/logs/run_launch_YYYYMMDD_HHMMSS.log (house naming) as the
  console mirror. The launcher does not interpose on the serial console
  itself; the emulator's console conventions remain authoritative.
* L3. STOP = CLEAN SHUTDOWN. Definition of clean: the emulator drains
  and persists all durable state -- flash .rom write-back at its commit
  cadence, open log streams flushed -- and exits by its own accord with
  a conventional exit code. Stop requests this through the sanctioned
  shutdown channel and then WAITS.
  - The shutdown channel is a core-tree contract, not a launcher
    invention. Candidates settled by evidence at gate G2a: (a) an
    existing handler for Windows console control events
    (CTRL_BREAK/CTRL_CLOSE) that drains and exits; (b) a command on an
    existing control/stdin surface; (c) a small dedicated mechanism
    added to the core (e.g. a named event the main loop polls at a
    safe boundary). Bare QProcess::terminate() posts WM_CLOSE, which a
    console-subsystem emulator will not see -- this is why G2a exists
    and why "QProcess-equivalent" appears in L2.
  - ESCALATION: if the emulator has not exited within a visible,
    configurable timeout (default 10 s) of the shutdown request, the
    launcher offers Force Stop (kill). A forced kill is logged in the
    mirror log as such, and the UI states plainly that a forced kill
    may lose un-persisted flash state. Kill is never automatic.
* L4. RUN STATE AND THE LIST. Start and Stop are separate buttons
  (wireframe): Start enabled when preflight passes and nothing is
  running; Stop enabled only while the emulator runs. v1 is
  single-flight per launcher instance -- while a system is running, its
  list row carries a visible RUNNING badge, Start is disabled for ALL
  systems (selecting another system and pressing Start is structurally
  impossible, not merely warned), and selection changes do not disturb
  the running process. Multi-instance orchestration is deferred
  (Section 15).
* L5. Exit: exit code and duration shown in the status area and
  appended to the mirror log, with clean/forced disposition recorded.
  Nonzero or forced exits surface a "Show log" action.
* L6. Log access: "Open logs folder" opens the selected system's
  {run-dir}/logs in Explorer. "Package logs for support" zips the
  newest N mirror logs (excluding traces/ -- multi-GB) to the Desktop
  with a timestamped name.

## 9. Storage creation (vDisk containers)

"Make Disk" creates virtual disk container images into the SELECTED
system's {run-dir}/disks/:

* S1. DiskImageFactory (UI-free TU): given {geometry, name, format
  parameters}, creates the container file. The single implementation of
  creation logic in this app.
* S2. StorageCreateDialog over S1. Fields: drive model combo populated
  from resources/data/dec_drive_geometries.tsv (the verified 28-drive
  DEC table -- cylinders/heads/sectors/capacity per model) plus a
  Custom entry with sanity bounds; name (file stem, ASCII, no path
  separators) with the resulting {run-dir}/disks/<name>.<ext> and
  projected size shown before creation. Placement is ALWAYS the
  selected system's disks/ -- no free path selection.
* S3. Container format: owned by SPEC-SCSIH-001 / the IBlockMedia layer
  (FileBlockMedia). Consumed here as gate G3 input; no launcher-local
  format invention.
* S4. tools/mkdisk.py: OPTIONAL scriptable dev twin of S1, reading the
  same TSV; explicitly NOT the tester path (tester machines have no
  Python interpreter -- the deciding argument for the native
  implementation). Must produce byte-identical output to
  DiskImageFactory for the same inputs; may be deferred entirely
  without affecting v1 done-ness.
* S5. A future CLI surface is a thin mkdisk.exe wrapper over
  DiskImageFactory (deferred, Section 15) -- recorded so the factory/UI
  split is preserved.

## 10. UI (v1) -- per the normative wireframe

docs/launcher_wireframe.qml (Tim's QML mock) is the normative wireframe:
window geometry, the two-tab structure, the vertical rhythm of Tab 0,
and the global bottom bar are as drawn there. Deviations from the mock,
each already argued in session:

* D1. The Run Directory combo is REMOVED. The System List is the sole
  selector (M2); the selected system's run dir displays read-only in
  Tab 1. "Browse..." becomes "Add Existing..." (M5) beside
  "New System..." (M4) and "Remove" (M6).
* D2. Tab 1 ("Named System Details") hosts the WHITELIST, not a device
  editor: platform (read-only), run dir (read-only), firmware (W2),
  console binding (W3), the W6 environment table, and "Open in PlatEd"
  (W5). The mock's placeholder list (CPU/Memory/SCSI/Network) is
  PlatEd's surface, reached through that button -- re-scoping it into
  the launcher would fork manifest editing across two applications and
  is explicitly rejected.
* D3. The bottom bar's "Configure" button is resolved AS the "Open in
  PlatEd" action (one affordance, present in the bar per the mock and
  duplicated in Tab 1 for discoverability). "Make Disk" is Section 9.
* D4. Tab 1 gains a CONSOLE group: the system's console port (W3,
  editable), bind-address opt-in, and the Open Console button. An
  adjacent SERVICE group position is RESERVED in the layout for the
  deferred run-as-a-service controls (Section 15) -- reserved means the
  Console group is placed so the Service group can appear beside it
  without relayout, not that any service UI ships in v1. Open Console
  is deliberately mode-agnostic: it targets the port, so it will serve
  both the launcher-child and service hosts unchanged.

Widget mapping (implementation is Qt Widgets; QML is the design medium
only): QTabWidget for the tabs; QListView over SystemModel for the
system list with a RUNNING badge role; QToolBar-style button rows for
Start/Stop and the global bar; standard item views and editors
elsewhere. Plain native style, no custom theming in v1.

## 11. Error handling and edge cases

* E1. Selected system's run dir deleted/unmounted: watcher invalidates
  preflight; the list row shows a broken state; status explains; no
  crash, no modal. "Add Existing..." can re-point after a move; the
  GUID (and thus env selections) survives re-pointing via an explicit
  "Relocate run dir..." context action.
* E2. Emulatr.exe exits instantly (bad firmware, config): L5 path; the
  mirror log is the diagnostic; launcher stays up.
* E3. Ini not writable (locked, ACL): preflight failure with the path
  named; the launcher never silently drops a setting.
* E4. Unknown/extra ini content: preserved verbatim (Section 7). The
  launcher must be safe to point at a dev run dir with exotic settings.
* E5. Two launcher instances: allowed; QSettings last-writer-wins is
  acceptable for v1; single-flight launch is per-instance.
* E6. Stop requested, emulator unresponsive: escalation path of L3
  only; the launcher never kills silently.
* E7. Disk creation with insufficient free space: projected size
  checked against the volume before creation; refusal names both
  numbers.
* E8. PlatEd left open while Start pressed: preflight re-reads manifest
  state at press time; mid-edit/unparseable manifest is a preflight
  failure line, not a race.
* E9. Registration conflicts: same canonical run dir twice -> refused
  with the existing system named (M1); duplicate display name ->
  refused case-insensitively (Section 4).
* E10. Console port conflicts: a port claimed by another registered
  system is a preflight failure naming that system; a port bound by an
  unrelated process surfaces at Start as the emulator's own bind error
  in the mirror log, with the status line pointing at the port setting.

## 12. Acceptance criteria (v1 done)

* A tester with only the Setup Factory install and a self-sourced
  firmware image can: create a named system (name + platform), drop
  firmware in via the opened folder, create a disk image via Make Disk,
  select the system, press Start, and reach P00>>> without touching a
  text editor or shell.
* The System List is demonstrably the sole selector: every action
  (Start, Stop, Make Disk, PlatEd, logs) operates on the selection, and
  no second run-dir chooser exists anywhere in the UI.
* Stop produces a clean shutdown: flash .rom persisted, conventional
  exit code, disposition logged. Force Stop appears only after the
  timeout and is recorded as forced. While one system runs, Start is
  disabled for all systems and the running row is badged.
* A run launched via EmulatrLaunch is byte-identical in behavior to the
  same configuration launched from the command line.
* IniOverlay round-trips a dev-grade Emulatr.ini (comments, exotic
  keys) with zero diff outside whitelisted keys.
* A checked env var is observably honored by the emulator; an unchecked
  one is absent from the child environment; a denied one is stripped
  even when set in the launcher's own environment; the mirror log head
  records the effective set; and env selections survive a system rename
  and a run-dir relocation (GUID keying, M1/E1).
* Deleting QSettings and re-adding every run dir via "Add Existing..."
  reconstructs a working system list (M3 recoverability).
* DiskImageFactory output for each of the 28 table geometries matches
  the SPEC-SCSIH-001 format definition; mkdisk.py (if built) is
  byte-identical to it.
* PlatEd opens on the correct manifest and preflight revalidates on
  return; with PlatEd absent, everything else works.
* Open Console connects a discovered terminal client to the selected
  system's console port and reaches P00>>> on a running system; two
  systems registered with the same port is a named preflight failure;
  the default bind address is loopback.
* Emulatr.exe never executes with cwd inside its install directory, and
  the launcher refuses Program Files run dirs with a clear message.

## 13. Gates (sign-off sequence)

* G1. PATH RESOLUTION AUDIT (core tree, blocking): verify by inspection
  of the config loader that ini/firmware/disks/flash resolve
  cwd-relative. Deliverable: evidence note (file/line citations)
  appended to this spec. No launcher code before G1.
* G2. INVOCATION CONTRACT ENUMERATION: exact command-line arguments,
  required ini keys per platform (including how platform selection
  reaches the emulator: ini key, argument, or manifest choice), and the
  console-binding key list (W3) -- including EVIDENCE of the network
  console listener: the key(s) naming port and bind address, listener
  semantics (telnet vs raw), and whether the listener exists in the
  core today or is a core-tree task. Deliverable: appendix to this
  spec. Depends on G1.
* G2a. SHUTDOWN CHANNEL (core tree, blocking for L3): evidence of an
  existing clean-shutdown path in Emulatr.exe (console ctrl handler,
  control command, or none). If none exists, a core-tree task doc
  specifies the minimal mechanism BEFORE Stop is implemented; the
  launcher builds against the sanctioned channel only.
  SELECTION CONSTRAINT (Rev D.1): the channel must be operable by a
  HEADLESS caller as well as the interactive launcher -- the deferred
  run-as-a-service capability (Section 15) will drive this same
  channel from a service wrapper translating SERVICE_CONTROL_STOP in
  session 0, where no console window exists. This favors candidate (c)
  (named kernel event or control pipe polled at a safe boundary) over
  console-ctrl-event mechanisms; if (a)/(b) are chosen anyway, the
  evidence note must state how a session-0 caller drives them.
* G2b. ENV VAR CENSUS (core tree, blocking for W6): enumerate every
  runtime getenv("EMULATR_*") consultation in the core with file/line
  anchors, value kind, and effect; classify each into safe/dev/denied
  with Tim's sign-off on the tiering. Deliverable:
  emulatr_env_registry.tsv plus the evidence note. The panel renders
  ONLY census output.
* G3. CONTAINER FORMAT INPUT: the SPEC-SCSIH-001 / FileBlockMedia
  container format decision recorded as an appendix here. Blocking for
  Section 9 implementation only.
* G4. SPEC SIGN-OFF: Tim approves Sections 1-12 as scoped (or amends).
* G5. SKELETON GATE: CMake project + empty two-tab window build under
  VS2022 proves the Qt6 toolchain wiring at the new root.
* G6. FEATURE GATES, each behind its own review: SystemModel+registry
  (with the M3 recoverability test as acceptance evidence);
  RunDirSkeleton+preflight; IniOverlay (round-trip byte-preservation
  test); EmulatorProcess (start + clean shutdown against G2a channel);
  EnvVarModel+Panel (against the G2b registry; denylist stripping
  verified by test); DiskImageFactory+dialog (against G3 format);
  PlatEdBridge; log packaging.

## 14. Dependency notes

The core launcher (Sections 1-8, 10-12) depends only on G1/G2/G2a/G2b
and can proceed while SPEC-SCSIH-001's format work is in flight.
Section 9 (storage) is severable behind G3: if the container format is
not final when the launcher otherwise reaches done, Make Disk ships
disabled with a status note rather than holding the release.

## 15. Deferred (recorded so they are not re-litigated)

* Changing a system's platform in place (v1: create a new system and
  move media deliberately; a guided migration flow may come later).
* Merge/shell-sharing with PlatEd; embedding PlatEd as an in-process
  widget rather than a spawned process.
* Free-form "add environment variable" row in the W6 panel (v1 is
  census-only so the denylist stays meaningful).
* mkdisk.exe CLI wrapper over DiskImageFactory (S5).
* RUN AS A SERVICE -- "Start as service" per system: registration of a
  system as a Windows service so the machine runs headless, survives
  logoff, and auto-starts at boot (the 24/7 OpenVMS-box end state).
  Architecture when it lands: a small EmulatrSvc.exe SCM wrapper that
  spawns Emulatr.exe under the SAME contract the launcher uses (cwd =
  run dir, W6 env composition, absolute exe path) and translates
  SERVICE_CONTROL_STOP into the G2a shutdown channel; the core gains no
  SCM plumbing. Known prerequisites, recorded so v1 does not foreclose
  them: network-reachable serial console -- SATISFIED IN ADVANCE by the
  W3 network console contract and the D4 mode-agnostic Open Console
  (PuTTY latches to the port, not the process, so the console path is
  identical under either host); elevation flow for service
  registration; a service-account / run-dir ACL story (LocalSystem
  cannot sensibly use a Documents run dir). Its UI lands in the D4
  RESERVED Service group beside the Console group. The G2a
  headless-caller constraint (Section 13) is the other piece pulled
  forward into v1.
* Multi-instance orchestration (several systems running at once).
* Snapshot management UI (firmware.snap / FAST_DECOMPRESS surface).
* Auto-update or version checks against the redist SHA256SUMS index.
* Any exposure of compile-gated diagnostics.

-- end of SPEC-LAUNCH-001 Rev D.2 --
