# EmulatR Platform Manifest Editor -- Design Brief and Implementation Spec

    Doc id      : SPEC-PLATED-001
    Status      : DRAFT -- partial sign-off 2026-07-17 (see Section 14, Decision Log)
                  Q-2 CLOSED. Q-4 REOPENED by T-00 evidence. Q-1, Q-3, Q-5, Q-6, Q-7 open.
                  T-00 evidence pass COMPLETE -- see T-00_read_vs_echo_evidence.md.
                  Sections 6-9 reconciled to the evidence (catalog is now a tool-side aid).
                  Frontend prototyped as a web UI (Section 3). Device-authoring model,
                  monikers, guard tiers, and SCSI (SPEC-SCSIH-001) captured in Section 16.
    Date        : 2026-07-17
    Author      : Tim (design) / drafted for Cowork (implementation)
    Subject     : Editor for EmulatR platform device manifests (platform.json)
    Reference   : ds20_v7_3_platform.json (manifest_version 1, 2026-06-16)
    Encoding    : ASCII-128 throughout. Hex radix for addresses and IDs.
    License     : GPL-3.0 (ASA-EmulatR). Copyright (C) 2025-2026 Timothy Peer,
                  eNVy Systems, Inc. Commercial licensing available (peert@envysys.com).

---

## 0. Sign-off gate

No source lands until Section 13 (Open Questions) is resolved. Sections 3, 6, and 7
carry architectural decisions that deviate from or reinterpret the field
classification in the originating request; those deviations are called out
explicitly and marked `[DEVIATION]`.

---

## 1. Problem statement

The platform manifest (`<platform>_platform.json`) is the declarative device
description consumed at EmulatR bring-up: IIC bus population, PCI hose/bus/slot/
func topology, BAR layout, storage attachment, and per-device provisional notes.
It is currently hand-authored. Hand-authoring is failing in three specific ways:

  1. Silent topology errors. A duplicated `(hose,bus,slot,func)` tuple or a
     mistyped IIC address (`0x9e` vs `0x9E` vs `0x09e`) produces a manifest that
     parses cleanly and enumerates wrongly. The failure surfaces hours later as a
     missing device in an SRM retire-trace.

  2. Identity drift. `vendor` / `device` / `class_code` are properties of the
     modelled silicon, not of the platform. A hand-edited manifest can claim
     `model: cypress_ide` with an ALi vendor ID and nothing objects.

  3. Cross-platform divergence. DS10, DS20, and ES40 manifests share most of
     their structure. There is no tooling that makes the deltas legible.

The tool is an authoring and validation aid. It is not part of the emulator
runtime and links no EmulatR core code -- the Qt-free core mandate is unaffected.

---

## 2. Non-goals

  - Not a schema authority. The manifest schema is explicitly not final. The tool
    must not become the thing that blocks schema evolution (see Section 4).
  - Not a device-model editor. It edits manifests, not the C++ device classes.
  - Not a live/attached editor. No connection to a running EmulatR instance.
  - No manifest migration engine in v1. `manifest_version` is displayed, not
    rewritten.

---

## 3. Frontend decision

**Decision (revised 2026-07-17): Qt-free terminal UI (TUI), C++20, CMake, std only.
Qt 6 Widgets is DROPPED. See D-019.**

The DRAFT recommended Qt 6 Widgets on the premise that the toolchain "already exists
on the Z6 (Qt6 + CMake + VS2022), zero new dependencies." In the isolated
development session that premise did not hold -- **Qt is not installed** where the
work is being built and tested, so it was the opposite of low-friction. Rather than
stand up a Qt environment, the frontend is a terminal UI over the same Qt-free core.

Rationale for the TUI:

  - Zero GUI dependency. Pure `std` + ANSI; builds with the compiler already
    present, runs and is testable in the same session as the core (a `--render`
    mode prints one frame with no TTY, so the layout is CI-checkable).
  - The master-detail model (Section 8) maps cleanly to a two-pane text layout:
    left = container tree, right = property pane, bottom = status/issues. The
    "nested hive -> expansion point" model becomes tree expand/collapse.
  - All view logic (label synthesis, tree flattening, property rows) lives in the
    Qt-free core (`ManifestView`), unit-tested headless; only raw-terminal I/O is
    frontend-specific. A different frontend later (Qt, web) reuses that core intact.

Retained from the DRAFT: a single master-detail layout (not nested modal dialogs),
and the DEVIATION note below still applies.

`[DEVIATION]` The request describes some fields as opening into "a window, dialog,
or frame on the page." The spec proposes a single master-detail layout (tree +
property pane) rather than nested modal dialogs. Nested dialogs for nested JSON
would require the user to open three dialogs deep to edit a BAR size. Sign-off
requested on Section 8.

---

## 4. Governing principle: the schema is not final

This is the design constraint that dominates every other decision. A GUI whose
field set is hardcoded in C++ dies on the first schema change. Therefore:

  **P-1. Data-driven.** Widget selection, enumerations, and read/write policy live
  in an external policy file (`platform_schema.json`), not in C++. Adding a field
  to the manifest means editing the policy file, not recompiling.

  **P-2. Preserve-unknown.** Any key present in the manifest but absent from the
  policy file is displayed with a generic editor, is fully editable, and is
  written back byte-for-byte on save. The tool never drops what it does not
  understand. This is what allows the manifest to evolve without the tool
  becoming a bottleneck.

  **P-3. Non-destructive.** The tool warns; it does not correct. `_PROVISIONAL`
  values, catalog conflicts, and validation failures are surfaced in a warnings
  dock and never silently rewritten.

---

## 5. Architecture

### 5.1 Layers

    +--------------------------------------------------+
    | Presentation   MainWindow, TreeView, PropertyPane |
    +--------------------------------------------------+
    | Policy         SchemaPolicy, PathMatcher,         |
    |                WidgetFactory, DeviceCatalog       |
    +--------------------------------------------------+
    | Model          ManifestModel (QAbstractItemModel) |
    +--------------------------------------------------+
    | DOM            Node tree, ordered, typed          |
    +--------------------------------------------------+
    | I/O            OrderedJsonReader / Writer         |
    +--------------------------------------------------+

Downward dependency only. DOM and I/O are Qt-Core-only (no Widgets) so they are
unit-testable headless and reusable by a CLI validate mode (T-11).

### 5.2 DOM node

Do not build the item model directly over `QJsonDocument`. `QJsonValue` is a value
type; `QAbstractItemModel` requires stable parent pointers and stable child
indices. Parse once into an owned tree:

    struct Node {
        NodeKind      kind;      // Object, Array, String, Number, Bool, Null
        std::string   key;       // "" for array elements
        std::string   raw;       // original scalar text, verbatim
        QVariant      value;     // typed scalar
        Node*         parent;
        std::vector<std::unique_ptr<Node>> children;   // AUTHORED ORDER
        int           originIndex;   // ordinal in source, for stable re-emit
        bool          dirty;
    };

Each node exposes a canonical path (Section 6.1) computed from its ancestry.

### 5.3 Key-order preservation -- MANDATORY

`QJsonObject` stores keys **sorted lexicographically**. A naive
`QJsonDocument::fromJson()` -> edit -> `toJson()` round trip **will reorder every
object in the manifest**. Applied to `ds20_v7_3_platform.json`, `comment` migrates
above `manifest_version`, `bars` above `class_code`, and every review diff becomes
unreadable noise.

Requirements:

  - `OrderedJsonReader` is a hand-rolled recursive-descent parser producing the
    Node tree in authored order. It does **not** route through `QJsonObject`.
  - `OrderedJsonWriter` emits authored order, 2-space indent, `\n` line endings,
    strict JSON (no `//`, no `/* */` -- the manifest convention of `comment`
    string fields is preserved as ordinary data).
  - Scalars that were not edited re-emit from `Node::raw` verbatim. This preserves
    `"0x9e"` as-authored rather than normalizing it to `"0x9E"`, and preserves
    hex-vs-decimal choices exactly.
  - Source file uses mixed tab/space indentation (see `iic_ocp0` entry). The
    writer normalizes indentation to 2 spaces. This is the one intentional
    normalization; it must be applied in a single dedicated commit so the diff is
    reviewable.

**Round-trip contract:** load any of the three manifests, make zero edits, save.
Output must be semantically identical and diff-clean modulo the indentation
normalization above. This is the T-01 acceptance test and it is a hard gate.

### 5.4 Tree model

`ManifestModel : QAbstractItemModel` exposes **containers only** -- objects and
arrays. Scalars are not tree rows; they are fields in the property pane of their
parent container. Rationale: a scalar-bearing tree produces 40-row single-column
chains and buries `bars[0].size` three expansions deep. Tree for structure,
property pane for values.

Tree shape for the DS20 reference manifest:

    DS20  (platform root)
    +-- iic_devices  [8]
    |   +-- iic_system0      0x70   status
    |   +-- iic_system1      0x72   status
    |   +-- iic_smb0         0xA2   fru_eeprom
    |   +-- iic_cpu0         0xA4   fru_eeprom
    |   +-- iic_rcm_nvram0   0xC0   nvram
    |   +-- iic_ocp0         0x40   status
    |   +-- iic_ocp1         0x42   status
    |   +-- iic_rcm_temp     0x9e   status
    +-- pci_devices  [3]
        +-- cypress_isa      0:0:5.0   0x1080:0xc693
        +-- cypress_ide      0:0:5.1   0x1080:0xc693
        |   +-- storage  [2]
        |       +-- ch0 unit0  ata_disk     dqa0
        |       +-- ch0 unit1  atapi_cdrom  dqa1
        +-- de500_tulip      0:0:7.0   0x1011:0x0019
            +-- bars  [2]
                +-- BAR0  io   0x80
                +-- BAR1  mem  0x80

Row label is synthesized from a policy-declared `labelFormat` per container type,
so it survives schema drift. Nodes carrying validation warnings get a decorated
icon; nodes whose `comment` contains `_PROVISIONAL` get a distinct decoration.

---

## 6. Field policy

### 6.1 The governing test: does the platform builder read this key?

**Signed off 2026-07-17. T-00 evidence pass COMPLETE
(`T-00_read_vs_echo_evidence.md`).** Tier assignment is not a matter of taste. One
question decides it:

  > A field is **editable** if changing it changes what the emulator does.
  > A field is **derived** if it does not.

The rule stands; the prediction that rode alongside it in the DRAFT does not. The
DRAFT assumed `CypressIde` hardcodes its own PCI config header, so
`pci_devices[*].vendor` is an **echo** the parser never reads. **The evidence
refutes this.** `systemLib/PlatformConfig.cpp` reads `vendor`, `device`,
`class_code`, `revision`, `subsys_vendor`, `subsys_id`, and every `bars[*]` field
(`:177-199`); `validate()` hard-errors on a missing or `0x0000`/`0xFFFF` `vendor`
(`:366`); and `synthesizePciConfig()` (`:595`) builds the 256-byte config header
and BAR size masks directly from those fields. By the test above they are
**read -> editable**, not echo. Greying them out would grey exactly the fields the
synthesizer consumes.

One caveat the evidence forces us to state plainly: **the PCI consumer is not wired
yet.** `synthesizePciConfig()` is called nowhere today -- only the IIC path is live
(`Machine.cpp:480`) -- so *at this instant* editing a PCI identity field changes
nothing observable. That is an accident of the unlanded "P4" consumer, not a design
choice: the header names config synthesis as the intended path
(`PlatformConfig.h:26-38`). We build the tool for the architecture, not for the
current gap. PCI identity is an editable input.

Actual partition (from the evidence artifact, superseding the DRAFT prediction):

| Class                    | Keys                                                                                                                                                                                 |
|--------------------------|--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| **Read -> editable**     | `platform`, `address`, `class`, `byte`, `manufacturer`, `model`, `part_class`, `serial`, `revision_ro`, `revision_rw`, `size`(nvram), `hose`, `bus`, `slot`, `func`, `vendor`, `device`, `class_code`, `revision`, `subsys_vendor`, `subsys_id`, `option_rom`, `interrupt_pin`, `bars[*].*`, all `storage[*].*` |
| **Read, non-behavioral** | `name` (informational: validate tag / log only, never consumed)                                                                                                                      |
| **Not read**             | `comment` (any level); `manifest_version` (a `== 1` gate, not a configurable value)                                                                                                   |

Consequences, reconciled with the evidence:

  - `interrupt_pin` is **editable** -- confirmed: read at `:187`, written to config
    offset 0x3D (`:611`). Q-2 stands.
  - `bars[*]` is **editable** (`hex`/`enum`/`int`/`bool`), *reversing* the DRAFT's
    `derived`. `size`, `kind`, `index`, `prefetch` are all read and synthesized into
    size-probe masks (`:613-637`). **Q-4 is REOPENED** -- see Section 13.
  - `vendor` / `device` / `class_code` are **editable**, not echo. The DRAFT's
    "identity is redundant with `model`" premise is false: the emulator has **no
    device catalog**, and `model` does not reconstruct identity -- it only selects a
    backing behavior (`generic` / `passive` / a named model, `:77-84`). Identity is
    authored in the manifest and read from it.

**What becomes of the catalog and the `derived` tier.** With identity authored (not
model-derived), `device_catalog.json` stops being a source of truth and becomes at
most a **tool-side authoring aid**: it may autofill sensible defaults for a known
`model` and warn (V-07) when the manifest disagrees, but the fields it touches stay
ordinary editable widgets, never greyed labels. The `derived` tier is retired
(Section 6.3), and Section 7 is rewritten to match -- the catalog is now an optional
autofill + drift-warning aid, not a source of truth.

### 6.2 The collision problem, and why policy is path-keyed

The originating classification lists `type` under both readonly and selectable,
`kind` under both readonly and selectable, and both `device-name` (readonly) and
`Name` (selectable). These are not contradictions in the request -- they are
evidence that **the same leaf name means different things at different depths**:

    pci_devices[*].bars[*].kind        io | mem               -> enum (editable)
    pci_devices[*].storage[*].type     ata_disk | atapi_cdrom  -> enum (editable)
    iic_devices[*].class               status|fru_eeprom|nvram|led -> enum
    iic_devices[*].size                nvram byte count        -> int  (bytes)
    pci_devices[*].bars[*].size        BAR aperture            -> hex
    pci_devices[*].storage[*].size     disk capacity           -> size-string (K/M/G)

The `size` collision is a **triple**, not a pair -- three keys, three paths, three
formats: `iic_devices[*].size` (int bytes, `PlatformConfig.cpp:154`),
`pci_devices[*].bars[*].size` (hex, `:196`), and `pci_devices[*].storage[*].size`
(suffixed capacity like `4G`, `:221`). Same leaf, incompatible policy. **Therefore
policy is keyed by path, never by leaf name.** Path grammar:

    $.platform
    $.iic_devices[*].address
    $.pci_devices[*].bars[*].kind
    $.pci_devices[*].storage[*].media

`*` matches any array index. Longest-literal-prefix wins on conflict. Unmatched
paths fall through to P-2 passthrough.

### 6.3 Reframing the three tiers `[DEVIATION]`

The request's "readonly" and "selectable" tiers overlap because both express the
same intent: *the user must not free-type this*. The distinction that actually
matters mechanically is **where the value comes from**:

| Tier          | Meaning                                          | Widget                  |
|---------------|--------------------------------------------------|-------------------------|
| `enum`        | Closed set, fully known                          | `QComboBox` (locked)    |
| `openEnum`    | Known set exists but authoring new values is legitimate | `QComboBox` (editable) |
| `free`        | Genuinely free text                              | `QLineEdit`             |
| `multiline`   | Prose                                            | `QPlainTextEdit`        |
| `int`         | Bounded integer                                  | `QSpinBox`              |
| `hex`         | Hex scalar, validated                            | `QLineEdit` + validator |
| `bool`        | Boolean flag                                     | `QCheckBox`             |
| `size`        | Byte capacity, optional K/M/G/T suffix           | `QLineEdit` + validator |
| `path`        | Filesystem reference                             | `QLineEdit` + browse    |
| `passthrough` | Unknown to policy (P-2)                          | Type-inferred, editable |

**The `derived` tier is retired.** The DRAFT defined it as "populated from the
catalog, never typed / greyed `QLabel`." The evidence (6.1) shows the fields it
covered -- `vendor`, `device`, `class_code`, `bars[*]` -- are read and synthesized,
so they are ordinary editable scalars (`hex` / `enum` / `int` / `bool`). The
per-field **override** mechanism of Section 7.1 existed only to escape the greyed
state; with no greyed state it is moot for identity fields. What survives is
optional **catalog autofill** (Section 7, follow-up): selecting a known `model` may
prefill these editable fields with catalog defaults and V-07 warns on disagreement
-- but the widget is always the editable one for its type, never a label.

Two new tiers the evidence requires: `bool` (`option_rom`, `bars[*].prefetch`,
`storage[*].enabled`, `storage[*].create_if_missing` are read via `toBool`,
`PlatformConfig.cpp:186,197,219,220`) and `size` (`storage[*].size` accepts a
K/M/G/T suffix, `:221`).

This still honours the request's real intent -- small bounded fields get validated
widgets, `media_kind` stays a dropdown -- while telling the truth about which fields
the emulator reads.

### 6.4 Policy table

Resolved by path. **T-00 complete** (`T-00_read_vs_echo_evidence.md`); tiers below
reflect what the parser actually reads, with `PlatformConfig.cpp` line cites. `[E]`
marks a constraint the **emulator itself** enforces (Section 9). `[?]` marks a
range still needing sign-off.

| Path                                | Tier        | Values / notes (`:line` = PlatformConfig.cpp) |
|-------------------------------------|-------------|-----------------------------------------------|
| `$.manifest_version`                | `int` (RO)  | gate only; `[E]` must be exactly 1 (`:266`); not a configurable value |
| `$.platform`                        | `openEnum`  | DS10, DS20, ES40, DS25, ES45; `[E]` must match ini `[System] model` (`Machine.cpp:508`) |
| `$.comment`                         | `multiline` | not read by emulator (`:21`); free doc field |
| `$.iic_devices[*].name`             | `openEnum`  | informational label (`:130`); emulator enforces no uniqueness |
| `$.iic_devices[*].address`          | `hex`       | 0x00..0xFE; `[E]` even + unique (`:341,343`)   |
| `$.iic_devices[*].class`            | `enum`      | status, fru_eeprom, nvram, **led** (`:70-74`)  |
| `$.iic_devices[*].byte`             | `hex`       | 0x00..0xFF; status/led read value (`:155`)     |
| `$.iic_devices[*].size`             | `int`       | nvram only; `[E]` > 0 (`:154,351`)             |
| `$.iic_devices[*].manufacturer`     | `openEnum`  | DEC, ...; `[E]` required for fru_eeprom (`:347`)|
| `$.iic_devices[*].model`            | `openEnum`  | `[E]` required for fru_eeprom (`:348`)         |
| `$.iic_devices[*].part_class`       | `free`      | FRU JEDEC (`:148`)                             |
| `$.iic_devices[*].serial`           | `free`      | FRU JEDEC, 6-bit packed (`:149`)               |
| `$.iic_devices[*].revision_ro`      | `hex`       | **new** -- FRU JEDEC byte (`:152`)             |
| `$.iic_devices[*].revision_rw`      | `hex`       | **new** -- FRU JEDEC byte (`:153`)             |
| `$.iic_devices[*].comment`          | `multiline` |                                                |
| `$.pci_devices[*].name`             | `openEnum`  | informational label (`:161`)                   |
| `$.pci_devices[*].model`            | `openEnum`  | `generic`, `passive`, or a **named** model string (`:77-84`); `[E]` required. NOT a closed catalog key |
| `$.pci_devices[*].hose`             | `int`       | 0..1 `[?]` (`:171`)                            |
| `$.pci_devices[*].bus`              | `int`       | 0..255 (`:172`)                                |
| `$.pci_devices[*].slot`             | `int`       | 0..31; `[E]` BDF unique (`:173,364`)           |
| `$.pci_devices[*].func`             | `int`       | 0..7 (`:174`)                                  |
| `$.pci_devices[*].vendor`           | `hex`       | **editable** (was derived); `[E]` required, != 0x0000/0xFFFF (`:177,366`) |
| `$.pci_devices[*].device`           | `hex`       | **editable** (was derived); `[E]` required (`:179`) |
| `$.pci_devices[*].class_code`       | `hex`       | **editable** (was derived); 24-bit (`:181`)    |
| `$.pci_devices[*].revision`         | `hex`       | **new** -- config header (`:182`)              |
| `$.pci_devices[*].subsys_vendor`    | `hex`       | **new** -- config header (`:183`)              |
| `$.pci_devices[*].subsys_id`        | `hex`       | **new** -- config header (`:184`)              |
| `$.pci_devices[*].option_rom`       | `bool`      | `toBool` (`:186`); was `enum` true/false       |
| `$.pci_devices[*].interrupt_pin`    | `int`       | 0..4 (0=none, A..D); config offset 0x3D (`:187,611`). Q-2 |
| `$.pci_devices[*].comment`          | `multiline` |                                                |
| `$.pci_devices[*].bars[*].index`    | `int`       | **editable** (was derived); 0..5; `[E]` <=5, unique (`:193,372-373`) |
| `$.pci_devices[*].bars[*].kind`     | `enum`      | **editable** (was derived); io, mem (`:194`)   |
| `$.pci_devices[*].bars[*].size`     | `hex`       | **editable** (was derived); aperture -> probe mask (`:196,632`) |
| `$.pci_devices[*].bars[*].prefetch` | `bool`      | **new** -- memory BARs (`:197`)                |
| `$.pci_devices[*].storage[*].label` | `free`      | **device moniker** (16.3): default = media basename minus ext, overrideable; tool/display only (emulator ignores; P-2 preserves) |
| `$.pci_devices[*].storage[*].channel`| `int`      | 0..1; `[E]` (`:207,382`)                       |
| `$.pci_devices[*].storage[*].unit`  | `int`       | 0..1; `[E]` (`:208,383`)                       |
| `$.pci_devices[*].storage[*].lun`   | `int`       | 0..7 (`:209`)                                  |
| `$.pci_devices[*].storage[*].type`  | `enum`      | ata_disk, atapi_cdrom (`:210`)                 |
| `$.pci_devices[*].storage[*].model` | `openEnum`  | INQUIRY string (`:216`)                        |
| `$.pci_devices[*].storage[*].media` | `path`      | resolves vs ini `[Storage] diskDir` (`:217`)   |
| `$.pci_devices[*].storage[*].media_kind`| `enum`  | image, iso, **host** (`:218`)                  |
| `$.pci_devices[*].storage[*].enabled`| `bool`     | **new** -- default true; false = skipped (`:219`) |
| `$.pci_devices[*].storage[*].create_if_missing`| `bool`| **new** -- ata_disk only (`:220`)          |
| `$.pci_devices[*].storage[*].size`  | `size`      | **new** -- create_if_missing capacity; `[E]` > 0 and %512 (`:221,392-394`) |
| `$.pci_devices[*].storage[*].comment`| `multiline`|                                                |

`[DEVIATION]` The request specifies `hose` / `bus` / `slot` / `func` as text edit.
Spec proposes `QSpinBox` with the ranges above. These are small bounded integers
where a validated spinner strictly dominates free text and eliminates an entire
class of topology typo. Sign-off requested.

The DRAFT's second `[DEVIATION]` (fields "not classified by the request") is
withdrawn: `address`, `byte`, `class`, `manufacturer`, `part_class`, `serial`,
`revision_ro`, `revision_rw`, `channel`, `unit`, and `lun` are no longer inferred
-- the T-00 pass confirms each is read by the parser at the line cited above, and
their tiers follow from how the code consumes them. `address` still earns the
strictest validator (`[E]` even + unique): the `iic_rcm_temp @ 0x9e` root-cause and
the parser's hard-error on an odd address (`:341`) both say a mistyped IIC address
is expensive.

### 6.5 Policy file sketch

```json
{
  "policy_version": 1,
  "rules": [
    { "path": "$.pci_devices[*].bars[*].kind",
      "tier": "enum", "values": ["io", "mem"] },
    { "path": "$.pci_devices[*].storage[*].media_kind",
      "tier": "enum", "values": ["image", "iso", "host"] },
    { "path": "$.pci_devices[*].vendor",
      "tier": "hex", "catalogDefault": "catalog:$.pci_devices[*].model#vendor" },
    { "path": "$.pci_devices[*].slot",
      "tier": "int", "min": 0, "max": 31 }
  ],
  "containers": [
    { "path": "$.pci_devices[*]",
      "labelFormat": "{name}  {hose}:{bus}:{slot}.{func}  {vendor}:{device}" },
    { "path": "$.iic_devices[*]",
      "labelFormat": "{name}  {address}  {class}" }
  ]
}
```

**`labelFormat` token resolution.** The `pci_devices[*]` format above references
`{vendor}` and `{device}`. The emulator requires both (V-15), so they are normally
present in the DOM -- but *optional* identity tokens a label might use
(`{class_code}`, `{subsys_vendor}`) may be absent from a minimal manifest. The
formatter resolves each token in this order:

    1. the node's own DOM value, if the key is present  (the authored truth)
    2. the tool catalog's default for the node's `model`, if one exists
    3. the empty string, and only then

Resolving from the DOM alone makes the label go blank for any token a manifest
legitimately omits. Order 1-before-2 matters: the authored value must win over the
catalog default -- the label is a view of the manifest, not of the catalog.

---

## 7. Device catalog -- optional authoring aid

**Rewritten 2026-07-17 (T-00).** The DRAFT called `device_catalog.json` "the reason
the `derived` tier exists" and "the single point of truth for silicon identity."
The evidence retired the `derived` tier (6.1): the emulator has **no catalog** and
reads PCI identity and BARs straight from the manifest. So the catalog is neither a
source of truth nor required for the tool to work. It is an **optional authoring
aid** with two jobs:

  1. **Autofill.** When the user selects a known `model`, prefill the (editable)
     identity fields with sensible defaults and show the catalog `comment` as
     field-level help.
  2. **Drift warning (V-07).** When an authored value disagrees with the catalog
     default for its `model`, surface it in the dock. Advisory only -- the manifest
     is authoritative; the catalog is a reference.

A tool with no catalog file still edits every field; it just offers no autofill and
raises no V-07. This is the opposite of the DRAFT, where a missing catalog would
have broken the `derived` fields.

Sketch (identity values are *defaults*, not truth; `model` values match the
parser's `generic` / `passive` / named vocabulary, `PlatformConfig.cpp:77-84`):

```json
{
  "catalog_version": 1,
  "pci_models": [
    { "model": "cypress_isa", "vendor": "0x1080", "device": "0xc693",
      "class_code": "0x060100", "interrupt_pin": 0, "bars": [], "supports": [],
      "comment": "Cypress 82C693 ISA bridge, func0. Shares 0x1080:0xc693 with cypress_ide (same multifunction part, func0 vs func1). Fixed legacy south bridge." },
    { "model": "cypress_ide", "vendor": "0x1080", "device": "0xc693",
      "class_code": "0x010100", "interrupt_pin": 0, "bars": [], "supports": ["storage"],
      "comment": "Cypress 82C693 IDE func1. Fixed legacy ports; no relocatable BARs." }
  ],
  "backing_models": ["generic", "passive"],
  "platforms": ["DS10", "DS20", "DS25", "ES40", "ES45"]
}
```

`generic` and `passive` are **backing-model keywords the parser understands**
(`:77-84`), not silicon entries: they carry no identity defaults, so the tool
offers no autofill and every field is authored -- exactly what `de500_tulip` does in
`ds20_v7_3_platform.json` (`model: generic` with hand-authored `vendor`/BARs). Any
other non-empty `model` string is a **named** model to the emulator (it routes to a
behavioral class); if the tool catalog has no entry for it, that is not an error --
no autofill, V-08 info, everything preserved (P-2).

**Identity is not a key.** `cypress_isa` and `cypress_ide` deliberately share
`0x1080:0xc693` -- one multifunction part, func0 (ISA bridge) and func1 (IDE),
distinguished by `class_code`. Two consequences, still binding:

  - Autofill and V-07 key on **model -> identity**, never identity -> model. The
    reverse is not a function.
  - **Do not add a "duplicate vendor/device" guard.** It would false-positive on
    every multifunction chip. V-01's `(hose,bus,slot,func)` tuple is the uniqueness
    check and already distinguishes these two (0:0:5.0 vs 0:0:5.1).

Future: generate the catalog from the EmulatR device registry at build time so tool
defaults and the emulator cannot disagree. Out of scope for v1.

### 7.1 Deliberate divergence -- the bring-up probe

**Signed off 2026-07-17; reframed by T-00.** A recurring bring-up case is
deliberately lying to the firmware -- presenting a wrong vendor ID -- to see whether
SRM enumerates differently. The `iic_rcm_temp @ 0x9e` root-cause is precedent.

Under the DRAFT this needed a special "override" to unlock a greyed `derived` field.
With identity now editable (6.1) there is nothing to unlock -- the user just edits
the field. What remains worth building is making the divergence **loud**, so a probe
is never mistaken for an authored-and-correct value:

  - When an editable identity field diverges from its catalog default, render it in
    a visually distinct **diverged** state (not the ordinary editable look) and
    raise V-07 in the dock for as long as the divergence lives.
  - Offer `Adopt catalog default` -- one click to snap back to the reference -- and
    show both values. Nothing changes without the click (P-3).
  - The value is persisted as an ordinary manifest scalar; the manifest gains
    nothing special. The *tool* remembers it disagrees with the catalog, because the
    catalog is right there to compare against.

Rationale: a throwaway probe should not require touching the catalog, but it must
not look identical to a real value. The experiment is loud instead of silent.

---

## 8. UI layout

```
    +-------------------------------------------------------------------+
    | File  Edit  Device  Validate  Help                                |
    +---------------------------+---------------------------------------+
    |  DS20                     |  cypress_ide                          |
    |  +-- iic_devices [8]      |  --------------------------------     |
    |  |   +-- iic_system0      |  name        [cypress_ide       v]    |
    |  |   +-- ...              |  model       [cypress_ide       v]    |
    |  |   +-- iic_rcm_temp  !  |  hose        [0]  bus  [0]            |
    |  +-- pci_devices [3]      |  slot        [5]  func [1]            |
    |      +-- cypress_isa      |  vendor      [0x1080]     = catalog   |
    |      +-- cypress_ide   <  |  device      [0xc693]     = catalog   |
    |      |   +-- storage [2]  |  class_code  [0x010100]   = catalog   |
    |      +-- de500_tulip   P  |  option_rom  [false             v]    |
    |                           |  interrupt_pin [0]                    |
    |                           |  comment     +-------------------+    |
    |                           |              | Cypress 82C693... |    |
    |                           |              +-------------------+    |
    +---------------------------+---------------------------------------+
    |  Issues (2)                                                       |
    |  ! iic_rcm_temp   address 0x9f is odd (V-06 error, rejects)       |
    |  P de500_tulip    comment contains _PROVISIONAL (slot unverified) |
    +-------------------------------------------------------------------+
```

**Reading this mockup.** The `!` row illustrates V-06 *after a fat-finger edit* of
`iic_rcm_temp` (`0x9e` -> `0x9f`) -- which is precisely the Section 1 motivating
failure, and precisely what this tool exists to catch. On the as-authored
reference manifest every IIC address is even (`0x70 0x72 0xA2 0xA4 0xC0 0x40 0x42
0x9e`), so **V-06 does not fire**; the tree decoration and the warning row appear
only once the value is mistyped. Do not read the mockup as a depiction of
`ds20_v7_3_platform.json` at rest -- it depicts the tool doing its job.
`interrupt_pin` renders as an editable int (Q-2). **V-06 is now an `error`** (§9):
the emulator would reject the whole manifest on an odd address, so the dock is
titled `Issues`, not `Warnings`. `vendor` / `device` / `class_code` render as
ordinary editable fields with a `= catalog` concordance hint (they match the
catalog default, so no V-07) -- no longer greyed `(catalog)` labels (6.1, Q-4).

  - Left: structure tree. Right: property pane for the selected node. Bottom:
    issues dock (dockable, hideable) -- errors, warnings, and info together.
  - Identity fields (`vendor`, `device`, `class_code`, `bars[*]`) render as ordinary
    editable widgets (6.1), with a subtle concordance hint: `= catalog` when the
    value matches the catalog default, a **diverged** decoration + V-07 when it does
    not (7.1). They are never greyed labels -- the emulator reads them, so they are
    the user's to set.
  - Clicking a warning selects the offending node and focuses the field.
  - Label copy follows the manifest's own vocabulary. The pane says `hose`,
    `func`, `class_code` -- not `Bus Number` or `Device Class`. The user speaks
    manifest.

---

## 9. Validation

All validation is non-blocking **in the tool** (P-3): save is never prevented;
issues persist in the dock. But the **emulator** is not so forgiving -- a hard error
in `PlatformConfig::validate` discards the *entire* manifest and boots the default
DS10 (`PlatformConfig.cpp:290-295`). So the tool's severities must mirror the
emulator where it enforces a rule; a tool `warn` against an emulator hard-reject
understates the consequence.

The `By` column: **E** = enforced by the emulator (mirror severity exactly);
**T** = tool-only value-add (the emulator does not check it); **I** = informational.
`:line` = `PlatformConfig.cpp` unless noted. Ids V-01..V-13 keep their DRAFT numbers
(cross-referenced elsewhere); V-14..V-24 are the enforced rules the DRAFT lacked.

| Id    | By | Severity | Rule                                                                  | Evidence |
|-------|----|----------|----------------------------------------------------------------------|----------|
| V-01  | E  | error    | duplicate BDF `(hose,bus,slot,func)` in `pci_devices`                 | `:364`   |
| V-02  | E  | error    | duplicate `address` in `iic_devices`                                  | `:343`   |
| V-03  | T  | warn     | duplicate `name` within an array (emulator ignores `name`)           | --       |
| V-04  | E  | error    | duplicate `(channel,unit)` among **enabled** storage targets (note: `(channel,unit)`, not `(channel,unit,lun)`) | `:386` |
| V-05  | T  | warn     | hex field fails `^0x[0-9a-fA-F]+$` (emulator silently reads 0)        | --       |
| V-06  | E  | **error**| IIC `address` is odd -- **CORRECTED from `warn`**: the emulator rejects the manifest | `:341` |
| V-07  | T  | warn     | catalog autofill value disagrees with the authored value (drift). Advisory now that identity is editable (6.1) -- the catalog is an aid, not a gate | -- |
| V-08  | T  | warn     | `model` unknown to the tool catalog (autofill unavailable; not an error) | --    |
| V-09  | T  | warn     | `bars[*].size` not a power of two                                    | --       |
| V-10  | T  | warn     | `media` non-empty and unresolvable under ini `[Storage] diskDir`     | --       |
| V-11  | I  | info     | `comment` contains `_PROVISIONAL`                                    | --       |
| V-12  | I  | info     | key present in manifest, absent from policy (P-2 passthrough)        | --       |
| V-13  | T  | error    | any non-ASCII-128 codepoint in any string (house style)             | --       |
| V-14  | E  | error    | `manifest_version != 1` (emulator falls back to default)             | `:331`   |
| V-15  | E  | error    | PCI `vendor` absent, `0x0000`, or `0xFFFF`                           | `:366`   |
| V-16  | E  | error    | `fru_eeprom` missing `manufacturer` or `model`                      | `:347-348`|
| V-17  | E  | error    | `nvram` `size == 0`                                                  | `:351`   |
| V-18  | E  | error    | `model` is a named controller but the name is empty                 | `:368`   |
| V-19  | E  | error    | BAR `index > 5`, or duplicate BAR `index` within a device           | `:372-373`|
| V-20  | E  | error    | storage `channel > 1` or `unit > 1`                                 | `:382-383`|
| V-21  | E  | error    | `create_if_missing` on non-`ata_disk`, with `size == 0`, or `size` not a multiple of 512 | `:389-394` |
| V-22  | E  | warn     | device has `storage` but `model` is not a named controller          | `:377`   |
| V-23  | E  | warn     | platform presence: DS10 missing 0x70/0x72/0xA2; DS20 missing 0x40/0x42 or present 0x4E (get_sysvar discriminator) | `:405-422` |
| V-24  | E  | error    | ini `[System] model` != manifest `platform` (cross-surface latch)   | `Machine.cpp:508` |
| V-25  | E  | error    | SCSI `storage[*].unit` == `channels[channel].initiator_id` (a device cannot occupy the HBA's own target id) | SCSIH-001 |
| V-26  | E  | error    | SCSI `storage[*].channel` has no matching `channels[*].index` (dangling FK) | SCSIH-001 |
| V-27  | E  | error    | `channels[*].width == narrow` and `storage[*].unit > 7`             | SCSIH-001 |
| V-28  | E  | error    | duplicate `channels[*].index` within one HBA                        | SCSIH-001 |
| V-29  | E  | warn     | `channels[]` on a model whose catalog declares no SCSI `supports`   | SCSIH-001 |

V-25..V-29 are the SCSI rules from SPEC-SCSIH-001 §9, **renumbered from that spec's V-14..V-18**
to avoid colliding with V-14..V-24 above. `_PROVISIONAL` until the emulator gains SCSI support.

V-24 is the cross-surface rule and it matters: the guest SRM badges by the device
bus it probes, not by the ini, so a model/platform mismatch is a real bring-up fault
catchable before launch -- but only if the tool can see the ini (Section 3 scope
question). V-06 is `error`, not the DRAFT's `warn`. V-11 stays `info`: `_PROVISIONAL`
is a valid authoring state, not a defect. The DRAFT's V-09 (power-of-two BARs) and
V-13 (ASCII-128) are genuine value-adds the emulator does *not* check -- kept, but
honestly labelled `T`.

---

## 10. Ticket plan

| Id   | Ticket                                                          | Gate |
|------|-----------------------------------------------------------------|------|
| T-00 | **Evidence pass. DONE 2026-07-17** -> `T-00_read_vs_echo_evidence.md`. Parser is `systemLib/PlatformConfig.cpp`; consumer `systemLib/Machine.cpp`; second surface `config/EmulatorSettings.h` | **Unblocks T-02/T-03.** Sections 6 and 9 rewritten from this result (echo hypothesis refuted; Q-4 reopened; 9 unlisted keys added) |
| T-01 | `Node` DOM + `OrderedJsonReader` / `OrderedJsonWriter`           | round-trip test on all three manifests, diff-clean |
| T-02 | `PathMatcher` + `SchemaPolicy` loader                            | unit tests incl. the `size` collision case |
| T-03 | `DeviceCatalog` loader + conflict detector                       | unit tests |
| T-04 | `ManifestModel : QAbstractItemModel` (containers only)           | tree renders DS20 per Section 5.4 |
| T-05 | `WidgetFactory` -- tier -> widget, driven by policy              | all nine tiers instantiate |
| T-06 | `PropertyPane` + two-way binding + dirty tracking                | edit -> DOM -> save round trip |
| T-07 | Catalog autofill + `Adopt catalog default` + diverged-state indication (7.1) | no silent overwrite; a value diverged from the catalog is visually distinct and raises V-07 |
| T-08 | Structural edit: add / remove / duplicate device `[?]` Q-3       | blocked on sign-off |
| T-09 | `Validator` + warnings dock + click-to-navigate                  | V-01..V-24 |
| T-10 | File open / save / save-as, `.bak` on overwrite, dirty prompt    |      |
| T-11 | Headless CLI mode: `PlatformEditor --validate <file>`, exit code | usable from a build step |
| T-12 | Packaging, windeployqt, ASCII-128 enforcement on save            |      |

**Exit criterion (litmus).** Load DS10, DS20, and ES40 manifests. For each: zero
edits, save, semantic diff empty. Then: change `de500_tulip` slot 7 -> 9 via the
GUI, save, diff shows exactly one changed line. Until both hold, the tool is not
trustworthy for authoring and no manifest is edited with it.

---

## 11. Build and placement

  - Standalone CMake target `PlatformEditor`, `find_package(Qt6 COMPONENTS Widgets)`.
  - C++20, MSVC, warnings-as-errors consistent with the main tree.
  - Links **no** EmulatR core. Enforced by the target's link list, reviewed at T-12.
  - Proposed location: `D:\EmulatR\EmulatRAppUniV4\PlatformEditor\` `_PROVISIONAL`
    -- see Q-6.
  - `platform_schema.json` and `device_catalog.json` ship beside the binary and are
    also checked into the tree as the authored source.

---

## 12. Risks

  - **R-1.** Catalog and emulator device registry drift apart; the editor asserts
    an identity the emulator does not implement. Mitigated in v1 by V-08 warning
    only; properly fixed by build-time catalog generation (out of scope).
  - **R-2.** Policy file becomes a second, competing schema definition. Mitigated
    by P-2: policy is advisory presentation metadata, never a gate on what the
    manifest may contain.
  - **R-3.** Scope creep into a general JSON editor. The catalog and the
    validation table are what make this an EmulatR tool rather than a worse
    version of a text editor. If those are thin, the tool has no reason to exist.

---

## 13. Open questions -- sign-off required

  **Q-1. `name` semantics.** The request lists `device-name` as readonly and
  `Name` as selectable. Spec reads `name` as the *instance label* (`iic_cpu0`,
  `cypress_ide`) and proposes `openEnum` + uniqueness check: pick a known role, or
  type a new one. Correct? Or is `name` intended to be catalog-derived from
  `model`, making it truly readonly?

  **Q-2. `interrupt_pin`. CLOSED 2026-07-17 -> editable (`int`).** The pin is board
  wiring, not silicon identity. If IRQ routing reads it, changing it changes what
  the emulator does, and the read-vs-echo test (6.1) puts it in the editable
  column. This reverses the spec's initial classification. Confirmation still
  contingent on T-00.

  **Q-3. Structural editing (T-08).** The field classification covers editing
  existing devices only. Must the GUI *add* and *remove* devices -- a new PCI card,
  a fourth IIC node -- or is v1 a value editor over an existing skeleton? This is
  the single largest scope fork in the spec.

  **Q-4. `bars` and identity fields. REOPENED 2026-07-17 by T-00 evidence.** The
  CLOSED decision ("`bars[*]` is `derived`; silicon decodes what it decodes") rested
  on the premise that the emulator does not read these fields. It does:
  `PlatformConfig.cpp:189-199` reads every BAR field and `synthesizePciConfig`
  (`:613-637`) builds the size-probe masks from them, exactly as it builds the
  config header from `vendor`/`device`/`class_code`. There is no catalog in the
  emulator and no `freeform` flag; identity and BARs are authored in the manifest.
  **Proposed re-close:** `bars[*]` and PCI identity are `editable` (`hex`/`enum`/
  `int`/`bool`), with the tool catalog demoted to optional autofill + drift warning
  (V-07), not a source of truth. Sections 6 and 9 are already rewritten on this
  basis; this question asks you to ratify that, or to override the evidence and keep
  a greyed guardrail despite the parser reading the fields. **Recommend ratify.**

  **Q-5. New manifest from scratch.** Does the tool need `File > New` (template
  from catalog `platforms`), or does it only ever open an existing manifest?

  **Q-6. Placement.** `PlatformEditor\` as a sibling of `Emulatr\`? The existing
  tools convention (`Emulatr\tools\`) is scoped to shell scripts, so a CMake
  subproject there seems wrong. Your call.

  **Q-7. `manifest_version`.** Display-only in v1. When the schema does move, does
  the tool refuse to open a newer `manifest_version`, or open it in
  passthrough-everything mode with a banner? Spec assumes the latter.

---

## 14. Decision log

| Date       | Id    | Decision                                                    |
|------------|-------|-------------------------------------------------------------|
| 2026-07-17 | D-001 | Qt 6 Widgets. Not QML, not web. Existing toolchain, and Widgets is the correct tool for tree-structured data editors |
| 2026-07-17 | D-002 | Policy is path-keyed, never leaf-name-keyed. Evidence: `iic_devices[*].size` and `pci_devices[*].bars[*].size` are the same leaf with opposite policy |
| 2026-07-17 | D-003 | Hand-rolled ordered JSON reader/writer. `QJsonObject` sorts keys lexicographically and would reorder every object in the manifest |
| 2026-07-17 | D-004 | P-2 preserve-unknown. The schema is not final; the tool must not become the thing that blocks it evolving |
| 2026-07-17 | D-005 | **The read-vs-echo test governs tier assignment.** A field is editable iff changing it changes what the emulator does. Echo fields made editable are worse than readonly: the user types, nothing happens, the manifest lies |
| 2026-07-17 | D-006 | T-00 evidence pass gates the policy table. Section 6.4 is a hypothesis until the parser is grepped |
| 2026-07-17 | D-007 | `interrupt_pin` -> editable (reverses initial classification). Q-2 closed |
| 2026-07-17 | D-008 | `bars[*]` -> derived; `freeform: true` is the escape hatch for `generic`. Q-4 closed |
| 2026-07-17 | D-009 | Per-field override for deliberate firmware probes, not a general unlock. V-07 stays lit for the life of the override |
| 2026-07-17 | D-010 | Echo fields (`vendor`/`device`/`class_code`) retained despite being pure redundancy, for manifest legibility. V-07 is what makes the redundancy safe. Accepted as a trade, with the drift risk noted |
| 2026-07-17 | D-011 | **Catalog lookup and V-07 key on model -> identity, never the reverse.** `cypress_isa` and `cypress_ide` share `0x1080:0xc693` (one multifunction part, func0/func1). No duplicate-identity guard; V-01's tuple is the uniqueness check. Raised by Cowork review |
| 2026-07-17 | D-012 | `labelFormat` tokens resolve DOM -> catalog -> empty, in that order. Derived tokens may be absent from a minimal manifest; overrides must win over the catalog. Raised by Cowork review |
| 2026-07-17 | D-013 | Section 8 mockup depicts the tool *catching* a fault, not the reference manifest at rest. V-06 does not fire on `ds20_v7_3_platform.json` -- all eight IIC addresses are even, `0x9e` included. Corrects an error in the original draft |
| 2026-07-17 | D-014 | **T-00 evidence pass complete** against `systemLib/PlatformConfig.cpp`. The "echo -> derived" hypothesis is **refuted**: the parser reads `vendor`/`device`/`class_code`/`revision`/`subsys_*`/`bars[*]`, validates them, and `synthesizePciConfig` builds the config header from them. **Supersedes D-005's application and D-010** (there are no echo fields; identity is authored, not model-derived). The read-vs-editable *principle* (D-005) stands; its predicted partition did not |
| 2026-07-17 | D-015 | PCI identity and `bars[*]` are **editable** (`hex`/`enum`/`int`/`bool`). The `derived` tier is retired; per-field override (7.1) is moot. Q-4 reopened for ratification. **Supersedes D-008** |
| 2026-07-17 | D-016 | There are **two config surfaces**: the JSON manifest and `config/EmulatrV4.ini` (`EmulatorSettings.h`). They are coupled -- `[System] model` selects the manifest + firmware, `[Storage] diskDir` resolves `media`, and `Machine.cpp:508` latches ini `model` == manifest `platform` (new rule V-24). Scope of a two-surface tool is an open question for Tim (Section 3) |
| 2026-07-17 | D-017 | Validation severities realigned to the emulator: odd IIC address is `error` not `warn` (V-06); a hard error discards the whole manifest. Rules split E (emulator-enforced, mirror) vs T (tool-only). Added V-14..V-24 for enforced rules the DRAFT lacked; V-04 keyed on `(channel,unit)` not `(channel,unit,lun)` |
| 2026-07-17 | D-018 | Nine keys the parser reads were absent from the DRAFT policy table and are now added: `revision_ro`, `revision_rw` (IIC FRU), `revision`, `subsys_vendor`, `subsys_id` (PCI), `bars[*].prefetch`, `storage[*].enabled`, `storage[*].create_if_missing`, `storage[*].size`. Plus enum corrections: IIC `class` gains `led`; `media_kind` gains `host`; PCI `model` is `openEnum` (`generic`/`passive`/named), not a closed catalog key |
| 2026-07-17 | D-019 | **Qt 6 Widgets DROPPED; frontend is a Qt-free terminal UI. Reverses D-001.** Qt was chosen for "zero new dependencies (already on the Z6)"; in the isolated build/test session Qt is not installed, making it high-friction. The TUI needs no GUI toolkit, builds/runs/tests in-session, and reuses the Qt-free core (DOM, policy, catalog, `ManifestView`) unchanged. A `--render` mode makes the layout CI-checkable without a TTY |
| 2026-07-17 | D-020 | **Frontend prototyped as a web UI** (browser + local server over the Qt-free core); working direction alongside/over D-019's TUI. The core is frontend-agnostic and shared, so neither TUI nor web work is wasted. EmulatR branding; GPL-3.0 attribution in About. See 16.1 |
| 2026-07-17 | D-021 | **Three device guard tiers** (16.2): required (hard, emulator presence-checked), baseline (soft, "not recommended", platform stock controllers), user (free CRUD). Required is per-platform, emulator-derived, belongs in the policy as data |
| 2026-07-17 | D-022 | **Device moniker** (16.3): storage `label`, default = media basename minus extension, overrideable; tool/display field (P-2). In tested core (`storageMoniker`) |
| 2026-07-17 | D-023 | **Device-kind picker** (16.4): user picks Virtual Disk / CD / Tape / Physical CD; tool sets (type x media_kind). `media_kind` is the IBlockMedia factory discriminator. Bus-aware; physical uses a host-drive selector |
| 2026-07-17 | D-024 | **SCSI follows SPEC-SCSIH-001 Option C** (16.5): `channels[]` sidecar, `storage[].channel` FK, `unit` = SCSI target. Catalog `qlogic_isp1040`/`symbios_53c810`. That spec's `derived` reconciled to editable+autofill (T-00); its V-14..V-18 renumbered to V-25..V-29 |

---

## 15. Review corrections applied

| Date       | Source        | Correction                                        |
|------------|---------------|---------------------------------------------------|
| 2026-07-17 | Cowork review | Section 8 mockup showed `interrupt_pin` as `(catalog)`, contradicting Q-2 CLOSED in the same document. Fixed to editable int |
| 2026-07-17 | Cowork review | Section 8 mockup warned that `0x9e` is odd. `0x9e` is 158, even. The illustration was fabricated -- the rule (V-06) is sound but fired on nothing. Re-illustrated with a mistyped `0x9f`, which is the Section 1 failure mode and genuinely odd |
| 2026-07-17 | Cowork review | Section 7 catalog sketch omitted `cypress_isa`, so the reference manifest's first PCI device would trip V-08 against the spec's own example. Added |
| 2026-07-17 | Cowork review | Section 6.5 `labelFormat` referenced derived tokens with no resolution rule. Specified (D-012) |
| 2026-07-17 | Cowork review | Section 7 lacked a keying-direction note; a future implementer could plausibly add an identity-uniqueness guard that false-positives on every multifunction chip. Specified (D-011) |
| 2026-07-17 | T-00 evidence (`PlatformConfig.cpp`) | The echo/`derived` classification of `vendor`/`device`/`class_code`/`bars[*]` was wrong -- the parser reads and synthesizes them. Sections 6.1, 6.3, 6.4 rewritten; `derived` tier retired; Q-4 reopened (D-014, D-015) |
| 2026-07-17 | T-00 evidence | `derived`-tier premise assumed a `model`-driven device catalog; the emulator has none, and `model` is `generic`/`passive`/named, not a catalog key. **Section 7 rewritten**: catalog demoted to optional autofill + drift-warning aid; 7.1 override reframed as diverged-state indication |
| 2026-07-17 | T-00 evidence | **Section 8 reconciled**: mockup identity fields (`vendor`/`device`/`class_code`) now editable with `= catalog` concordance hint, not greyed `(catalog)` labels; dock retitled `Issues`; V-06 shown as error |
| 2026-07-17 | T-00 evidence | Section 9 severities did not match the emulator (odd address was `warn`, is `error`; whole-manifest reject on any hard error). Rewritten with E/T split and V-14..V-24 (D-017) |
| 2026-07-17 | T-00 evidence | Second config surface (`EmulatrV4.ini` / `EmulatorSettings.h`) and the ini/manifest latch were undocumented. Added as V-24 and the Section 3 scope question (D-016) |
| 2026-07-17 | SPEC-SCSIH-001 | SCSI spec's `derived` tier and its `V-14..V-18` collided with the post-T-00 PLATED-001 (derived retired; V-14..V-24 already assigned). Reconciled: SCSI identity/BARs are editable+autofill; SCSI validators renumbered to `V-25..V-29` (D-024, Section 16.5) |

---

## 16. Device-authoring model, branding, and SCSI (2026-07-17 session)

Decisions taken while prototyping the frontend. All are design-level and data-driven
(P-1/P-2): none required core code changes beyond the shared, tested view layer.

### 16.1 Frontend and branding
Qt -> TUI -> **web UI** (D-001 -> D-019 -> D-020). The web UI (browser over a small local
server exposing the Qt-free core) is the working prototype: master-detail tree + property
pane + issues dock, a `+` CRUD toolbar, and a File / Validate / Help menu. Branding is
**EmulatR Platform Editor**; the About panel carries the GPL-3.0 / eNVy Systems attribution.
The core (DOM, policy, catalog, `ManifestView`) is frontend-agnostic; TUI and web share it,
so neither line of work is wasted. Prototype: `webui/mockup.html`.

### 16.2 Guard tiers -- required / baseline / user
The tool SUPPLIES the per-platform baseline device tree (controllers + topology); the user
CRUD-manages the leaves. Three tiers govern edit/delete:

| Tier     | Guard                       | Applies to                                                        |
|----------|-----------------------------|-------------------------------------------------------------------|
| required | hard -- Delete blocked       | emulator presence-checked IIC: 0x70/0x72/0xA2, DS20 OCP 0x40/0x42, discriminator 0x9e |
| baseline | soft -- CRUD allowed, warns   | platform stock controllers (cypress_isa/ide, on-board NIC)        |
| user     | free CRUD                   | anything the user adds (e.g. a SCSI HBA + disks)                  |

The `required` set is emulator-derived (`PlatformConfig.cpp` presence checks) and is
**per-platform** (DS10 vs DS20 vs ES40 differ), so it belongs in the policy as data, not
hardcode.

### 16.3 Device moniker (storage `label`)
Storage leaves get a friendly moniker: the explicit `label` override, else the media
filename minus directory and extension, else the address. Tool/display only (the emulator
ignores it; P-2 preserves it). In the tested core (`storageMoniker`, `containerLabel`).
Example: media `Alpha/OpenVMS_v82.iso` -> moniker `OpenVMS_v82`.

### 16.4 Device-kind picker (type x media_kind)
The user does not set `type`/`media_kind` by hand -- they pick a device KIND and the tool
sets both plus defaults. `media_kind` is the IBlockMedia factory discriminator
(SPEC-SCSIH-001 Sec 8):

| Kind             | type                     | media_kind | backend                    |
|------------------|--------------------------|------------|----------------------------|
| Virtual Disk     | ata_disk / scsi_disk     | image      | FileBlockMedia (r/w)       |
| Virtual CD-ROM   | atapi_cdrom / scsi_cdrom | iso        | FileBlockMedia (r/o)       |
| Virtual Tape     | scsi_tape                | image      | tape backend               |
| Physical CD-ROM  | atapi_cdrom / scsi_cdrom | host       | HostOpticalMedia           |
| (empty media)    | --                       | --         | MockBlockMedia (no media)  |

Bus-aware: an IDE controller offers Disk / CD / Physical CD; a SCSI controller adds Tape and
uses `scsi_*` types. Virtual media browses a file (resolved vs `[Storage] diskDir`); Physical
browses a host drive. Address auto-assigns to the next free `(channel,unit,lun)`, skipping the
SCSI initiator id.

### 16.5 SCSI -- SPEC-SCSIH-001 (Option C)
SCSI is authored per SPEC-SCSIH-001, Option C (flat leaf + `channels[]` sidecar): `channels[]`
on the HBA carries `initiator_id`/`width`; `storage[].channel` is a foreign key into
`channels[].index`; the SCSI target id **reuses `storage[].unit`** (0..7 narrow / 0..15 wide) --
"IDE is degenerate SCSI". Catalog models `qlogic_isp1040` (`0x1077:0x1020`) and `symbios_53c810`
(`0x1000:0x0001`), with `supports:[storage,scsi]` + `channels:N`. `type` gains
`scsi_disk`/`scsi_cdrom`/`scsi_tape`. New policy paths: `channels[*].{index,initiator_id,width,
comment}`. Guest-name check (Sec 7): SCSI `dk{bus}{unit*100+lun}`, IDE `dq{ch}{unit}`.

Two reconciliations with that spec (it was written against pre-T-00 PLATED-001):
its `derived` tier for identity/BARs is **editable + catalog autofill** here (an HBA's BARs are
real and manifest-authored); its `V-14..V-18` are **renumbered `V-25..V-29`** (Section 9).
**All SCSI is `_PROVISIONAL`:** the v4 emulator has no SCSI storage type
(`storageTypeFromString` = ata_disk/atapi_cdrom only) and the PCI/SCSI consumer + S2 BAR-rebind
are deferred (SCSIH-001 Q-2/Q-3). Verify identity/addressing against v5 before landing.
