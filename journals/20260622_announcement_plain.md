# EmulatR Milestone: A Second Classic Alpha Computer Comes to Life

EmulatR is a project to bring classic DEC Alpha computers back to life -- in
software. The Alpha was a powerful 64-bit machine from the 1990s and 2000s that
ran operating systems such as Tru64 UNIX and OpenVMS. The original hardware is
now rare and aging. EmulatR recreates these machines faithfully enough that the
original startup software runs exactly as it did on the real thing -- no
modifications and no shortcuts.

Today we reached a significant milestone: a second Alpha model now starts up
completely.

Earlier this year, the single-processor "DS10" model booted all the way to its
console -- the interactive screen where an operator types commands, just like
sitting in front of the real computer. Today the larger, dual-processor "DS20"
server did the same. It powers on, finds its disks and devices, runs its
built-in self-checks and firmware-update utility, and arrives at a live command
prompt. In other words, the full startup sequence now runs from cold power-on to
a working console, behaving like the genuine hardware.

Getting there meant solving a series of small but stubborn puzzles in how the
machine talks to its own internal parts -- the kind of details that have to be
exactly right, or the system simply will not start. Each fix carried the startup
a little further, until the whole sequence finally ran end to end.

## What's next

* A few more Alpha models are already in the pipeline.
* The larger goal is to boot the operating systems themselves -- Tru64 UNIX and
  OpenVMS -- not just the firmware console.
* Downloadable kits so others can run EmulatR on their own computers. A Windows
  version is on the way, with Linux and Mac to follow.

## Why it matters

Faithful emulation keeps important computing history usable -- for preservation,
for education, and for anyone who still depends on software written for these
systems. A physical machine eventually wears out; a faithful emulator can keep
running indefinitely.

More updates to come as we cross the finish line.
