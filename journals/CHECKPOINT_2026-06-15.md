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
