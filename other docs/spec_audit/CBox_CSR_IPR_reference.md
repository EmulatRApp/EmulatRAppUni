# CBox CSR / IPR Serial Shift-Chain Reference (EV6 / 21264)

Authoritative bit values provided by Tim (Project Architect) 2026-05-20 from
the 21264/EV67 HRM (Cbox section, "Digital Confidential" CSR/IPR tables).
This is the spec of record for modeling the CBox serial-shift CSR write path
and the IPR error-readback path in V4.

Status: REFERENCE ONLY -- not yet wired into V4's CBox model.  Tagged for the
CBox-CSR-modeling task.  V4 already declares the transport IPRs in
coreLib/HW_IPR.h: HW_C_DATA = 0x012B (CBOX_DATA), HW_C_SHFT = 0x012C
(CBOX_SHIFT).

---

## Transport mechanism

The CBox CSRs are **write-only**; the CBox IPRs are **read-only**.  Both are
accessed through a 6-bit serial shift register, NOT by direct addressing.

### CBOX_DATA  (HW_C_DATA, IPR 0x012B)
- `C_DATA<5:0>` -- the 6-bit serial shift register.  All other bits IGN.
- WRITE: 6 bits of CSR data are written via HW_MTPR; on retire the 6 bits are
  shifted into the CBox.  Repeat until all CSR data is shifted in.
- READ (after a C_SHIFT): C_DATA<5:0> contains the next 6 sequential bits of
  CBox IPR error data, read via HW_MFPR.  Repeat until all IPR data is out.

### CBOX_SHIFT  (HW_C_SHFT, IPR 0x012C)
- `C_SHIFT<0>` -- write '1' to shift 6 bits of CBox IPR data into CBOX_DATA
  for software to read.  All other bits IGN.  All bits of the IPR scan chain
  must be shifted.

Shift ordering note: the IPR read is MSB-first/high-field-first.  Per the HRM,
the first read-group is ERR_ADDR<43:38>, and ERR_ADDR<43> appears in
CBOX_DATA<5>.  The CSR write chain is filled in the field order listed below;
the total chain length is padded to a multiple of 6 (see MBZ / RAZ pads).

---

## CBox CSRs (write-only; shifted IN via CBOX_DATA, 6 bits at a time)

| Field                  | Width   | Encoding / meaning |
|------------------------|---------|--------------------|
| FRAME_SEL              | <2:0>   | framing-clock : bit-time ratio (samples per framing clock). 0001=1, 0010=2, 0100=4, 1000=8 |
| VICTIM_THRESH          | <7:0>   | dcache read-victim threshold before bcache write; one-hot set bit. 00000001=1 ... 10000000=8 |
| BC_RDVICTIM            | <0>     | when set, abut victim writes with reads (victim+read data on same DRAM page) |
| SYSCLK_RATIO           | <15:0>  | CPU:SYSCLK ratio; final mult = 1 + 0.5*N, one-hot. 0001=1.5, 0010=2.0, 0100=2.5, 1000=3, 10000=3.5, 100000=4.0 |
| DUP_TAG_ENA            | <0>     | external system has duplicate tag store |
| SET_DIRTY_ENA          | <2:0>   | set-dirty command protocol. 000=none, 001=clean-to-dirty, 010=shared/clean, 011=clean, 100=shared/dirty, 101=shared/dirty+clean, 110=all shared, 111=all off chip |
| ZEROBLK_ENA            | <1:0>   | <1>=enable zeroblk cmds to system (MP / dup-tag systems); <0>=process zeroblk as zero-block (else convert to read-modified) |
| SPEC_READ_ENA          | <0>     | enable speculative reads (issued before bcache-hit known) |
| SYSBUS_FORMAT          | <0>     | PA format on system bus. 0=interleaved on bcache block boundaries, 1=page mode-hit |
| SYSBUS_MB_ENA          | <0>     | send memory-barrier (MB) commands to system |
| SYSBUS_ACK_LIMIT       | <4:0>   | max outstanding commands system accepts. 00000=INF, 00001=1, 00010=2, ... 10110=22 |
| STIO_32_LIMIT          | <0>     | system imposes 32-byte limit for stores to I/O space |
| BC_ENA                 | <0>     | bcache enabled |
| BC_CLEAN_VICTIM        | <0>     | notify system (CleanVictimBlk + addr) when clean block evicted |
| BC_SIZE                | <3:0>   | bcache size. 0000=1MB, 0001=2MB, 0011=4MB, 0111=8MB, 1111=16MB |
| BC_RD_RD_BBL           | <1:0>   | bubble cycles between reads to different SRAM banks. 00=0 ... 11=3 (0 if single bank) |
| BC_RD_CLK_RATIO        | <15:0>  | bcache:CPU clock ratio; final mult = 1 + 0.5*N, one-hot (same encoding as SYSCLK_RATIO) |
| BC_RD_WR_BBL           | <5:0>   | bcache clock cycles between a bcache read and write. 00000=0 ... 01111=15 |
| BC_LATE_WR_BC          | <2:0>   | Late-Write SRAM: bcache clock cycles to delay write data. 000=0 ... 111=7 |
| BC_LATE_WR_CPU         | <1:0>   | CPU clock cycles to delay write data. 00=0 ... 11=3 |
| BC_LATE_WR_PHASE       | <0>     | delay write data by one CPU clock phase |
| BC_BURST_MODE_ENA      | <0>     | enable bcache burst mode |
| BC_RDCLK_VECTOR        | <15:0>  | bcache read-clock vector, 1 bit per phase, 50% duty (e.g. 111000 for 1.5) |
| MBZ                    | <4:0>   | write zeros to extend the shift chain to a multiple of six |

Late-Write delay total = BC_LATE_WR_BC bcache cycles + BC_LATE_WR_CPU CPU
cycles + (BC_LATE_WR_PHASE ? one CPU clock phase : 0).

---

## CBox IPRs (read-only; shifted OUT via CBOX_SHIFT then read from CBOX_DATA)

Read 6 bits at a time.  First read-group = ERR_ADDR<43:38>; ERR_ADDR<43> is
in CBOX_DATA<5> (MSB-first).

| Field          | Width    | Meaning |
|----------------|----------|---------|
| ERR_ADDR       | <43:6>   | address of last reported ECC or parity error |
| ERR_CODE       | <2:0>    | where error detected. 000=no error, 001=Bcache tag parity, 010=Triplicate tag parity, 011=Memory data ECC, 100=Bcache data ECC, 101=Dcache data ECC |
| ECC_SYNDROME   | <7:0>    | syndrome of last reported ECC error |
| RAZ            | <4:0>    | padded zeros to extend the shift chain to a multiple of 6 |

---

## V4 modeling notes (open, for the CBox-CSR-modeling task)

- The firmware writes CSR config by repeated HW_MTPR CBOX_DATA (6 bits/step)
  and reads error info by HW_MTPR CBOX_SHIFT then HW_MFPR CBOX_DATA.  V4 must
  model the shift register state machine, not direct CSR addressing.
- Exact total chain widths (CSR write chain vs IPR read chain) and the precise
  field ordering within each chain need a final pass against the HRM figure
  before implementation -- the field LIST and per-field encodings above are
  authoritative; the absolute bit offsets within the concatenated chain are to
  be confirmed when modeling begins.
- Relevant to the historical CBox polling-hang investigation (memory:
  project_pal_takeover_polling_hang / project_sys_cbox_outer_hang): the SRM
  reads ERR_CODE/ERR_ADDR via this chain; returning a sane all-zero (no-error)
  IPR scan is the likely correct default until error injection is modeled.
