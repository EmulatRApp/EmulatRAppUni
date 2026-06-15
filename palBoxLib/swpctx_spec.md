# SWPCTX (VMS PALcode) — Implementation Specification

**Instruction:** `CALL_PAL SWPCTX` (opcode `0x00`, function `0x30`)
**Mode required:** Kernel
**References:**
- *Alpha Architecture Reference Manual* (AARM), 4th ed., §II-B (OpenVMS PALcode), SWPCTX
- Local PAL source: `alpha_pal_vms.c` (SimH-derived), `swpctx` handler at line ~1430
- HWPCB layout: AARM §II-B HWPCB
- Memory model: SimH `WritePQ` / `ReadPQ` semantics (quadword-indexed, low-3-bit don't-cares)

---

## 1. Purpose

Atomically switch the running process context. The current per-process state
(stacks, address space, AST state, FP enable, cycle counter offset) is written
to the *old* Hardware Privileged Context Block (HWPCB) addressed by the
current `PCBB` internal processor register, and the *new* state is loaded
from the HWPCB whose physical address is supplied in R16. The previous
`PTBR` value is returned in R0 so the caller may flush per-process
translations if required.

This is a privileged operation; behavior is UNDEFINED if executed outside
kernel mode (PALcode SHOULD raise an opcode-reserved exception, but this
implementation relies on the dispatch layer's mode check).

---

## 2. Inputs

| Source            | Meaning                                      |
|-------------------|----------------------------------------------|
| R16               | Physical address of *new* HWPCB (`newPcbb`)  |
| `cpu->pcbb`       | Physical address of *old* HWPCB (`oldPcbb`)  |
| `cpu->ptbr`       | Outgoing page table base register            |
| `cpu->intReg[30]` | Live kernel SP (KSP at call time)            |
| `cpu->{esp,ssp,usp,asn,asten,astsr,fen,cc}` | Outgoing context fields |
| Physical memory at `[newPcbb, newPcbb+0x47]` | Incoming context     |

R16 is consumed before any write to the old HWPCB or any update to CPU
state, so a corrupt R16 cannot poison the saved old context.

---

## 3. Outputs

| Destination       | Value                                        |
|-------------------|----------------------------------------------|
| R0                | Old `PTBR` (captured before any state change)|
| `cpu->pcbb`       | `newPcbb`                                    |
| `cpu->intReg[30]` | New KSP loaded from new HWPCB                |
| `cpu->{ptbr,asn,asten,astsr,fen,cc,esp,ssp,usp}` | New HWPCB values |
| Memory at `[oldPcbb, oldPcbb+0x47]` | Old context (see §5)       |

R0 is reported via the BoxResult write-back channel
(`regWriteIdx=0, regWriteIsFp=false`); the running R0 in `intReg[0]` is
updated by the dispatcher after the handler returns, consistent with
`S_WritesRa` semantics elsewhere in the box.

---

## 4. HWPCB Layout

All fields are 64-bit quadwords. Offsets are HWPCB-relative.

| Offset | Field      | Notes                                              |
|--------|------------|----------------------------------------------------|
| 0x00   | KSP        | Kernel stack pointer                               |
| 0x08   | ESP        | Executive stack pointer                            |
| 0x10   | SSP        | Supervisor stack pointer                           |
| 0x18   | USP        | User stack pointer                                 |
| 0x20   | PTBR       | Page table base (physical page number form)        |
| 0x28   | ASN        | Address space number; low N bits significant       |
| 0x30   | ASTEN/ASTSR| AST enable (high 32) / summary (low 32) packed     |
| 0x38   | FEN        | Bit 0 = floating point enable; other bits MBZ      |
| 0x40   | CC         | Cycle counter offset (used by RPCC)                |

The trailing 0x48..0x7F region is reserved/UNPREDICTABLE in the AARM and is
neither read nor written.

---

## 5. Sequence (normative)

The handler shall execute the following steps in order. Steps are not
externally observable individually — the operation is atomic with respect
to other CPU instructions on this core (see §8).

1. **Capture return value.** Read `cpu->ptbr` into a local; this becomes the
   R0 write-back. Captured before any subsequent step so the value reported
   is unambiguously the outgoing PTBR.

2. **Snapshot live state.** Build `oldCtx` from current CPU registers via
   `storeCpuToHwpcb`. Overwrite `oldCtx.ksp` with the live R30 (the
   architectural KSP is the running SP whenever PALcode executes — entry to
   PAL forces kernel mode).

3. **Write old HWPCB.** Issue nine quadword stores to `oldPcbb + {0x00,
   0x08, ..., 0x40}` via the memory bus (`write8`). See §6 for bounds and
   §7 for failure handling.

4. **Read new HWPCB.** Issue nine quadword loads from `newPcbb + {0x00,
   0x08, ..., 0x40}` into a local `newCtx`. See §6 and §7.

5. **Install new context.** Apply `newCtx` to CPU state via
   `loadCpuFromHwpcb`. Update `cpu->pcbb := newPcbb`. Update running R30 to
   `newCtx.ksp` (this is the step the AARM pseudocode elides and that SimH
   makes explicit at `alpha_pal_vms.c:1430`).

6. **Report R0.** Populate `BoxResult` with the captured old PTBR.

Steps 3 and 4 may not be reordered: although the architectural state
changes only at step 5, swapping the order would corrupt the old HWPCB if
`oldPcbb == newPcbb` (legal per AARM — a process switching to itself).

---

## 6. Bounds and Alignment

### 6.1 Alignment

- R16 (and `cpu->pcbb`) SHOULD be quadword-aligned. The AARM declares
  behavior UNPREDICTABLE otherwise.
- The implementation does NOT enforce alignment at runtime in release
  builds. Rationale: the SimH-style memory bus masks the low 3 bits in
  `WritePQ`/`ReadPQ` (`M[pa >> 3] = dat`), so an unaligned PCBB silently
  rounds down to the containing quadword. This matches real Alpha
  behavior, where the physical port ignores the low 3 bits of a quadword
  access.
- All field offsets (0x00 through 0x40) are multiples of 8. If the base is
  aligned, every field access is aligned; no per-field check is required.
- Debug builds SHALL assert quadword alignment of both `newPcbb` and
  `oldPcbb` on entry:
  ```cpp
  DCHECK_EQ(newPcbb & 7, 0);
  DCHECK_EQ(oldPcbb & 7, 0);
  ```
  A fired assertion indicates either guest corruption or a bug in a
  previous SWPCTX / boot-time PCBB install.

### 6.2 Addressability

- `newPcbb` and `oldPcbb` SHALL satisfy `addr + 0x47 < MEMSIZE` for all
  nine fields to be in-memory. The minimum legal HWPCB straddles
  `[addr, addr+0x47]` (72 bytes).
- The handler does NOT pre-validate this range. Per-access checking is
  delegated to the memory bus: any `write8` or `read8` to a non-memory
  physical address returns `SCPE_NXM` (or the box-local equivalent) and
  does not modify memory or `newCtx`.
- I/O-space PCBBs are not architecturally meaningful but are not
  prohibited; the SimH model would route the access through `WriteIO` /
  `ReadIO`. This implementation follows suit by virtue of using the same
  bus primitives.

### 6.3 PTBR Validity

The PTBR loaded from the new HWPCB is installed verbatim. Its validity
(points to a legitimate page table root) is the operating system's
responsibility. A bad PTBR will not be diagnosed here; the next translated
access will fault.

---

## 7. Failure Modes

The handler currently discards `write8`/`read8` return values
(`(void)c.memory->write8(...)`). This is a known gap. The following
behavior SHALL apply once return-value checking is added:

| Condition                          | Required behavior                       |
|------------------------------------|-----------------------------------------|
| Any old-HWPCB store fails (NXM)    | Abort: do not load new context; signal machine check via BoxResult; CPU state unchanged from step 1 onward |
| Any new-HWPCB load fails (NXM)     | Abort: do not load new context; signal machine check; CPU state and `pcbb` unchanged |
| New-HWPCB load succeeds but yields architecturally invalid PTBR/ASN/FEN | Install as-is; not the PALcode's responsibility |

Until the gap is closed, an NXM on the new HWPCB will cause `newCtx` to
remain zero-initialized and a zero context to be installed — a guest-
visible bricking failure. Tracked separately; see §11.

The handler MUST NOT raise C++ exceptions (`noexcept`). All failure
reporting goes through `BoxResult`.

---

## 8. Atomicity and Ordering

- SWPCTX is atomic with respect to other instructions executed on the same
  virtual CPU. The dispatcher does not interleave instruction execution.
- SWPCTX is NOT atomic with respect to other virtual CPUs or DMA agents
  observing the old or new HWPCB. The architecture does not require it to
  be: HWPCBs are per-process structures and the OS is responsible for
  preventing concurrent access (typically by holding the scheduler
  spinlock and raising IPL).
- Memory ordering: writes to the old HWPCB are issued before reads of the
  new HWPCB. This is observable to a second CPU if (and only if) it is
  examining the HWPCB without OS-level synchronization, which is
  architecturally ill-formed.

---

## 9. ASN / TB Considerations

- `ASN` is loaded from the new HWPCB into `cpu->asn`. The handler does NOT
  flush the translation buffer; on a real Alpha with ASN support the new
  ASN selects a distinct TB partition automatically.
- If the implementation models a TB that does not honor ASN (a
  simplification), the caller (or a wrapper around SWPCTX) must invalidate
  the TB. This is not done here.
- `PTBR` is loaded but does not in itself invalidate translations; the
  AARM relies on the OS to issue `MTPR_TBIA`/`MTPR_TBIAP` when reusing an
  ASN. The R0 return of the old PTBR exists precisely so the OS can decide
  whether to recycle.

---

## 10. Cycle Counter (CC)

- `CC` at offset 0x40 is the **cycle-counter offset**, not the live
  counter value. The architectural RPCC instruction returns
  `(host_cycles + cc_offset) & 0xFFFFFFFF` in the low 32 bits.
- The handler stores `cpu->cc` to the old HWPCB and loads it from the new
  HWPCB unchanged. No relationship to wall time is established.

---

## 11. Known Gaps / TODO

1. **Bus return-value checking.** §7. All nine writes and nine reads
   silently swallow errors. Closing this requires propagating an NXM
   indication into `BoxResult.semFlags` (likely a new `S_MachineCheck`
   bit).
2. **Alignment asserts.** §6.1. Add `DCHECK_EQ(... & 7, 0)` once a
   project-wide DCHECK macro lands.
3. **Bare-unit harness.** The early `c.memory == nullptr` return makes
   SWPCTX a no-op in unit tests that don't construct a memory bus.
   Acceptable for now; document in test fixtures that SWPCTX coverage
   requires the integration harness.
4. **Self-switch optimization.** When `newPcbb == oldPcbb` the entire
   read-modify-write cycle is pointless. Not optimized; SWPCTX is not on
   any hot path that would benefit.

---

## 12. Test Vectors (minimum)

1. **Round-trip.** Set up HWPCB A with known values, SWPCTX to A from a
   zero context, verify all nine fields land in CPU state and R0 holds
   the prior PTBR (zero).
2. **Two-context swap.** Initialize two HWPCBs A and B with distinct
   values. SWPCTX A→B, then B→A. After the second swap, CPU state must
   match the initial A state byte-for-byte; HWPCB A must be unmodified
   from its initial write; HWPCB B must hold the A-state from the first
   swap.
3. **Self-switch.** `newPcbb == oldPcbb`. CPU state after must equal CPU
   state before (modulo R0, which holds the prior PTBR).
4. **R30 is KSP at call time.** Pre-set R30 to a recognizable value, call
   SWPCTX, verify the old HWPCB's offset 0x00 contains that value (NOT
   whatever was previously in `cpu->ksp` if such a field exists
   separately).
5. **R0 is OLD PTBR.** Set old PTBR ≠ new HWPCB's PTBR field, call
   SWPCTX, verify R0 holds the old value and `cpu->ptbr` holds the new.
6. **NXM on new HWPCB.** Once §11.1 is fixed: point R16 past MEMSIZE;
   verify the old HWPCB is unmodified, CPU state unchanged, and a
   machine-check is signaled.
7. **Unaligned PCBB (debug).** Once §11.2 is added: set R16 = aligned + 4;
   verify the debug assertion fires. (Release builds will silently round
   down; this is per-spec but not behavior we want to test against.)
