# Path to the SRM `P00>>>` prompt -- corrected map + decisive diagnostic

Status: 2026-06-03. Supersedes the 2026-06-02 lean (memory
`project_path_to_p00_prompt`). The firmware path that emits `P00>>>` is
source-confirmed; the leading hypothesis was corrected from H4 (unmodeled
HWRPB comm slot) to H1/H2/H3 (the foreground shell never reaches the
prompt write). This note records the corrected reasoning, the SuperIO
scoping, and the single run that collapses the remaining hypotheses.

All source paths below are under
`D:\EmulatR\Processor Support\Palcode\palcode\apisrm\apisrm\ref\`.

---

## 1. The firmware path that prints `P00>>>`

1. Platform init (`pc264_init.c`): `platform_init1` -> `SMC_init`
   (SMC37c669 SuperIO) -> `init_serial_port` (COM1). Console data
   structures are set up here.
2. `combo_driver.c` ("COM1 serves as the primary console for the
   firmware") binds the `"tt"` device to the COM1 serial port:
   `allocinode("tt", 1, &ip); ip->misc = console_ttpb;`. This `tt`
   inode (`tt_ddb`) is the interactive console device.
3. The foreground shell is created as its own process with stdin/stdout
   bound to `"tt"` (e.g. `execute_script` / the console main:
   `krn$_create(sh, shell_startup, ..., "tt","w","tt","w", ...)`).
   `powerup.c` references `sh()` -- the interactive shell launch.
4. Shell body (`blex.c`): `yyl->tty = isatty(pcb->pcb$a_stdin)`
   (blex.c:1005). `isatty(fp)` returns 1 **iff** `fp->ip->dva == &tt_ddb`
   (filesys.c:2168). If false, the shell runs but never prompts.
5. The do-loop: `yy_reset` builds `prompts[0] = "P00>>>"`
   (`sprintf("P%02d>>>", whoami())`, blex.c:506/509) ->
   `read_with_prompt(prompts[0], ...)` (blex.c:672) WRITES the prompt to
   the `tt` port, then BLOCKS on the read.

Egress: `tt` -> `combo_driver` -> COM1 16550 UART -- the **same UART**
that already prints the banner. So if the shell reaches step 5, `P00>>>`
appears on the UART mirror. It does not, today.

---

## 2. Why H4 (HWRPB polled comm slot) is NOT the uniprocessor blocker

The 2026-06-02 lean was that the prompt goes through the HWRPB per-CPU
comm slot (`slot->TX` / `TXRDY` / `RXRDY`), which V4 does not model.
Source says otherwise for a single-CPU primary console:

* `hwrpbtt_driver.c` header: "HWRPB TT terminal port driver. This driver
  is used at boot time by SECONDARY PROCESSORS to receive console
  commands from the operating system."
* `hwrpbtt_txsend` writes `slot->TX`, `bbssi(TXRDY)`, then
  `mtpr_ipir(primary_cpu)` -- it signals the PRIMARY.
* The PRIMARY drains it: `hwrpb_comm_flush()` polls `TXRDY` and does
  `pprintf("\n%s\n", slot->TX)` -- i.e. relays the secondary's console
  output to the real (primary) console, then clears `TXRDY`.

So the HWRPB comm slot is an SMP console-forwarding channel whose sink is
the primary's UART. On single-CPU EmulatR it is inert. V4 genuinely lacks
the `TXRDY`/`RXRDY`/`TX`/`RX` model, but that gap only bites SMP, not the
first prompt.

Conclusion: the wall is UPSTREAM of the prompt write.
  H1 -- foreground shell never created.
  H2 -- created but scheduler never dispatches it.
  H3 -- runs but `isatty(stdin)` is false (stdin `dva != tt_ddb`),
        so it never prompts.
Consistent with the SYSFAULT dump showing only `krn$_idle` /
`timer_check` (the console IDLE process, not the shell).

---

## 3. SuperIO (SMC37c669 / FDC37C669) scoping -- a parked, optional stub

The DS10/PC264 serial ports live in an SMSC FDC37C669 SuperIO (two
16C550 UART cores + floppy/parallel/IDE/game). `pc264_init.c`'s
`SMC_init` configures it, but is gated:

    if ( ( SMC_base = SMC37c669_detect() ) != NULL ) { ...configure... }

`SMC37c669_detect()` (smcc669_driver.c:274) enters config mode by writing
`CONFIG_ON_KEY` (0x55) twice to the config index port (3F0/370 per the
datasheet), reads the device-ID config register, exits with
`CONFIG_OFF_KEY` (0xAA), and only proceeds if the ID matches
`SMC37c669_DEVICE_ID`.

EmulatR does not model the 3F0/3F1 config port, so detect returns NULL
and `SMC_init` skips all configure/enable calls. **This is almost
certainly benign for reaching the prompt**, because:

* EmulatR's `Uart16550` is unconditionally present and decoded at 0x3F8
  (the banner proves output works without any SuperIO config).
* `init_serial_port` then programs the 16550's own data registers
  (LCR/DLL/DLM/MCR/FCR, drains RBR) at 0x3F8 -- registers V4 models.
* The `"tt"` bind in `combo_driver` keys off the 16550 responding and
  `get_console_base_pa()`, not off SMC state.

Caveat: this stays benign only because an access to the unmodeled 3F0/3F1
returns cleanly (detect fails gracefully) -- boot proceeds, so it does.

DECISION 2026-06-08: BUILD the FDC37C669 SuperIO (task #22). It is needed
for the floppy (dva0), the keyboard-controller "not plugged" path, and
interrupt routing. Minimal first cut: a config-port `IIoPortHandler` at
0x3F0/0x3F1 that accepts the 0x55,0x55 enter / 0xAA exit key, returns the
correct chip-ID on the DEVICE_ID index, and no-ops the configure/enable
writes -- ~a dozen lines, no floppy/parallel/ECP machinery, the real
`Uart16550` still doing all data movement. Then grow it: expose the logical
devices (UART1/UART2 -> existing Uart16550, FDC -> Floppy82077) via their
config registers (base addr / IRQ / enable), so the firmware enumerates the
floppy through the SuperIO rather than the bare 0x3F0 legacy window.

---

## 4. The decisive diagnostic -- one run, four checkpoints

Tooling landed 2026-06-03: `BreakpointSink` now has an independent
multi-PC checkpoint ledger (orthogonal to the open/close gate). Arm up to
8 tripwire PCs; each first hit records its cycle + a full GPR snapshot;
at run end the sink prints, to stderr and the `_break.trc` file, which
checkpoints were reached and which was reached LAST.

Arm via env var (turnkey for a headless run):

    set EMULATR_CHECKPOINTS=shcreate:0xAAA,shentry:0xBBB,isatty:0xCCC,rwp:0xDDD

(each entry is `label:0xPC` or bare `0xPC`; up to
`BreakpointSink::kMaxCheckpoints` = 8). Or call
`BreakpointSink::setCheckpoint(idx, pc, "label")` from the VS Immediate
window before `run()`.

The four checkpoints:
  (a) shcreate -- the PRIMARY foreground-shell `krn$_create(sh,...)` site
                  that binds `"tt"` (NOT `hwrpbtt_driver.c:191`, which is
                  the secondary-CPU shell)
  (b) shentry  -- `shell_startup` / `sh` entry (bshell.c:306 / blex.c)
  (c) isatty   -- the `isatty` call at blex.c:1005 (R0 after = the result)
  (d) rwp      -- `read_with_prompt` (blex.c:672)

Interpretation (last checkpoint reached):
  never (a)            -> H1  console-driver init didn't run / device-gated
  (a) not (b)          -> H2  shell created, scheduler never dispatches it
  (b) not (c)/(d)      -> H3  (early) shell runs but bails before isatty
  (c) reached, R0 == 0 -> H3  isatty() false -- stdin dva != tt_ddb
                              (the SuperIO / console-bind suspect)
  (c) reached, R0 == 1 then (d) reached, no glyph
                       -> tt/UART egress broken (NOT the comm slot)
The one-shot GPR snapshot at (c) gives R0 directly, so H3 is settled in
the same run.

---

## 5. Recovering the four PCs (Ghidra GUI -- symbols are GP-relative)

The headless decompile names functions `FUN_xxxx` and accesses the
prompt rodata GP-relative, so the PCs must come from the GUI where
symbols are applied:

1. Open the analyzed program; run `ApplySrmSymbols.py` (names
   `read_with_prompt`, `isatty`, `tt_init_port`, etc.).
2. Xref the literal format strings in the 0x167xxx rodata:
     0x167C10 "P%02d>>>"   -> yy_reset (confirms the prompt builder)
     0x167D70 "hwrpbtta%d"  + 0x167DE0 "shell_%d"  -> the SECONDARY
       shell create (hwrpbtt) -- use to DISTINGUISH from, not as, (a).
3. For (a) the PRIMARY shell: xref the `krn$_create` callers that pass
   `"tt"` as the stdin/stdout device (execute_script / console main),
   not the hwrpbtt one.
4. (c) isatty call site: find the call inside the shell body (blex.c:1005
   region); the instruction after the call holds the R0 result.
5. (d) read_with_prompt: the function `yy_reset` calls into.

Drop the four addresses into `EMULATR_CHECKPOINTS` and run from the
autoload snapshot (do not re-grind the multi-hour cold boot).

---

## 6. Resume checklist (next session)

* Build V4 with the checkpoint-ledger changes
  (`traceLib/BreakpointSink.{h,cpp}`, `main.cpp`); doctest stays green.
* Recover the four PCs per section 5.
* Run from autoload snapshot with `EMULATR_CHECKPOINTS` set; read the
  `CKPT_SUMMARY (decisive)` stderr block -> collapses H1/H2/H3.
* Branch the fix on the result (scheduler vs shell-create vs the
  console-bind / SuperIO-stub path in section 3).
