<!--
============================================================================
PLATFORM_EDITOR_SPEC.md -- Section 17 addendum (2026-07-18 session)
Project: EmulatR -- Alpha AXP / EV6 Architecture Emulator (V5 active tree)
Copyright (C) 2025, 2026 eNVy Systems, Inc.  All rights reserved.
Licensed under eNVy Systems Non-Commercial License v1.1
Project Architect: Timothy Peer.  AI Collaboration: Claude (Anthropic).
Per ADR-0001. ASCII(128) only.
----------------------------------------------------------------------------
Handoff note: this file APPENDS to PLATFORM_EDITOR_SPEC.md after Section 16.
The Decision-log, Open-questions, and Ticket-plan blocks below are marked
[MERGE INTO ...] and are meant to be folded into the existing tables in that
document -- not left as a separate file. Cowork holds the live spec; apply
these deltas against it.
============================================================================
-->

## 17. Qt Widgets frontend reinstated; run-directory file management (2026-07-18 session)

### 17.1 Frontend reinstatement -- Qt 6 Widgets is the shipping GUI

**Decision (D-025): the shipping frontend is Qt 6 Widgets. This supersedes
D-019 and D-020 for the shipping build.** D-019 dropped Qt for a TUI on a single
stated ground -- "Qt is not installed where the work is being built." That ground
does not hold on the project dev host, where Qt 6.9.1 is present. Reinstating Qt
reactivates a design that already exists in this document: the layer stack of
Section 5.1 (`MainWindow` / `QAbstractItemModel`), the tree/property master-detail
of Section 5.4 and Section 8, and the ticket plan (T-04 `ManifestModel`, T-05
`WidgetFactory`, T-06 `PropertyPane`, T-12 `windeployqt`).

The TUI (D-019) and the web mockup (D-020) are RETAINED as validated alternate
frontends over the same Qt-free core. Nothing built for them is wasted: the core
(`OrderedJson`, `SchemaPolicy`, `DeviceCatalog`, `ManifestView`) is frontend-
agnostic and unit-tested headless (Section 5.1, downward-dependency rule). Qt is
one more presentation layer over that core, not a rewrite of it.

Qt surface stays minimal and std-first per the project convention: Widgets is
used for the view (window, tree view, property pane, dialogs); all model, policy,
catalog, validation, and JSON I/O remain in the Qt-free core. No Qt types leak
below the Presentation and Model layers.

### 17.2 Core reuse -- the model wraps the existing DOM, not QJsonDocument

Section 5.2 cautions against building the item model over `QJsonDocument` because
`QJsonValue` is a value type with no stable identity. That caution stands, and the
resolution is now cleaner than 5.2 states: the Qt model does not parse JSON at all.

**Decision (D-026): `ManifestModel : QAbstractItemModel` adapts the EXISTING std
`Node` tree produced by `OrderedJson`, not `QJsonDocument`.** The core already
parses the manifest once into an owned, ordered, typed `Node` tree (Section 5.2)
and re-emits it byte-order-preserving on save (D-003). The Qt model is a thin
adapter that exposes stable `Node*` as `QModelIndex::internalPointer` and maps
authored child order to model rows. Key-order preservation is a property of the
core writer and is therefore preserved by construction -- the Qt layer never
touches serialization. `ManifestView` (the headless view-model already used by the
TUI/web) supplies label synthesis, tree flattening, and property-row derivation;
the Qt `PropertyPane` renders those rows via `WidgetFactory` (T-05).

Section 5.2's `Node` struct and Section 5.4's tree model are unchanged and remain
the single DOM for all frontends.

### 17.3 Runtime layout and run-directory discovery

This section fills the run-directory selection requirement that T-10 named but did
not specify.

**Runtime layout (authoritative, from the root CMakeLists.txt staging):**

    out/build/{config}/                <- RUN DIRECTORY  ($<TARGET_FILE_DIR:Emulatr>)
      Emulatr.exe
      config/                          <- staged ini(s)
      firmware/                        <- staged firmware blobs
      <stem>_platform.json  ...        <- staged platform manifests (run-dir ROOT)
      traces/
      tools/                           <- staged auxiliary tools
        <editor binary>               <- PlatformEditor GUI lives HERE
        webui/                         <- web mockup (already staged)

The platform manifests are staged into the run-dir ROOT (next to `Emulatr.exe`),
because the emulator resolves them next to its own executable. The editor is staged
one level down, in `tools/`.

**Decision (D-027): the run directory is derived from the editor's own executable
location; discovery is a glob, not a hardcoded model list.**

Discovery algorithm:

  1. `exeDir` = the editor executable's directory
     (`QCoreApplication::applicationDirPath()`), which by the staging convention
     is `{config}/tools`.
  2. `runDir` = parent of `exeDir` = `{config}`.
  3. Enumerate `runDir` for files matching the glob `*_platform.json`
     (name-filter, case-insensitive). This is the confirmed pattern: it anchors on
     the mandatory `_platform.json` suffix and is agnostic to the stem, so it
     matches every real manifest across both naming conventions
     (`ds20_platform.json` under the V5 file-naming rule, and the legacy
     `ds20_v7_3_platform.json`) and auto-includes ds25 / es45 / any future
     platform with no code change. A hardcoded model allowlist is explicitly
     rejected here per P-1 (data-driven) and P-2 (preserve-unknown).
  4. For each matched file, read the top-level `platform` field for the picker's
     display label. A file that fails to parse or lacks `platform` is still LISTED
     and flagged (broken/unknown), never silently hidden -- P-3 (non-destructive:
     warn, do not drop).

Overrides and fallbacks:

  - `--run-dir <path>` on the command line WINS over the derived `runDir` (for
    running the editor detached from the staged layout, e.g. against a source-tree
    manifest folder during development).
  - If discovery finds zero manifests, the editor presents a folder chooser and
    remembers the choice (editor config), rather than opening blank.

Coupling to the ini (read-only, for validation context only): the editor reads
`{runDir}/config/*.ini` to resolve `[Storage] diskDir` for media validation (V-10)
and `[System] model` for the ini/manifest latch (V-24, D-016). The editor is a
MANIFEST editor in v1; it does not write the ini. The two-surface question (D-016)
stays out of scope -- the editor reads the ini only to cross-check, never to edit.

### 17.4 The platform picker (launch UX)

**Decision: file selection is a list of discovered manifests, not a bare file
dialog (confirms F-2).** On launch (and via `File > Open Platform...`) the editor
shows a platform picker listing every manifest found by 17.3, one row per file:

    +---------------------------------------------------------------------+
    |  Open a platform manifest        run dir: out/build/relwithdebinfo  |
    +---------------------------------------------------------------------+
    |  platform   file                          state                     |
    |  --------   ----------------------------  --------------            |
    |  DS10       ds10_platform.json                                      |
    |  DS20       ds20_platform.json            * unsaved edits           |
    |  DS25       ds25_platform.json                                      |
    |  ES40       es40_platform.json                                      |
    |  ES45       es45_platform.json                                      |
    |  (?)        legacy_platform.json          ! unreadable: no platform |
    +---------------------------------------------------------------------+
    |  [Open]  [New from template...]  [Duplicate]  [Delete]  [Refresh]   |
    +---------------------------------------------------------------------+

  - Column `platform` is the value read from inside each file (17.3 step 4); column
    `file` is the on-disk basename; `state` shows dirty / lock / broken flags.
  - A broken manifest is shown with a `(?)` platform and an inline reason; opening
    it enters passthrough-everything mode with a banner (Q-7).
  - Secondary path: `File > Open File...` is a standard file dialog defaulted to
    `runDir` and filtered to `*_platform.json`, for opening a manifest outside the
    run directory.

### 17.5 File operations -- fills T-10

  - **Open.** Parse via `OrderedJson` into the `Node` DOM (17.2). If
    `manifest_version` is newer than the tool knows, open in passthrough mode with a
    banner and no schema gating (Q-7).
  - **Save.** Serialize via the ordered writer: authored key order preserved
    (D-003), scalars re-emitted from `Node.raw` where unedited. ASCII(128) is
    enforced on write (T-12) -- a non-ASCII byte in any string value is a save-time
    error surfaced in the Issues dock, not silently transcoded. On overwrite of an
    existing file, the prior contents are renamed to `<file>.bak` before the new
    bytes are written.
  - **Save As.** Defaults to `runDir`; same writer rules.
  - **New from template.** Clones the platform baseline from the catalog
    `platforms` entry (Section 7), prompts for `platform` and firmware stem, and
    writes `<stem>_platform.json` into `runDir`. This closes Q-5 (yes, the tool
    creates manifests).
  - **Duplicate.** Copies the selected manifest to a new stem the user names;
    lands in `runDir`.
  - **Delete.** Guarded confirm. If the target manifest's `platform` equals the
    active ini `[System] model`, deletion is additionally warned (V-30) because it
    orphans the emulator's current selection.
  - **Reload / dirty prompt.** Reload re-reads from disk and discards edits behind
    a guarded confirm. Switching platforms or closing with unsaved edits prompts.

### 17.6 New validators

`[MERGE INTO Section 9 validator table]`

  | Id   | E/T | Severity | Rule                                                                 |
  |------|-----|----------|----------------------------------------------------------------------|
  | V-30 | T   | warn     | Delete/rename of a run-dir manifest whose `platform` == active ini `[System] model` (orphans the emulator's current selection). |
  | V-31 | T   | warn     | Two or more manifests in `runDir` declare the same `platform` value (ambiguous to a human operator; the emulator still resolves by firmware stem). |

### 17.7 Ticket-plan updates

`[MERGE INTO Section 10 ticket table]`

  - **T-10 (gate filled).** File open / save / save-as, `.bak` on overwrite, dirty
    prompt. Gate: open->save round-trip is semantic-diff-clean on all staged
    manifests (the Section 10 litmus); `.bak` created on overwrite; a non-ASCII
    string value blocks save with an Issues-dock error.
  - **T-13 (new).** Run-directory discovery + platform picker (17.3, 17.4). Gate:
    launched from `{config}/tools`, the picker lists every `{config}/*_platform.json`
    with its internal `platform` label; `--run-dir` override honored; a broken
    manifest is listed and flagged, not hidden.
  - **T-14 (new).** CMake staging: the editor binary is staged to
    `$<TARGET_FILE_DIR:Emulatr>/tools/` (analogous to the webui copy already in the
    root CMakeLists.txt), gated behind `EMULATR_BUILD_EDITORS`. Gate: after a build
    with the option ON, `{config}/tools/<editor>` and `{config}/tools/webui` both
    exist and discovery resolves `runDir` = parent(exeDir). This ticket is what
    makes the 17.3 discovery assumption true; without it the derived run dir is
    wrong.
  - **T-15 (new).** File management: New-from-template / Duplicate / Delete (17.5),
    guarded by V-30. Gate: New writes a schema-valid `<stem>_platform.json` from the
    catalog template; Duplicate copies; Delete confirms and honors V-30.

### 17.8 Open-question closures

`[MERGE INTO Section 13]`

  - **Q-5. CLOSED -> yes.** The tool creates manifests via `File > New from
    template` (catalog `platforms` basis) and via Duplicate. Full file management
    (New / Duplicate / Delete) is in scope (D-028).
  - **Q-6. CLOSED -> resolved.** Placement is `tools/Editors/PlatformEditor` in the
    V5 tree, integrated into the root CMakeLists.txt behind the
    `EMULATR_BUILD_EDITORS` option, with the editor and webui staged to
    `{config}/tools/`. This supersedes the Section 11 `_PROVISIONAL`
    `D:\EmulatR\EmulatRAppUniV4\PlatformEditor\` location (D-029). Section 11's
    "sibling of Emulatr" premise is retired: the editor is a staged auxiliary tool
    under the run directory, not a sibling of the emulator source tree.

### 17.9 Decision-log entries

`[MERGE INTO Section 14 decision log]`

  | Date       | Id    | Decision                                                    |
  |------------|-------|-------------------------------------------------------------|
  | 2026-07-18 | D-025 | **Shipping frontend is Qt 6 Widgets. Supersedes D-019 and D-020 for the shipping build.** D-019's sole ground (Qt not installed) does not hold on the dev host (Qt 6.9.1 present). Reactivates the Qt design already in Sections 5.1/5.4/8 and the ticket plan. TUI (D-019) and web (D-020) retained as alternate frontends over the shared Qt-free core; nothing built for them is wasted. Qt surface stays minimal/std-first: no Qt types below Presentation/Model. |
  | 2026-07-18 | D-026 | **`ManifestModel : QAbstractItemModel` adapts the existing std `Node` tree from `OrderedJson`, not `QJsonDocument`.** Resolves the Section 5.2 caution: the Qt layer never parses or serializes JSON; key-order preservation (D-003) is preserved by construction because the core writer is untouched. `ManifestView` feeds label/row derivation to the Qt `PropertyPane`. |
  | 2026-07-18 | D-027 | **Run directory is derived from the editor exe location; file discovery is a glob.** `runDir` = parent of `applicationDirPath()` = `{config}` (editor staged at `{config}/tools`). Enumerate `runDir/*_platform.json` (case-insensitive), label each row from the file's internal `platform` field, list-and-flag broken files rather than hide them. `--run-dir <path>` overrides. Hardcoded model allowlist rejected (P-1, P-2). Editor reads `{runDir}/config/*.ini` read-only for `diskDir` (V-10) and the model latch (V-24); it does not edit the ini (D-016 stays out of scope). |
  | 2026-07-18 | D-028 | **Full file management in scope.** `File > New from template` (catalog `platforms`), Duplicate, and Delete. Closes Q-5. Delete guarded by V-30. |
  | 2026-07-18 | D-029 | **Placement resolved:** `tools/Editors/PlatformEditor` (V5), integrated into the root CMakeLists.txt behind `EMULATR_BUILD_EDITORS`; editor + webui staged to `{config}/tools/`. Supersedes the Section 11 `_PROVISIONAL` location and the "sibling of Emulatr" premise. Closes Q-6. |

### 17.10 Section 11 (Build and placement) revision

`[REPLACE the Section 11 placement bullets with:]`

  - CMake subproject at `tools/Editors/PlatformEditor`, pulled into the root build
    behind `option(EMULATR_BUILD_EDITORS OFF)` via `add_subdirectory` (done
    2026-07-18). `find_package(Qt6 COMPONENTS Widgets)` inside the subproject.
  - The sub-CMakeLists still requires, before the option is safe to enable: an MSVC
    guard around its `-Wall -Wextra` flags, namespaced test-target names
    (`platedit_*`) to avoid collisions with the emulator test tree, and
    `PLATEDIT_MANIFEST_DIR` pointed at the real manifest corpus.
  - C++20, MSVC, ASCII(128) enforced on save (T-12).
  - Links NO EmulatR core. Enforced by the target's link list.
  - Editor binary + webui staged to `$<TARGET_FILE_DIR:Emulatr>/tools/` (T-14);
    `platform_schema.json` and `device_catalog.json` ship beside the editor and are
    also the authored source in the tree.
