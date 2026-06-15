# DS10 SRM Halt -- TLB GH-Escalation Over-Match: Discovery and Diagnostics

Date: 2026-05-23
Component: pteLib/SPAMShardManager (localized V1-derived TLB front-end)
Firmware: DS10_V7_3.exe (single-CPU 21264 / Tsunami)
Status: root cause proven; two-line fix landed; doctests green (313/313);
        DS10 boot re-run in progress to confirm the halt clears.
 --------------------------------------------------------------------------
## 1. One-line summary

The SPAM TLB lookup over-matched: a single 8 KiB (GH=0) ITB entry for VPN
0x300 was satisfying instruction-fetch lookups for the adjacent VPN 0x301,
because the GH-escalation probe masked the lookup VPN down to 0x300 before
the bucket match. The firmware's console code, run at VA 0x602xxx, was
therefore fetched from PA 0x600xxx; when PALcode returned to that VA in
PAL mode (where I-fetch is physical, un-translated), it landed on the data
page at PA 0x6021ec = 0x00000000 = CALL_PAL HALT.

--------------------------------------------------------------------------
## 2. The symptom

DS10 SRM boots, relocates PALcode to PA 0x600000, runs ~4.19M cycles, then
halts cleanly:

    PC = 0x6021ec   palMode = true   halted = true   cycles ~= 4194619-4194625
    lastFault = 13 (kFaultHalt)   excAddr = 0x60083c

The longword at PC 0x6021ec is 0x00000000, which decodes as CALL_PAL HALT.
The halt cycle drifts ~+6 between runs (a host-influenced counter, e.g. an
RPCC/cycle-counter read, makes the run slightly non-deterministic; it never
moved the halt out of a ~200-cycle window).

--------------------------------------------------------------------------
## 3. The path -- what we ruled out

The investigation passed through several plausible-but-wrong hypotheses.
Recording them because each elimination narrowed the search.

3a. "PAL mode should consult the ITB."
    Tempting fix: make translateInstruction translate in PAL mode so the
    return at 0x6021ed would map like a native fetch. REJECTED on the
    architecture: 21264 HRM Restriction 9 ("PALmode[physical] Istream
    addresses") -- EV6 PAL-mode instruction fetch is UNCONDITIONALLY
    physical. V4's identity bypass is correct. Teaching the ITB into the
    PAL fetch path would diverge from silicon and mask the real bug.

3b. "Synthetic fault."
    A PAL-mode LDQ at 0x60083c appeared to fault despite the PAL identity
    bypass, suggesting the MEM drainer manufactured a DTB miss. REFUTED by
    evidence: the instruction is a plain LDQ (opcode 0x29, no HW_LD hint
    bits); it executed in NATIVE mode (the PAL bypass returns Success and
    can never miss, so a miss proves the native path ran); and the retry
    one HW_REI later succeeded with identity PA. It was a normal first-touch
    DTB miss, correctly refilled. Not synthetic.

3c. "Sparse memory backing (GuestMemory V2)."
    REJECTED: the skew is exactly 0x2000 (one 8 KiB Alpha page), not 0x10000
    (the 64 KiB sparse-allocation page); the 0x602000 crossing is interior
    to a single sparse page; the wrong value lived in a TLB entry (filled by
    firmware HW_MTPR), which the backing store never supplies; and decisively
    the D-side, reading the same VA in the same sparse page, was correct. A
    backing-store fault would skew both sides together. The divergence was
    I-side-only.

3d. "Decompressor placed the code one page low" / "Step D relocation."
    REJECTED: SrmLoader does no relocation (it loads the raw .exe at
    0x900000); the firmware's own decompressor relocates, and was observed
    writing real code to physical 0x600xxx via physical (PAL-mode) HW_ST --
    its intended layout. PAL code runs there correctly at identity.

3e. "Bad faulting VA fed to the ITB-miss handler at the page crossing."
    Our leading hypothesis before the final probe: at the 0x601ffc ->
    0x602000 crossing, V4 hands the PAL ITB-miss handler the prior page's
    VA, so it computes PFN 0x300 for VPN 0x301. REFUTED by the probe: there
    was no ITB miss for VPN 0x301 at all (see section 5).

--------------------------------------------------------------------------
## 4. The instrumentation (MEMDIAG probe)

We added a compile-gated (EMULATR_MEMDIAG), behavior-neutral fprintf probe
in three places, capturing exactly the fields needed and nothing else:

  - MemDrainer.h applyMemEffect    (D-side): cyc, pc, encoding, ld/st, va,
    pal, phys-bypass flag, TranslationResult, resolved pa, faultCode.
    Cycle-gated to a ~200-cycle window around the halt.
  - PipelineDriver.h after translateInstruction (I-side): cyc, pc, pal,
    TranslationResult, resolved pa. Cycle-gated.
  - PalEntries.cpp ITB/DTB insert + PipelineDriver.h ItbMiss delivery:
    VPN-gated to VA 0x600000-0x607fff (fires whenever the fill/miss happens,
    independent of cycle), logging the staged tag, raw IPR value, decoded
    VPN and PFN, ASN, GH; and on a miss, the faulting fetch VA.

Cycle-gating (rather than address-gating) was the key to a surgical capture
once we had the halt cycle: deterministic build => the fault recurs at the
same cycle, and ~200 lines bracket the entire fault/handler/retry/halt
sequence. VPN-gating caught the TB fill, which predated the cycle window.

Operational notes that cost us a cycle each:
  - A runtime "-DEMULATR_MEMDIAG=1" in the program args does NOTHING; it is
    a compile-time macro. Preflight check: grep the built exe for the
    "MEMDIAG" strings before trusting a run.
  - Clear snapshots/ before each run, or autoload restores the halt image
    and skips the replay entirely.

--------------------------------------------------------------------------
## 5. The smoking gun

D-side (the LDQ at 0x60083c, va 0x602270):

    MEMDIAG-D cyc=4194521 ld8 va=0x602270 pal=0 phys=0 tr=3 pa=0x0      fault=5
    MEMDIAG-D cyc=4194543 ld8 va=0x602270 pal=0 phys=0 tr=0 pa=0x602270 fault=0

  => real native DTB miss (fault=5=DtbMiss), refilled to identity (PFN 0x301).

I-side, around the page crossing:

    MEMDIAG-I cyc=4194581 pc=0x601ffc pal=0 itr=0 pa=0x601ffc   (identity)
    MEMDIAG-I cyc=4194582 pc=0x602000 pal=0 itr=0 pa=0x600000   (diverted -0x2000)
    ...
    MEMDIAG-I cyc=4194610 pc=0x6021e8 pal=0 itr=0 pa=0x6001e8   (the BSR)
    MEMDIAG-I cyc=4194624 pc=0x6021ed pal=1 itr=0 pa=0x6021ec   (PAL return -> HALT)

The decisive counts from the VPN-gated probes:

    MEMDIAG-ITBFILL : 1   tag=0x6006e0 vpn=0x300 opB=0x600f01 pfn=0x300 gh=0
    MEMDIAG-IMISS   : 1   pcAddr=0x6006e0 (VPN 0x300), itr=4 (ItbMiss)
    MEMDIAG-DTBFILL0: 1   tag=0x602270 vpn=0x301 opB=0x301_0000_ff01 pfn=0x301

There is EXACTLY ONE ITB fill, and it is for VPN 0x300. There is NO miss and
NO fill for VPN 0x301. Yet every native fetch of VA 0x602xxx (VPN 0x301)
HITS and resolves to PFN 0x300. The PA composes as (0x300 << 13) | (va &
0x1FFF), which reproduces 0x600000, 0x6001e8, 0x6007a4 -- the requested VPN
is discarded. The lookup is matching a VPN 0x301 request against the VPN
0x300 entry. (The D-side is correct because the DTB took its own miss for
VPN 0x301 and got a separate identity entry -- the over-match did not shadow
it, by ordering.)

--------------------------------------------------------------------------
## 6. Root cause

pteLib/SPAMShardManager::lookup probes GH = 0..3 (entries are sharded by
their GH-normalised VPN, so a super-page entry can only be found by hashing
with its GH). The defect was in WHAT it passed to the bucket match:

    for (uint8_t gh = 0; gh <= 3; ++gh) {
        TlbTag const probe{rawVpn, asn, realm, gh};  // ctor: vpn = rawVpn & vpnMaskForGh(gh)
        std::size_t const shardIdx = shardIndexOf(probe);
        LookupOutcome const out =
            m_shards[shardIdx].lookup(probe.vpn, ...);   // <-- BUG: GH-masked vpn
        ...
    }

On the GH=1 iteration, a lookup of VPN 0x301 has its probe VPN masked to
0x301 & ~0x7 = 0x300 (GH=1 drops the low 3 VPN bits, the 64 KiB super-page
block). The ITB/DTB on CpuState is SPAMShardManager<16,8> -- only 16 shards,
a 4-bit shardIndexOf mask -- so the GH=1 probe's shard frequently collides
with the GH=0 entry's shard. Inside that bucket, TlbEntry::matches() then
re-masks by the ENTRY's own GH:

    if ((lookupVpn & vpnMaskForGh(gh())) != tag.vpn) return false;  // entry gh=0 -> mask ~0

With lookupVpn already pre-masked to 0x300 and the entry's GH=0, this is
0x300 == tag.vpn(0x300) -> spurious match. The GH escalation had erased the
exact bit that distinguishes VPN 0x301 from 0x300, and the per-entry re-mask
could not recover it.

Net effect: a single-page (GH=0) entry answered lookups for any VPN in its
enclosing GH=1/2/3 block whenever the masked probe collided into its shard.
This also SUPPRESSED the legitimate VPN 0x301 miss, so the correct identity
fill never happened.

--------------------------------------------------------------------------
## 7. The fix

Pass the RAW (un-GH-masked) VPN into the bucket match, and let
TlbEntry::matches() do the per-entry-GH masking it already implements. Shard
selection still uses the GH-normalised probe (that part is correct and
necessary to locate genuine super-page entries).

SPAMShardManager.cpp, lookup (and the identical pattern in invalidateSingle):

    -   m_shards[shardIdx].lookup(probe.vpn, ...)
    +   m_shards[shardIdx].lookup(rawVpn,    ...)

Correctness:
  - GH=0 entry for 0x300, lookup 0x301: matches() computes
    (0x301 & ~0) != 0x300 -> correctly REJECTED. The miss now fires and the
    correct VPN 0x301 entry gets filled (identity, PFN 0x301).
  - Genuine GH=1 super-page entry (tag.vpn=0x300, gh=1): matches() computes
    (0x301 & ~0x7) == 0x300 -> correctly ACCEPTED. Super-page hits unaffected.
  - Exact GH=0 hits unaffected.

Two lines; no shard-layout or tag-format change.

--------------------------------------------------------------------------
## 8. Why the existing tests missed it -- and the regression to add

This SPAM manager is a localized port from V1, edit-tailored for V4, and it
carries its own doctests. None exercised the triggering condition: a small-GH
entry present, followed by a lookup for a DIFFERENT VPN inside the same
larger-GH block, on the production shard shape (16 shards) where the
escalation probe collides into the small-GH entry's shard.

Suggested regression test (deterministic on the <16,8> shape):

  - Insert one ITB entry, GH=0, for VPN 0x300 (identity, PFN 0x300).
  - Assert lookup(VPN 0x301) MISSES. (Pre-fix this HITS, returning PFN 0x300.)
  - Generalize: for a GH=0 entry at VPN V, every lookup of V' in the same
    GH=1/2/3 block with V' != V must miss; only V hits.
  - Mirror for a GH=1 entry: every VPN inside the 8-page block hits and
    returns the same PFN; the first VPN outside the block misses.
  - Run against all three instantiated shapes (<16,8>, <32,8>, <8,4>) so a
    future shard-count change cannot silently reintroduce a collision class.

The general invariant the test pins: a TLB entry must satisfy a lookup if
and only if the lookup VA lies within the entry's own GH block -- never
because an escalation probe's masking happened to alias a different VPN into
the entry's shard.

--------------------------------------------------------------------------
## 9. Architecture confirmed along the way

  - EV6 PAL-mode I-fetch is physical (HRM Restriction 9). V4 correct; do not
    route the ITB into the PAL fetch path.
  - i_ctl = 0x1080: bit 7 (HWE) set -> PAL instructions (HW_LD/HW_ST/etc.)
    execute in native mode; explains the helper's HW_ST at pal=0.
  - i_spe = m_spe = 0: no super-page enable; all native translation goes
    through the software-filled ITB/DTB. ptbr = 0: the PAL miss handlers
    compute the PFN algorithmically, not via a hardware page-table walk.
  - The firmware's intended layout for this region is identity (VA == PA):
    the code page below (0x601xxx) is physically contiguous and
    identity-mapped, and the D-side for 0x602xxx is identity. The over-match
    was the only thing breaking that identity on the I-side.

--------------------------------------------------------------------------
## 10. Verification status / remaining

  - [done] doctest suite: 313/313, 1279 assertions, 0 failed (no GH /
    super-page / AAR / CSR / snapshot regression).
  - [in progress] DS10 boot re-run with MEMDIAG on: expect a MEMDIAG-IMISS
    at pcAddr=0x602000, a MEMDIAG-ITBFILL vpn=0x301 pfn=0x301, MEMDIAG-I
    lines for 0x602xxx now showing pa=0x602xxx, and the 0x6021ec halt to
    clear or advance to a new PC.
  - [todo] add the section-8 regression test.
  - [todo] revert EMULATR_MEMDIAG to 0 in MemDrainer.h and PalEntries.cpp.
  - [todo] commit Windows-side (fix + dormant probe scaffold if desired).
