# Platform Axis Classification of "Global Fixes Under Suspicion" (2026-07-05)

## Purpose

A recurring worry: fixes landed to unblock ONE platform (ES40) apply GLOBALLY, so
are they secretly platform-specific and wrong on the other five? The disciplined
answer is **classify before scoping** -- name each fix's AXIS of divergence before
reaching for any platform gate. Most turn out to be CPU-level truths: correct
everywhere, only ever suspicious because unscoped, not because they were wrong.

## The four orthogonal axes (V4)

| Axis | Governs | Correct scope of a fix on it | Keyed by |
|------|---------|------------------------------|----------|
| **CPU / PALcode (EV6)** | instruction decode, IPRs, TB, superpage, VA forms | UNIVERSAL -- shared by all 6 platforms; usually NO gate (just correct) | (none) |
| **Firmware / model identity** | HWRPB, SRM image, badge/serial | per-MODEL (finest; DS10 != DS20) | model string |
| **Chipset** | memory tiling, CSR layout, AAR/ASIZ | Tsunami vs Typhoon vs Titan | `variantFromModel(model)` |
| **Southbridge / devices** | ISA/PCI I/O, IIC, UART, boot storage | ALi vs Cypress; per-device | `<model>_platform.json` |

The six platforms and their axis coordinates:

```
        CPU    Chipset   Southbridge   CPUs
ES40    EV6    Typhoon   ALi M1543C    up to 4   (Cypress STAND-IN today)
ES45    EV6    Titan     ALi M1543C    up to 4
DS10    EV6    Tsunami   Cypress       1
DS20    EV6    Tsunami   Cypress       2
DS15    EV6    Titan     ?             1
DS25    EV6    Titan     Cypress       2
```

Note: **all six share the EV6 CPU.** That single fact is why most "suspicious"
fixes are universal.

## Classification of the fixes

| Fix | Axis | Verdict |
|-----|------|---------|
| **Reserved-IPR 0x2d accept** (commit eb74087) | CPU | UNIVERSAL -- EV6 ignores writes to unassigned IPR indices. Correct on all 6. **Stays global.** Scoping to ES40 would encode the same falsehood "SL_XMIT" did. |
| **VA_FORM 3-form** (`computeVaForm`, 35fd23e) | CPU | UNIVERSAL -- 43/48/32-bit VA-form computation is EV6 architecture, identical on all 6. VA_48 selection is per-boot (va_ctl), not per-platform. **Stays global.** |
| **unalignTrapEnabled = true** | CPU | UNIVERSAL -- faithful unaligned-access trap is EV6 behavior. **Stays global.** |
| **SPE / superpage `tryKsegTranslate`** | CPU | UNIVERSAL (and confirmed INNOCENT for the ACV loop -- see ACV journal sec 10-12). **Stays global.** |
| **MTPR_VPTB propagation gap** | CPU | UNIVERSAL, latent (0 MTPR_VPTB calls in SRM console). **Stays global.** |
| **spin-skip** (commit f7e2f4d) | CPU / emulation | UNIVERSAL -- proves a loop side-effect-free from opcodes; platform-agnostic; env-gated OFF. **Stays global.** |
| **ES40 -> Typhoon variant** (`variantFromModel`, decd3cb) | Chipset | GENUINELY platform-specific. ALREADY correctly scoped -- `variantFromModel("ES40")` keys on the model. Not a global fix; no action. |
| **ES40 32GB / Typhoon AAR tiling** (`isExtendedAar`) | Chipset | GENUINELY chipset-specific. ALREADY scoped (`isTyphoon() || isTitan()`). No action. |

## Conclusion

**There is no current residue that needs a new platform gate.** Every "global fix
under suspicion" is a **CPU-axis truth** -- correct on all six platforms, correctly
global. The two genuinely-divergent behaviors (Typhoon variant, extended AAR) are
already model/chipset-scoped through the existing `variantFromModel` dispatch. The
suspicion is retired by classification, not by machinery.

The capability primitive (below) is therefore landed **inert** -- it earns its keep
at the FIRST honestly capability-divergent fix. The strongest near-term candidate is
the **southbridge axis**: the ES40/ES45 real southbridge is the ALi M1543C, modeled
today by a Cypress STAND-IN (per `es40_v7_3_platform.json`). When the ALi is modeled
and the SRM's ALi-specific south-bridge init needs different handling from the
Cypress boxes, that gate is `plat_has(SB_ALI)` -- NOT `model == ES40` (ES45 shares
it) and NOT the chipset (ES40=Typhoon, ES45=Titan differ on chipset but share the
southbridge). That is exactly the case the chipset axis cannot express, and why the
key is the MODEL resolving to manifest-enumerated CAPABILITIES, with the gate testing
the capability.

## The primitive (systemLib/PlatformCapabilities.h)

- **Model is the key; manifest capabilities are the values; the gate tests the
  capability.** `plat_has(SB_ALI)`, never `model == ES40`.
- Derived at construction from (model string, resolved ChipsetVariant, DeviceManifest)
  and latched before any guest instruction retires, so it is live for every future
  gate by definition. `latched()` assert trips a too-late latch instead of silently
  gating nothing.
- Fail-inert both edges: an unset capability gates nothing (0 matches nothing), and a
  platform added later that does not assert a capability does NOT inherit any gate
  scoped to it. There is no `~0` "everything" -- capabilities are enumerated positive
  features.
- The primitive makes SCOPE explicit and reviewable; it does NOT make an exception
  correct. Correctness is still established per-platform by boot-to-`>>>` evidence
  before a gate is widened. Two separate jobs; keep them separate in review.
- Zero call sites at landing (inert infrastructure). First consumer: the ALi
  southbridge, when modeled.
