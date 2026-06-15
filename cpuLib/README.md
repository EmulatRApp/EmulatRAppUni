# cpuLib

`AlphaCPU` -- the per-CPU run loop, halt handling, and exception
entry.  Owns one `AlphaPipeline` and ties together the trace,
snapshot (when in scope), and fault-dispatch subsystems.

## Contents

- `AlphaCPU`
- `runOneInstruction` / `runUntilHalt` / `runForCycles`
- `haltCPU` -- halt-state transition
- Exception entry / vector dispatch (deferred to PAL where appropriate)
- Snapshot save / load entry points (when snapshot is in scope; see
  SPEC section 9.6 -- currently a deferred decision)

## Dependencies

- pipelineLib
- iprLib
- memoryLib
- traceLib

## Consumed by

- main.cpp (entry point creates the AlphaCPU instance and runs it)
