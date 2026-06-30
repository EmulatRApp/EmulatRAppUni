<!--
EmulatR V4 -- DS20 "AlphaPC 264DP" badge: deterministic cause chain (apisrm source)
Project: EmulatR (Alpha 21264 / EV67 emulator), V4 active tree
Architect: Timothy Peer.  AI collaboration: Claude / Anthropic.
Date: 2026-06-29.
Source authority: apisrm SRM console source (read-only SSOT) under
  Processor Support/Palcode/palcode/apisrm/apisrm/ref/  -- hwrpb.c, pc264.c, iic_driver.c.
ASCII(128) only.
-->

# DS20 Badge -- Deterministic Cause Chain (it is an incomplete interface, not an exception)

**Date:** 2026-06-29
**Status:** CAUSE SETTLED FROM SOURCE. EmulatR-side confirmation (timing-vs-presence)
pending one EMULATR_IIC_TRACE cold boot. Supersedes the PA-watch + Ghidra-of-the-binary
approach (the readable apisrm source gives the cause the stripped image could not).

---

## 0. TL;DR

The DS20 SRM badges "AlphaPC 264DP" because the firmware deterministically takes the
default branch of a **device open**: `get_sysvar()` does `fopen("iic_ocp0","sr+")`, and when
that returns NULL it selects member 1 = AlphaPC 264DP. The badge is the **correct output of a
complete, deterministic firmware path reading an interface we leave incomplete** -- exactly as
the architect framed it. There is nothing to patch at the SYSVAR write site; the fix is to make
the `iic_ocp0` open succeed at the moment `get_sysvar()` runs, after which the same deterministic
path badges DS20.

---

## 1. The deterministic chain (three links, all in apisrm source)

1. **`hwrpb.c:414`** -- under `#if APC_PLATFORM` (the DS20 / pc264 build):
   ```c
   *(UINT *)hwrpb->SYSVAR = get_sysvar( );
   ```
   SYSVAR is populated BY `get_sysvar()` during HWRPB construction. (The line just above,
   `hwrpb.c:411 *(UINT *)hwrpb->SYSVAR = HWRPB$_SYSVAR;`, sets the family/base; the APC arm
   overwrites it with the computed member.)

2. **`pc264.c:636` `get_sysvar()`** -- the member id is chosen purely by device-open:
   ```c
   int sysvar = HWRPB$_SYSVAR;                 /* family/base (SYSTYPE 34 = DEC_TSUNAMI) */
   if      ( fp = fopen("iic_ocp0",     "sr+") ) sysvar |= 6 << 10;  /* AlphaServer DS20  */
   else if ( fp = fopen("iic_8574_ocp", "sr+") ) sysvar |= 8 << 10;  /* DS20E / Goldrack  */
   else                                          sysvar |= 1 << 10;  /* AlphaPC 264DP     */
   return sysvar;
   ```

3. **`pc264.c:566` `build_dsrdb()`** -- maps the member to the badge:
   ```c
   switch ( hwrpb->SYSVAR[0] >> 10 ) {
     case 1: SysType = 0;               break;  /* "AlphaPC 264DP"   (clean, no print) */
     case 6: SysType = dualCPU()?2:1;   break;  /* "AlphaServer DS20"                  */
     case 8: ... DS20E/XP2000 family ...  break;
     default: qprintf("Error determining system type, SYSVAR = %x\n", ...);
              qprintf("Defaulting system type to AlphaPC 264DP\n");
              SysType = 0;                       /* only path that PRINTS the error    */
   }
   ```
   `SysType` then indexes `dsrdb_n[]` (pc264.c:489): entry 0 = "AlphaPC 264DP %3d MHz",
   entry 1/2 = "AlphaServer DS20 %3d MHz".

**Consequence:** because `get_sysvar()` always returns member 1, 6, or 8, the `default` arm
(the "Error determining system type" print) effectively cannot fire on this platform. A 264DP
badge therefore means member == 1 == the **clean else branch of get_sysvar** == `fopen("iic_ocp0")`
returned NULL.

## 2. What "iic_ocp0" actually is (the interface)

`iic_driver.c` device table (the non-WEBBRICK arm):
```
"iic_ocp0",       IIC_LED_TYPE, 1, 1, 0x40, 0, 0, 0
"iic_ocp1",       IIC_LED_TYPE, 1, 1, 0x42, 0, 0, 0
"iic_8574_ocp",   IIC_LED_TYPE, 1, 1, 0x4E, 0, 0, 0
```
So `fopen("iic_ocp0","sr+")` resolves the NAME `iic_ocp0` to **IIC node 0x40**, type
`IIC_LED_TYPE`, 1 byte. The open runs the guest `iic_driver` against EmulatR's emulated
PCF8584 controller. It SUCCEEDS iff, at the instant `get_sysvar()` runs (during the
`hwrpb.c` HWRPB build), the controller is initialized and node 0x40 answers the open-probe.

**Necessary-but-not-sufficient note:** the previously-recorded "OCP at 0x40 ACKs" came from
`sable_ocp_init` (the LCD "Console Started" bit-bang), which runs at a DIFFERENT (later) point
than the HWRPB build. A raw bus ACK there does not prove `get_sysvar`'s open succeeds at
HWRPB-build time. The contract is specifically: **node 0x40 open-able when get_sysvar runs.**

## 3. The interface to make solid (EmulatR side)

Make `fopen("iic_ocp0")` succeed at HWRPB-build time. Three conditions must hold AT THAT MOMENT:
1. **Controller ready** -- EmulatR's PCF8584 (`deviceLib/Tsunami/IicPcf8584.h`) is initialized
   and serviceable when the guest issues the open (init-order vs the HWRPB build).
2. **Device present** -- the platform manifest (`ds20_v7_3_platform.json`) registers an OCP
   device at IIC node 0x40 that responds.
3. **Open-probe ACKs** -- the guest `iic_driver` open sequence for an `IIC_LED_TYPE` node at
   0x40 completes (address ACK; any status/read the open performs is satisfied).

If all three hold, `get_sysvar()` returns member 6 and the deterministic firmware badges
"AlphaServer DS20" with NO change anywhere else. This is a family-wide mechanism: the same
path drives DS10/ES40 identity, so fixing the open interface fixes the class.

## 4. Confirmation run (the one empirical step, deterministic)

```
export EMULATR_IIC_TRACE=1
export EMULATR_FLASH_ROM=ds20_flash.rom; rm -f ds20_flash.rom
./run_fw.sh ds20 cold 2>&1 | tee fw_ds20_iic.out
grep -nE "Error determining system type|IIC|0x40|ocp" fw_ds20_iic.out
```
Read off:
- (a) NO "Error determining system type" line  => confirms the clean member-1 path (expected).
- (b) Is node 0x40 opened/probed during the EARLY (HWRPB-build) window, and does it ACK or NAK?
  - NAK early but ACK later  => init-order / controller-not-ready interface gap.
  - 0x40 never probed by the guest open  => controller-ready or device-registration gap
    (the open never reaches the bus).

The branch from (b) names the exact EmulatR interface to complete. No symptom chasing.

## 5. Status of related tasks

- Task 7 reframed: "Make the iic_ocp0 open interface solid so get_sysvar picks DS20 (member 6)".
- Task 6 reframed: deterministic disambiguation via the IIC trace above (PA-watch demoted to
  optional confirmation; the source supersedes it).
- The two-HWRPB question (task 3) is informed: on APC_PLATFORM the SRM-built HWRPB's SYSVAR is
  authored by get_sysvar in hwrpb.c -- EmulatR's own HwrpbBuilder (PA 0, hardcoded) is not the
  one carrying this value, reinforcing that the live SRM HWRPB @0x2000 is what the OS consumes.
