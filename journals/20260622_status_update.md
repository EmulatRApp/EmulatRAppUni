# EmulatR: Project Status Update (2026-06-22)

## Where We Are

I initially targeted the DS10 because it represents our smallest hardware
footprint: a single 21264 processor with limited PCI/I/O, memory, and controller
surfaces. Modeling the absolute minimum required to achieve an end-to-end boot
paid off. The DS10 implementation is now mostly complete; it cold-boots to the
interactive SRM (`>>>`) console, correctly enumerates its target devices, and
executes commands like `set sys_serial_num` successfully.

As a regression test, I verified the DS20 -- the dual-processor sibling to the
DS10. It shares the same basic device set but features an expanded PCI bus
design (two PCI hoses) and four times the memory. (Note: we currently execute a
single CPU; SMP implementation is slated for future work.)

The result was highly successful. The DS20 now cold-boots all the way to the
interactive `P00>>>` prompt. The boot path successfully executes device
enumeration, the GCT/FRU build, the option-firmware scan, and the LFU
firmware-update utility (where `update srm` successfully writes and verifies the
flash image) before performing a reinit to the prompt. Interactive commands
respond -- `show config`, `show dev`, and a bare `show` (full environment dump)
all work. (A few console commands, notably `help` and `memtest`, currently
return "No such command"; these are minor gaps tracked alongside the cosmetic
nits below.)

## Cosmetic Identity Quirks (Deferred)

There are two cosmetic identity discrepancies currently visible. Both are
thoroughly understood and intentionally deferred, recorded here so they are not
mistaken for bugs:

1. **"Running on the ISP model."** This is a deliberate firmware execution mode,
   not an emulation mislabel. The SRM console's `platform()` routine returns
   `ISP_MODEL` if the magic word `0xCAFEBEEF` is present at address `0xBFFC`;
   otherwise it returns `REAL_HW`. "ISP" refers to DEC's internal Instruction
   Set Processor -- the architectural simulator engineering teams used to
   validate firmware before physical silicon existed. In this mode the firmware
   takes simulator-friendly execution paths (simplified NVRAM and device
   access), making it an ideal bring-up path with fewer silicon quirks to model.
   Moving to the `REAL_HW` (silicon) path remains future work.

2. **The banner reads "AlphaPC 264DP 100 MHz" instead of "AlphaServer DS20."**
   This behavior is independent of the ISP mode. The AlphaPC 264DP, AlphaServer
   DS20, and DS20E all share the same underlying dual-21264 Tsunami "264DP"
   reference design. The firmware selects the displayed string in `get_sysvar()`
   by probing the IIC operator-control panel (OCP) and environmental devices to
   disambiguate the chassis. Because we do not yet model those specific IIC
   devices, `get_sysvar()` defaults to the base system-variation member id, and
   the name string is then resolved from the platform DSRDB table at that member
   -- the base reference-board name, "AlphaPC 264DP." The "100 MHz" CPU clock
   field and the garbled SROM-revision string belong to this same cosmetic class.

By contrast, the DS10 identifies itself correctly ("AlphaServer DS10") because it
is a distinct system type that resolves directly without requiring a
chassis-disambiguation probe -- regardless of whether it runs in ISP or silicon
mode. Performance across both platforms at this stage is comparable and stable.

## Next Up: Certification

Reaching the `P00>>>` prompt is a major milestone; *certifying* it means ensuring
the emulator does this deterministically from a cold start, every single time,
with fully understood execution states.

The immediate checklist:

* Commit all in-flight fixes (the model-conditional PCF8584 IIC base table and
  the three 82077 floppy interrupt-edge fixes).
* Run a full DS10 regression pass to guarantee no architectural backsliding.
* Mint a model-tagged `P00>>>` machine snapshot to enable near-instantaneous
  resume times.
* Review and formally accept the outstanding cosmetic nits.

Three additional platforms are queued, ordered by incremental development effort
on top of the DS20 infrastructure:

* **DS20E (Low Effort):** Same Tsunami silicon and device set. Primarily requires
  the chassis-identity layer (the IIC OCP/sysvar discriminator) and the dual-CPU
  descriptor. Mostly a configuration task.
* **ES40 (Moderate Effort):** Also built on Tsunami, but the south bridge changes
  from the Cypress CY82C693 to the ALi M1543C (already modeled in our tree).
  Requires the bridge swap, the ES40 system manifest, and finishing whatever ES40
  exercises in that bridge.
* **ES45 (High Effort):** Built on the Titan (21274) chipset with dual-port
  PA-chips and AGP support. This is genuinely new silicon, not a platform reskin.

Crucially, the foundational IIC, floppy, and GCT/FRU infrastructure developed for
the DS20 transfers directly to all three targets.

## Distribution & Engine Roadmap

A downloadable Windows runtime kit is available. I can produce equivalent binary
kits for Linux and macOS (AArch64), and targeting ARM hosts presents an
interesting architectural opportunity.

Before deploying to native AArch64, I plan to lower the emulation engine into a
clean, retargetable Intermediate Representation (IR) to decouple guest Alpha
decoding from the host backend. AArch64 is an attractive host target because, at
the ISA level, it aligns far closer to Alpha EV6 than x86-64 does. Both are
fixed-length, load/store RISC architectures with relaxed memory models and large
register files (Alpha 31+31; ARM64 31+32 usable).

While binary translation must still occur, the expansion depth should be
significantly shallower than an x86-64 target:

* Alpha's relaxed memory model maps near 1:1 to ARM64 memory barriers, avoiding
  the overhead of fighting x86's strict (TSO) memory ordering.
* Alpha's 31 general-purpose registers map cleanly onto ARM64's 31 GPRs,
  minimizing the severe register spilling seen when targeting the 16 GPRs of
  x86-64.

This architectural alignment implies fewer host instructions per guest
instruction, and thus a meaningful performance advantage.

Two caveats:

1. This shallower instruction expansion is an expectation to be **measured, not
   assumed**. Host clock speed, IPC, and JIT compiler quality dominate real-world
   execution speed as much as the instruction-expansion ratio.
2. The inherently complex aspects of the architecture -- PALcode environment
   semantics, EV6 floating-point rounding/trap modes, byte/word extensions (BWX),
   and precise exception delivery -- remain equally difficult to handle on any
   host architecture.

## Emulation Profiles: Interpreter vs. JIT

The runtime is designed around two distinct execution profiles, which are
different *fidelity tiers*, not merely two speeds:

* **Interpreter (cycle-faithful):** Evaluates guest instructions sequentially and
  models the EV6 pipeline behavior. This is the highly deterministic, stable
  profile we are hardening now. It is our correctness oracle and our determinism
  guarantee.
* **Just-In-Time (JIT) / Binary Translation:** Translates blocks of guest
  instructions into native host machine code on the fly and caches them
  ("compile-once, execute-many"), deployed on hot execution paths. The large
  gains come precisely from executing the *architectural* result without
  reproducing the per-cycle pipeline the interpreter models -- so the JIT is a
  **functional fast-path**, validated against the interpreter at
  architectural-state checkpoints rather than cycle-for-cycle. For compute-bound
  code, binary translation typically yields order-of-magnitude gains (we project
  on the order of 10x or more); this is an expectation to be measured once the
  JIT exists, not a benchmarked result -- EmulatR has no JIT today.

Implementing the JIT infrastructure entails moving to a versioned (v5)
application architecture that folds in the engineering lessons from prior
revisions. The interpreter remains the validation baseline against which the
future JIT track is verified.

Ultimately, the goal is for EmulatR to boot and execute unmodified Tru64 UNIX
(OSF/1) and OpenVMS environments out of the box across Windows and Linux hosts.

The source tree is fully up to date on GitHub. Next week I expect to deliver
ready-to-run, zipped Windows binaries for deployment testing. I will keep you
posted as we cross the certification line.
