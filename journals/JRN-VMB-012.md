<!--
EmulatR V4/V5 -- Session Journal JRN-VMB-012
Project: EmulatR (Alpha 21264 / EV6 emulator), V5 active tree (emulatrappuniv5,
         branch v5-tb).
Architect: Timothy Peer.  AI collaboration: Claude (Anthropic).
Copyright (C) 2025, 2026 eNVy Systems, Inc.  All rights reserved.
Licensed under eNVy Systems Non-Commercial License v1.1.
Per docs/notes/ADR-0001-source-file-headers.md (Markdown header as HTML comment).
ASCII(128) only.  Hex radix.
-->

# Session Journal -- ROOT CAUSE of the DTB loop: TLB was set-associative (16x8), not fully associative; conflict-eviction thrash. FIX LANDED as SPAMShardManager<2,64>. WIN + macOS both boot to the VMB handoff (SRM console fully live).

    Doc id      : JRN-VMB-012
    Status      : FIX LANDED + VERIFIED ON BOTH TREES. SPAMShardManager<2,64>
                  applied to the Win hive (3 files) and macOS (Tim). Win rebuild
                  CONFIRMS: SRM console is fully interactive (LFU update, P00>>>),
                  and `b dqa0` / `b dqa1` both boot the VMB all the way to the
                  handoff (Sec 8) -- byte-identical to the macOS result. The DTB
                  thrash is GONE (console-phase low-VA faults thousands -> 4).
                  NEXT WALL confirmed on both: the VMB "jumping to bootstrap
                  code" halts CPU 0 (halt code 0, PC=0x20000000) instead of
                  restarting the OS there (Sec 5). That restart defect is the
                  new frontier.
    Date        : 2026-07-19
    Model       : claude-opus-4-8 (Cowork) + macOS/web variant (Tim). Device
                  bridge to tim-hpz640, tree D:\EmulatR\emulatrappuniv5.
    Relates to  : JRN-VMB-011 Sec 9 (residency fork). ANSWERED here:
                  installed-then-evicted via TLB CONFLICT eviction (wrong
                  associativity). Vindicates Tim's original "shard seam"
                  intuition -- the biting shard is the TLB's, not the RAM's.
    Prohibitions: P-0 honored (macOS doc = the reviewed instruction set;
                  Cowork applied verbatim). P-1, all JRN-VMB-001..005.
    Encoding    : ASCII-128.  Hex radix.

---

## 1. Root cause (Cowork source + Tim's macOS profiling, agreed)

EV6 DTB and ITB are each 128-entry FULLY ASSOCIATIVE (21264/EV67 HRM Sec 2.5,
lines 2545-2546; HRM 4.2). EmulatR modeled them as SPAMShardManager<16, 8> =
16 shards x 8 ways = 128 slots but 16-way SET-ASSOCIATIVE: a VPN maps to ONE
shard (shardIndexOf = tlbKeyHash(tag) & kShardMask, SPAMShardManager.h:198) and
competes for only 8 ways there. When >8 live VMB working-set pages hash to the
same shard, that shard's round-robin cursor (SPAMBucket.cpp:61-87) CONFLICT-
evicts a still-live entry -- identity-mapped console-phase pages (PFN=VPN,
KRE=1, valid=1) get installed, dropped, and re-missed forever. That is the
JRN-VMB-011 Sec 9 "installed-then-evicted" fork, and the eviction is CONFLICT
eviction from wrong associativity (not capacity, not ASN, not never-installed).

## 2. The fix LANDED -- SPAMShardManager<2, 64> (NOT <1,128>)

Cowork initially proposed <1,128> (exact fully-associative). That does NOT
compile: SPAMBucket has static_assert(Ways > 0 && Ways <= 64) (SPAMBucket.h:93)
-- the occupancy word is 64-bit. The macOS-verified fix keeps 128 slots and
raises ways/shard to the cap: <2, 64> = 2 shards x 64 ways = 128 slots. The
~78-page VMB working set splits ~39/shard, well under 64 -> no live entry
conflict-evicted.

Four edits, three files (committed to the Win tree via the device bridge):

1. coreLib/CpuState.h (~:390-391 -> now :398-399)
   SPAMShardManager<16, 8> itbMgr / dtbMgr  ->  <2, 64>
   (+ comment updated: 16x8 matched TB SIZE, not associativity)
2. pteLib/SPAMShardManager.cpp (:221)
   ADDED: template class SPAMShardManager<2, 64>;
   (kept <16,8>/<32,8>/<8,4>; <16,8> now unused-but-harmless)
3. pteLib/SPAMBucket.cpp (:171)
   ADDED: template class SPAMBucket<64>;   (required or LNK2019)
   (kept <8>/<4>)

That is the ENTIRE functional fix: 2 template args + 2 explicit instantiations.
Nothing else hardcodes 16x8 (tests use their own <8,4> / SPAMBucket<4>).

## 3. Verification

macOS (Tim), with the DTBWATCH probe, EMULATR_DTBWATCH_VA=0x1ae000, boot dqa0:
DTBWATCH MISS   15478 -> 99    (159x thrash reduction)
DTBWATCH INSTALL 30956 -> 198
Win (Cowork), rebuild + boot dqa0/dqa1 (log kbd_vga_boot_ds20_20260719_134707,
faults.log): the console-phase low-VA conflict-miss cluster (0x179xxx / 0x17axxx)
collapsed from thousands to 4 total faulting instances. The watched page stays
resident. See Sec 8 for the console-level confirmation.

## 4. OPEN reconciliation item -- snapshot kCpuStateVersion

Neither the macOS doc nor the landed edits bump kCpuStateVersion (currently 10,
Snapshot.h:163). But <16,8> -> <2,64> changes sizeof(CpuState): the TLB block
goes from 16 buckets (16 cursors/occupancy words) to 2 buckets (2), so the
snapshot POD layout changes. Consequence: a snapshot written by the NEW binary
is byte-incompatible with the OLD layout at the SAME version number -> silent
corruption if an old binary loads it. The boot test uses --no-autoload so it
does not bite now. RECOMMENDATION (reconcile across BOTH trees so they stay
identical): bump kCpuStateVersion 10 -> 11 and add the history line, on macOS
AND Win together -- OR consciously defer and rely on --no-autoload. Not yet done
(kept trees byte-identical pending Tim's call). Flag for the next sync.

## 5. NEXT WALL (separate defect -- do NOT conflate with TB associativity)

With the thrash gone, boot dqa0/dqa1 STILL does not reach the OS (BOTH trees).
The console prints the normal handoff banner and returns to P00>>>:
base = 5bc000 ... jumping to bootstrap code
halted CPU 0 / halt code = 0 / PC = 20000000   (back to P00>>>)
boot0 checkpoint (0x20000000) NOT hit; ITBPROBE for 0x20000000 fires 0 times --
NOTHING executes at 0x20000000. The VMB completes its DTB-phase work and hands
off, but the console->system-software transfer to HALT_PC=0x20000000 does not
RESTART the CPU there (halt code 0). This is a SEPARATE defect from TB
associativity -- the new frontier -- likely in the guest SRM/PAL bootstrap
RESTART mechanism, NOT translation. This connects to the long-standing
"0x20000000 never reached" observation (JRN-VMB-003/005/010 CKPT boot0
NOT-REACHED): now we know the boot GETS to the handoff (once the TB stops
thrashing) but the restart-at-0x20000000 does not fire.

Next: retire-trace the guest instructions at the handoff and compare to
reference boot.c (JRN-VMB-003) + the PAL restart / swpctx path (EV6_OSF_PAL.MAR
CALL_PAL__SWPCTX, and the console START/HALT_PC restart mechanics).

## 6. Fidelity note (deferred)

<2,64> is a pragmatic match (full 128 capacity, 64-way -- enough for the VMB
working set). Exact EV6 is 128-entry fully-associative (1 set). For an exact
match: widen SPAMBucket occupancy to 128-bit (__uint128_t or two uint64_t), lift
the Ways<=64 assert to <=128, use <1,128>. Deferred.

## 7. Status / next action

- DONE: <2,64> landed on Win (Sec 2) and verified (Sec 3, Sec 8). macOS + Win
  agree: SRM console fully live, VMB boots dqa0/dqa1 to the handoff-halt.
- RECONCILE: the snapshot kCpuStateVersion bump (Sec 4) across both trees.
- NEW FRONTIER: the 0x20000000 console->OS restart (Sec 5). Next root-cause
  hunt -- retire-trace the handoff vs boot.c + PAL restart/swpctx.

## 8. Win rebuild confirmation -- SRM console live, VMB boots to the handoff

Win run kbd_vga_boot_ds20_20260719_134707 + its SRM console session
(putty_console_p10023_20260719134707.log) confirm the fix end-to-end. The
console is fully interactive:

- Loadable Firmware Update ran: `UPD> u srm` -> "Updating to 7.3-1... PASSED".
- Reset, "Initializing / Testing the System / Testing the Memory".
- Banner: "AlphaServer DS20 ... Console V7.3-2, Feb 27 2007" -> P00>>> prompt.

Then `b dqa0` and `b dqa1` BOTH run the full VMB boot (verbatim console):

    (boot dqa0.0.0.105.0 -flags 0)
    block 0 of dqa0.0.0.105.0 is a valid boot block
    reading 1226 blocks from dqa0.0.0.105.0
    bootstrap code read in
    base = 5bc000, image_start = 0, image_bytes = 99400(627712)
    initializing HWRPB at 2000
    initializing page table at 3ff04000
    initializing machine state
    setting affinity to the primary CPU
    jumping to bootstrap code
    halted CPU 0 / halt code = 0 / PC = 20000000
    P00>>>

This is byte-identical to the macOS next-wall (Sec 5). No Win/macOS divergence.
The stderr kFaultDtbMiss activity (loop at ~0x1ad6xx, and the residual self-map
/ 0xffcXX double-misses in faults.log) is the console's ongoing execution and
post-halt idle when EMULATR_STOP was touched -- NOT a boot blocker: the boot
itself completes to "jumping to bootstrap code". No OutOfRange faults surfaced
(the JRN-VMB-011 memory refactor remains inert). New concrete handoff data for
the Sec 5 hunt: HWRPB built at PA 0x2000, VMB page table built at PA 0x3ff04000,
bootstrap image at base 0x5bc000 (image_bytes 0x99400). The transfer to VA
0x20000000 halts (code 0) rather than fetching/executing there.