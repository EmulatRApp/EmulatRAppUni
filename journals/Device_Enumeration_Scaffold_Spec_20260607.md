# Device Enumeration Scaffold + JSON Platform Manifest -- Specification and Architecture
# EmulatR V4 -- 2026-06-07
# Author: Claude (Anthropic), with T. Peer (project architect)
# Status: DESIGN. No code rides with this doc (house rule: discuss before code).
# Companion to: GCT_FRU_Support_Spec_20260607.md (the FRU hang this scaffold subsumes).
# Locked decisions (architect, 2026-06-07; REVISED post-review same day):
#   D-PARSE   USE Qt QJsonDocument (REVISED -- was hand-rolled). Qt6 is already a
#             project dependency and QJsonDocument is already used no-throw in
#             deviceLib/SRMEnvStore.cpp (QJsonParseError error-return, NOT
#             exceptions). Parsing happens ONCE at/near init, off any hot path, so
#             Qt's overhead is irrelevant. The no-exceptions constraint that
#             originally motivated a hand-rolled reader is moot with Qt's API.
#   D-SCOPE   IIC FIRST, PCI AFTER A CHECKPOINT (REVISED -- was "together"). The
#             IIC/FRU/GCT path is the current DS10 bring-up critical path; PCI
#             enumeration layers in afterward. New order: P1 JSON, P2 manifest+
#             synthesis, P3 IIC+FRU/GCT, ==CHECKPOINT==, P4 PCI.
#   D-CONTENT high-level fields in the manifest; the loader synthesizes the
#             on-wire image (JEDEC EEPROM bytes + checksums, PCI config header).
#   D-VERSION manifest carries a manifest_version; loader rejects/migrates
#             incompatible versions (mirrors kChipsetVersion discipline).
#   D-VALIDATE an explicit validation phase runs after parse, before any consumer
#             binds, failing fast with a clear diagnostic on a bad manifest.
#   D-PCIMODEL each PCI device names an explicit "model" backing (generic config-
#             only vs a named behavioral model vs passive), so routing is declared.

--------------------------------------------------------------------------------
## 0. Summary

Replace per-device hardcoded seeding in the C++ device models with a single
declarative platform manifest (JSON) that EmulatR loads at chipset construction
into an in-memory DeviceManifest. Two consumers read that manifest: IicPcf8584
(IIC node table) and TsunamiPchip (PCI device table, via the existing
registerPciDevice seam). The firmware's own discovery -- iic_init bus sniff,
build_fru walk, PCI get_matching_pb scan -- then finds a faithful, complete set
of devices every cold boot, with no recompile required to add or edit one. This
subsumes the FRU `set sys_serial_num' fix (model IIC 0x70/0x72 + FRU identity)
and provides the home for the deferred PCI-enumeration / on-board-device work.

--------------------------------------------------------------------------------
## 1. Motivation and scope

1.1  Shared discovery surface. The IIC node sniff (iic_init / iic_node_list),
     the FRU branch build (build_smb_hw/cpu_hw/power_hw/fru_*), and the PCI
     option scan (build_pci_fru -> get_matching_pb) are the SAME activity:
     reading device presence and identity off the buses. Point-fixing each in
     C++ is N hardcoded seedings, each needing a recompile and a ~30-minute cold
     boot to test. One manifest collapses that into a single editable surface.

1.2  Iteration economics. The win is in OUR loop, not the guest's. A true cold
     boot still re-enumerates every power-on (~30 min init + the dva0.0.0.0
     option-firmware phase, ~45-60 min). The manifest removes the recompile from
     each device change; paired with predig snapshots it removes the replay too:
     edit JSON, mint a snapshot once, restore thereafter.

1.3  Honest boot-time caveat. The scaffold does NOT shorten the guest's per-boot
     enumeration. The ONLY guest-time win available is the dva0 option-firmware
     scan IF that time is dominated by the firmware timing out on PCI devices
     that answer ambiguously; modeling them to respond definitively (including
     "no expansion ROM") could shrink it. That is a HYPOTHESIS to confirm by
     profiling the scan (what PA range / which BDFs it sweeps), not a promise.

1.4  In scope (first cut): the manifest format, the reader, the synthesis, the
     IIC table consumer (IicPcf8584), and the PCI table consumer (registerPciDevice
     + a generic config-space handler). Out of scope: a full Tulip/SCSI behavioral
     model (config-space identity + sane BARs only), and any guest-time bypass of
     enumeration (that is the snapshot story, not this).

--------------------------------------------------------------------------------
## 2. Architecture overview

    ds10_platform.json  (manifest, high-level fields)
            |
            v
    PlatformConfig::load(path)            <-- Qt QJsonDocument (no-throw) + validate()
            |  returns LoadStatus{ok,error,line}; populates:
            v
    DeviceManifest  (in-memory, owned by TsunamiChipset)
       |-- iicDevices[]  : { name, addr, class, <synthesized 256B image or 1B status> }
       |-- pciDevices[]  : { name, bdf, vendor, device, classCode, bars[], optionRom, <256B cfg> }
            |
       +----+--------------------------------+
       v                                      v
    IicPcf8584 (consumes iicDevices)     TsunamiPchip (consumes pciDevices)
       address -> table lookup ->            registerPciDevice(bdf, handler)
       ACK + byte/stream, else NAK           handler.pciConfigRead/Write from cfg[]

Ownership and timing: TsunamiChipset constructs the DeviceManifest from the
manifest path (env EMULATR_PLATFORM_CONFIG, default "ds10_platform.json"), BEFORE
it constructs/initializes m_iic and registers PCI devices, so both consumers bind
to populated tables. Missing/invalid manifest -> a built-in default manifest
(equivalent to today's hardcoded DS10 set) so the build never hard-fails on a bad
file; the load error is logged.

--------------------------------------------------------------------------------
## 3. JSON manifest schema (high-level fields)

Top level:
    manifest_version : number  (REQUIRED; current = 1. Loader rejects an unknown
                                future version and may migrate an older one.
                                Same discipline as the snapshot kChipsetVersion.)
    platform   : string   (label, e.g. "DS10"; selects built-in defaults if no file)
    iic_devices: array of IIC device objects
    pci_devices: array of PCI device objects

IIC device object:
    name      : string    (matches iic_node_list name, informational)
    address   : hex-string (even node address, e.g. "0xA2", "0x70")
    class     : enum  "fru_eeprom" | "nvram" | "status" | "led"
    -- class fru_eeprom (256-byte JEDEC, synthesized; see Sec 5):
       manufacturer : string  (e.g. "DEC")     -> manuf TLV / JEDEC id
       model        : string  (e.g. "DS10")
       part_class   : string  (e.g. "54-")
       serial       : string  (may be empty; guest may overwrite at runtime)
       revision_ro  : hex-string (e.g. "0x20")
       revision_rw  : hex-string (e.g. "0x21")
    -- class nvram (zero-filled by default, mutable at runtime):
       size         : number (bytes, default 256)
    -- class status / led (1-byte register):
       byte         : hex-string (the value reads return, e.g. "0x00")

PCI device object:
    name       : string
    hose,bus,slot,func : numbers (BDF; slot == PCI device number)
    vendor     : hex-string  (e.g. "0x1011")
    device     : hex-string  (e.g. "0x0019")
    class_code : hex-string  (24-bit, e.g. "0x020000" = network/ethernet)
    revision   : hex-string  (default "0x00")
    subsys_vendor, subsys_id : hex-string (optional)
    bars       : array of { index:0..5, kind:"mem"|"io", size:hex-string,
                            prefetch:bool }  (loader sizes/masks them)
    option_rom : bool        (false => expansion-ROM BAR reads 0 => no ROM scan)
    interrupt_pin : number   (1..4 = INTA..INTD; 0 = none)
    model      : enum  "generic" | "passive" | "<named>"   (REQUIRED, D-PCIMODEL)
                 "generic" = the synthesized config-only ManifestPciDevice (Sec 7);
                 "passive" = answers config reads but absorbs all writes (a pure
                 presence stub, e.g. to silence a probe); "<named>" routes to a
                 specific behavioral model class (e.g. "cypress_isa" ->
                 Cypress_CY82C693ISABridge) when one exists. Declaring the backing
                 in the manifest keeps device routing explicit and greppable.

Notes: hex-strings (not JSON numbers) for all addresses/IDs so authors never fight
JSON's double-precision number rules; the loader reads "0x.." explicitly (Qt's
QJsonValue::toString then a hex parse). All fields except manifest_version,
name/address(IIC), and bdf/vendor/device/model(PCI) have defaults.

--------------------------------------------------------------------------------
## 3.1 DS10 reference manifest (initial; IDs marked _PROVISIONAL pending dump)

    {
      "manifest_version": 1,
      "platform": "DS10",
      "iic_devices": [
        { "name":"iic_system0", "address":"0x70", "class":"status", "byte":"0x00" },
        { "name":"iic_system1", "address":"0x72", "class":"status", "byte":"0x00" },
        { "name":"iic_smb0",  "address":"0xA2", "class":"fru_eeprom",
          "manufacturer":"DEC", "model":"DS10", "part_class":"54-", "serial":"" },
        { "name":"iic_cpu0",  "address":"0xA4", "class":"fru_eeprom",
          "manufacturer":"DEC", "model":"EV6", "part_class":"54-", "serial":"" },
        { "name":"iic_rcm_nvram0", "address":"0xC0", "class":"nvram", "size":256 }
      ],
      "pci_devices": [
        { "name":"cypress_isa", "model":"cypress_isa", "hose":0,"bus":0,"slot":5,"func":0,
          "vendor":"0x1080","device":"0xc693","class_code":"0x060100",
          "option_rom":false, "interrupt_pin":0 },
        { "name":"cypress_ide", "model":"generic", "hose":0,"bus":0,"slot":5,"func":1,
          "vendor":"0x1080","device":"0xc693","class_code":"0x010180",
          "bars":[{"index":4,"kind":"io","size":"0x10"}], "option_rom":false },
        { "name":"symbios_scsi", "model":"generic", "hose":0,"bus":0,"slot":6,"func":0,
          "vendor":"0x1000","device":"0x000f","class_code":"0x010000",
          "option_rom":false, "interrupt_pin":1 },
        { "name":"de500_tulip", "model":"generic", "hose":0,"bus":0,"slot":7,"func":0,
          "vendor":"0x1011","device":"0x0019","class_code":"0x020000",
          "bars":[{"index":0,"kind":"io","size":"0x80"},
                  {"index":1,"kind":"mem","size":"0x80"}],
          "option_rom":false, "interrupt_pin":1 }
      ]
    }
    (IDs above are _PROVISIONAL -- see the note below. The real file is strict
     JSON: no /* */ or // comments; use a "comment" string field instead.)

The slot 5 (ISA) and slot 6 (SCSI) BDFs match reference_pc264_pci_irq_and_bridge_bdf
(Cypress ISA = Bus0/Dev5/Func0); build_pci_fru SKIPS hose0/bus0 slot5 + slot6 by
design, so those two do not get FRU descriptors but DO answer config cycles.
Vendor/device/class_code values are _PROVISIONAL -- confirm against a real DS10
lspci/SRM `show config' dump or apisrm dc287_def.h before claiming fidelity.

--------------------------------------------------------------------------------
## 4. JSON parsing -- Qt QJsonDocument (D-PARSE, REVISED)

No hand-rolled reader. Qt6 is already a project dependency (CMakeLists
find_package(Qt6 Core Network Concurrent), AUTOMOC on) and QJsonDocument is
ALREADY used in-tree at deviceLib/SRMEnvStore.cpp with the no-throw idiom we
mirror:

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(bytes, &err);  // never throws
    if (err.error != QJsonParseError::NoError) {               // error-return
        // log err.errorString() + err.offset; fall back to built-in default
        return loadStatus(false, err.errorString(), err.offset);
    }

Why this is correct for V4: Qt's JSON API reports failure through QJsonParseError,
not C++ exceptions, so it is fully compatible with the exceptions-disabled build
(the original reason a hand-rolled reader was considered). Parsing runs ONCE
at/near system init (TsunamiChipset construction), off every hot path, so Qt's
allocation cost is irrelevant. We get full RFC 8259, QJsonObject/QJsonArray
navigation, and a maintained parser for free.

Placement: systemLib/PlatformConfig.{h,cpp} reads the file (QFile), parses via
QJsonDocument, then walks QJsonObject -> DeviceManifest. Hex-strings are pulled
with QJsonValue::toString() + a "0x" QString hex parse (toULongLong(&ok,16)); a
small helper jsonHex(const QJsonValue&, uint64_t&)->bool centralizes it. No new
systemLib/Json.{h,cpp} component is created.

NOTE: Qt JSON does NOT support // or /* */ comments or trailing commas. The
manifest must be strict JSON. Author comments via a "comment" string field
(ignored by the loader) rather than // lines. The _PROVISIONAL /* */ markers in
the Sec 3.1 listing above are DOC-ONLY illustration and must NOT appear in the
real file.

--------------------------------------------------------------------------------
## 4.1 Validation phase (D-VALIDATE)

After QJsonDocument parse succeeds and BEFORE any consumer binds, run an explicit
PlatformConfig::validate() pass that fails fast with a precise diagnostic:

    - manifest_version present and == supported (else reject/migrate; D-VERSION).
    - every IIC device: address present, even, in 0x00..0xFE; class in the enum;
      no DUPLICATE IIC addresses; class-required fields present (status->byte,
      fru_eeprom->manufacturer/model, nvram->size>0).
    - every PCI device: bdf (hose/bus/slot/func) in range; vendor/device present;
      model present and resolvable ("generic"/"passive"/a known named class);
      no DUPLICATE (hose,bus,slot,func); each BAR index 0..5 unique, kind valid.
    - cross-check: IIC FRU addresses the firmware is known to probe for DS10
      (0x70,0x72 status; 0xA2 smb) SHOULD be present -- warn (not fail) if absent,
      since a partial board is legal but usually a mistake.
    validate() returns a status list (ok + per-error message + which device);
    on any hard error the loader logs all of them and uses the built-in default
    manifest, so a bad file degrades gracefully instead of half-configuring.

--------------------------------------------------------------------------------
## 5. Synthesis (high-level fields -> on-wire image)

5.1  FRU EEPROM (class fru_eeprom) -> 256-byte JEDEC image.
     Reuse the offset map already in IicPcf8584::seedJedecImage (jedec_def.sdl):
         0x48 manuf_location; 0x49..0x4B part_class; 0x7D/0xFD DEC id 0xB5;
         0x7E revision_ro; 0xFE revision_rw; serial into the model/serial TLV span.
         0x3F/0x7F/0xFF = JEDEC-21C segment checksums (sum low byte of each span).
     The loader fills the named fields, writes the manufacturer/model/serial
     strings into their byte ranges, then computes the three checksums LAST. The
     existing jedecSum() helper moves into the synthesizer. Output is the 256-byte
     array the IIC read path streams.

5.2  status / led (class status) -> single byte (the "byte" field). For 0x70/0x72
     this is the build_power_hw fail/func register. Default 0x00 (passes step 5;
     see the FRU spec Sec 5.2 for the bit semantics if a realistic `show power'
     is wanted later).

5.3  nvram (class nvram) -> zero-filled array of "size"; mutable at runtime; the
     guest's writes are runtime state (Sec 7.4), not manifest content.

5.4  PCI device -> 256-byte config-space header.
     vendor/device at 0x00/0x02; revision at 0x08; class_code at 0x09..0x0B;
     header-type 0x00 at 0x0E; interrupt_pin at 0x3D. For each declared BAR, the
     loader writes a base+flags pattern and remembers the size so config writes of
     all-ones return the size mask (the standard BAR-sizing handshake) -- this is
     what lets the firmware compute a real base instead of the all-ones garbage
     that today pokes TsunamiPchip UNHANDLED (see project_pci_enum_unhandled_ffff0000).
     expansion-ROM BAR (0x30) = 0 when option_rom=false.

--------------------------------------------------------------------------------
## 6. IIC consumer -- IicPcf8584 refactor

Today IicPcf8584 hardcodes a FRU bank (0xA0..0xAE) + RCM bank (0xC0..0xCE) with a
compile-time enable flag. Replace with a manifest-bound table:

    - Construction takes a const reference / span of the manifest IIC devices.
    - eepromIndex(addr) becomes a lookup into that table by even address; present
      -> ACK + the device's class behavior (256B stream for fru/nvram, 1B for
      status/led); absent -> NAK (unchanged "module absent" semantics).
    - The transaction state machine (START/ACK/NAK, P1-P5 contract, dummy-first
      read) is UNCHANGED -- only the presence/content source changes.
    - status/led devices answer reads with their constant byte; writes absorbed.
    - The EMULATR_FRU_EEPROM gate and seedFruBank()/seedJedecImage() are retired
      into the synthesizer; presence is now "is it in the manifest", which is the
      faithful model (a populated manifest = a populated board).
    - EMULATR_IIC_TRACE logging is preserved and extended to status/led nodes.

Snapshot: the mutable EEPROM/NVRAM bytes remain serialized (kChipsetVersion bump
if the layout changes); the manifest itself is NOT serialized -- it is reload-time
config, re-read at construction (Sec 7.4).

--------------------------------------------------------------------------------
## 7. PCI consumer -- manifest-driven registration

7.1  A generic ManifestPciDevice implements IPciDeviceHandler (pciConfigRead/
     pciConfigWrite). It owns the synthesized 256-byte config header and the BAR
     size masks. Reads return header bytes; writes store, except BAR-sizing writes
     of 0xFFFFFFFF return the size mask on the next read (standard handshake).

7.2  TsunamiChipset, after loading the manifest, iterates pci_devices and calls
     the existing m_pchip.registerPciDevice(bus, slot, func, handler) for each.
     Empty slots already float 0xFFFFFFFF in TsunamiPchip (no change needed).

7.3  Option ROM / the dva0 scan. With option_rom=false every modeled device's
     expansion-ROM BAR reads 0, so the firmware's option-firmware probe finds "no
     ROM" immediately instead of shadowing/timing out. IF profiling shows the scan
     dwelling on a specific BDF, give that device a definitive answer in the
     manifest. (Confirm the scan's actual cost source first -- Sec 1.3.)

7.4  Snapshot relationship. The manifest defines device IDENTITY (vendor/device/
     class/BARs) -- constant, reloaded every construction, never serialized.
     Runtime-mutable state (BAR base values the firmware programs, EEPROM writes)
     stays in the existing snapshot image. Rule: identity from manifest, state
     from snapshot. This keeps a stale snapshot from masking a manifest edit.

7.5  Provisional identity fields. PCI vendor, device, class_code, and
     BAR size values in the initial DS10 manifest are estimates until 
     confirmed against a real hardware dump or firmware source. Any
     such field in the C++ PciDeviceEntry struct (and its JSON
     counterpart) must carry a _PROVISIONAL suffix comment at the
     declaration site:

       uint16_t vendor;   // _PROVISIONAL -- confirm against hw dump

     The comment and suffix are removed in the same edit that lands
     the confirmed value. This mirrors the standing _PROVISIONAL
     discipline on IPR/SCBD values in the data-fidelity convention.

--------------------------------------------------------------------------------
## 8. Integration, build, and house conventions

    - New files: systemLib/PlatformConfig.{h,cpp} (DeviceManifest + QJsonDocument
      loader + validate() + synthesizer), deviceLib/Tsunami/ManifestPciDevice.h.
      NO hand-rolled JSON component (uses Qt; see Sec 4). Manifest data file:
      ds10_platform.json next to the firmware/flash (cwd or EMULATR_PLATFORM_CONFIG).
    - CMake: add PlatformConfig.cpp to the lib target (Qt6::Core already linked) +
      a test_platform_config doctest TU (CHECK only). No new Qt module needed --
      QtCore provides QJsonDocument.
    - No exceptions (Qt JSON is error-return, not throw), include guards (not
      pragma once), ASCII(128) source, doctest CHECK only -- per house rules.
    - Built-in default manifest compiled in (the current DS10 set) so a missing
      file is non-fatal and CI needs no external data file.

--------------------------------------------------------------------------------
## 9. Phased implementation plan (REVISED -- IIC first, PCI after a checkpoint)

Architect re-sequencing (2026-06-07): the IIC/FRU/GCT path is the current DS10
bring-up CRITICAL PATH; PCI enumeration layers in afterward. Validate FRU/GCT
behavior at a hard checkpoint BEFORE any PCI work.

P1  Qt JSON load wrapper (systemLib/PlatformConfig parse side via QJsonDocument)
    + manifest_version handling (D-VERSION) + validate() phase (D-VALIDATE).
    doctest: good manifest parses; malformed -> ok=false + default; version
    mismatch rejected; validate() catches dup address / missing required field.
P2  PlatformConfig: DeviceManifest structs + the synthesizer (fields -> JEDEC
    256B + checksums; PCI 256B cfg + BAR masks) + built-in default DS10 manifest.
    doctest on synthesis (checksum correctness, BAR-sizing mask, cfg field offsets).
P3  IIC consumer: refactor IicPcf8584 to table-driven presence/content; retire
    seedFruBank gate; preserve state machine + EMULATR_IIC_TRACE; doctests green.
    Includes the 0x70/0x72 status nodes -- this is the FRU-hang fix.

==== CHECKPOINT (D-SCOPE gate -- do NOT start P4 until this passes) ====
C1  Cold-boot (or restore + targeted) run with EMULATR_IIC_TRACE=1 +
    EMULATR_GCT_WATCH=1: confirm 0x70/0x72 answered, build_power_hw succeeds,
    pc264_hw reaches build_fru_root, FRU_ROOT (type 0x14) is created. Re-mint
    coldgct + >>> snapshots.
C2  From the >>> snapshot, inject set sys_serial_num / show fru / show error /
    clear_error all -- all terminate (no runaway walk). This closes the original
    GCT_FRU_Support_Spec objective WITHOUT any PCI work.

-- PCI layered in only after the checkpoint is green --
P4  PCI consumer: ManifestPciDevice (model:"generic") + registerPciDevice wiring
    from the manifest; BAR-sizing handshake; option_rom=false path; model:
    "passive"/"<named>" routing (D-PCIMODEL).
P5  Cold-boot validation: PCI config sweep reads real vendor/device IDs (no
    all-ones UNHANDLED pokes at TsunamiPchip). Re-mint snapshots.
P6  show config / show device list the modeled PCI devices; build_pci_fru builds
    per-option FRU descriptors (skipping slot5/slot6 by firmware design).
P7  (Conditional) profile + tune the dva0 option-firmware scan ONLY IF Sec 1.3
    confirms it is device-timeout bound.

P1->P2 are pure host-side code (fast, doctest-only -- no 30-min boot). P3 + the
checkpoint deliver the FRU/GCT fix. PCI (P4+) is a separable follow-on that does
not block the critical path. Cold-boot cost is paid at C1 and P5, each once,
then snapshotted.

--------------------------------------------------------------------------------
## 10. Verification matrix

    check                              method                               pass
    --------------------------------   ----------------------------------   --------
    Qt parse rejects malformed         doctest: QJsonParseError set          ok==false
    validate() catches dup/missing     doctest: bad manifest -> error list   caught
    manifest_version mismatch rejected doctest: wrong version -> default      rejected
    JEDEC synthesis checksums valid    doctest vs jedecSum spans            match
    PCI cfg header fields placed       doctest read 0x00/0x09/0x3D          correct
    BAR-sizing handshake               doctest write 0xFFFFFFFF -> mask     size mask
    IIC presence from manifest         EMULATR_IIC_TRACE cold boot          ACK seen
    FRU_ROOT created                   EMULATR_GCT_WATCH type-0x14 store    present
    PCI sweep reads real IDs           no UNHANDLED 0xffff0000 pokes        clean
    set sys_serial_num / show fru      >>> snapshot + plink inject          no hang
    no boot regression                 cold boot to >>>                     reaches >>>
    doctest suite                      ctest                                all green

--------------------------------------------------------------------------------
## 11. Risk register

R1  No-exceptions JSON reader bugs on edge input -> crash instead of clean error.
    Mitigation: depth cap + bounds checks + a malformed-input doctest battery; the
    built-in default manifest means a rejected file degrades gracefully.
R2  Provisional PCI vendor/device/class IDs wrong -> firmware mis-IDs a device or
    build_pci_fru builds a wrong descriptor. Mitigation: mark _PROVISIONAL, confirm
    against a real dump before fidelity claims; identity errors are non-fatal to the
    FRU hang fix (FRU_ROOT is upstream).
R3  BAR-sizing handshake subtly wrong -> firmware programs a bad window / resumes
    the all-ones poke. Mitigation: doctest the handshake; cross-check against the
    existing UNHANDLED-write PA in project_pci_enum_unhandled_ffff0000.
R4  Scope creep into behavioral device models. Mitigation: first cut is config-space
    identity + BARs ONLY; no DMA/IRQ/register behavior beyond what enumeration reads.
R5  dva0 scan time not actually device-bound -> P7 yields no win. Mitigation: P7 is
    explicitly conditional on a profiling confirmation, not assumed.
R6  Snapshot/manifest drift -> stale snapshot hides a manifest edit. Mitigation:
    identity-from-manifest / state-from-snapshot rule (Sec 7.4); manifest never
    serialized.

--------------------------------------------------------------------------------
## 12. Bottom line

Stand up a PlatformConfig/DeviceManifest built on Qt's QJsonDocument (already a
project dependency, already used no-throw in SRMEnvStore.cpp), with manifest_version
gating and an explicit validate() phase. It synthesizes on-wire images from
high-level fields and binds two consumers -- IicPcf8584 (IIC table) and
TsunamiPchip (PCI table via the existing registerPciDevice seam, each device
declaring its model). The firmware's own discovery then finds a faithful, fully
declared board every cold boot. The plan is sequenced IIC-FIRST: P1-P2 are pure
host-side doctest work (no boot cost), P3 + the CHECKPOINT deliver the FRU
`set sys_serial_num' fix (IIC 0x70/0x72 + FRU identity) and must pass before any
PCI work; PCI enumeration (P4+) then layers in as a separable follow-on that gives
the deferred on-board-device work its permanent home. Device changes become a JSON
edit + snapshot rather than a recompile + 30-minute boot.
