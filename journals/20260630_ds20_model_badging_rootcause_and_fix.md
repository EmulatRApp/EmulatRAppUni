# DS20 Model Badging -- root cause and fix (2026-06-30)

## Outcome
DS20 mis-badged "AlphaPC 264DP" (SYSVAR member 1) instead of "AlphaServer DS20"
(member 6). Root-caused to a **missing IIC discriminator device** in the platform
manifest and fixed with a **one-line manifest addition** (no rebuild). The badge
never depended on the IIC completion path or on node 0x40, both of which prior
work had chased.

## What the badge actually is
`build_hwrpb` calls `get_sysvar()` (pc264.c:636). It seeds `sysvar = 0x5`, then
`fopen()`s a model-specific IIC device; success ORs `member << 10` into
`SYSVAR<15:10>`, else member 1. Stored to the HWRPB SYSVAR word at PA `0x2058`.
Member map (SYSTYPE 0x22): 1 = AlphaPC 264DP (0x405, fallback); 6 = AlphaServer
DS20 (0x1805, `iic_rcm_temp` @ 0x9e); 8 = DS20E/Goldrack (0x2005, `iic_8574_ocp`
@ 0x4E).

## The investigation arc (what was disproven)
1. **Interrupt-completion theory -- DEAD.** V7.3-2 runs the IIC driver POLLED
   (`iic_driver.c:138 #define POLLED ... PC264`); ENI is never written. IRQ sweep
   was inert as expected.
2. **Polled-completion theory -- DEAD.** Retire-trace proved node 0x40 reads
   cleanly (dummy 0xff, data 0x00, NACK, STOP) and `iic_service` posts
   `krn$_post` -- the completion IS delivered (timer-paced, ~262K cyc/step). Both
   "(A) scheduler never dispatches" and "(B) PIN never 0" are disproven.
3. **iic_ocp0 @ 0x40 -- RED HERRING.** It is the OCP LED, present on BOTH DP264
   and DS20, so it cannot discriminate. The manifest already populated it (reads
   fine every trace); had it been the gate, the badge would already be member 6.

## Root cause (proven by trace + source)
`DIAG_ARM=sysvar` capture armed the retire window on the base-SYSVAR store
(PA 0x2058 == 0x5) and caught `get_sysvar` (pc 0x7f5c0). In the entire 14K-instr
window it byte-walks exactly one IIC device name -- `iic_rcm_temp` -- and never
references `iic_ocp0`. The decision is `BEQ pc=0x7f608` on R16: R16==0 (fopen
failed) -> `LDA 0x405` (member-1 literal, pc 0x7f660) -> store to 0x2058
(pc 0x5c3f8).

`iic_rcm_temp` is the KCRCM server-management-card temperature sensor at IIC node
**0x9e** (iic_driver.c:264, IIC_LED_TYPE), "only present on the AlphaServer DS20"
per the get_sysvar source comment. EmulatR's `IicPcf8584` populates its bus from
the platform manifest; absent nodes NAK. Node 0x9e was NOT in the DS20 manifest,
so the scan NAK'd it (`IIC-TXN addr=0x9e dir=R -> NAK`) -> not registered ->
`fopen("iic_rcm_temp")` NULL -> member 1.

Note: the shipped V7.3-2 binary probes `iic_rcm_temp` (matching the source
comment), while the reference apisrm `get_sysvar` *code* line reads
`fopen("iic_ocp0")`. The running-binary trace is authoritative.

## Fix (applied, no rebuild -- manifest is runtime-loaded)
Added to `ds20_v7_3_platform.json` `iic_devices` (source + run-dir copy
out/build/relwithdebinfo/), and corrected the wrong iic_ocp0 comment:

    { "name": "iic_rcm_temp", "address": "0x9e", "class": "status", "byte": "0x19" }

Registration gates on the read succeeding (rec_count), not the byte value, so
0x19 (~25 C) is cosmetic. Expect member 6 -> SYSVAR 0x1805 -> "AlphaServer DS20".

## Verification method (fast check, no full LFU needed)
Run `./tools/diag_ds20_badge_ab.sh`; watch `GMEM-WATCH(0x2058)`. Fix confirmed
when the final store reads `v=0x1805` (member 6) instead of `0x405`. This store
lands at ~cyc 222M, before the banner -- no need to drive `update srm` to the
banner to know it worked (though the banner gives the visual "AlphaServer DS20").

## Tooling added this session
- `tools/diag_ds20_badge_ab.sh` -- DS20 badge diagnostic; `DIAG_ARM=iic|sysvar`
  mode toggle (iic = arm node-0x40 read; sysvar = arm base-SYSVAR store to catch
  get_sysvar). Resolves run dir to out/build/relwithdebinfo. Console mirror +
  SYSVAR/GMEM watches. No rebuild needed for the PA-store arm.
- H&M topic rewritten: `journals/DS20_Model_Badging_topic.xml` (supersedes
  DS20_Badge_IIC_Polled_Completion_topic.xml). Generalized to the discriminator-
  device pattern; cut the garbled PIN table and the dead IIC_IRQ_BIT sweeps.

## Open / next
- Verify the fix run shows SYSVAR 0x1805 / "AlphaServer DS20".
- The general pattern (probe-for-discriminator-device) applies to other models;
  trace their get_sysvar and populate the matching manifest node.
