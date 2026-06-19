# Checkpoints — 2026-06-15

## 15:07 — Docs pass on the FP backend (IFpBackend + Berkeley SoftFloat)
- **Working on:** Updating the floating-point architecture doc page to match the
  current code, which has moved past the June-10 FP coverage map. Reworked the
  page's backend section into two parts: the `IFpBackend` abstraction and the
  `SoftFloatBackend` deterministic oracle.
- **Done since last checkpoint:** Confirmed the project vendors **Berkeley
  SoftFloat Release 3** (`berkeley-softfloat-3-master/`, built as `softfloat`)
  behind a real `IFpBackend` abstraction in `fpBoxLib`. Edited the FP page:
  documented `IFpBackend` (grain leaves call an abstract op set — IEEE S/T + VAX
  F/G arithmetic, compares, conversions — passing register images +
  `FpExecCtx`/`FpResult`); documented `SoftFloatBackend` as the reference impl
  (rounding passed as data, exception flags harvested as data, no host control
  word touched → bit-identical across hosts, full VAX F/G surface for OpenVMS).
  Corrected an earlier framing that described the backend as just a native
  `bit_cast` pattern. Verified Attributions & Credits and Dependencies pages
  already credit "Berkeley SoftFloat (John Hauser)" — no change needed there.
  Presented the updated page.
- **Open / next:** Wiring `IFpBackend` into every grain leaf is the in-progress
  step (optional `HostBackend` inherits `SoftFloatBackend`, validated by
  `fp_softfloat_vs_host_bench`, selected via `EMULATR_FP_SOFTFLOAT`; runtime
  SoftFloat-vs-native toggle is the longer-term aim). Pending decision: fill the
  `[TODO]` on the Credits page confirming each third-party license (SoftFloat is
  BSD 3-clause).
- **Watch-outs:** Doc-only session — no code/test landings. Source-of-truth was
  drifting (page predated the SoftFloat backend); keep doc pages checked against
  `fpBoxLib` rather than the dated coverage map.

## 19:07 — License docs page + GitHub remote migration (timothyPeer → EmulatRApp)
- **Working on:** Two threads in the active session. (1) Docs: filled the **License**
  topic with the verbatim source-file header block. (2) Migrating the git remote
  for the V4 tree to a new org repo.
- **Done since last checkpoint:** Wrote the License page reproducing the exact
  source header (eNVy Systems 2025–2026, "Licensed under eNVy Systems
  Non-Commercial License v1.1," architect Timothy Peer, commercial-use contact),
  rendered in Code Example style, cross-linked to Attributions & Credits.
  Inspected git config: the `.git` lives at
  `D:\EmulatR\EmulatRAppUniV4\Emulatr`, single remote `origin` =
  `https://github.com/timothyPeer/Emulatr.git`, branch `master`. Gave the user the
  three remote-migration options (set-url switch / two named remotes / multi
  push-URL mirror) targeting the new repo `https://github.com/EmulatRApp/EmulatR.git`.
- **Open / next:** User hit "permission denied / unable to access EmulatR.git" on
  push. Diagnosed as an auth/access problem, not a git-command one: GitHub dropped
  HTTPS password auth (Aug 2021) so a PAT is required; `EmulatRApp` is the **org**,
  not a login — must push as a personal account that is a member with Write. Next
  action awaiting user: clear cached `timothyPeer` credential (Windows Credential
  Manager → `git:https://github.com`), create a fine-grained PAT (owner
  EmulatRApp, repo EmulatR, Contents: R/W) or switch remote to SSH
  `git@github.com:EmulatRApp/EmulatR.git`, then `git push -u origin master`.
- **Watch-outs:** **Licensing contradiction in the tree, unresolved** — source
  headers (and the new License page) say *eNVy Systems Non-Commercial License
  v1.1 (commercial use prohibited)*, but `Emulatr/LICENSE.md` says *GPL v3.0 +
  commercial*. Materially different; needs the user to declare which is
  authoritative before publishing, and the Credits `[TODO]` on per-component
  license terms still open. License page is **not yet wired into the TOC**
  (candidate slot: Reference chapter near Attributions, or Introduction
  front-matter). Possible unrelated-histories rejection on first push if the new
  repo was created with a README/LICENSE. Doc-only — no code/test landings.

## 21:07 — GitHub plan settled: transfer repo to new account EmulatRApp (no org)
- **Working on:** Same git-remote thread, now resolved into a concrete plan. User
  abandoned the multi-remote push approach after the persistent "permission
  denied" and decided to **park V4 next to V1 in one repository** and move that
  repo to a new identity.
- **Done since last checkpoint:** Walked the user through the alternatives and
  landed on a path. Confirmed the existing repo path is `timothyPeer/Emulatr`
  (`https://github.com/timothyPeer/Emulatr.git`, branch `master`). Established
  GitHub has **no native URL shortener** (git.io fully deprecated Apr 2022) — to
  drop the `timothyPeer` handle from the URL, the route is owner change + optional
  external shortener / `envysys.com` redirect. User then created a new **personal
  account** literally named `EmulatRApp` (empty), so the plan is **Transfer
  ownership** `timothyPeer/Emulatr` → `EmulatRApp` (Settings → Danger Zone →
  Transfer; target must accept). Empty account = no name collision, so transfer
  will succeed. Also gave repo-rename and repo-delete procedures.
- **Open / next:** User to execute the transfer from `timothyPeer`, then locally
  `git remote set-url origin https://github.com/EmulatRApp/Emulatr.git`. After
  that, draft the **Source Repository** doc page (clone URL / origin / `master`)
  and fix baked-in literals — the source-header `Documentation:` line
  (`timothypeer.github.io/ASA-EMulatR-Project/`) and README/docs URLs.
- **Watch-outs:** Advised **do NOT delete the `timothyPeer` account** — deletion
  frees the username (breaks old→new redirects) and kills the existing Pages docs
  site `timothypeer.github.io/ASA-EMulatR-Project/`, whose URL is baked into every
  source header. Pages site does **not** follow the repo transfer; would need
  re-creating under `emulatrapp.github.io` + header repoint. If the earlier empty
  `EmulatRApp/EmulatR` repo still exists it would block the transfer (case-
  insensitive name clash) — delete/rename it first. Longer-term cleaner home is a
  GitHub **org**, not a second personal account (account can be converted to org
  later). Licensing contradiction (eNVy Non-Commercial v1.1 vs LICENSE.md GPLv3)
  still unresolved. Doc/admin session — no code/test landings.

## 23:01 — Repo published as EmulatRApp/EmulatRAppUni: README, branch consolidation, Pages-from-main
- **Working on:** Standing up the public repo after the transfer. Now at
  `https://github.com/EmulatRApp/EmulatRAppUni.git`. Three threads: a real
  root `README.md`, consolidating onto a single default branch, and serving the
  H&M-generated docs from `main` via GitHub Pages.
- **Done since last checkpoint:** (1) Replaced the `# Emulatr` stub `README.md`
  at the repo root with a full, fact-derived README — modeled **DS10**
  (21264A / 268 MHz, 21272 Tsunami, Cypress CY82C693, 1024 MB, SRM 7.3), status
  table (boots to `>>>`; OS boot is the frontier), architecture highlights
  (six-stage pipeline, grain dispatch, Boxes, GuestMemory, Ev6Translator/SPAM,
  SoftFloat FP backend), build (CMake / Qt 6.10.2 / MSVC 2022 / C++20,
  `EMULATR_*` options), run instructions, layout, license, credits; presented.
  (2) Diagnosed the **two-branch split** — GitHub default `main` (auto-created,
  empty) vs pushed code on `master`. Recommended standardizing on `main`
  (`git branch -m master main` → `git push -f origin main` → `git branch -u
  origin/main` → `git push origin --delete master`; `git config --global
  init.defaultBranch main` going forward). (3) Wrote
  `.github/workflows/pages.yml` to publish `<repo>/html/` to Pages on every push
  to `main` touching `html/**` (+ `workflow_dispatch`), since branch-deploy mode
  only allows `/` or `/docs`, not `/html`. Presented.
- **Open / next:** User on the GitHub **Actions "Get started"** page; told to
  ignore the starter-workflow gallery — the real switch is **Settings → Pages →
  Source = GitHub Actions**, then `git add .github/workflows/pages.yml html/`,
  commit, push. Live URL will be `https://emulatrapp.github.io/EmulatRAppUni/`.
  Pending: generate H&M HTML into `html/` with a top-level `index.html` (offer to
  drop a redirect stub once the landing filename is known); then add the
  **Source Repository** doc page.
- **Watch-outs:** First Actions run **fails at the upload step if `html/` is
  empty** — must populate `html/` (with `index.html`) before/with the workflow
  push. Ensure `.gitignore` does not swallow `html/` or `.github/`. Baked-in
  literal still stale: source-header `Documentation:` URL points at
  `timothypeer.github.io/ASA-EMulatR-Project/`; new home is
  `emulatrapp.github.io/EmulatRAppUni/`. **Licensing contradiction unresolved** —
  README says GPLv3 + commercial, but source-file headers still say eNVy Systems
  Non-Commercial License v1.1; needs the user to declare which is authoritative.
  Contact left obfuscated (`peert (at) envysys.com`). Doc/admin session — no
  code/test landings.

## 01:07 — Console-output gating (EMULATR_BRINGUP_PROBES) + LinkedIn milestone post
- **Working on:** Active session (`local_30e23842`, "Directory d:/emulatr request").
  Two threads, both complete: (1) auditing/cleaning bring-up console spam behind a
  new compile-time switch; (2) drafting the LinkedIn announcement of the `>>>`
  milestone. Current focus is finalizing the LinkedIn post text.
- **Done since last checkpoint:** (1) Added CMake option **`EMULATR_BRINGUP_PROBES`
  (default OFF)** in `CMakeLists.txt`, mirroring the diagnostics-off pattern; gated
  **~45 probe print sites across 12 files** behind `#if EMULATR_BRINGUP_PROBES` so
  they compile out (zero hot-path cost; rebuild with `-DEMULATR_BRINGUP_PROBES=ON`
  to restore). Gated: per-step CPU probes (`PCSAMPLE`, `PCWIN`, `ITBMISS-PROBE`),
  memory watchpoints, `UARTBP#1–#10`, `HALTPROBE`, Pchip/Cchip/Dchip
  `UNHANDLED`/`UNKNOWN` throttle spam, interval-timer + `b_irq<0>/<1>` divert logs,
  Step-D trigger, `CSERVE`/`REI-PROBE`/`HW_REI`/`DIVERT-REI` PAL probes,
  `HW_PAL_BASE` line, end-of-run `PROFILE`/instruction-stream/RetireProfiler dumps.
  **Only prints wrapped** — functional divert logic, ledger bookkeeping, `R31`
  re-zero left intact. Kept ON: init banner/device-registration/FlashRom messaging,
  snapshot-save messages, genuine errors, `dumpStopReason`/`dumpCpuState` exit
  report. Verified `#if`-family vs `#endif` balance per file with the **Grep/file
  tools** (PipelineDriver.h 13/13, MemDrainer.h 13/13, TsunamiPchip.h 10/10,
  PalEntries.cpp 14/14, Uart16550.h 6/6, Machine.cpp 6/6, Pic8259Pair.h 5/5,
  main.cpp 4/4, etc. — all balanced). (2) Iterated the LinkedIn post to a final
  draft the user approved on framing/tone: DS10, EV6/EV67 21264/21264A + 21272
  Tsunami, cold-boots stock SRM 7.3 to `>>>`, `show config/memory/pal/version`
  correct; "~7 months / three versioned attempts"; trillions of guest instructions
  under the test-and-trace harness; PCI I/O bus + IDE/ATAPI "in the build"; next
  platforms DS20/ES40/ES45; goal = boot OpenVMS + Tru64/OSF. Source/docs URLs:
  github.com/EmulatRApp/EmulatRAppUni and emulatrapp.github.io/EmulatRAppUni/.
- **Open / next:** User deciding whether to have the final post saved as a `.md`
  to copy from vs pasting from chat. Post not yet published to LinkedIn. CMake
  reconfigure + release rebuild still needed to actually register the new option
  and confirm a quiet `Emulatr.exe`.
- **Watch-outs:** **Bash D: mount served stale/truncated copies** during this
  session (main.cpp showed 718 lines ending `std:` vs real 726 ending `}`) — the
  `#if/#endif` "imbalance" was a mount artifact, not real; file/Grep tools are the
  source of truth (reaffirms the file-tool-only rule). **Compile not run here** —
  `#if/#endif` balance verified but the build is the final arbiter of the gating.
  Accuracy guardrail on the post: OpenVMS/Tru64 framed as *goal*, PCI/IDE-ATAPI as
  *"in the build"* not complete (device enumeration is still the active frontier).
  No code/test landings beyond the source edits; no rebuild/run executed.

## 03:06 — SRM `>>>` supported-command catalogue + revised LinkedIn draft (15mo/3 rewrites)
- **Working on:** Same active session (`local_30e23842`). LinkedIn post thread
  continued: pinned down the authoritative list of SRM `>>>` commands EmulatR
  actually carries through to correct output, then reviewed a user-rewritten,
  longer post draft. No files written this segment — discussion/advisory only.
- **Done since last checkpoint:** Pulled the verified command set from the HMDocs
  "Common Commands" / "SRM Boot/Commands" topics (Grep + Read). Settled
  **working set:** `show config` / `show device` / `show memory` / `show pal` /
  `show version` / `show <var>`, `set <var> <value>` (persisted to emulated NVRAM),
  `examine`(`e`) and `deposit`(`d`) (mem/IPR/GPR read-write — promoted into the
  working set on the user's explicit confirmation), `help` / `?`. **Caveat set:**
  `boot` parsed but resolves to **HALT** (no bootable disk / PCI walk yet — next
  milestone); **LFU / `update srm`** runs against emulated flash. Flagged that the
  user's "evaluate" wording maps to SRM `examine`, not a literal evaluator.
  Reviewed the user's new post draft (hook "15 months. 3 complete architectural
  rewrites. Trillions of instructions executed."; full command bullet list;
  "fault hidden 21 billion cycles deep"; `boot`→HALT honesty) and gave notes.
- **Open / next:** Post still unpublished. Open user decisions: (a) reconcile the
  **timeline number** now public — earlier draft said "~7 months", new draft says
  **15 months** (load-bearing in the hook; pick one); (b) re-add the **GitHub +
  docs URLs into the body** (LinkedIn de-ranks comment-only links) —
  github.com/EmulatRApp/EmulatRAppUni and emulatrapp.github.io/EmulatRAppUni/;
  (c) images — recommended leading with a `show config` terminal screenshot as the
  thumbnail, optional carousel (prompt → config → DS10 block diagram). Standing
  offer: drive EmulatR to `>>>` and capture real `show config`/`device`/`memory`
  transcripts + screenshots for the post.
- **Watch-outs:** Accuracy nits raised on the draft — "PCI I/O bus and IDE/ATAPI
  **already built out**" overstates (IDE/ATAPI still in-progress per the status
  table; prefer "in the build"). Timeline inconsistency (7 vs 15 months) is the
  one factual item to lock before publishing. Doc/advisory session — no code,
  test, or file landings; CMake reconfigure + release rebuild from the prior
  `EMULATR_BRINGUP_PROBES` work still outstanding.
