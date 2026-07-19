# EmulatR Platform Manifest -- PCI to SCSI vDisk Nesting Hierarchy

    Doc id      : SPEC-SCSIH-001
    Status      : DRAFT _PROVISIONAL -- sign-off required on Section 3.2 and
                  Section 11 before manifest schema lands
    Date        : 2026-07-17
    Subject     : The nesting order from PCI hose down to the vDisk backing
                  file, and the manifest shape that expresses it
    Companions  : SPEC-PLATED-001 (manifest editor) -- Section 9 lists impact
                  PCI five-stratum model (S0..S4)
                  IBlockMedia refactor (FileBlockMedia / MockBlockMedia /
                  HostOpticalMedia)
    Reference   : ds20_v7_3_platform.json (manifest_version 1)
    Encoding    : ASCII-128. Hex radix.

---

## 1. The architectural hierarchy

What the hardware actually is, and where each level lands in the manifest.

    Platform  (DS20 / ES40)
    |
    +-- Tsunami / Typhoon chipset
        |
        +-- Pchip N ......................... "hose"     -> hose
            |
            +-- PCI bus ..................... 0x00..0xFF -> bus
                |
                +-- PCI device .............. slot/func  -> slot, func
                    |
                    +-- config header ....... vendor / device / class_code
                    |                          (DERIVED from catalog -- SPEC-PLATED-001)
                    +-- BAR 0..5 ............ register aperture -> bars[]
                    |
                    +-- SCSI HBA function
                        |
                        +-- initiator ID .... the HBA's OWN target ID, typically 0x7
                        |                      -> channels[].initiator_id   *NEW*
                        +-- SCSI bus / channel
                            |                  -> channels[].index <-> storage[].channel
                            +-- target (SCSI ID)
                                |              0x0..0x7 narrow / 0x0..0xF wide
                                |              -> storage[].unit
                                +-- LUN
                                    |          0x0..0x7 -> storage[].lun
                                    +-- logical unit  (VirtualScsiDevice, S4)
                                        |      -> storage[].type
                                        +-- IBlockMedia
                                            |  -> storage[].media_kind
                                            +-- backing file (vDisk)
                                                -> storage[].media

## 2. The key observation -- the triple is already the hierarchy

**`(channel, unit, lun)` IS the SCSI hierarchy, flattened.** It was named for IDE,
but the mapping is exact:

  | Manifest field | IDE meaning         | SCSI meaning     |
  |----------------|---------------------|------------------|
  | `channel`      | primary / secondary | SCSI bus         |
  | `unit`         | master / slave      | target (SCSI ID) |
  | `lun`          | always 0            | LUN              |

**IDE is degenerate SCSI**: two targets, one LUN, one implicit initiator. So the
existing `storage[]` shape generalizes to SCSI **for free**. No new nesting is
required to address a SCSI logical unit.

There is exactly one thing the flat shape cannot hold: **bus-level properties**.
Those are real for SCSI and absent for IDE --

  - `initiator_id`  -- the HBA occupies a target ID on its own bus (typically 0x7)
  - `width`         -- narrow (8-bit) vs wide (16-bit); this DETERMINES the legal
                       target range (0x0..0x7 vs 0x0..0xF)
  - `termination`, `max_speed` -- `_PROVISIONAL`, may not need modelling

They are per-bus, not per-device, and a flat `storage[]` has nowhere to put them.

## 3. The manifest shape

### 3.1 The decision

  **Option A -- deep nest.** `channels[] -> targets[] -> luns[] -> device`.
  Mirrors the physical hierarchy literally. Bus properties sit naturally at the
  bus. Costs: four levels of array nesting; a wholly new schema path set; SCSI
  and IDE diverge into two shapes, two editor paths, two code paths; and in the
  editor, reaching a `media` field means expanding four times.

  **Option B -- flat only.** Keep `storage[]` exactly as-is. Costs: bus
  properties are homeless. `initiator_id` would have to be inferred or hardcoded,
  and target-range validation cannot know the bus width.

  **Option C -- RECOMMENDED. Flat leaf + bus sidecar.**

    "channels": [ { "index": 0, "initiator_id": 7, "width": "wide" } ],
    "storage":  [ { "channel": 0, "unit": 0, "lun": 0, ... } ]

  `storage[].channel` is a **foreign key** into `channels[].index`. Bus properties
  live once, at the bus. Device leaves stay flat and byte-identical in shape to
  the IDE ones.

### 3.2 Why C `[SIGN-OFF]`

  - **One shape for IDE and SCSI.** `storage[]` is unchanged. SPEC-PLATED-001's
    policy paths (`$.pci_devices[*].storage[*].*`) already cover the leaf --
    nothing to re-author, nothing to re-validate.
  - **Nesting depth is where editors go to die.** The nesting the request asked
    for is real, but it is *architectural*, not *representational*. Option C
    expresses the same hierarchy; it just declines to make the user walk it to
    change a filename.
  - **`channels[]` is a sibling of `bars[]`**, at the same level, with the same
    "array of small objects describing controller-level facts" shape. It fits the
    existing grammar rather than extending it.
  - **IDE can adopt `channels[]` later** for free (`index: 0/1`, no
    `initiator_id`) if per-channel properties ever matter, without touching
    `storage[]`.

**Deviation from the request, stated plainly:** the request asked for a
hierarchical nest from PCI to vDisk. Section 1 IS that hierarchy and is
normative. Option C represents it with a foreign key rather than physical
containment at the target/LUN levels. Sign-off requested; if Option A is wanted
regardless, Section 11 Q-1 carries the consequences.

## 4. Level-by-level fields

### 4.1 PCI device level -- the HBA

  | Field           | Tier (SPEC-PLATED-001) | Notes                            |
  |-----------------|------------------------|----------------------------------|
  | `name`          | `openEnum`             | instance label, e.g. `isp1040_0` |
  | `model`         | `enum`                 | catalog key -- drives the derived fields |
  | `hose`/`bus`/`slot`/`func` | `int`       | topology; wiring, editable       |
  | `vendor`/`device`/`class_code` | `derived` | from catalog. SCSI class code is `0x010000` |
  | `bars[]`        | `derived`              | SCSI HBAs have REAL relocatable BARs, unlike Cypress IDE's fixed legacy ports. See Q-3 |
  | `interrupt_pin` | `int`                  | board wiring (JRN/SPEC Q-2 CLOSED) |
  | `channels[]`    | *new*                  | Section 4.2                      |
  | `storage[]`     | unchanged              | Section 4.3                      |

### 4.2 `channels[]` -- the SCSI bus  *NEW*

  | Field          | Tier       | Range / values          | Notes                    |
  |----------------|------------|-------------------------|--------------------------|
  | `index`        | `int`      | 0..(catalog `channels`-1) | FK target for `storage[].channel` |
  | `initiator_id` | `int`      | 0x0..0xF                | HBA's own target ID; default 0x7 |
  | `width`        | `enum`     | `narrow` \| `wide`      | **determines legal `unit` range** |
  | `comment`      | `multiline`|                         |                          |

### 4.3 `storage[]` -- the logical unit. UNCHANGED SHAPE.

  | Field        | Tier        | IDE            | SCSI                          |
  |--------------|-------------|----------------|-------------------------------|
  | `channel`    | `int`       | 0..1           | FK -> `channels[].index`      |
  | `unit`       | `int`       | 0..1           | 0x0..0x7 narrow, 0x0..0xF wide |
  | `lun`        | `int`       | 0              | 0x0..0x7                      |
  | `type`       | `enum`      | `ata_disk`, `atapi_cdrom` | + `scsi_disk`, `scsi_cdrom`, `scsi_tape` `_PROVISIONAL` |
  | `model`      | `openEnum`  | INQUIRY product string -- the guest reads this |
  | `media`      | `path`      | resolved against `[Storage] diskDir` |
  | `media_kind` | `enum`      | Section 8      |                               |
  | `comment`    | `multiline` |                |                               |

## 5. Mapping to the PCI five-stratum model

From the record: **S0 = access primitive**, **S2 = BAR->range rebind (the
identified critical gap)**, **S4 = VirtualScsiDevice**. S1 and S3 are not
reconstructed here -- fill in from the stratum spec rather than trusting this
table.

  | Stratum | Manifest surface                                   |
  |---------|----------------------------------------------------|
  | S0      | none -- host-side primitive, below the manifest     |
  | S1      | `_PROVISIONAL` -- likely config header / `vendor`/`device`/`class_code` |
  | S2      | `bars[]` -- **the rebind gap.** A SCSI HBA is the first device in the tree with genuinely relocatable BARs; Cypress IDE has fixed legacy ports and never exercised S2. See Q-3 |
  | S3      | `_PROVISIONAL` -- likely the function/device model  |
  | S4      | `storage[]` leaf -> `VirtualScsiDevice`            |

**S2 is on the critical path for SCSI and was not on it for IDE.** The manifest's
own comment says the Cypress IDE has "no relocatable BARs" -- so every storage
device modelled so far has bypassed the rebind. The first SCSI HBA is the first
real S2 customer. Scaffolding SCSI without closing the S2 gap will present as
"BARs programmed, registers unreachable."

## 6. Worked example -- DS20E onboard SCSI, slot 7

All `_PROVISIONAL` -- verify identity against the HRM / REFERENCE_INDEX before
use. The DS20E onboard HBA is believed to be a QLogic ISP1040 family part
(KZPBA lineage), the canonical Alpha SCSI HBA for OpenVMS and Tru64.

```json
{ "name": "isp1040_0", "model": "qlogic_isp1040", "hose": 0, "bus": 0,
  "slot": 7, "func": 0,
  "vendor": "0x1077", "device": "0x1020", "class_code": "0x010000",
  "comment": "QLogic ISP1040 SCSI HBA. Slot 7 per REFERENCE_INDEX (DS20E). All identity _PROVISIONAL -- verify. Note slot 7 COLLIDES with de500_tulip's current _PROVISIONAL slot 7 in ds20_v7_3_platform.json; V-01 will fire. Resolve before use.",
  "option_rom": false, "interrupt_pin": 1,
  "bars": [ { "index": 0, "kind": "io",  "size": "0x100" },
            { "index": 1, "kind": "mem", "size": "0x1000" } ],
  "channels": [
    { "index": 0, "initiator_id": 7, "width": "wide",
      "comment": "Single SCSI bus. initiator_id 7 is the conventional HBA ID; targets 0-6 and 8-15 are available to devices." }
  ],
  "storage": [
    { "channel": 0, "unit": 0, "lun": 0, "type": "scsi_disk",
      "model": "EMULATR VIRTUAL DISK", "media": "Alpha/dka0.vdisk",
      "media_kind": "image",
      "comment": "bus 0, target 0, lun 0 -> guest dka0" },
    { "channel": 0, "unit": 3, "lun": 0, "type": "scsi_cdrom",
      "model": "EMULATR VIRTUAL CDROM", "media": "Alpha/ds20_fw_v7_3.iso",
      "media_kind": "iso",
      "comment": "bus 0, target 3, lun 0 -> guest dka300" }
  ] }
```

**Note the slot collision.** `de500_tulip` currently sits at slot 7
(`_PROVISIONAL`, DS10-derived) while the manifest's own comment says DS20E places
DE500 at slot 9 and SCSI at slot 7. Adding SCSI at slot 7 trips V-01 immediately.
That is the validator doing its job -- resolve the DE500 slot first.

## 7. Guest device naming -- how the leaf gets its identity

The triple is not arbitrary; the guest computes its device name from it. This is
the check that a manifest is right.

  | Bus  | Guest name rule                        | Example                        |
  |------|----------------------------------------|--------------------------------|
  | IDE  | `dq{channel_letter}{unit}`             | ch0 unit0 -> `dqa0`; ch0 unit1 -> `dqa1` |
  | SCSI | `dk{bus_letter}{unit*100 + lun}`       | tgt0 lun0 -> `dka0`; tgt3 lun0 -> `dka300`; tgt1 lun1 -> `dka101` |

`_PROVISIONAL` -- confirm the OpenVMS/SRM unit-number formula against the console
before relying on it for expected-output tests. `show dev` at `P00>>>` is the
oracle: if the manifest says target 3 and the console says `dka300`, the mapping
is confirmed end to end.

## 8. `media_kind` -> IBlockMedia

`media_kind` is the **IBlockMedia factory discriminator**. Making that explicit
in the manifest is the point of the field.

  | `media_kind` | IBlockMedia impl    | Notes                                  |
  |--------------|---------------------|----------------------------------------|
  | `image`      | `FileBlockMedia`    | read/write vDisk                       |
  | `iso`        | `FileBlockMedia`    | read-only; ATAPI/SCSI CD semantics      |
  | `host`       | `HostOpticalMedia`  | **not in the current enum** -- Q-4       |
  | (empty)      | no media / `MockBlockMedia` | "no disk", "no disc loaded"     |

The current schema enum is `image | iso` only. If `HostOpticalMedia` is to be
reachable from a manifest, the enum needs `host` and SPEC-PLATED-001's policy
table needs the value added. Q-4.

## 9. Impact on SPEC-PLATED-001

New policy paths:

    $.pci_devices[*].channels[*].index          int
    $.pci_devices[*].channels[*].initiator_id   int   0x0..0xF, default 0x7
    $.pci_devices[*].channels[*].width          enum  narrow | wide
    $.pci_devices[*].channels[*].comment        multiline

Container label format:

    { "path": "$.pci_devices[*].channels[*]",
      "labelFormat": "ch{index}  init={initiator_id}  {width}" }

`storage[*].type` enum extends: `+ scsi_disk, scsi_cdrom, scsi_tape`.

**And one that breaks the policy model.** `storage[*].unit`'s legal range is
`0x0..0x7` on a narrow bus and `0x0..0xF` on a wide one -- it depends on a
**sibling field in a different array** (`channels[width]`, joined by the FK).
SPEC-PLATED-001's policy grammar has only static bounds:

    { "path": "$.pci_devices[*].slot", "tier": "int", "min": 0, "max": 31 }

There is no way to express "max depends on `channels[channel].width`". Two ways
out:

  1. **Widen the widget, narrow the validator.** Spinbox allows 0x0..0xF always;
     a new validation rule catches the violation. Keeps the policy grammar
     static. **Recommended** -- it is consistent with the spec's own P-3
     (non-blocking, warn-don't-correct).
  2. Extend the policy grammar with conditional bounds. More power, more schema,
     and the first step toward the policy file becoming a second competing schema
     definition -- which is SPEC-PLATED-001 R-2 exactly.

New validation rules for that spec's Section 9:

  | Id   | Sev   | Rule                                                          |
  |------|-------|---------------------------------------------------------------|
  | V-14 | error | `storage[*].unit == channels[channel].initiator_id` -- a device cannot occupy the HBA's own target ID |
  | V-15 | error | `storage[*].channel` has no matching `channels[*].index` (dangling FK) |
  | V-16 | error | `width == "narrow"` and `unit > 0x7`                           |
  | V-17 | error | duplicate `channels[*].index` within one HBA                   |
  | V-18 | warn  | `channels[]` present on a model whose catalog entry declares no SCSI support |

## 10. Catalog entries needed

All identity `_PROVISIONAL` -- verify against primary sources before landing.

  | model            | chip              | vendor:device   | Notes                    |
  |------------------|-------------------|-----------------|--------------------------|
  | `qlogic_isp1040` | QLogic ISP1040    | `0x1077:0x1020` | KZPBA lineage; canonical Alpha HBA |
  | `symbios_53c810` | Symbios/NCR 53C810| `0x1000:0x0001` | KZPAA lineage; narrow    |

Catalog entries need a new field to drive the editor:

    "supports": ["storage", "scsi"],
    "channels": 1,          // how many SCSI buses this part has -- bounds channels[].index

## 11. Open questions -- sign-off required

  **Q-1. Option C vs Option A.** Flat leaf + `channels[]` sidecar (recommended,
  Section 3.2), or literal `channels[]->targets[]->luns[]` containment? If A: the
  IDE and SCSI paths diverge permanently, SPEC-PLATED-001's storage policy paths
  are rewritten, and the editor grows two more expansion levels. The hierarchy in
  Section 1 is identical either way -- this is representation only.

  **Q-2. Which HBA is the scaffold target?** `qlogic_isp1040` is the
  recommendation (canonical for Alpha, well-supported by both guest OSes). But
  SCSI is not on the current boot path -- you boot `dqa1` via Cypress IDE, and the
  manifest scopes SCSI to DS20E. **Is SCSI needed for a guest OS boot, or is this
  scaffolding ahead of the need?** If IDE boots OpenVMS, SCSI may be deferrable.

  **Q-3. S2 BAR rebind.** A SCSI HBA is the first device in the tree with real
  relocatable BARs; the Cypress IDE's fixed legacy ports never exercised S2, which
  is the model's known critical gap. **Does S2 close before or as part of SCSI
  scaffolding?** Scaffolding on an open S2 will present as "BARs programmed,
  registers unreachable" -- a confusing failure that looks like an HBA bug.

  **Q-4. `media_kind: host`.** Add it now so `HostOpticalMedia` is reachable, or
  leave the enum at `image | iso` until a manifest needs a physical drive?

  **Q-5. Slot collision.** `de500_tulip` is at slot 7 `_PROVISIONAL` (DS10-derived)
  while the manifest comment says DS20E puts DE500 at slot 9 and SCSI at slot 7.
  Resolve the DE500 slot before adding SCSI, or V-01 fires on arrival.

  **Q-6. S1 / S3.** Section 5 reconstructs S0/S2/S4 from the record and marks S1
  and S3 unknown. Fill from the stratum spec -- the mapping table should not stay
  half-guessed.
