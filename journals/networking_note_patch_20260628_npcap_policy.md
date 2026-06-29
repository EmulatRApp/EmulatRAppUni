<!--
EmulatR V4 -- networking note PATCH BRIEF (NDIS 6 / Npcap policy commit)
Date: 2026-06-28.  Source: claude.ai web.  Apply against
journals/20260628_networking_packet_backend_architecture.md (written earlier today).
PATCH BRIEF, not a file. Surgical edits -- do NOT rewrite the note. ASCII(128) only.
-->

# networking note patch brief -- COMMIT NDIS 6 / Npcap policy (2026-06-28)

Three edits to `journals/20260628_networking_packet_backend_architecture.md`:
(A) promote the note Status to reflect a committed policy decision; (B) replace the
"DESIGN CONSEQUENCE" paragraph in Section 9 with the committed policy; (C) add the
policy + its rejected alternative to the Section 10 ledger.

================================================================================
EDIT A -- Section header Status line (top of the note, the **Status:** line)
================================================================================

FIND:
**Status:** DESIGN RECORD. No code landed. Forward-looking -- this decides the backend
architecture BEFORE it reaches code, so the determinism implications are not made
implicitly later.

REPLACE WITH:
**Status:** DESIGN RECORD + COMMITTED POLICY (host backend). No code landed.
Forward-looking architecture; the host-backend driver choice is now POLICY (Section 9,
2026-06-28): consume an already-signed NDIS 6 filter (Npcap LWF); never author or sign
our own kernel driver.

================================================================================
EDIT B -- Section 9, replace the "DESIGN CONSEQUENCE" paragraph
================================================================================

FIND (the paragraph beginning "DESIGN CONSEQUENCE:"):
DESIGN CONSEQUENCE: NDIS is NOT a baseline requirement of EmulatR networking. It is the
backend selected for the real-wire-L2 capability tier, behind the `IPacketBackend`
seam, with a hard install/signing cost (collides with #42 run-dir staging -- a kernel
driver cannot stage below the run dir; it is a system install with its own lifecycle).
The user opts into the Npcap/TAP install only for the physical-LAN tier; guest-to-guest
and IP-NAT tiers need no kernel driver. Make this a per-capability cost, not a baseline
tax.

REPLACE WITH:
COMMITTED POLICY (2026-06-28): the real-wire-L2 tier consumes an ALREADY-SIGNED NDIS 6
filter driver -- Npcap's LWF -- and EmulatR NEVER authors or signs its own kernel
driver. Rationale: performance is identical (same NDIS 6 filter mechanism on the same
real adapter), egress shape/timing/density is the faithful "real host on the wire"
behavior we want, and the signing/maintenance/crash-liability burden stays with the
upstream driver. Two axes were decided together:
  - TOPOLOGY = real-adapter FILTER (LWF), NOT virtual miniport. The LWF binds the real
    NIC's actual transmit path -- fewest reshaping layers, real offload, real egress
    density. A virtual miniport presents a synthetic adapter that then needs host
    bridging/routing to reach the wire; that bridging step reshapes frame shape/density
    and adds latency -- the LONGER, LESS faithful path. "Performance + faithful shape +
    compressed stack" selects the LWF.
  - KERNEL OWNERSHIP = CONSUME Npcap (signed, maintained, redistributable), NOT author
    our own. Authoring our own (LWF or miniport) reaches a WORSE version of the same
    egress at an order-of-magnitude cost (WDK, WHQL/attestation signing, per-Windows
    validation, kernel crash liability, #42 installer weight). Rejected.
What EmulatR BUILDS is the user-mode `IPacketBackend` implementation over the Npcap API
(open the bound adapter, program the filter, raw send/recv), feeding the host-thread ->
deterministic-slot split. The kernel LWF underneath is Npcap's.
SCOPE/INSTALL: NDIS is still NOT a baseline tax -- it is the per-capability cost of the
physical-LAN tier ONLY. Guest-to-guest/internal-switch and IP-NAT tiers need NO kernel
driver. Npcap is a bundled/required signed redistributable for the physical-LAN tier; it
is a system install with its own lifecycle and CANNOT stage below the run dir (#42) --
the installer pipeline plans for it as a dependency, not an in-tree staged file.
FIDELITY REQUIREMENT (egress shape): on the bound adapter, DISABLE host checksum/LSO
offload, so the guest's own checksums/segmentation are not reshaped by the host NIC.
This is the concrete mechanism that makes egress shape faithful; it is policy, not
optional.

================================================================================
EDIT C -- Section 10 ledger, add two lines
================================================================================

ADD under Section 10 (VERIFY/CONFIRM ledger):
- **POLICY (committed 2026-06-28):** real-wire-L2 backend = consume Npcap's signed
  NDIS 6 LWF; never author/sign our own kernel driver; topology = real-adapter filter,
  NOT virtual miniport; disable host checksum/LSO offload on the bound adapter.
- **REJECTED (do not reopen):** authoring our own NDIS driver (LWF or miniport) -- an
  order-of-magnitude kernel-dev + signing + installer track that reaches a worse egress
  than Npcap already provides. Only revisit if a hard Npcap licensing/redistribution or
  frame-path blocker is discovered; if so, scope it as its own track with a pipeline plan.

================================================================================
memory.md -- one-line amend to the 2026-06-28 networking entry
================================================================================
Append to the "WHY NDIS" bullet of the networking memory entry:
"  COMMITTED POLICY 2026-06-28: consume Npcap's signed NDIS 6 LWF (real-adapter filter,
NOT virtual miniport); never author/sign our own kernel driver; disable host
checksum/LSO offload on the bound adapter for faithful egress shape. Authoring our own
driver = REJECTED (order-of-magnitude kernel+signing track; revisit only on a hard Npcap
blocker)."
