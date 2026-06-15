# traceLib

`CpuTrace` and supporting trace-formatter infrastructure.  Emits
INS, REG, EVT, PIP lines into `cpu_trace.log` and the secondary
`.lst` file.  Per-instruction observability for debugging and
golden-trace comparison.

## Contents

- `CpuTrace::initialize`, `::shutdown`
- `CpuTrace::instruction` -- INS lines, hot path, gated by
  TRACE_INSTR
- `CpuTrace::pipeline` -- PIP lines, gated by TRACE_PIPELINE; used
  for stage-level diagnostics
- `CpuTrace::event` -- EVT lines, gated by TRACE_EVENT
- `CpuTrace::recordLookback` -- always-on lookback ring (no I/O)
- `CpuTrace::onPalEntry` / `::onPalExit` -- PAL window markers
- `CpuTrace::logFullRegisters` -- REG dump per cycle, gated by
  TRACE_REGFILE / TRACE_FPRFILE

## Trace mask flags

- `TRACE_INSTR` -- emit INS lines per retired instruction
- `TRACE_PIPELINE` -- emit PIP diagnostic lines (stage transitions,
  flushes, redirects)
- `TRACE_REGFILE` -- emit per-cycle REG dump (integer)
- `TRACE_FPRFILE` -- emit per-cycle FRG dump (float)
- `TRACE_EVENT` -- emit EVT marker lines

Mask is set at initialization via `CpuTrace::setTraceMask` and can
be queried via `CpuTrace::getTraceMask`.

## Trace file rotation

Output goes to a timestamped file in the `Trace Output/` directory.
Naming: `cpu_trace_YYYYMMDD_HHMMSS.log`.  Never overwrites; each
run produces a new file.  See SPEC section on trace output policy.

## Dependencies

- coreLib
- iprLib (register file dump)

## Consumed by

- pipelineLib (per-cycle observability)
- cpuLib (top-level events, halt notification)
