
SRM operations.
>>> set oem_string snapshot      # writes predig_oemsnap_cyc<N>.axpsnap, autoloadable



$ plink -raw -P 10023 127.0.0.1


ASA EmulatR -- Alpha AXP (EV6 / 21264) Emulator
Alpha Emulator Console V4.0-0
(c) 2026 Timothy Peer / eNVy Systems, Inc.

Checking dva0.0.0.0.0 for the option firmware files. . .

Option firmware files were not found on CD or floppy.
If you want to load the options firmware,
please enter the device on which the files are located(ewa0),
or just hit <return> to proceed with a standard console update:


                ***** Loadable Firmware Update Utility *****
------------------------------------------------------------------------------
 Function    Description
------------------------------------------------------------------------------
 Display     Displays the system's configuration table.
 Exit        Done exit LFU (reset).
 List        Lists the device, revision, firmware name, and update revision.
 Readme      Lists important release information.
 Update      Replaces current firmware with loadable data image.
 Verify      Compares loadable and hardware images.
 ? or Help   Scrolls this function table.
------------------------------------------------------------------------------

UPD> update srm

Confirm update on:
srm              [Y/(N)]y

WARNING: updates may take several minutes to complete for each device.

                          DO NOT ABORT!

srm             Updating to 7.3-1...
UPD> exit

 Initializing....
Testing the System

AlphaServer DS10 266 MHz Console V7.3-2, Feb 27 2007 13:25:53

Halt Button is IN, AUTO_ACTION ignored

>>>
>>>
>>>show
auto_action             HALT
boot_dev
boot_file
boot_osflags            0
boot_reset              OFF
bootbios
bootdef_dev
booted_dev
booted_file
booted_osflags
char_set                0
com1_baud               9600
com1_flow               NONE
com1_mode               SNOOP
com1_modem              OFF
com2_baud               9600
com2_flow               SOFTWARE
com2_modem              OFF
console                 serial
console_memory_allocation       old
controlp                ON
d_bell                  off
d_cleanup               on
d_complete              off
d_eop                   off
d_group                 field
d_harderr               halt
d_loghard               on
d_logsoft               off
d_omit
d_oper                  on
d_passes                1
d_quick                 off
d_report                full
d_runtime               0
d_softerr               continue
d_startup               off
d_status                off
d_trace                 off
d_verbose               0
dump_dev
enable_audit            ON
ffauto                  OFF
ffnext                  OFF
full_powerup_diags      ON
heap_expand             NONE
i                       g
j                       w
k                       g
kbd_hardware_type       PCXAL
language                36
language_name           English (American)
license                 MU
memory_test             full
mfg_status              19bcf8
N1
N10
N11
N12
N13
N14
N15
N16
N2
N3
N4
N5
N6
N7
N8
N9
oem_string
os_type                 OpenVMS
page_table_levels       3
pal                     OpenVMS PALcode V1.98-83, Tru64 UNIX PALcode V1.92-73
pci_parity              ON
prefetch_mode           ON
prompt                  >>>
reset_boot_arg0
reset_boot_arg1
reset_boot_arg2
rmc_halt                DISABLED
scsi_poll               ON
scsi_reset              ON
shutdown_temp           60
srm2ctrl
srm2dev
sys_serial_num          test123
tt_allow_login          1
tty_dev                 0
user_def1
user_def2
version                 V7.3-2 Feb 27 2007 13:25:53
wwid0
wwid1
wwid2
wwid3
wwid4
wwid5
wwid6
wwid7
>>>
>>>set oem_string snapshot
>>>show oem_string
oem_string              snapshot
>>>
>>>show device
dva0.0.0.0.0               DVA0
>>>show bootdef_dev
bootdef_dev
>>>show auto_action
auto_action             HALT
>>>




