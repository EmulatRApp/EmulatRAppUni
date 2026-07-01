# EmulatR SRM Model Identity / Badging -- Test Plan (2026-06-30)

Status: DS20 member-6 path VERIFIED 2026-06-30 (banner "AlphaServer DS20").
This plan formalizes the permutation matrix, the regression guards, and the
framework to extend to the other models (ES40 next).

## Phasing (what is in scope now vs deferred)

IN SCOPE NOW (no guest OS, no `boot` command):
- Platform identity: SYSTYPE / SYSVAR / member / banner per model and per
  discriminator-device permutation.
- Pre-boot device prerequisites that never invoke `boot`: `show config`,
  `show device`, `set`/`show bootdef_dev`, storage/device addressability.

DEFERRED to the guest-OS scaffold phase:
- The SRM `boot <dev>` command, its `-fl` / `-flags` arguments (not yet
  supported; implementing them forces the higher OS-handoff layer), and the
  end-to-end path: boot block -> primary bootstrap -> OS loader (APB/vmb) ->
  OS PALcode takeover -> secondary-CPU start -> OS banner.
Rationale: `boot` cannot be validated at a "plumbing only" level while `-fl`
is unhandled, so it belongs with the OS handoff, not this identity pass.

## Method / harness

- Tool: `tools/diag_ds20_badge_ab.sh` (`DIAG_ARM=sysvar` for the identity
  capture; arms the retire window on the base-SYSVAR store).
- Pass signals (two independent):
  1. `GMEM-WATCH(0x2058)` final store value = SYSVAR. Member = `(SYSVAR>>10)&0x3F`.
  2. Console banner string (mirrored to the .out via `EMULATR_CONSOLE_MIRROR`).
- SYSVAR base = 0x5. Member encodings: 1 -> 0x405, 6 -> 0x1805, 8 -> 0x2005.
- Discriminator precedence in the running V7.3-2 binary (get_sysvar order):
  `iic_rcm_temp` (member 6) > `iic_8574_ocp` (member 8) > default (member 1).

## Part A -- DS20 identity permutation matrix (READY; P2 VERIFIED)

Manifest = `ds20_v7_3_platform.json` `iic_devices`. Toggle the two discriminator
entries; leave everything else fixed.

| # | iic_rcm_temp @0x9e | iic_8574_ocp @0x4E | Expect SYSVAR | Member | Banner | Status |
|---|---|---|---|---|---|---|
| P1 | absent | absent | 0x405 | 1 | AlphaPC 264DP | baseline (pre-fix state) |
| P2 | present | (either) | 0x1805 | 6 | AlphaServer DS20 | VERIFIED 2026-06-30 |
| P3 | absent | present | 0x2005 | 8 | AlphaServer DS20E | to run |

Regression guards:
- R1: remove `iic_rcm_temp` -> must revert to P1 (member 1). Confirms the fix
  is the sole cause of member 6, not an unrelated change.
- R2: with both present -> must be member 6 (P2 wins by precedence), never 8.
- R3: node 0x9e must show `IIC-TXN addr=0x9e dir=R -> ACK` (was NAK pre-fix);
  and node 0x40 (iic_ocp0) stays present but is NOT load-bearing for the badge.

## Part B -- Pre-boot device prerequisites (READY; no `boot`)

| ID | Check | Expected |
|---|---|---|
| B1 | `show config` | Tsunami 21272 + Cypress CY82C693 (ISA 00:05.0, IDE 00:05.1), COM1, IIC devices |
| B2 | `show device` | dqa0 (IDE disk), dqa1 (ATAPI CD), dva0 (floppy), ewa0 (DE500 net) enumerate |
| B3 | `set bootdef_dev dqa0` then `show bootdef_dev` | value set and persisted (flash NVRAM) |
| B4 | device addressability | attached media / presence reflected in `show device`; no probe fault |

Note: Part B intentionally does NOT execute `boot` (deferred). It validates that
the device tree the eventual `boot` will consume is enumerable and addressable.

## Part C -- Other models (FRAMEWORK; fill before/with ES40)

For each model, the two unknowns are the expected identity (SYSTYPE/member/
banner) and the discriminator device+node. Fill each row by BOTH:
  (1) apisrm `get_sysvar` / platform source for that model's build, and
  (2) a `DIAG_ARM=sysvar` trace of the running binary (authoritative -- the
      shipped image can disagree with the reference source, as DS20 did).

| Model | Firmware stem | Expect SYSTYPE | Member | Banner | Discriminator (node) | Source? | Trace? |
|---|---|---|---|---|---|---|---|
| DS10 | ds10_v7_3 | TBD | TBD | TBD | TBD | | |
| DS20 | ds20_v7_3 | 0x22 | 6 | AlphaServer DS20 | iic_rcm_temp (0x9e) | yes | VERIFIED |
| DS25 | ds25_v7_3 | TBD | TBD | TBD | TBD | | |
| ES40 | es40_v7_3 | TBD | TBD | TBD | TBD (see es40_platform_discovery_map journal) | | |
| ES45 | es45_v7_3 | TBD | TBD | TBD | TBD | | |

ES40 is the declared next model; populate its row first, then run its identity
matrix the same way as Part A.

## Exit criteria (to declare identity "validated" and advance to ES40)

- Part A: P1, P2 (done), P3 all produce the correct member + banner; R1-R3 pass.
- Part B: B1-B4 pass.
- Part C: DS20 row complete (done); ES40 row populated and its identity matrix
  passing.

## Deferred backlog (guest-OS scaffold phase)

- `boot <dev>` command parse incl. `-fl`/`-flags` args.
- Boot block (LBN 0) read + bootstrap loader handoff.
- OS loader -> OS PALcode takeover -> secondary-CPU start -> OS banner.
- Cosmetic: tune `iic_rcm_temp` returned bytes so "System Temperature" reports a
  realistic value (currently 0 C; badge unaffected).

## Open fidelity nit (separate from badge) -- CPU clock in banner

Banner reports "AlphaServer DS20 100 MHz" but a real DS20 is 500 MHz EV6
(DS20E = 833). The MHz string is NOT from the IIC identity path; it comes from
the CPU cycle-counter frequency the SRM records in its self-built HWRPB per-CPU
slot (see HWRPB_PerCpuSlot_FieldMap journal; relates to the earlier "500MHz vs
1GHz" HWRPB nit in reference_hwrpb_not_critical_path). Likely cause: the SRM
measures CPU speed by counting cycles over an interval-timer window, so the
value is tied to our cycle-vs-timer calibration (the ~262K cyc/tick interval
timer), OR it reads a fixed SROM/HWRPB field we populate with 100e6.
Investigation method (same as the badge): watch the store to the HWRPB
cycle-counter-frequency field (GMEM-WATCH on that PA) or trace the speed
measurement loop; compare our value to the expected 500 MHz. Deferred candidate
follow-up after ES40 identity, or sooner if OS timing depends on it.
