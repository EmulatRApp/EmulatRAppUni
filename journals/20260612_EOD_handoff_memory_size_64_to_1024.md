# EOD Handoff — 2026-06-12 — Memory size 64 MB → 1024 MB (SSOT plumbing fix #6)

Durable record reconstructed from the session transcripts (the live
session did not write a journal before stopping). Captures the
`64 Meg → 1024 Meg of system memory` work plus the M2/M3 follow-ons.

---

## 1. Headline

The DS10 boots reporting **64 MB** instead of the configured **1 GiB**.
Root-caused to an **SSOT value-plumbing bug**, NOT a memory-model defect:
`main.cpp` constructed `Machine` with `opts.memSize` (the `--mem` CLI
**default**, 64 MiB) instead of the ini's `[System] memorySize` (1 GiB).
The ini value was parsed into `settings` but never used for memory, so
64 MiB always won. `computeAAR` itself was already HRM-correct.

Chain of consequence: `memSize 64M → TsunamiChipset → Cchip
computeAAR(64M)` = `asiz 0x3` = `AAR0 0x3009` → firmware sizes 64 MB.
For 1 GiB it should be `asiz 0x7` = `AAR0 0x7009`.

---

## 2. The fix (coded — mirrors the existing firmware CLI>ini fallback at main.cpp:154)

- **`systemLib/AppOptions.h`** — added `bool memSizeSet` (tracks whether
  `--mem` was actually given on the CLI).
- **`systemLib/AppOptions.cpp`** — set `memSizeSet = true` in the `--mem`
  parse arm.
- **`main.cpp`** — before constructing `Machine`: if `--mem` was NOT
  given and the ini has a size, use `settings.system.memorySizeBytes`.
  The value then flows `memSize → TsunamiChipset → Cchip AARs`.
  Precedence preserved: explicit `--mem` still wins (CLI > ini).
  GuestMemory is sparse-paged, so 1 GiB costs nothing until touched.

Expected stderr on engage:
`memory: using [System] memorySize from ini: 1073741824 bytes`

---

## 3. Reconciliation with the web "MEMORY MODEL FIDELITY" spec

A web-sourced spec (interface-faithful vs discovery-faithful; M1/M2/M3)
was checked against the live tree. Outcome:

- **Consistent on principles + encoding.** EmulatR is already
  interface-faithful (M1): `computeAAR` sets the AAR directly, no SROM
  SPD/MPD bit-bang. The encoding audit (M-spec deliverable #1) **passes
  as-is**: `computeAAR` uses `ADDR<34:24>` (`(baseAddr>>24)&0x7FF`) and
  `ASIZ<15:12>` (`asiz<<12`), the new format — NOT the old
  `[12:0]`/`[25:16]`/`[31]` layout. 64 MB → `0x3`, confirmed.
- **The spec missed the real bug** — it was written blind (like the dq
  spec) and assumed a model/encoding fault. The actual defect is the
  upstream value plumbing, already fixed in §2.
- **Adopted from it: M2 consistency assert** (see §4) — the lasting
  takeaway.
- **M3 platform-gating** (mem-ctrl sub-block, fail-closed Typhoon/Titan)
  — good discipline, but deferred (see §5); only Tsunami/DS10 in play.

---

## 4. M2 — consistency doctest (landed)

Added to `tests/.../test_ticket02_aar_encoding.cpp`: verifies the
AAR-encoded total — decoded by the **firmware's own formula** — round-
trips to the configured `memSize`. Firmware formula confirmed from
`memconfig_pc264.c`:

```c
size = (ReadTsunamiCSR(CSR_AAR0 + array*0x40) >> 12) & 7;   // ASIZ
if (size != 0) return ((uint64)1 << (size + 3)) * 1024 * 1024; // 2^(asiz+3) MB
```

So array size = **2^(ASIZ+3) MB**: `0x3 → 64 MB`, `0x7 → 1024 MB`.
Explicit cases asserted: 64 MB → `0x3009`, 1 GiB → `0x7009`. This is the
permanent guard for the #6 bug class.

### show config / show memory (source-confirmed, settles M1 [CONFIRM])
Both come from `show_config_pc264.c` via `get_array_size`, which reads
the AAR and computes `2^(ASIZ+3) MB`. After the fix the banner reads
`1024 Meg of system memory` and the table shows `Array 0: 1024 MB`.
**`show memory` on DS10 reads NO per-DIMM SPD data** — purely AAR-
derived — so SPD/MPD modeling stays correctly out of scope (defer
indefinitely unless `show fru` per-DIMM detail is ever wanted). The
firmware masks `& 7` (honors ASIZ 0x1–0x7 only = Tsunami 16 MB–1 GB);
Typhoon's 0x8–0xA would need a different read — reinforcing M3.

---

## 5. M3 — deferred (recorded as Cowork task #10 in-session)

The spec's mem-ctrl sub-block needs the "platform identity record"
(dq-spec section 2A) which was **never actually built**, and EmulatR has
no Titan variant to fail-closed against. The Tsunami/Typhoon ASIZ clamps
already live in `computeAAR` and M2 now guards encoding, so M3 becomes
real work only when the platform record is built or a non-Tsunami
platform is added.

---

## 6. OPEN — live confirmation still pending (DO THIS FIRST NEXT SESSION)

The last RelWithDebInfo run in-session **still printed `64 Meg of system
memory`** — i.e. that binary did not yet reflect the fix (stale build or
the ini fallback didn't engage). The fix is coded but **not yet
confirmed live.** Cold boot (no autoload — memory size is set at chipset
construction/reset, so a snapshot restore won't show it) and verify, in
order:

1. **stderr early:** `memory: using [System] memorySize from ini:
   1073741824 bytes` — proves the SSOT fallback engaged.
2. **banner:** `1024 Meg of system memory` (was `64 Meg`).
3. **`show config` / `show memory`:** `Array 0: 1024 MB`,
   `Total Good Memory = 1024 MBytes`, `AAR0 = 0x7009` (was `0x3009`).

```
EMULATR_CONSOLE_MIRROR=1 EMULATR_NO_AUTOLOAD=1 \
  ./out/build/release/Emulatr.exe --firmware firmware/ds10_v7_3.exe \
  --autosnapshot off --max-cycles 200000000000 > boot.log 2>&1
```

Cheap pre-check that doesn't need the long boot: run `Emulatr_tests.exe`
and confirm the new M2 AAR-consistency cases pass.

If it still shows 64 MB after a clean rebuild: confirm the `--mem`
default isn't being treated as "set", and that `[System] memorySize` is
actually populated in the loaded ini (`EmulatorSettings` /
`memorySizeBytes`).

After confirming 1 GiB cold-boots to `>>>`, **re-mint snapshots** — any
existing `coldgct`/`oemsnap` are stale (64 MB baked in) and must not be
used going forward.

---

## 7. Related state from the same day (IDE / S4)

Separately, the CY82C693 IDE scaffold (S4) reached completion: func1
enumerates at bus0/dev5/func1 (vendor 0x1080 / device 0xC693), legacy
0x1F0/0x170 ports route to `m_ide` with no Cypress catch-all shadowing,
and the full suite is **407 green**. A cold boot reached `>>>` via the
LFU path; `update srm` to 7.3-1 succeeded; `sys_serial_num=test123`
persisted across reboot (FRU/NVRAM fix holding). Still to confirm at
`>>>`: `show device` listing `dqa0` and the boot-time reduction from the
dq-search loop. (See the June 7–8 EOD handoffs for the IDE/SuperIO
lineage.)

---

## 8. Next-session order

1. **Confirm the 64→1024 fix live** (§6) — banner + `AAR0=0x7009`.
2. Re-mint fresh 1 GiB snapshots.
3. `show device` for the dqa0 verdict + boot-time metric (§7).
4. Backlog: #7 PCI enum / DE500 tulip; #8 LFU detour + exer;
   #9 halt-button + dqa0 disk image (gates an actual `boot`).
