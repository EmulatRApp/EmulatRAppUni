# EmulatR -- Execution Model Specification (Draft)

**Status:** Draft, open for discussion
**Replaces in proposal:** Sections 7-10 of `journals/EmulatR4_proposal.txt`
**Source of truth for instruction set:** `grainFactoryLib/GrainMaster.tsv`
**Date:** 2026-05-05

---

## 0. Purpose and scope

This section defines how an Alpha 21264 instruction is decoded, dispatched, and
executed inside EmulatR.  It pins down:

- The pipeline stage shape and what each stage may and may not do.
- The `InstructionGrain` POD layout: what fields it holds, how it is built,
  and how it dispatches.
- The function-table architecture: how (opcode, function-code) is mapped to
  an executor function pointer, and how the table is generated from
  `GrainMaster.tsv`.
- The Box layer: what Boxes are, what their interface looks like, how they
  receive an executed grain's `BoxResult`, and how they commit side effects
  to architectural state.
- The decode and operand-resolution flow: how `rawBits` becomes a fully
  populated grain.
- PAL-personality disambiguation when one (opcode, function-code) pair has
  multiple candidate mnemonics in the master table.
- Code generation policy: which artifacts are generated from
  `GrainMaster.tsv`, which are hand-written, and how the two layers stay in
  sync.

**Concurrency model (decided 2026-05-05):**  EmulatR v1 is a
**single-threaded, single-core, deterministic** simulation.  One
`AlphaCPU`, one `AlphaPipeline`, one main thread driving ticks.  No
locking primitives in the memory subsystem, no per-CPU thread
scheduling, no inter-CPU synchronization barriers.  Multi-CPU support
is a v2 extension that will be designed against this v1 baseline; the
APIs are kept clean enough to extend later, but the v1 implementation
does not pre-pay any concurrency tax.

**Source synthesis:**  This design draws the **BoxResult** pattern
(clarity at the call site, paired flag-and-payload setters) from V1
and the **per-format function-table dispatch** (bounded sub-tables
indexed by extracted function code, no flat mega-table) from V2.  Each
of those layers worked in its respective version; the V3 missteps
were elsewhere (per-grain class hierarchy, BoxResult merge defect,
loader drain elision).

Out of scope for this section (defined elsewhere in SPEC):
- Memory subsystem and aliasing rules (sections 2-6 of proposal).
- IBox fetch path and the ROM/cache-view distinction (section 3).
- Trace formatting and observability.
- Snapshot save/restore semantics (designed in section 11; implemented
  post-v1).
- Build, test, and toolchain conventions.

---

## 1. Pipeline shape

### 1.1 Stage list

Six stages, in instruction-flow order from oldest at retire to newest at fetch:

| Index | Stage | Role |
|-------|-------|------|
| 0     | IF    | Instruction Fetch.  Reads `rawBits` from the IBox view (ROM image during boot, GuestMemory after).  Extracts opcode, sets `slot.di.pc` and `slot.di.rawBits`.  No register reads.  No architectural side effects. |
| 1     | DE    | Decode.  Extracts ra / rb / rc / disp / literal / function-code per the opcode's format rule.  Populates the operand fields of the grain. |
| 2     | GR    | Grain resolution.  Looks up the executor function pointer from the function table indexed by (opcode, function-code, personality), and sets `grain.execute_fn`.  Determines the dispatch Box.  Reads register operands at this point (so EX has them ready).  No architectural register writes. |
| 3     | EX    | Execute.  Calls `grain.execute_fn(slot)`.  The executor reads operand values from the grain, computes the result and any side effects, and emits an `EffectSet` into the slot.  No direct register writes. |
| 4     | MEM   | Memory access AND architectural commit.  Loads complete and write their result into the slot's pending commit.  Stores translate the address but defer the actual memory write to the next stage if needed.  Pending integer / float register commits from the EffectSet are applied to the architectural register file here.  This is the single architectural-write point in the pipeline. |
| 5     | WB    | Writeback / retirement.  Trace formatter emits the `INS` and `REG` lines.  Branch predictor is updated.  Slot is marked retired.  No architectural state changes after this point. |

### 1.2 Stage invariants

The following are hard contracts.  Code that violates any of them is a bug
even if it appears to work.

1. The architectural integer and float register files are written **only**
   at MEM.  Not at EX, not at WB, not anywhere else.  If a stage needs a
   register's *new* value before MEM has committed it, the bypass network
   (section 5.5) supplies it.

2. The PC redirect (m_pc.redirectPC) is performed **only** at EX, in the
   executor for branches and indirect jumps.  Subsequent stages observe
   the new PC; earlier-cycle slots in IF/DE/GR are squashed by the same
   call that performed the redirect.

3. Each stage operates on exactly one PipelineSlot per tick.  The pipeline
   ring rotates once per tick.  No stage looks at any slot other than its
   own.  Cross-stage observation is allowed only through the bypass
   network and the explicit slot.boxResult / slot.m_pending fields.

4. No stage may invoke another stage's logic directly.  IF does not call
   DE; DE does not call GR.  Stage progression happens through ring
   rotation only.

5. Once a slot enters MEM, it cannot be squashed.  Squashing a committed
   register write is a correctness violation that no flush path may produce.
   If a flush would otherwise reach MEM, the flush predicate is wrong.

### 1.3 Why six stages

The shape mirrors the EV6 functional stages closely enough to make
fidelity-tracing comparisons against the 21264 HRM straightforward, while
still being a tractable in-order single-issue model.  GR is split out from
DE because grain resolution requires the function-table lookup -- a
distinct activity from instruction-format decode -- and pinning it in its
own stage prevents the "DE that secretly does dispatch" anti-pattern.

---

## 2. PipelineSlot: the per-cycle data carrier

A `PipelineSlot` is the data structure that travels through the pipeline
ring.  It is owned by `AlphaPipeline`, which holds a fixed-size array
(`m_slots[6]`) and rotates via `m_head`.

### 2.1 Slot layout (sketch)

```
struct PipelineSlot {
    // ---- Identity -------------------------------------------------------
    uint64_t        slotSequence;     // monotonic; assigned at fetch
    uint64_t        cycle;            // cycle this slot was fetched
    uint8_t         currentStage;     // 0..5, set by stage advance

    bool            valid;            // true if this slot holds an instruction
    bool            stalled;
    bool            mispredict;

    // ---- Decoded instruction (the grain) --------------------------------
    InstructionGrain di;              // POD; see section 3

    // ---- Operand values read by GR -------------------------------------
    uint64_t        ra_value;         // R[di.ra] at GR time, possibly bypassed
    uint64_t        rb_value;         // R[di.rb] at GR time, possibly bypassed
    uint64_t        literal_value;    // sign-extended literal if di.is_literal
    // (FP operands tracked similarly; omitted for brevity)

    // ---- BoxResult (filled by EX, consumed by MEM and WB) --------------
    // Replaces V3's BoxResult plus PendingCommit; one record carries
    // flags + paired payloads for all side effects this slot requests.
    // See section 6.5 for shape and producer/consumer contract.
    BoxResult       boxResult;

    // ---- Branch / jump resolution --------------------------------------
    bool            branchTaken;
    uint64_t        branchTarget;
    uint64_t        nextPC;
    uint64_t        predictionTarget;
    bool            predictionTaken;
    bool            predictionValid;

    // ---- Box-handle sidecars (set at construction, immutable) -----------
    EBox*           m_eBox;
    FBox*           m_fBox;
    MBox*           m_mBox;
    PalBox*         m_palBox;
    CBox*           m_cBox;
    IprMaster*      m_iprGlobalMaster;

    // ---- Trace + debug --------------------------------------------------
    PCReason        pcReason;
    BoxResult       boxResult;
    // ... trace fields ...
};
```

The layout above is illustrative.  Final field ordering is determined by the
cache-line / alignment policy in section 7.

### 2.2 Slot lifecycle

A slot's lifecycle is one full pipeline traversal.  The same physical slot
in `m_slots[]` is reused across cycles via ring rotation.

- **Fetch (IF):** `slot.di.pc` is set, `slot.di.rawBits` is set,
  `slot.valid = true`, `slot.cycle = m_cycleCount`,
  `slot.slotSequence = next`.  All other fields are zeroed by `clear()`.
- **Decode (DE):** `slot.di` operand fields (ra, rb, rc, disp, literal,
  function-code) are populated from rawBits.  `slot.di.execute_fn` is still
  null at this point.
- **Resolve (GR):** `slot.di.execute_fn` is set from the function table.
  `slot.ra_value` and `slot.rb_value` are read from the architectural
  register file (with bypass).  The dispatch Box pointer (already
  available in the slot from construction) does not change here.
- **Execute (EX):** `slot.di.execute_fn(slot)` runs.  Populates
  `slot.boxResult` via paired setters (e.g., `setIntCommit(rc, val)`,
  `setBranch(target, writesLink, linkVal)`, `setCommittedArchitecturalPC(pc)`).
  May call `m_pc.redirectPC()` for branches.  No direct register or
  memory writes occur here -- everything is staged through
  `slot.boxResult`.
- **Memory (MEM):** Loads finish (data populates `slot.boxResult` via
  `setIntCommit` / `setFpCommit`).  Stores translate.  `commitPending(slot)`
  applies `slot.boxResult` to the architectural register file and
  issues store memory writes.  R31 / F31 zero-write skip happens here,
  in one place.
- **Writeback (WB):** Trace formatter emits `INS` and `REG` lines from
  `slot`.  Branch predictor is updated.  `slot.valid = false` and the
  slot is conceptually retired.  Ring rotation will reuse this physical
  slot at next IF.

---

## 3. The InstructionGrain POD

The grain is the heart of the design.  It is a plain-old-data struct (no
virtual methods, no vtable, no inheritance).  All dispatch is via a
function pointer stored in the grain itself.

### 3.1 Layout

```
struct InstructionGrain {
    // ---- Raw and identity ----------------------------------------------
    uint64_t   pc;            // address of this instruction
    uint32_t   rawBits;       // the 32-bit instruction word as fetched

    // ---- Format-level decode (filled by IF / DE) ------------------------
    uint8_t    opcode;        // bits[31:26], 6 bits
    uint16_t   functionCode;  // format-dependent; see section 4
    uint8_t    format;        // OperateFmt, MemoryFmt, BranchFmt, JumpFmt,
                              //   FloatFmt, PalFmt, HwFmt, ...
    uint8_t    personality;   // 0=OpenVMS, 1=Tru64; selects between
                              //   personality-aliased entries

    // ---- Operand fields (filled by DE) ---------------------------------
    uint8_t    ra;            // bits[25:21]
    uint8_t    rb;            // bits[20:16]
    uint8_t    rc;            // bits[4:0] for Operate format only
    bool       is_literal;    // Operate format bit[12]
    uint8_t    literal_byte;  // bits[20:13] when is_literal
    int32_t    disp;          // sign-extended branch / memory displacement

    // ---- Semantic flags (filled by DE; bit-field) ----------------------
    uint32_t   semantics;     // S_Branch, S_Load, S_Store, S_FloatFmt,
                              //   S_BranchWriteLink, S_Privileged, ...

    // ---- Execution dispatch (filled by GR) -----------------------------
    void     (*execute_fn)(PipelineSlot& slot);
    uint8_t    execBox;       // EBox / FBox / MBox / PalBox / CBox / Branch

    // ---- Pre-decoded mnemonic for trace formatter (filled by GR) -------
    const char* mnemonic;     // points at static string table; never freed
};
```

Total size target: <= 64 bytes (one cache line).  Precise layout is
finalized once we run the design through `static_assert(sizeof(...))`.

### 3.2 Why a function pointer instead of a virtual method

This is the central design decision.  Three reasons.

**Vtable corruption is not a failure mode.** With one C++ class per
instruction, every grain object carries a vtable pointer as its first
8 bytes.  A wild write to that vptr silently re-routes `execute()` to a
different class's method while the data members still report the original
identity.  This is exactly the failure mode we suspected during EmulatR's
final debugging session.  With a function pointer in a POD, a wild write
to `execute_fn` produces an obvious crash on the very next call rather
than a silent dispatch to a wrong-but-valid executor.

**No 600-class proliferation.** With function pointers, per-instruction
differentiation lives entirely in *data* (the grain's fields and the
table entry).  There are no per-instruction headers, no auto-registration
singletons, no risk of a generator emitting `BIS_InstructionGrain.h` with
HW_MTPR's body in it (an actual EmulatR bug).

**The function table is the source of truth.** The dispatch table is
generated from `GrainMaster.tsv`, the executor names are looked up from a
single registry, and a (opcode, function) pair cannot be wired to a
contradictory body because there is no place in the source where that
contradiction would be expressible.

### 3.3 Trade-off versus virtual dispatch

Function-pointer dispatch is one indirect call per instruction (load
function pointer from grain, call through it).  Virtual dispatch is one
indirect call too (load vptr from object, load function pointer from
vtable, call through it -- same number of indirections in optimized
builds, since modern compilers cache the vtable pointer).  Performance
parity is expected.  Function pointers have slightly better cache
behavior because the grain itself is the only thing the executor needs to
load, whereas virtual dispatch requires the vtable to be cache-warm too.

---

## 4. The function table

### 4.1 Top-level shape

A naive flat `functionTable[opcode][functionCode]` is too large because
function-code widths vary by format (2 bits for Jump, up to 16 bits for
HW format).  Instead, the table is two-level:

```
struct OpcodeEntry {
    DispatchKind  kind;           // how to extract function-code
    const ExecutorFn* table;      // sub-table indexed by extracted code,
                                  //   or pointer to a single executor
                                  //   if kind == NoFunc
    uint16_t      tableSize;      // size of *table for bounds check
    uint8_t       defaultBox;     // EBox / FBox / MBox / PalBox / etc.
    const char*   formatName;     // diagnostic string
};

OpcodeEntry  g_opcodeTable[64];   // indexed by bits[31:26] of rawBits
```

The IF stage reads opcode = `(rawBits >> 26) & 0x3F` and indexes
`g_opcodeTable[opcode]` to get the descriptor.

### 4.2 DispatchKind enum

```
enum DispatchKind : uint8_t {
    NoFunc,         // single executor per opcode (Memory, Branch formats)
    OperateFunc,    // function-code = bits[11:5], 7 bits, 128 entries
    PalFunc,        // function-code = bits[7:0], 8 bits, 256 entries
    HwMemFunc,      // function-code = bits[15:13] (TYPE), 3 bits, 8 entries
    HwIprFunc,      // function-code = bits[7:0] (IPR index), 256 entries
    JumpFunc,       // function-code = bits[15:14], 2 bits, 4 entries
    FloatFunc,      // function-code = bits[10:0], 11 bits, 2048 entries
                    //   (sparse; actual table is the matrix from
                    //   GrainMaster.tsv compacted via a map or
                    //   sparse-array approach)
};
```

The kind-specific extraction rule is encoded once per kind in a small
free function:

```
uint16_t extractFunctionCode(DispatchKind kind, uint32_t rawBits) noexcept;
```

DE calls this to populate `grain.functionCode`.  GR uses it again
(idempotent) when it indexes the sub-table.  Encoding the extraction in
one place rather than in every executor prevents the "operate-format
function-code extracted as PAL-format function-code" bug class.

### 4.3 Sub-table layout

Each `OpcodeEntry::table` points at an array of `ExecutorFn` (function
pointer with the executor signature).  For sparse formats (Float),
unreachable entries point at a single `executeOpcdec` stub that triggers
OPCDEC.  The table is sized to cover the full extracted range so the
lookup is a direct index, never a linear scan.

```
using ExecutorFn = void (*)(PipelineSlot&);

extern const ExecutorFn  g_opTable_Operate_0x10[128];   // INTL group
extern const ExecutorFn  g_opTable_Operate_0x11[128];   // INTL group
extern const ExecutorFn  g_opTable_Operate_0x12[128];   // INTSL group
extern const ExecutorFn  g_opTable_Operate_0x13[128];   // INTM group
extern const ExecutorFn  g_opTable_Pal_0x00[256];       // CALL_PAL
extern const ExecutorFn  g_opTable_Hw_0x1B[8];          // HW_LD type
// ... etc per opcode that has function codes ...

extern const ExecutorFn  g_opSingle[64];                // for NoFunc kinds
```

### 4.4 PAL personality disambiguation

`GrainMaster.tsv` contains entries where the same (opcode, function-code)
pair maps to two different mnemonics depending on OS personality
(OpenVMS vs Tru64/UNIX).

**Naming convention (project rule):** a mnemonic with the `_64` suffix
is the **Tru64 / UNIX-personality** variant.  The base mnemonic without
the suffix is the **OpenVMS-personality** variant (or the architectural
common form if no Tru64 variant exists).  Examples from
`GrainMaster.tsv`:

| Opcode | Function | OpenVMS / Common | Tru64 (_64 suffix) |
|--------|----------|-------------------|---------------------|
| 0x00   | 0x0000   | CALL_PAL, HALT    | HALT_64            |
| 0x00   | 0x0001   | CFLUSH, RESTART   | CFLUSH_64          |
| 0x00   | 0x0002   | DRAINA, REBOOT    | DRAINA_64          |
| 0x00   | 0x0009   | CSERVE            | CSERVE_64          |
| 0x00   | 0x000A   | SWPPAL            | SWPPAL_64          |

This is a structural convention the codegen relies on, not a runtime
heuristic.  The TSV row is the source of truth; the suffix is the
personality tag.

The dispatch table compiles as follows: for each (opcode, function-code)
slot, if there are N personality variants, the table holds a pointer to
a small `PersonalityResolver`:

```
struct PersonalityResolver {
    ExecutorFn  vms;          // OpenVMS-personality executor (no suffix)
    ExecutorFn  unix;         // Tru64-personality executor (_64 suffix)
    ExecutorFn  common;       // fallback if personality is unset, or for
                              //   entries whose only variant is unsuffixed
                              //   and applies regardless of personality
};
```

GR resolves personality from the IPR / runLoop state (`pal_personality`
field, 0 = OpenVMS, 1 = Tru64) and selects the right executor at lookup
time.  The grain's `execute_fn` is set to the chosen executor.  GR pays
the resolver cost only for the small number of table entries that have
personality variants; the common case is a single executor pointer with
no resolver indirection.

Codegen consequence: the generator parses each mnemonic and partitions
rows by the `_64` suffix.  A row whose mnemonic lacks `_64` becomes the
OpenVMS slot of the `PersonalityResolver`; a `_64` row becomes the Tru64
slot.  When a (opcode, function-code) pair has only one mnemonic with
no suffix and no Tru64 sibling, the entry collapses to a single
`ExecutorFn` (no resolver indirection).  When both variants exist the
entry promotes to a `PersonalityResolver`.

### 4.5 Generation from GrainMaster.tsv

`GrainMaster.tsv` is the single source of truth.  A code generator reads
it and emits:

- `g_opcodeTable[]` (the top-level dispatch table).
- The per-opcode sub-tables.
- Forward declarations of every executor function name referenced.
- A header listing all required executor functions for each Box, so that
  a missing executor is a link error rather than a runtime fault.
- A reference matrix in Markdown form for documentation.
- A coverage stub for tests (one test per (opcode, function) row that
  fires the dispatch and asserts the expected mnemonic and Box).

The executor function bodies themselves are **not** generated.  They are
hand-written in the appropriate Box library (`EBoxLib/`, `FBoxLib/`,
`MBoxLib/`, `PalBoxLib/`, `CBoxLib/`).  The generator produces only the
glue that wires names to addresses.

This split keeps the architecturally interesting logic (instruction
semantics) hand-readable and reviewable, while the boilerplate
(declarations, table layout, registration) is regenerated from the TSV
every build.

---

## 5. Decode and operand resolution

### 5.1 IF (stage 0)

- `m_pc.getPC()` provides the PC for this fetch.
- The IBox view (ROM image during boot, GuestMemory after) supplies
  `rawBits = readInst32(pa)`.
- `slot.di.pc = pc`, `slot.di.rawBits = rawBits`,
  `slot.di.opcode = (rawBits >> 26) & 0x3F`,
  `slot.di.format = g_opcodeTable[opcode].format` (a derived enum).
- `m_pc.advancePC()` for sequential fetch; redirects from prior cycles'
  EX stages have already updated m_pc.

### 5.2 DE (stage 1)

- Looks up `desc = g_opcodeTable[slot.di.opcode]`.
- Calls `extractFunctionCode(desc.kind, rawBits)` to get the function
  code if any; stores in `slot.di.functionCode`.
- Extracts ra, rb, rc, is_literal, literal_byte, disp per the format.
- Sets `slot.di.semantics` from a per-(opcode, function) flag table
  (also generated from `GrainMaster.tsv`, with hand-curated per-row
  semantic flags in a sibling TSV column or a parallel file).

### 5.3 GR (stage 2)

- Looks up the executor: `entry = desc.table[slot.di.functionCode]`.
- If `entry` is a `PersonalityResolver*`, dispatches to the right variant
  based on the runLoop personality bit.
- Sets `slot.di.execute_fn = entry`, `slot.di.execBox = desc.defaultBox`.
- Reads register operands: `slot.ra_value = readIntReg(slot.di.ra)` with
  bypass; `slot.rb_value = readIntReg(slot.di.rb)` or
  `slot.literal_value = sign_extend(slot.di.literal_byte)` if
  `slot.di.is_literal`.  FP-format instructions read from the float
  register file with the float bypass network instead.

**Store-operand timing rule (decided 2026-05-05).**  For store
instructions, both the data operand (Ra) and the address-base operand
(Rb) are read at GR and latched into the slot.  The store does not
actually issue to memory until MEM, but the operand values are not
re-read at MEM.  Justification: EV6-style cores resolve store data
early and treat it as architecturally committed at issue time;
re-reading at MEM would model an out-of-order forwarding behavior the
architecture does not expose.  Document this explicitly so future
contributors do not "fix" the design by adding a re-read.

### 5.4 Why register reads happen at GR, not EX

Two reasons.

**Pipeline-stage clarity.** Register read is a distinct activity from
operand computation.  Putting it at GR means the EX stage receives a
slot with operands already populated, and EX is purely about computing.
This makes the stage contract testable: a GR-stage unit test verifies
operand reads; an EX-stage unit test verifies computation given known
operands.

**Bypass becomes explicit.** GR is the only place that needs the bypass
network.  It reads the architectural register file and folds in any
in-flight values from later stages (MEM-stage commit, EX-stage pending
write).  Confining bypass to one stage means the bypass logic lives in
one place.

### 5.5 Bypass network

Two parallel tables, one per register class.  The integer and float
pipelines have independent forwarding semantics on EV6 (separate
result buses, distinct hazard windows), so modeling them as separate
networks rather than a unified table keyed on register class keeps
the contract clear and the per-class hazard windows easy to reason
about.

```
struct BypassEntry {
    bool      valid;          // there is a pending write
    uint8_t   destReg;        // R0..R31 or F0..F31
    uint64_t  newValue;       // value that will be written
    uint8_t   committedBy;    // stage index that performs the write (MEM)
};

BypassEntry  g_intBypass[32];   // integer register file forwarding
BypassEntry  g_fpBypass[32];    // float register file forwarding
```

GR consults `g_intBypass[ra]` and `g_intBypass[rb]` after the
architectural integer-register read; FP-format instructions consult
`g_fpBypass[fa]` / `g_fpBypass[fb]` instead.  If an entry is valid and
committed-by-MEM in a stage downstream of GR (i.e., the slot in MEM
right now), GR uses `newValue` instead of the architectural value.
This handles the V3 class of bug where HW_MTPR's slot grabbed a stale
R30 because BIS at 0x6005c0 had not yet committed.

The bypass tables are updated by EX (which knows `slot.boxResult`'s
staged commits) and cleared by MEM after `commitPending` (see section
6.6).

**Note on FPCR.**  FPCR is its own IPR (read via HW_MFPR / written via
HW_MTPR with the FPCR IPR index), not the F31 register.  F31 is the
architectural zero-FP register, analogous to R31.  Bypass entries for
F31 are never produced (writes to F31 are dropped at MEM commit);
FPCR-as-IPR has its own staging path through the IPR write side of
PalBox executors and does not flow through the FP register-file
bypass.

---

## 6. The Box layer

### 6.1 What a Box is

A Box is a **collection of executor functions** that share a common
execution unit and resource model:

- **EBox** -- integer ALU executors (BIS, ADDQ, SUBQ, XOR, shifts, ...).
- **FBox** -- floating-point executors (ADDS, MULT, CVTQS, ...).
- **MBox** -- memory executors (LDQ, STL, LDA, LDAH, ...).
- **PalBox** -- PALcode-format and HW_* executors (CALL_PAL, HW_MTPR,
  HW_MFPR, HW_REI, HW_LD, HW_ST).
- **CBox** -- branch and indirect-jump executors (BR, BSR, BNE, BEQ,
  JMP, JSR, RET, JSR_COROUTINE).

Each Box is implemented as a namespace or a class with static methods.
There is no Box base class with virtual methods.  The function-table
glue holds raw function pointers that resolve at link time.

### 6.2 Executor signature

All executors share the same signature:

```
void executeXxx(PipelineSlot& slot) noexcept;
```

The executor reads operands from the slot, computes the result, and
writes effects back into the slot.  It does not return a value.  It does
not allocate.  It does not throw (the `noexcept` is enforced).

### 6.3 What an executor may do

- Read from `slot.di.*`, `slot.ra_value`, `slot.rb_value`,
  `slot.literal_value`.
- Read IPR state via `slot.m_iprGlobalMaster->...`.
- Read guest memory via `slot.m_mBox->load(...)` (loads only).
- Stage a register write through the paired setter
  `slot.boxResult.setIntCommit(rc, val)` or
  `slot.boxResult.setFpCommit(fc, val)`.  MEM applies it.
- Stage a memory write through `slot.boxResult.setMemWrite(va, data, width)`.
- Stage a branch through `slot.boxResult.setBranch(target, writesLink, link)`.
- Stage a PC commit through `slot.boxResult.setCommittedArchitecturalPC(pc)`.
- Stage a fault through `slot.boxResult.setFault(code, pc, va)`.
- Request payloadless cross-cutting effects via the corresponding
  flag setter (`requestFlushPipeline()`, `requestMemoryBarrier()`, etc.).
- Call `slot.m_pc->redirectPC(target)` for resolved branches and jumps
  to update IF immediately (the architectural commit still goes through
  `setCommittedArchitecturalPC` so MEM applies it consistently).

### 6.4 What an executor may NOT do

- Write directly to the architectural integer or float register file.
  That is MEM's job, via `commitPending(slot)`.
- Mutate `slot.boxResult.flags` directly for a payload-bearing flag --
  use the paired setter (section 6.5.2).
- Squash any other slot.  Squashing is the pipeline-flush logic's job.
- Call other executors directly.  If two instructions share logic, the
  shared logic lives in a free helper function called by both.
- Allocate.  All side-effect emission writes into pre-sized
  `slot.boxResult` fields.
- Throw exceptions.  Faults are signaled via
  `slot.boxResult.setFault(...)`.

### 6.5 BoxResult

`BoxResult` is the structured side-effect record produced by an executor
at EX, consumed by MEM (commit) and WB (trace).  It carries flags for
side-effect requests and the matching payload values for those requests.
The shape is deliberately the same family as V1's BoxResult; this is
the pattern that worked.

The V3 BoxResult failure was a maintenance defect in `merge()` /
`operator|`, not a flaw in the BoxResult concept itself.  V4 keeps the
pattern and adds three structural guards (sections 6.5.2 - 6.5.4) that
make the V3 bug class structurally impossible to reintroduce.

#### 6.5.1 Shape

```cpp
struct BoxResult {
    // ---- Flag bitmap ----------------------------------------------------
    // Marks which side effects this BoxResult requests.  Payloadless
    // cross-cutting flags (flush, barrier, halt) live here exclusively;
    // payload-bearing requests pair with named struct members below
    // and the producer-side setters maintain the (flag, payload) pairing.
    uint32_t  flags{ 0 };

    // ---- Architectural commit ------------------------------------------
    // Paired with BOX_PC_COMMITTED.
    uint64_t  architecturalPC{ 0 };
    uint64_t  architecturalPS{ 0 };

    // ---- Branch / jump resolution --------------------------------------
    // Paired with BOX_BRANCH_TAKEN (or by branchWritesLink for BSR/JSR).
    bool      branchTaken{ false };
    uint64_t  branchTarget{ 0 };
    bool      branchWritesLink{ false };
    uint64_t  linkValue{ 0 };

    // ---- Pending register commit (consumed by MEM commitPending) -------
    // Paired with BOX_INT_WRITE / BOX_FP_WRITE.
    bool      hasIntWrite{ false };
    uint8_t   intReg{ 0 };
    uint64_t  intValue{ 0 };
    bool      hasFpWrite{ false };
    uint8_t   fpReg{ 0 };
    uint64_t  fpValue{ 0 };

    // ---- Memory store --------------------------------------------------
    // Paired with BOX_MEM_WRITE.
    bool      hasMemWrite{ false };
    uint64_t  memVA{ 0 };
    uint64_t  memData{ 0 };
    uint8_t   memWidth{ 0 };

    // ---- Fault / trap --------------------------------------------------
    // Paired with BOX_FAULT_DISPATCHED.
    TrapCode  trapCode{ TrapCode::NONE };
    uint64_t  trapVA{ 0 };
    uint64_t  trapPC{ 0 };

    // ---- Producer-side setters: paired flag + payload ------------------
    // EVERY payload-bearing flag has a setter.  Direct `flags |= BOX_X`
    // for a payload-bearing flag is forbidden (see 6.5.2).

    BoxResult& setCommittedArchitecturalPC(uint64_t pc) noexcept {
        architecturalPC = pc;
        flags |= BOX_PC_COMMITTED;
        return *this;
    }

    BoxResult& setIntCommit(uint8_t reg, uint64_t val) noexcept {
        intReg     = reg;
        intValue   = val;
        hasIntWrite = true;
        flags |= BOX_INT_WRITE;
        return *this;
    }

    BoxResult& setFpCommit(uint8_t reg, uint64_t val) noexcept {
        fpReg     = reg;
        fpValue   = val;
        hasFpWrite = true;
        flags |= BOX_FP_WRITE;
        return *this;
    }

    BoxResult& setBranch(uint64_t target, bool writesLink, uint64_t link)
        noexcept {
        branchTaken      = true;
        branchTarget     = target;
        branchWritesLink = writesLink;
        linkValue        = link;
        flags |= BOX_BRANCH_TAKEN;
        return *this;
    }

    BoxResult& setMemWrite(uint64_t va, uint64_t data, uint8_t width)
        noexcept {
        memVA       = va;
        memData     = data;
        memWidth    = width;
        hasMemWrite = true;
        flags |= BOX_MEM_WRITE;
        return *this;
    }

    BoxResult& setFault(TrapCode tc, uint64_t pc, uint64_t va = 0) noexcept {
        trapCode = tc;
        trapPC   = pc;
        trapVA   = va;
        flags |= BOX_FAULT_DISPATCHED;
        return *this;
    }

    // ---- Payloadless flag setters (cross-cutting; no payload to pair) --
    BoxResult& requestFlushPipeline() noexcept {
        flags |= BOX_FLUSH_PIPELINE; return *this;
    }
    BoxResult& requestMemoryBarrier() noexcept {
        flags |= BOX_REQUEST_MEMORY_BARRIER; return *this;
    }
    // ... etc for halt, retry, IC-flush, drain, etc. ...

    // ---- Merge: tested for paired propagation (see 6.5.3) --------------
    void merge(const BoxResult& other) noexcept;
};
```

#### 6.5.2 Producer-side rule: paired setters only

Direct mutation of `BoxResult.flags` outside of a paired setter is
prohibited for **payload-bearing** flags.  Payloadless cross-cutting
flags (`BOX_FLUSH_PIPELINE`, `BOX_REQUEST_MEMORY_BARRIER`,
`BOX_HALT_EXECUTION`, ...) may be set via `flags |= BOX_X` directly or
via a payloadless setter (`requestFlushPipeline()`).

The intent: there is no path in the source that sets a payload-bearing
flag without setting its paired payload, and vice versa.  The setter
API is the only legal producer for those flags.

Optional enforcement: make `flags` private and expose only the setters
plus a `flags() const` reader.  Friends in the file scope (or a
permission table) can OR in payloadless flags directly.  Decision
deferred to implementation; documentation alone is the minimum.

#### 6.5.3 Consumer-side rule: merge() is tested for paired propagation

The V3 BoxResult bug was that `merge()` propagated the `flags` bitmap
(via `flags |= other.flags`) but did not copy the matching payload
fields.  V4 prevents this regression by structural test:

- A `(flag, payload-field)` **propagation registry** table enumerates
  every (flag, payload-field-set) pair in `BoxResult`.  This table is
  the single source of truth for what `merge()` must propagate.
- `merge()` iterates the registry rather than duplicating the
  propagation logic, so the test and the implementation share the same
  table.
- A unit test (in `tests/coreLib/test_boxresult_merge.cpp`) iterates
  the registry, builds two BoxResults each carrying a different payload
  value with the corresponding flag set, merges them, and asserts the
  merged result has the correct payload paired with the flag.
- Adding a new payload-bearing field to `BoxResult` requires adding it
  to the propagation registry; forgetting to update the registry causes
  the test to detect that the new pair is unmerged.
- Sketch:

```cpp
struct MergeRule {
    uint32_t flag;
    void   (*propagate)(BoxResult& dst, const BoxResult& src);
};

constexpr MergeRule g_mergeRules[] = {
    { BOX_PC_COMMITTED,    [](BoxResult& d, const BoxResult& s) {
        d.architecturalPC = s.architecturalPC;
    }},
    { BOX_INT_WRITE,       [](BoxResult& d, const BoxResult& s) {
        d.intReg = s.intReg; d.intValue = s.intValue;
        d.hasIntWrite = true;
    }},
    { BOX_BRANCH_TAKEN,    [](BoxResult& d, const BoxResult& s) {
        d.branchTarget = s.branchTarget;
        d.branchWritesLink = s.branchWritesLink;
        d.linkValue = s.linkValue;
        d.branchTaken = true;
    }},
    // ... one row per payload-bearing flag ...
};

void BoxResult::merge(const BoxResult& other) noexcept {
    for (const auto& rule : g_mergeRules) {
        if (other.flags & rule.flag) rule.propagate(*this, other);
    }
    flags |= other.flags;     // payloadless flags propagate uniformly
}
```

#### 6.5.4 Documentation rule

Every payload-bearing flag definition has a comment block listing its
paired payload field(s).  Example:

```cpp
// BOX_PC_COMMITTED -- architecturalPC field is valid.
// Paired payload: BoxResult::architecturalPC.
// Setter: setCommittedArchitecturalPC(pc).
constexpr uint32_t BOX_PC_COMMITTED = 1u << 0;
```

The `merge()` comment lists every (flag, payload) pair it propagates,
which is also the propagation registry's source.  When a new pair is
added, the flag definition's comment, the registry, and the merge()
comment all update in the same diff; forgetting any one of the three
is a code-review-checklist item.

#### 6.5.5 Why this works

The V3 bug was a silent runtime fault: `merge()` propagated `flags |= other.flags`
but didn't copy `architecturalPC`, and the consumer at `AlphaCPU.h:622`
used the inconsistent result to commit `setPC(0)`.  V4 makes the same
defect either a compile error (`flags` private + no setter for the new
flag), a link error (registry missing the propagate function), or a
test failure (registry test catches missing propagation).  The defect
becomes loud rather than silent.

### 6.6 commitPending at MEM

```cpp
void commitPending(PipelineSlot& slot) {
    BoxResult& br = slot.boxResult;

    if ((br.flags & BOX_INT_WRITE) && br.intReg != 31) {
        globalCPUState().intRegs(slot.cpuId).r[br.intReg] = br.intValue;
        // Clear bypass entry for this register: write is now committed.
        g_intBypass[br.intReg] = BypassEntry{};
    }
    if ((br.flags & BOX_FP_WRITE) && br.fpReg != 31) {
        globalCPUState().floatRegs(slot.cpuId).f[br.fpReg] = br.fpValue;
        g_fpBypass[br.fpReg] = BypassEntry{};
    }
    if (br.flags & BOX_MEM_WRITE) {
        slot.m_mBox->commitStore(br.memVA, br.memData, br.memWidth);
    }
    if (br.flags & BOX_PC_COMMITTED) {
        slot.m_iprGlobalMaster->m_pc.setPC(br.architecturalPC);
    }
}
```

The R31 and F31 zero-write skip is performed here, in one place.  No
executor is responsible for skipping its own R31 write; the executor
unconditionally calls `setIntCommit(rc, value)`, and the commit stage
applies the architectural rule.  This is the inverse of V3's pattern
where each executor checked `if (rc != 31)` itself, which was the
source of the BIS-with-stomped-rc-field bug.

---

## 7. Codegen pipeline

### 7.1 Inputs

- `grainFactoryLib/GrainMaster.tsv` -- canonical (opcode, function,
  mnemonic, description, type, box) table.  Personality is encoded in
  the mnemonic itself via the `_64` suffix convention (section 4.4):
  no separate personality-map file is required.  The codegen parses
  the mnemonic to determine which personality slot in
  `PersonalityResolver` the row populates.
- `grainFactoryLib/SemanticFlags.tsv` -- per-(opcode, function) semantic
  flag bits (S_Branch, S_Load, S_Store, S_Privileged, ...).  Hand-curated
  but stored in a TSV alongside `GrainMaster.tsv` so the generator
  consumes one merged view.

### 7.2 Generated outputs

- `grainFactoryLib/generated/OpcodeTable.cpp` -- the top-level
  `g_opcodeTable[64]` definition with per-opcode descriptors.
- `grainFactoryLib/generated/SubTables.cpp` -- the per-opcode sub-tables
  (`g_opTable_Operate_0x10[128]`, `g_opTable_Pal_0x00[256]`, etc.).
- `grainFactoryLib/generated/ExecutorDecls.h` -- forward declarations
  of every executor function name referenced.  Inclusion of this header
  in each Box library forces missing executors to be link errors.
- `grainFactoryLib/generated/SemanticFlags.cpp` -- the per-grain semantic
  flag bits, also indexed by (opcode, function).
- `docs/generated/InstructionMatrix.md` -- a human-readable Markdown
  table of every instruction, mnemonic, opcode, function, dispatch box,
  and personality variant.  Useful for documentation and review.
- `tests/generated/dispatch_test.cpp` -- one test per row that calls
  `extractFunctionCode` with synthesized rawBits and asserts the table
  yields the expected mnemonic and Box.

### 7.3 Generator

The generator is a small standalone tool, written in modern C++ (no Qt,
no other dependencies), that reads the TSVs and writes the output files.
It is invoked as a CMake custom command before the main build.  Its
output is checked into the source tree (not built fresh on each run)
because the regenerated files are deterministic given the TSV inputs and
review of generated diffs is part of the workflow.

### 7.4 Hand-written portion

The executor function bodies themselves -- e.g., `executeBIS`,
`executeADDQ`, `executeJSR`, `executeHW_MTPR` -- are hand-written in the
appropriate Box library.  The generator produces only the wiring.

### 7.5 Performance optimization policy

The function-table lookup is two indirections (top-level descriptor +
sub-table index) per dispatched instruction.  In tight boot loops
(e.g., the SRM decompression copy loop) the same handful of executors
fire millions of times in succession, raising the question of whether
to PC-cache the resolved `execute_fn` so the lookup doesn't repeat.

**Decided policy (2026-05-05): defer optimization until profiling
proves a hotspot.**  Build the straightforward two-indirection
dispatch first.  Add a fast path only if a profiled measurement
identifies the table lookup as a meaningful fraction of execution
time.  Premature optimization here would complicate the design without
measurable benefit.

---

## 8. Anti-patterns this design forbids

This section is a catalog of EmulatR bugs the design specifically
prevents.  Each one is encoded as a structural constraint rather than a
runtime check, so the bug becomes either a compile error or
architecturally impossible to express.

### 8.1 No vptr-corruption silent dispatch

Grains have no vtable.  A wild write to a grain object's first 8 bytes
either crashes immediately or produces garbage that the next call
through `execute_fn` will fault on.  The "wrong dispatch with right data"
failure mode (BIS appearing to call executeHW_MTPR while the data
members say BIS) cannot occur.

### 8.2 No grain dispatching to a different Box's executor

The `execute_fn` for each grain is set from a generated table whose
input is `GrainMaster.tsv`.  The table cannot be hand-edited to bind a
BIS row to the HW_MTPR executor without that mismatch being visible in
the TSV diff and in the generated `ExecutorDecls.h`.  Reviewers see one
line, not 600 grain headers.

### 8.3 No per-executor R31-skip drift

The R31 / F31 zero-write skip is performed exactly once, at MEM in
`commitPending`.  Executors do not check `rc != 31` themselves.  An
executor that mistakenly reads `slot.di.rc` from a stomped field will
still go through `commitPending`, which performs the skip uniformly.

### 8.4 No payload-flag decoupling in BoxResult

The V3 BoxResult bug -- `merge()` propagating `flags |= other.flags`
without copying the matching payload field, so the consumer sees a
flag set with stale or zero payload -- is structurally precluded in
V4 by three mechanisms (full description in section 6.5):

1. **Producer side.**  Every payload-bearing flag has a paired setter
   (`setCommittedArchitecturalPC`, `setIntCommit`, `setBranch`, ...).
   Direct `flags |= BOX_X` for a payload-bearing flag is forbidden.
   The setter is the only legal producer.

2. **Consumer side.**  `merge()` propagates payloads via a
   `(flag, propagate-fn)` registry table that enumerates every
   payload-bearing flag in `BoxResult`.  A unit test iterates the
   registry, builds two `BoxResult`s with different payload values,
   merges them, and asserts the merged result has the right payload
   paired with the flag.  Adding a new flag without a registry entry
   is detected by the test.

3. **Documentation.**  Every flag definition lists its paired payload
   in a comment block; the merge() comment lists every (flag, payload)
   pair it propagates.  Code review compares the two comments.

The defect that consumed two of EmulatR's late-stage debugging
sessions becomes either a compile error, a link error, or a test
failure -- not a silent runtime bug.

### 8.5 No premature loader exit before pipeline drain

The boot loader contract (specified in the boot path section, not here)
requires the loader to drain the pipeline -- step until all
in-flight slots are retired -- before disarming the I-stream override
or returning control to the main run loop.  No loader path exits with
slots in flight.

### 8.6 No write-aliasing between physical address regions

Memory is single-sourced.  The 0x900000 ROM region, 0x600000 PAL region,
and 0x000000 DRAM region are distinct unless the MMU explicitly maps
them.  No code path performs cross-region shadow writes (the EmulatR
hypothesis we explicitly disproved).

### 8.7 No architectural register write outside MEM

The single-write-point-at-MEM contract (section 1.2) is enforced by
making `commitPending` the only function that calls into the architectural
register file's mutating accessors.  Executors only stage; MEM commits.
A code review process flag any other call site of `intRegs(...).r[i] =`.

### 8.8 No mnemonic-based dispatch or trace gating

All dispatch is by (opcode, function-code, personality), never by
mnemonic string.  The mnemonic is derived from the dispatch result and
used only for trace output.  This makes the JMP/JSR mnemonic-mislabel
problem from EmulatR's trace formatter inert: a misnamed mnemonic in
the trace cannot affect dispatch or any control flow.

---

## 9. Decisions (formerly open questions)

All seven open questions from the initial draft have been decided as
of 2026-05-05.  Each is recorded here with the resolution and a brief
note on where it landed in the spec.

### 9.1 Float register file shape -- DECIDED

**Resolution:** separate float bypass network, parallel to integer.
The float register file (F0..F31, F31 hardwired zero) gets its own
`g_fpBypass[32]` table.  FPCR is its own IPR (read/written via
HW_MFPR / HW_MTPR with the FPCR IPR index), not the F31 register;
F31 is the architectural FP zero, analogous to R31.

**Spec impact:** section 5.5 describes the two parallel bypass
networks and clarifies the FPCR-as-IPR vs F31-as-zero-register
distinction.

### 9.2 Operand fetch timing for stores -- DECIDED

**Resolution:** latch store operands at GR; do NOT re-read at MEM.
EV6-style cores resolve store data early and treat it as
architecturally committed at issue time.  Re-reading at MEM would
model an out-of-order forwarding behavior the architecture does not
expose.

**Spec impact:** note added to section 5.3.

### 9.3 Boot-time dispatch fast path -- DEFERRED

**Resolution:** defer optimization until profiling proves a hotspot.
Build the straightforward two-indirection dispatch (top-level
descriptor + sub-table) first.  Add a PC-keyed fast path only if a
profiled measurement identifies the table lookup as a meaningful
fraction of execution time.

**Spec impact:** policy added as section 7.5.

### 9.4 Test harness for individual executors -- DECIDED

**Resolution:** harness scope is the **(Grain + Box + BoxResult)**
triplet -- the unit of dispatched execution, not just an isolated
function.  The fixture provides a synthesized grain (rawBits +
decoded fields), invokes through the function-table dispatch (so the
test exercises wiring as well as body), and asserts on the resulting
`BoxResult`.  Plus a dedicated `BoxResult::merge()` propagation test
keyed on the registry described in section 6.5.3.

**Spec impact:** see `tests/README.md` for harness shape.  Test
framework choice deferred to the build-and-tooling discussion.

### 9.5 Integration with existing EmulatR source -- DECIDED

**Resolution:**

Reuse (preserve, with tightening where noted):
- SRM ROM decompression logic.
- Trace formatter (cpu_trace.log shape, INS/REG/EVT/PIP lines, lookback
  ring, PAL window).
- Guest memory subsystem (tighten semantics per section 2 of proposal:
  single-source GuestMemory, no shadow writes, IBox view distinct).

Discard (replace, do not port):
- Per-grain C++ class hierarchy with vtables -- replace with POD
  grain + function-pointer dispatch (section 3).
- Auto-registration of grain singletons via `GrainAutoRegistrar<T>` --
  replace with codegen-emitted static dispatch tables built from
  `GrainMaster.tsv` (section 4 / 7).  The argument for static tables
  over auto-registration is link-time safety and diff reviewability,
  not performance; expected perf parity.

Keep the pattern, fix the merge defect:
- BoxResult.  V1's BoxResult worked.  V3's bug was in `merge()` /
  `operator|` dropping payload while propagating flags.  V4 keeps
  BoxResult and adds three structural guards (paired setters, tested
  merge propagation, paired documentation) -- see section 6.5.

**Spec impact:** new section 10 codifies this policy as the
integration contract between V3 source and V4 development.

### 9.6 Snapshot save / restore -- DECIDED

**Resolution:** **design now, implement later.**  v1 ships without
snapshot save/restore.  The APIs and data shapes are designed during
v1 so that adding the implementation post-v1 does not require
restructuring the pipeline, the register file, or the trace formatter.

**Spec impact:** new section 11 specifies the snapshot save/restore
design.  No code lands in v1 to implement it.

### 9.7 Concurrency model -- DECIDED (refined 2026-05-05)

**Resolution:** **single-threaded** simulation for v1.  One OS
thread drives the simulator.  The project architectural target is
**4-CPU ES45**, but v1 boots only CPU 0 through SRM-to-PAL handoff;
CPUs 1-3 are present in the simulator but quiescent until brought
online by post-v1 work.

The four simulated CPUs (when active) are scheduled round-robin on
the single OS thread; per-CPU pipeline parallelism on multiple OS
threads is a v2+ extension.  Memory subsystem and TLB machinery
are designed to support multi-CPU semantics from day one (per
SPEC_memory_realms_and_scaffold.md section 4) so multi-CPU
bring-up does not require subsystem redesign.

**Spec impact:** noted at section 0 (Purpose and scope) and
SPEC_memory_realms_and_scaffold.md sections 4 and 5.2.
---

## 10. Integration with prior EmulatR source

This section codifies the integration policy between V3 source
(`D:\EmulatR\EmulatRAppUni\`, also referred to as EmulatRAppUni) and
V4 development.  It is a checklist for reviewers when source from V3
is brought forward.

### 10.1 Reuse (preserve, with the noted tightenings)

#### 10.1.1 SRM ROM decompression

The decompression / relocation logic from V3's `SrmRomLoader`
component is preserved.  The pipeline-drain semantics in the `done:`
predicate (V3's `singleStep()` calls before `m_decompressActive =
false`) are part of the boot path contract and carry forward as
documented in the boot-path section of SPEC.

Tightening: the contract becomes explicit -- the loader MUST drain
the pipeline before disarming the I-stream override.  Section 8.5
encodes this as a structural anti-pattern V4 forbids.

#### 10.1.2 Trace formatter

V3's `CpuTrace` (INS / REG / EVT / PIP line shapes, lookback ring,
PAL window markers) is preserved largely intact.  The output format
of `cpu_trace.log` carries forward unchanged so existing trace
analysis tooling continues to work against V4 traces.

Tightening: the `_64` mnemonic suffix in trace output reflects the
runtime personality, not the TSV row name -- the formatter looks up
the canonical mnemonic for the dispatched grain rather than echoing
whatever string was in the TSV.

#### 10.1.3 Guest memory subsystem

V3's `GuestMemory` is preserved as the single-source-of-truth physical
memory model.  No shadow banks, no cross-region aliasing.

Tightening:
- IBox fetch view becomes distinct from data view (section 3 of
  proposal).
- HW_LD / HW_ST always operate on GuestMemory, never the ROM backing
  store (section 3.3 of proposal).
- Cross-region writes between 0x000000, 0x600000, and 0x900000 are
  explicitly forbidden unless the MMU maps them (section 6 of
  proposal).

### 10.2 Discard (replace; do not port)

#### 10.2.1 Per-grain C++ class hierarchy

V3's 600+ `*_InstructionGrain.h` headers, each defining a class with
an `execute()` virtual method, are discarded.  Replaced by POD
`InstructionGrain` + function-pointer dispatch (section 3).

Reason: the per-class hierarchy was the source of multiple V3 bugs
(BIS_InstructionGrain delegating to executeHW_MTPR, possible vtable
corruption suspicion, 600-class maintenance overhead).  None of these
are expressible in the V4 design.

#### 10.2.2 Auto-registration of grain singletons

V3's `GrainAutoRegistrar<T>(opcode, function)` static-init pattern is
discarded.  Replaced by codegen-emitted static dispatch tables
(`g_opcodeTable[64]` + per-opcode sub-tables) built from
`GrainMaster.tsv` (section 7).

Reason: link-time safety (missing executors are link errors, not
runtime faults), diff reviewability (one TSV diff vs 600 header
diffs), and const-correctness (tables in `.rodata`, not writable
runtime registries).  The argument is structural, not performance --
both designs reduce to function-pointer indirect calls at the dispatch
site.

### 10.3 Keep the pattern, fix the defect

#### 10.3.1 BoxResult

V1's BoxResult pattern is preserved.  V3's `merge()` / `operator|`
defect (propagating flags without payloads) is fixed structurally by
the three guards in section 6.5.2-6.5.4.

The clarity at the call site -- `slot.boxResult.setCommittedArchitecturalPC(target)`
reads as a coordinated request -- is the property V1 had and V3
preserved-but-broke-in-merge.  V4 keeps the property and makes the
merge defect either a compile error, a link error, or a test
failure.

### 10.4 Process

When a V3 file is considered for carryover:

1. Identify which 10.1 / 10.2 / 10.3 bucket it falls in.
2. If 10.1 (reuse): copy the file, apply any tightening noted, add a
   header comment citing the V3 source path and the V4 spec section
   it satisfies.
3. If 10.2 (discard): do not copy.  Read the V3 source for inspiration
   only; write the V4 replacement against the spec.
4. If 10.3 (keep-and-fix): copy the file, apply the structural guards
   from the spec, add a header comment citing the V3 defect and the
   V4 fix.

Every carryover gets a `// EmulatR V3 carryover, V4-tightened ` or
`// EmulatR V3 reuse, no semantic change ` header so a reader can
trace V4 source back to V3 origins when debugging.

---

## 11. Snapshot save / restore (designed now, implemented post-v1)

This section specifies the snapshot save and restore design.  The
implementation is **deferred to post-v1**; v1 ships without snapshot
support.  This design exists in v1 so that the API surface, data
shapes, and pipeline boundaries do not require restructuring when
the implementation lands.

### 11.1 What is saved

A snapshot captures the **architectural state** sufficient to resume
deterministic execution from a specific cycle boundary.  It does NOT
capture pipeline state (the in-flight slots in IF/DE/GR/EX/MEM/WB);
on restore the pipeline starts empty and re-fetches from the saved
PC.

#### 11.1.1 Per-CPU architectural state

- Integer register file (R0..R31).
- Float register file (F0..F31, FPCR).
- All IPRs that hold architectural state (PAL_BASE, PCBB, PTBR, ASN,
  PS, IPL, EXC_ADDR, ...).
- m_pc value (the next PC to fetch).
- Cycle counter (m_cycleCount).
- Personality bit (OpenVMS / Tru64).
- HWPCB pointer (the IPR_PCBB value).

#### 11.1.2 Memory regions

- A configurable list of physical-memory regions to capture.  Default
  region: `[0x0, 0xD00000)` (covers ES40/ES45 firmware footprint).
- Each region is captured as a contiguous byte range; the snapshot
  format is implementation-defined but should be either raw or
  zlib-compressed.

#### 11.1.3 Snapshot metadata

- Format version.
- ROM-image hash (so a restore against a different ROM is rejected).
- Timestamp.
- Cycle count at save.
- Saved PC.
- CPU model identifier.

### 11.2 What is NOT saved

- In-flight pipeline slots.  On restore, the pipeline is empty.
- Bypass tables (regenerated as the pipeline executes).
- The branch predictor state (regenerates from execution; minor
  fidelity loss accepted as a v1 trade-off).
- The trace formatter's lookback ring (it's pure observation; lost
  on restore is fine).
- MMIO transient state in flight (the memory subsystem is responsible
  for ensuring no MMIO writes are mid-flight at the snapshot
  boundary).

### 11.3 Save / restore boundary

A snapshot is taken at a **cycle boundary** -- between two ticks of
the pipeline, with no slots in flight.  Implementations should drain
the pipeline (run ticks until all stages are empty) before capturing.
This is the same drain pattern the SRM ROM loader's `done:` predicate
uses; a shared utility function should handle both.

### 11.4 API shape (v1 design, post-v1 implementation)

```cpp
class AlphaCPU {
public:
    // Save the current architectural state to the given path.
    // Drains the pipeline first; returns false on I/O error.
    // Implementation lands post-v1; v1 stub returns false.
    bool saveSnapshot(const std::string& path) noexcept;

    // Load a snapshot from the given path, replacing current state.
    // Verifies the ROM hash matches; returns false on mismatch or
    // I/O error.  Implementation lands post-v1; v1 stub returns false.
    bool loadSnapshot(const std::string& path) noexcept;

    // Schedule a one-shot snapshot trigger at the named cycle.
    // Implementation lands post-v1; v1 stub is a no-op.
    void requestSnapshotAtCycle(uint64_t cycle,
                                const std::string& path) noexcept;
};
```

The v1 stubs return `false` / no-op so callers compile; post-v1 fills
in the bodies without changing the signatures.

### 11.5 Lessons from V3's snapshot work

V3's snapshot implementation surfaced two specific bugs we want the
v4 design to preclude:

- **Cycle counter reset on restore.**  V3's restore path did not
  populate `m_cycleCount` from the saved value; the post-restore run
  started counting from 0.  V4 design: cycle counter is a first-class
  member of the saved state and is restored unconditionally.
- **BoxResult.architecturalPC drop during merge.**  V3 had a snapshot
  trigger that fired mid-cycle and called `setCommittedArchitecturalPC`
  on a BoxResult that was then merged with another, dropping the
  payload.  V4 design: snapshot triggers fire only at cycle
  boundaries (after pipeline drain), not mid-cycle, so no merge of
  in-flight BoxResults can drop payload.  And the merge defect
  itself is precluded by section 6.5.

### 11.6 What v1 provides

- The API stubs (so callers compile).
- The saved-state field set documented (so post-v1 implementation
  knows what to capture).
- The snapshot-boundary contract (cycle boundary, drained pipeline).
- The architectural-vs-pipeline state distinction documented.

What v1 does NOT provide:
- The save format.
- The serialization code.
- The integration with a UI or CLI flag.

These land post-v1 against the design above.

---

## 12. Glossary

- **Slot.** A `PipelineSlot`; the per-cycle data carrier.
- **Grain.** An `InstructionGrain`; the POD that holds the decoded
  instruction and its dispatch function pointer.
- **Box.** A namespace or class containing executor functions for a
  specific dispatch unit (EBox, FBox, MBox, PalBox, CBox).
- **BoxResult.** The structured side-effect record produced by an
  executor at EX, consumed by MEM (commit) and WB (trace).  Carries
  flags + paired payload values for each requested side effect.  See
  section 6.5.
- **Paired setter.** A producer-side method on `BoxResult` that sets
  both a flag and its corresponding payload field in one call.  The
  required producer API for payload-bearing flags.
- **Propagation registry.** The `(flag, propagate-fn)` table that
  drives `BoxResult::merge()` and is exercised by the merge unit
  test.  Single source of truth for what merge() copies.
- **commitPending.** The MEM-stage function that applies a
  `BoxResult`'s register and memory writes to architectural state.
- **Bypass.** The forwarding network that supplies a register's
  not-yet-committed value to a downstream stage that needs it.
  Two parallel tables: `g_intBypass` and `g_fpBypass`.
- **Personality.** OS-level dispatch context (OpenVMS or Tru64) that
  selects between aliased entries in `GrainMaster.tsv`.  Encoded in
  the mnemonic itself via the `_64` suffix convention.
- **Function code.** The format-dependent secondary opcode that
  selects between instructions sharing the same primary opcode.
- **DispatchKind.** The enum that names how a function code is
  extracted from `rawBits` for a given opcode (NoFunc, OperateFunc,
  PalFunc, HwIprFunc, JumpFunc, FloatFunc, ...).

---

## 13. Acceptance criteria for this section

This section is considered settled when:

1. The pipeline stage list and stage invariants (section 1) are
   uncontested.
2. The `InstructionGrain` field set and POD constraint (section 3) are
   uncontested.
3. The function-table two-level shape (section 4) is uncontested.
4. The Box layer interface and executor signature (section 6) are
   uncontested.
5. The `BoxResult` shape, paired setters, and merge contract
   (section 6.5) are uncontested.
6. The codegen pipeline inputs and outputs (section 7) are uncontested.
7. The anti-pattern list (section 8) is reviewed and any additions
   from project-specific experience are merged.
8. The decisions list (section 9) reflects the final resolutions.
9. The integration policy (section 10) is reviewed against intended
   V3 carryovers.
10. The snapshot design (section 11) is reviewed; v1 stubs and
    post-v1 implementation plan agreed.

Once acceptance criteria are met, the spec moves from "Draft" to
"Settled" and downstream sections (decode tables, executor stubs,
test harness) can begin building against it.
