# pipelineLib

The `AlphaPipeline` class and stage logic.  Owns the slot ring buffer,
performs ring rotation, drives the six pipeline stages on each tick,
and manages the bypass network and selective flush.

## Contents

- `AlphaPipeline` -- per-CPU pipeline.  Owns `m_slots[6]`, `m_head`,
  `m_cycleCount`, `m_pendingFetch`.
- Stage functions: `stage_IF`, `stage_DE`, `stage_GR`, `stage_EX`,
  `stage_MEM`, `stage_WB`.
- `tick()` -- one cycle of the pipeline.
- `advanceRing()` -- ring rotation.
- Bypass network update logic.
- Selective flush: clears IF/DE/GR slots only, never reaches MEM or WB.

## Dependencies

- coreLib (PipelineSlot, InstructionGrain, EffectSet, BypassEntry)
- iprLib (PC tracker, register file, IprMaster)
- memoryLib (GuestMemory, IBoxView for fetch)
- traceLib (CpuTrace for diagnostics)

## Consumed by

- cpuLib (AlphaCPU drives ticks)

## Stage invariants (see SPEC_execution_model section 1.2)

1. Architectural register writes happen ONLY at MEM, via `commitPending`.
2. PC redirects happen ONLY at EX (executor calls `m_pc->redirectPC`).
3. Each stage operates on exactly one slot per tick.
4. No stage invokes another stage's logic directly.
5. Once a slot enters MEM it cannot be squashed.
