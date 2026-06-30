// ============================================================================
// Hwrpb.h -- Alpha Hardware Restart Parameter Block data structures
// ============================================================================
// Project: EmulatR -- Alpha AXP / EV6 Architecture Emulator (V4)
// Copyright (C) 2025, 2026 eNVy Systems, Inc.  All rights reserved.
// Licensed under eNVy Systems Non-Commercial License v1.1
//
// Project Architect: Timothy Peer
// AI Collaboration:  Claude (Anthropic)
//
// Commercial use prohibited without separate license.
// Contact:        peert@envysys.com  |  https://envysys.com
// Documentation:  https://timothypeer.github.io/ASA-EMulatR-Project/
// ============================================================================
//
// Byte-precise C++ structures mirroring the Alpha firmware-OS data
// contract.  These are POD types overlaid onto guest physical memory
// pages -- the firmware (PALcode + SRM Console) populates them at cold
// boot, and the booted OS (Tru64 / OpenVMS / Linux) reads them to learn
// the hardware configuration, find its per-CPU context, and call back
// into firmware services.
//
// Source authority (in order of precedence):
//
//   1. AARM Section III "Common Architecture, Console Interface" --
//      D:\EmulatR\Processor Support\alpha_arch_ref.txt (Section III)
//   2. Digital SDL definitions, the canonical Digital naming --
//      Processor Support\Palcode\palcode\apisrm\apisrm\ref\
//          apu_hwrpb_def.h    (HWRPB / SLOT / HWPCB / MEMDSC layout)
//          apu_crb_def.h      (CRB I/O entry layout)
//          apu_ctb_def.h      (CTB layout + console-type enum)
//          hwrpb_def.sdl      (source SDL of the above)
//   3. Modern Alpha-Linux header for cross-checked field semantics --
//      Processor Support\Palcode\palcode\diags\diags\include\hwrpb.h
//   4. Live initialization code (three independent implementations) --
//      apisrm/ref/hwrpb.c, milo-sources/.../hwrpb.c, diags/dbm/hwrpb.c
//
// Design conventions in this file:
//
//   * All multi-byte fields are little-endian quadwords (uint64_t)
//     unless explicitly typed otherwise.  Alpha is little-endian.
//   * #pragma pack(push, 1) forces no compiler-inserted padding.
//     Layouts are quadword-aligned by construction.
//   * static_assert pins sizeof() and offsetof() at compile time so
//     drift is caught immediately.
//   * No virtual methods, no Qt types, no STL containers.  Pure POD,
//     trivially-copyable, suitable for direct memcpy onto a guest
//     physical-memory page.
//   * Field names retain a hint of the canonical Digital SDL name
//     (e.g., HWPCB::ksp -- corresponds to hwpcb$Q_KSP) so a reader
//     cross-referencing apu_hwrpb_def.h finds them immediately.
//
// Platform variation: the HWRPB carries a per-CPU SLOT array whose
// length is implementation-defined.  Tsunami-class systems (DS10/DS20/
// ES40, V4's primary target) have up to 4 CPUs.  This file declares
// fixed-size structures; the SLOT array is laid out by the firmware
// initializer at runtime starting at the offset given in HwrpbHeader.
// ============================================================================

#ifndef EMULATR_DEVICELIB_HWRPB_H
#define EMULATR_DEVICELIB_HWRPB_H

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace deviceLib {
namespace hwrpb {

// ----------------------------------------------------------------------------
// Processor type codes (HWRPB SLOT$Q_CPU_TYPE values).
// Source: diags/include/hwrpb.h enum hwrpb_processor_codes (lines 84-97);
// AARM Section III Table 32.
// ----------------------------------------------------------------------------
enum class CpuType : uint64_t {
    EV3   = 1,    // 21064 prototype
    EV4   = 2,    // 21064
    LCA4  = 4,    // 21066/21068
    EV5   = 5,    // 21164
    EV45  = 6,    // 21064A
    EV56  = 7,    // 21164A
    EV6   = 8,    // 21264
    PCA56 = 9,    // 21164PC
    PCA57 = 10,   // 21164PC variant
    EV67  = 11,   // 21264A
    EV68  = 12,   // 21264B/C
};

// ----------------------------------------------------------------------------
// System type codes (HWRPB SYSTYPE field).
// Tsunami-class systems are V4's primary target.  Add others as needed.
// Source: diags/include/hwrpb.h.
// ----------------------------------------------------------------------------
enum class SystemType : uint64_t {
    DEC_TSUNAMI    = 34,   // Tsunami: DS10/DS20/ES40 (EV6 + 21272 chipset)
    API_NAUTILUS   = 201,  // Alpha Processor Inc. Nautilus (UP1000/UP2000)
    // Extend with other systypes as platforms are added.
};

// ----------------------------------------------------------------------------
// Halt codes (SLOT$Q_HALTCODE values).
// Source: apu_hwrpb_def.h lines 148-156 (HALT$K_*).
// PALcode populates this on every halt-class entry so the OS can read
// why the previous run terminated.
// ----------------------------------------------------------------------------
enum class HaltCode : uint64_t {
    Bootstrap        = 0,   // cold/warm boot entry
    Powerup          = 1,   // power-on
    OperatorHalt     = 2,   // explicit operator halt
    OperatorCrash    = 3,   // operator-requested crash dump
    KStackNotValid   = 4,   // KSP invalid on trap entry
    ScbbNotValid     = 5,   // SCBB invalid on trap entry
    PtbrNotValid     = 6,   // PTBR invalid on translation
    UnknownRequest   = 7,   // unrecognized service request
    DoubleError      = 8,   // exception during exception handling
};

// ----------------------------------------------------------------------------
// Console type codes (CTB$Q_CONS_TYPE values).
// Source: apu_ctb_def.h lines 15-19; apu_hwrpb_def.h lines 178-182.
// ----------------------------------------------------------------------------
enum class ConsoleType : uint64_t {
    None             = 0,
    DetachedService  = 1,
    SerialLine       = 2,   // VT100 / VT220 / VT320 over serial
    Graphics         = 3,   // local framebuffer console
    Network          = 192, // NI / Ethernet / MOP boot console
};

// ----------------------------------------------------------------------------
// HaltRequest -- 8-bit value packed into SlotState bits 16..23.
// Source: diags/include/hwrpb.h lines 36-43 (PERCPU_REASON / REASON_*).
// Plain integer values (no shift); assign directly to
// SlotState::halt_request, or shift << 16 if writing to .raw.
// ----------------------------------------------------------------------------
namespace HaltRequest {
constexpr uint64_t Default  = 0;
constexpr uint64_t TermExit = 1;
constexpr uint64_t ColdBoot = 2;
constexpr uint64_t WarmBoot = 3;
constexpr uint64_t Halt     = 4;
}  // namespace HaltRequest

// ----------------------------------------------------------------------------
// SlotFlags -- legacy bit-mask constants for raw .state.raw OR'ing.
// Prefer the typed SlotState bit-field interface below; these masks remain
// for code paths that still touch the raw qword (trace dumps, snapshot
// serialization, etc.).  Bit positions match SLOT$M_* in apu_hwrpb_def.h.
// ----------------------------------------------------------------------------
namespace SlotFlags {
constexpr uint64_t Bip          = 1ULL << 0;   // Bootstrap In Progress    (BIP)
constexpr uint64_t RestartCap   = 1ULL << 1;   // Restart Capable          (RC)
constexpr uint64_t CpuAvail     = 1ULL << 2;   // CPU Present and Avail    (PA)
constexpr uint64_t CpuPresent   = 1ULL << 3;   // CPU physically Present   (PP)
constexpr uint64_t OperatorHalt = 1ULL << 4;   // Operator Halt            (OH)
constexpr uint64_t CtxValid     = 1ULL << 5;   // HWPCB Context Valid      (CV)
constexpr uint64_t PalValid     = 1ULL << 6;   // PALcode Valid            (PV)
constexpr uint64_t MemValid     = 1ULL << 7;   // PAL Mem/scratch Valid    (PMV)
constexpr uint64_t PalLoaded    = 1ULL << 8;   // PALcode Loaded           (PL)
}  // namespace SlotFlags

// ============================================================================
// Layout structures.  All packed; no compiler-inserted padding.
// All structs use alignas(8) for strong (8-byte) alignment of their start
// address -- guarantees host-CPU efficient access to the qword fields and
// matches the Alpha-side natural alignment expected by PALcode.
// ============================================================================
#pragma pack(push, 1)

// ----------------------------------------------------------------------------
// SlotState -- typed view of SLOT$R_STATE_FLAGS.
// 64-bit value with bit-field accessors for the firmware/OS handshake state
// and the halt-request reason.  Bit positions match the canonical
// apu_hwrpb_def.h struct STATE layout exactly -- the OS reads SLOT$L_STATE_
// FLAGS at offset 0x80 of each PerCpuSlot and unpacks per these bit
// positions, so any reordering would break the OS contract.
//
// Two access modes:
//   raw           -- whole 64-bit word for OR-mask manipulation,
//                    snapshot serialization, or debugger inspection.
//   bit-field     -- semantic names; assign directly:
//                       slot.state.bootstrap = 1;
//                       slot.state.halt_request = HaltRequest::ColdBoot;
//
// The anonymous-struct-in-union pattern is non-standard ISO C++ but
// supported by GCC, Clang, and MSVC; it lets callers write
// slot.state.bootstrap rather than slot.state.bits.bootstrap.
// ----------------------------------------------------------------------------
union alignas(8) SlotState {
    uint64_t raw;
    struct {
        uint64_t bootstrap     : 1;  // bit 0   -- BIP, Bootstrap In Progress
        uint64_t restart_cap   : 1;  // bit 1   -- RC, Restart Capable (a.k.a. "primary" colloquially)
        uint64_t available     : 1;  // bit 2   -- PA, CPU Present and Available
        uint64_t present       : 1;  // bit 3   -- PP, CPU physically Present
        uint64_t halted        : 1;  // bit 4   -- OH, Operator Halt
        uint64_t ctx_valid     : 1;  // bit 5   -- CV, HWPCB Context Valid
        uint64_t pal_valid     : 1;  // bit 6   -- PV, PALcode Valid
        uint64_t pal_mem_valid : 1;  // bit 7   -- PMV, PAL mem/scratch addrs Valid
        uint64_t pal_loaded    : 1;  // bit 8   -- PL, PALcode Loaded
        uint64_t reserved_lo   : 7;  // bits 9-15  reserved (filler1 in SDL)
        uint64_t halt_request  : 8;  // bits 16-23 HaltRequest::* value
        uint64_t reserved_hi   : 8;  // bits 24-31 reserved (filler2 in SDL)
        uint64_t reserved_top  : 32; // bits 32-63 reserved (filler3 in SDL)
    };
};
static_assert(sizeof(SlotState) == 8, "SlotState must be exactly 64 bits");

// ----------------------------------------------------------------------------
// HWPCB -- Hardware Process Control Block (16 quadwords = 128 bytes).
// Embedded at the start of every per-CPU SLOT.  Holds the architectural
// register context that defines the current process realm: stack pointers
// for all four privilege modes, page table base, ASN, AST state, FP enable,
// cycle count, plus 7 PALcode-private scratch slots.
// CALL_PAL SWPCTX swaps the live HWPCB on context switch.
// Source: AARM Section III; apu_hwrpb_def.h lines 95-107 (SLOT$R_HWPCB);
// diags/include/hwrpb.h lines 72-81 (HWPCB_t).
// ----------------------------------------------------------------------------
struct alignas(8) Hwpcb {
    uint64_t ksp;             // 0x00 -- kernel stack pointer
    uint64_t esp;             // 0x08 -- executive stack pointer
    uint64_t ssp;             // 0x10 -- supervisor stack pointer
    uint64_t usp;             // 0x18 -- user stack pointer
    uint64_t ptbr;            // 0x20 -- page table base register (PFN)
    uint64_t asn;             // 0x28 -- address space number
    uint64_t asten_sr;        // 0x30 -- AST enable / status (per-mode bits)
    uint64_t fen;             // 0x38 -- floating-point enable
    uint64_t cc;              // 0x40 -- cycle count snapshot
    uint64_t scratch[7];      // 0x48 -- PALcode-private scratch (56 bytes)
};
static_assert(sizeof(Hwpcb) == 128, "HWPCB must be exactly 16 quadwords (128 bytes) per AARM Section III");
static_assert(offsetof(Hwpcb, ksp)      == 0x00, "HWPCB KSP offset");
static_assert(offsetof(Hwpcb, ptbr)     == 0x20, "HWPCB PTBR offset");
static_assert(offsetof(Hwpcb, asn)      == 0x28, "HWPCB ASN offset");
static_assert(offsetof(Hwpcb, scratch)  == 0x48, "HWPCB scratch offset");

// ----------------------------------------------------------------------------
// PerCpuSlot -- one SLOT in the HWRPB's per-CPU array.
// Carries HWPCB at the front, then state flags, PAL metadata, halt
// context, identity, and (last) the DSRDB section that the OS may use.
// Source: apu_hwrpb_def.h lines 95-147 (SLOT structure).
// ----------------------------------------------------------------------------
struct alignas(8) PerCpuSlot {
    Hwpcb     hwpcb;                 // 0x000 -- 128 bytes (16 qwords)
    SlotState state;                 // 0x080 -- typed; see SlotState above
    uint64_t pal_mem_len;            // 0x088 -- length of PALcode mem region
    uint64_t pal_scr_len;            // 0x090 -- length of PALcode scratch region
    uint64_t pal_mem_pa;             // 0x098 -- PA of PALcode memory image
    uint64_t pal_scr_pa;             // 0x0A0 -- PA of PALcode scratch
    uint32_t pal_rev;                // 0x0A8 -- PALcode revision (longword)
    uint32_t pal_var;                // 0x0AC -- PALcode variation (longword)
    uint64_t cpu_type;               // 0x0B0 -- CpuType (EV4/EV5/EV6/...)
    uint64_t cpu_var;                // 0x0B8 -- CPU variation
    uint64_t cpu_rev;                // 0x0C0 -- CPU revision
    uint64_t serial_no[2];           // 0x0C8 -- 16-byte ASCII serial number
    uint64_t logout_mem_pa;          // 0x0D8 -- machine-check logout area PA
    uint64_t logout_length;          // 0x0E0 -- logout area length
    uint64_t halt_pcbb;              // 0x0E8 -- saved PCBB on halt
    uint64_t halt_pc;                // 0x0F0 -- saved PC on halt
    uint64_t halt_ps;                // 0x0F8 -- saved PS (mode/IPL) on halt
    uint64_t halt_arglist;           // 0x100 -- saved arg-list on halt
    uint64_t halt_ret_addr;          // 0x108 -- saved RA (R26) on halt
    uint64_t halt_proc_value;        // 0x110 -- saved procedure value (R27)
    uint64_t halt_code;              // 0x118 -- HaltCode reason
    uint64_t reserved_sw;            // 0x120 -- reserved for software
    uint64_t icba[21];               // 0x128 -- IPC block (21 qwords = 168 = 0xA8 bytes)
    uint64_t palcode_revs[16];       // 0x1D0 -- PAL revs by personality
                                     //          [0]=unused, [1]=VMS, [2]=OSF
                                     //          (16 qwords = 128 = 0x80 bytes)
    // ---- AARM Table 26-4 slot tail (+592..+631) -------------------------
    // CORRECTED 2026-06-29.  These five fields were previously hidden inside
    // an opaque hwpcb_filler[176], with an (incorrect) in-slot DSRDB after it
    // that inflated the slot to 0x400.  The DSRDB is in fact a SEPARATE
    // top-level HWRPB section reached via the header's dsrdb_offset (+312) --
    // it is NOT part of the per-CPU slot.  See the Dsrdb struct below.  The
    // live DS20 SRM builds slots with stride 0x280, which matches this layout
    // (AARM fields end at +624, last byte +631, padded to an octaword = 640).
    // Full map + reconciliation: journals/HWRPB_PerCpuSlot_FieldMap_AARM_20260629.md
    uint64_t sw_compat;              // 0x250 (+592) processor SW-compat (fmt follows SLOT[176])
    uint64_t console_log_pa;         // 0x258 (+600) console data-log physical address (0 = none)
    uint64_t console_log_len;        // 0x260 (+608) console data-log length (0 = none)
    uint64_t cache_descriptor;       // 0x268 (+616) assoc / char-mask / block-size / total-size
    uint64_t cycle_count_freq;       // 0x270 (+624) per-CPU SCC/PCC freq (0 => use HWRPB[112])
    uint64_t slot_pad;               // 0x278 -- octaword pad to slot size 0x280 (640 bytes)
};
static_assert(offsetof(PerCpuSlot, state)        == 0x080, "state (SlotState) after HWPCB");
static_assert(offsetof(PerCpuSlot, halt_pc)      == 0x0F0, "halt_pc offset");
static_assert(offsetof(PerCpuSlot, halt_code)    == 0x118, "halt_code (REASON FOR HALT) offset");
static_assert(offsetof(PerCpuSlot, icba)         == 0x128, "icba (RXTX BUFFER AREA) offset");
static_assert(offsetof(PerCpuSlot, palcode_revs) == 0x1D0, "palcode_revs (PALCODE AVAILABLE) offset");
static_assert(offsetof(PerCpuSlot, sw_compat)        == 0x250, "sw_compat at +592 (AARM Table 26-4)");
static_assert(offsetof(PerCpuSlot, console_log_pa)   == 0x258, "console_log_pa at +600");
static_assert(offsetof(PerCpuSlot, console_log_len)  == 0x260, "console_log_len at +608");
static_assert(offsetof(PerCpuSlot, cache_descriptor) == 0x268, "cache_descriptor at +616");
static_assert(offsetof(PerCpuSlot, cycle_count_freq) == 0x270, "cycle_count_freq at +624");
static_assert(sizeof(PerCpuSlot) == 0x280,
              "PerCpuSlot = 640 bytes (0x280): AARM Table 26-4 fields end at +624 (last byte "
              "+631), padded to an octaword.  Matches the live DS20 SRM slot stride; supersedes "
              "the prior 0x400 (which embedded a now-extracted in-slot DSRDB).");

// ----------------------------------------------------------------------------
// Dsrdb -- Dynamic System Recognition Data Block.
// A SEPARATE top-level HWRPB section (NOT part of PerCpuSlot), located via the
// HWRPB header's dsrdb_offset (+312).  Found live on DS20 at PA 0x2ac0.  Holds
// OS-readable descriptive data: the system marketing-model code, the system
// name, and the LURT (logical-unit recognition table).
// Promoted out of PerCpuSlot 2026-06-29 (it was incorrectly embedded in the
// slot).  The field layout below preserves EmulatR's prior intent; VALIDATE it
// field-by-field against the live 0x2ac0 dump and apisrm apu_hwrpb_def.h before
// relying on it (HWRPB field-map task 1 follow-up).
// ----------------------------------------------------------------------------
struct alignas(8) Dsrdb {
    uint64_t smm;                // 0x00 -- system marketing-model code
    uint64_t lurt_offset;        // 0x08 -- byte offset to the LURT table
    uint64_t name_offset;        // 0x10 -- byte offset to the system name
    uint64_t lurt_count;         // 0x18 -- number of LURT entries
    uint64_t lurt_table[20];     // 0x20 -- 20 qwords = 160 bytes
    uint64_t name_count;         // 0xC0 -- length of the system name
    uint64_t name[7];            // 0xC8 -- 7 qwords = 56 bytes (ends 0x100)
};
static_assert(offsetof(Dsrdb, lurt_table) == 0x20,  "Dsrdb lurt_table offset");
static_assert(offsetof(Dsrdb, name)       == 0xC8,  "Dsrdb name offset");
static_assert(sizeof(Dsrdb) == 0x100, "Dsrdb = 256 bytes");

// ----------------------------------------------------------------------------
// MemoryCluster + MemoryDescriptor -- physical memory layout.
// Source: apu_hwrpb_def.h lines 160-174 (CLUSTER / MEMDSC).
// ----------------------------------------------------------------------------
struct alignas(8) MemoryCluster {
    uint64_t start_pfn;          // first page frame number
    uint64_t pfn_count;          // number of pages
    uint64_t test_count;         // pages tested at boot
    uint64_t bitmap_va;          // bitmap virtual address (per-page valid)
    uint64_t bitmap_pa;          // bitmap physical address
    uint64_t bitmap_checksum;    // bitmap checksum
    uint64_t usage;              // OS-reserved usage flags
};
static_assert(sizeof(MemoryCluster) == 56, "MemoryCluster: 7 quadwords");

struct alignas(8) MemoryDescriptor {
    uint64_t      checksum;      // sum of all clusters
    uint64_t      reserved;
    uint64_t      cluster_count; // number of valid clusters
    MemoryCluster cluster[3];    // up to 3 clusters per apu_hwrpb_def.h
                                 // (extend if multi-node systems land)
};

// ----------------------------------------------------------------------------
// CrbIoEntry -- one I/O region descriptor inside the CRB.
// Source: apu_crb_def.h lines 9-13 (struct crb_io_entry).
// ----------------------------------------------------------------------------
struct alignas(8) CrbIoEntry {
    uint64_t vir_addr;           // virtual address (PALcode-mapped)
    uint64_t phy_addr;           // physical address (bus-side)
    uint64_t page_count;         // length in 8KB pages
};
static_assert(sizeof(CrbIoEntry) == 24, "CrbIoEntry: 3 quadwords");

// ----------------------------------------------------------------------------
// CrbHeader -- Console Routine Block header (platform-agnostic prefix).
// The full CRB tail is platform-specific (FLAMINGO had ~30 entries; Tsunami
// has its own set).  Define the header here; per-platform code appends the
// actual CrbIoEntry array sized to its hardware.
// Source: apu_crb_def.h lines 21-58 (struct crb_def header).
// ----------------------------------------------------------------------------
struct alignas(8) CrbHeader {
    uint64_t dispatch_va;        // OS-callable firmware entry, virtual
    uint64_t dispatch_pa;        // ditto, physical
    uint64_t fixup_va;           // OS-callable VA fixup routine
    uint64_t fixup_pa;           // ditto, physical
    uint64_t nbr_of_entries;     // count of trailing CrbIoEntry records
    uint64_t pages_to_map;       // total page count for CRB-mapped I/O
};
static_assert(sizeof(CrbHeader) == 48, "CrbHeader: 6 quadwords");

// ----------------------------------------------------------------------------
// Ctb -- Console Terminal Block.
// Describes the console-attached hardware: type, IPL/interrupt vectors,
// keyboard mapping, font tables, monitor geometry, and a putchar callback
// the firmware/OS can invoke.
// Source: apu_ctb_def.h lines 21-50 (head fields shown; full struct is
// 12248 bytes per the CTB$K_CTB_DATA_SIZE constant).
// ----------------------------------------------------------------------------
struct alignas(8) CtbHeader {
    uint64_t cons_type;          // ConsoleType (0=none,2=serial,3=graphics,...)
    uint64_t cons_unit;          // device unit number
    uint64_t reserved;
    uint64_t length;             // length of the full CTB record
    uint64_t dev_ipl;            // device IPL for console interrupts
    uint64_t txint;              // transmit interrupt vector
    uint64_t rxint;              // receive interrupt vector
    uint64_t terminal_type;      // VT100 / VT220 / VT320 / VT420 / ...
    uint64_t keybd_typ;          // keyboard family
    uint64_t keybd_trans_ptr;    // keyboard scan-code translation table
    uint64_t keybd_map_ptr;      // keyboard map (modifier states)
    uint64_t kbdstate;           // current keyboard state
    uint64_t last_key;           // last key processed
    uint64_t usfont_table_ptr;   // US font table
    uint64_t mcsfont_table_ptr;  // multinational char-set font table
    uint64_t font_width;
    uint64_t font_height;
    uint64_t mon_width;
    uint64_t mon_height;
    uint64_t mon_density;
    uint64_t planes;
    uint64_t cursor_width;
    uint64_t cursor_height;
    uint64_t number_of_heads;
    uint64_t window_up_down;
    uint64_t head_offset;
    uint64_t tc_putchar;         // putchar callback (FW/OS shared)
    uint64_t iostate;
    uint64_t listener_state;
};

// ----------------------------------------------------------------------------
// HwrpbHeader -- HWRPB front-matter, byte-precise per AARM Figure 26-2
// and Table 26-1 (alpha_arch_ref.txt ~line 35460+).
//
// All field offsets are AARM-canonical and decimal-named (e.g., +144 =
// number of processor slots).  Total fixed-header size is 320 bytes;
// the trailing dynamic sections (TBB, per-CPU SLOTs, CTBs, CRB, MEMDSC,
// CDB, FRU, RX/TX extension, DSRDB) follow at offsets carried in this
// header's *_offset fields.
//
// HWRPB revision in this file is set to 13 (current AARM); the older
// Digital SDL (apu_hwrpb_def.h, hwrpb_def.sdl) was revision 2 for the
// Avanti generation -- the layout has been backward-compatible since.
// ----------------------------------------------------------------------------
struct alignas(8) HwrpbHeader {
    // ---- Identity and size (AARM Table 26-1) ----
    uint64_t hwrpb_pa;                // +0   -- physical address of this HWRPB
    uint64_t identifier;              // +8   -- "HWRPB\0\0\0" = 0x0000004250525748
    uint64_t revision;                // +16  -- HWRPB revision (13 for current AARM)
    uint64_t hwrpb_size;              // +24  -- total HWRPB size in bytes
    uint64_t primary_cpu_id;          // +32  -- WHAMI of primary processor
    uint64_t page_size;               // +40  -- page size in bytes (8192 on Alpha)

    // PA size and extended VA size are TWO 32-bit fields packed into
    // one quadword at +48 (PA Size in low half, Extended VA Size in high).
    uint32_t pa_size_bits;            // +48  -- physical-address size in bits (<=48)
    uint32_t extended_va_size;        // +52  -- 5 if 48/43 mixed mode, else 0

    uint64_t max_valid_asn;           // +56  -- maximum ASN this CPU supports

    // System serial number is a 16-byte octaword (10 ASCII chars + pad).
    uint64_t system_serial_number[2]; // +64  -- COMPAQ STD 12 serial

    uint64_t system_type;             // +80  -- SystemType (Tsunami / Nautilus / ...)
    uint64_t system_variation;        // +88
    uint64_t system_revision;         // +96  -- 4-char ASCII revision code
    uint64_t intrclock_freq;          // +104 -- interval clock interrupts/sec * 4096
    uint64_t cycle_count_freq;        // +112 -- cycle-counter frequency (RPCC/SCC/PCC rate)
    uint64_t vptb_va;                 // +120 -- virtual address of page table base
    uint64_t reserved_arch;           // +128 -- reserved for architecture

    // ---- Offsets to dynamic sections (relative to HWRPB base) ----
    uint64_t tbb_offset;              // +136 -- offset to Translation Buffer Hint Block
    uint64_t cpu_slot_count;          // +144 -- number of per-CPU SLOTs
    uint64_t cpu_slot_size;           // +152 -- size of each PerCpuSlot
    uint64_t cpu_slot_offset;         // +160 -- offset to per-CPU SLOT array
    uint64_t ctb_count;               // +168 -- number of CTBs
    uint64_t ctb_size;                // +176 -- size of each CTB
    uint64_t ctb_offset;              // +184 -- offset to CTB table
    uint64_t crb_offset;              // +192 -- offset to Console Callback Routine Block
    uint64_t mddt_offset;             // +200 -- offset to Memory Data Descriptor Table
    uint64_t cdb_offset;              // +208 -- offset to Configuration Data Block (0 if absent)
    uint64_t fru_offset;              // +216 -- offset to FRU table (0 if absent)

    // ---- Console save/restore state and CPU restart routines ----
    uint64_t terminal_save_va;        // +224 -- VA of save-state routine
    uint64_t terminal_save_pv;        // +232 -- procedure value of save-state routine
    uint64_t terminal_restore_va;     // +240 -- VA of restore-state routine
    uint64_t terminal_restore_pv;     // +248 -- procedure value of restore-state routine
    uint64_t cpu_restart_va;          // +256 -- VA of CPU restart routine
    uint64_t cpu_restart_pv;          // +264 -- procedure value of CPU restart routine
    uint64_t reserved_software;       // +272 -- reserved for system software
    uint64_t reserved_hardware;       // +280 -- reserved for hardware

    // ---- Checksum and trailing extension blocks ----
    uint64_t checksum;                // +288 -- sum of qwords +0..+280 inclusive
    uint64_t rxtx_block[2];           // +296, +304 -- RX/TX serial extension block
    uint64_t dsrdb_offset;            // +312 -- offset to DSRDB table (0 if absent)
};
static_assert(sizeof(HwrpbHeader) == 320, "HWRPB header is 320 bytes per AARM Figure 26-2");
static_assert(offsetof(HwrpbHeader, identifier)        == 8,   "identifier at +8");
static_assert(offsetof(HwrpbHeader, revision)          == 16,  "revision at +16");
static_assert(offsetof(HwrpbHeader, hwrpb_size)        == 24,  "hwrpb_size at +24");
static_assert(offsetof(HwrpbHeader, primary_cpu_id)    == 32,  "primary_cpu_id at +32");
static_assert(offsetof(HwrpbHeader, page_size)         == 40,  "page_size at +40");
static_assert(offsetof(HwrpbHeader, pa_size_bits)      == 48,  "pa_size at +48");
static_assert(offsetof(HwrpbHeader, extended_va_size)  == 52,  "extended VA size at +52");
static_assert(offsetof(HwrpbHeader, max_valid_asn)     == 56,  "max_valid_asn at +56");
static_assert(offsetof(HwrpbHeader, system_serial_number) == 64,  "system serial at +64");
static_assert(offsetof(HwrpbHeader, system_type)       == 80,  "system_type at +80");
static_assert(offsetof(HwrpbHeader, intrclock_freq)    == 104, "intrclock_freq at +104");
static_assert(offsetof(HwrpbHeader, cycle_count_freq)  == 112, "cycle_count_freq at +112");
static_assert(offsetof(HwrpbHeader, vptb_va)           == 120, "vptb_va at +120");
static_assert(offsetof(HwrpbHeader, tbb_offset)        == 136, "tbb_offset at +136");
static_assert(offsetof(HwrpbHeader, cpu_slot_count)    == 144, "cpu_slot_count at +144");
static_assert(offsetof(HwrpbHeader, cpu_slot_size)     == 152, "cpu_slot_size at +152");
static_assert(offsetof(HwrpbHeader, cpu_slot_offset)   == 160, "cpu_slot_offset at +160");
static_assert(offsetof(HwrpbHeader, ctb_offset)        == 184, "ctb_offset at +184");
static_assert(offsetof(HwrpbHeader, crb_offset)        == 192, "crb_offset at +192");
static_assert(offsetof(HwrpbHeader, mddt_offset)       == 200, "mddt_offset at +200");
static_assert(offsetof(HwrpbHeader, cdb_offset)        == 208, "cdb_offset at +208");
static_assert(offsetof(HwrpbHeader, fru_offset)        == 216, "fru_offset at +216");
static_assert(offsetof(HwrpbHeader, checksum)          == 288, "checksum at +288");
static_assert(offsetof(HwrpbHeader, rxtx_block)        == 296, "rxtx_block at +296");
static_assert(offsetof(HwrpbHeader, dsrdb_offset)      == 312, "dsrdb_offset at +312");

#pragma pack(pop)

// ----------------------------------------------------------------------------
// Helpers for populating an HWRPB-shaped region in guest physical memory.
// These are declarations only; implementations land alongside firmware
// initialization (FirmwareDeviceManager Phase 0).  Kept here so the API
// surface lives next to the data definitions.
// ----------------------------------------------------------------------------

// Compute the cluster checksum required by MemoryDescriptor::checksum.
// AARM defines this as the sum of all preceding 64-bit words modulo 2^64.
uint64_t computeMemDscChecksum(MemoryDescriptor const& md) noexcept;

// Compute the HWRPB header checksum (sum of all preceding qwords).
uint64_t computeHwrpbChecksum(HwrpbHeader const& hdr) noexcept;

// HWRPB validation identifier.  Per AARM Table 26-1 +08, the quadword
// at HWRPB+8 must contain "HWRPB\0\0\0" (5 ASCII bytes + 3 nulls).
// Stored little-endian, this reads as 0x0000004250525748:
//   byte 0='H'(0x48)  byte 1='W'(0x57)  byte 2='R'(0x52)  byte 3='P'(0x50)
//   byte 4='B'(0x42)  byte 5..7 = 0
constexpr uint64_t kHwrpbIdentifier =
    (uint64_t{'H'})       |
    (uint64_t{'W'} <<  8) |
    (uint64_t{'R'} << 16) |
    (uint64_t{'P'} << 24) |
    (uint64_t{'B'} << 32);
static_assert(kHwrpbIdentifier == 0x0000004250525748ULL,
              "HWRPB identifier matches AARM Table 26-1 +08");

// HWRPB revision for current AARM (Section 26.1.1).  The Avanti-era
// Digital SDL was revision 2; the layout has been backward-compatible
// since.  Real systems shipped with various revisions; pick whichever
// the platform target requires.
constexpr uint64_t kHwrpbRevisionCurrent = 13;

// Standard Alpha page size (8 KB; AARM defines this as fixed for all
// implementations).
constexpr uint64_t kAlphaPageSize = 8192;

// ============================================================================
// kKeyValue offset map -- named byte-offset constants ("key = value" pairs),
// one per field, mirroring kAlphaPageSize-style convenience constants.  Value
// is the field's byte offset from its region base.  These let raw-memory
// walkers (the HWRPB scan instrument, the boot-time validator, snapshot tools)
// locate a field by name instead of a magic number, and pair with the View
// unions + peek/poke helpers below.  Every constant is pinned to the matching
// struct offsetof() by the static_asserts at the end of this block, so the two
// access paths (typed member <-> offset key) can never silently drift apart.
//
// Authoritative layout: AARM Section III Console Interface (Table 26-1 header,
// Table 26-4 per-CPU slot).  EmulatR-only; apisrm/srmapi remain the read-only
// SSOT and are cross-referenced, never edited.
// ============================================================================

// ---- HWRPB header field offsets (decimal, AARM Table 26-1) ----
constexpr uint64_t kHwrpbPhysAddr        = 0;    // +0   self physical address
constexpr uint64_t kHwrpbId              = 8;    // +8   "HWRPB\0\0\0"
constexpr uint64_t kHwrpbRevision        = 16;   // +16  HWRPB revision
constexpr uint64_t kHwrpbSize            = 24;   // +24  total HWRPB size
constexpr uint64_t kHwrpbPrimaryCpuId    = 32;   // +32  primary WHAMI
constexpr uint64_t kHwrpbPageSize        = 40;   // +40  page size (8192)
constexpr uint64_t kHwrpbPaSize          = 48;   // +48  PA size in bits (u32)
constexpr uint64_t kHwrpbExtVaSize       = 52;   // +52  extended VA size (u32)
constexpr uint64_t kHwrpbMaxValidAsn     = 56;   // +56  max valid ASN
constexpr uint64_t kHwrpbSysSerialNum    = 64;   // +64  system serial (octaword)
constexpr uint64_t kHwrpbSystemType      = 80;   // +80  SYSTYPE
constexpr uint64_t kHwrpbSystemVariation = 88;   // +88  SYSVAR
constexpr uint64_t kHwrpbSystemRevision  = 96;   // +96  system revision (4 ASCII)
constexpr uint64_t kHwrpbIntrClockFreq   = 104;  // +104 interval-clock freq
constexpr uint64_t kHwrpbCycleCountFreq  = 112;  // +112 cycle-counter freq (fallback)
constexpr uint64_t kHwrpbVptbVa          = 120;  // +120 virtual page-table base VA
constexpr uint64_t kHwrpbReservedArch    = 128;  // +128 reserved for architecture
constexpr uint64_t kHwrpbTbbOffset       = 136;  // +136 -> Translation Buffer hint Block
constexpr uint64_t kHwrpbCpuSlotCount    = 144;  // +144 number of per-CPU slots
constexpr uint64_t kHwrpbCpuSlotSize     = 152;  // +152 size of each slot (=> 0x280)
constexpr uint64_t kHwrpbCpuSlotOffset   = 160;  // +160 -> per-CPU slot array
constexpr uint64_t kHwrpbCtbCount        = 168;  // +168 number of CTBs
constexpr uint64_t kHwrpbCtbSize         = 176;  // +176 size of each CTB
constexpr uint64_t kHwrpbCtbOffset       = 184;  // +184 -> CTB table
constexpr uint64_t kHwrpbCrbOffset       = 192;  // +192 -> Console Routine Block
constexpr uint64_t kHwrpbMddtOffset      = 200;  // +200 -> Memory Data Descriptor Table
constexpr uint64_t kHwrpbCdbOffset       = 208;  // +208 -> Configuration Data Block
constexpr uint64_t kHwrpbFruOffset       = 216;  // +216 -> FRU table
constexpr uint64_t kHwrpbTermSaveVa      = 224;  // +224 save-state routine VA
constexpr uint64_t kHwrpbTermSavePv      = 232;  // +232 save-state routine PV
constexpr uint64_t kHwrpbTermRestoreVa   = 240;  // +240 restore-state routine VA
constexpr uint64_t kHwrpbTermRestorePv   = 248;  // +248 restore-state routine PV
constexpr uint64_t kHwrpbCpuRestartVa    = 256;  // +256 CPU restart routine VA
constexpr uint64_t kHwrpbCpuRestartPv    = 264;  // +264 CPU restart routine PV
constexpr uint64_t kHwrpbReservedSw      = 272;  // +272 reserved for software
constexpr uint64_t kHwrpbReservedHw      = 280;  // +280 reserved for hardware
constexpr uint64_t kHwrpbChecksum        = 288;  // +288 header checksum
constexpr uint64_t kHwrpbRxtxBlock       = 296;  // +296 RX/TX serial extension
constexpr uint64_t kHwrpbDsrdbOffset     = 312;  // +312 -> DSRDB section
constexpr uint64_t kHwrpbHeaderSize      = 320;  // total fixed-header size

// ---- Per-CPU slot field offsets (hex, AARM Table 26-4) ----
constexpr uint64_t kSlotHwpcb            = 0x000; // HWPCB (128 bytes)
constexpr uint64_t kSlotStateFlags       = 0x080; // STATE FLAGS (BIP/RC/PA/PP/.../PE-via-+184)
constexpr uint64_t kSlotPalMemLen        = 0x088; // PALcode memory length
constexpr uint64_t kSlotPalScrLen        = 0x090; // PALcode scratch length
constexpr uint64_t kSlotPalMemPa         = 0x098; // PA of PALcode memory
constexpr uint64_t kSlotPalScrPa         = 0x0A0; // PA of PALcode scratch
constexpr uint64_t kSlotPalRevision      = 0x0A8; // PALcode revision (qword in AARM)
constexpr uint64_t kSlotPalVariation     = 0x0AC; // (EmulatR splits the low half here)
constexpr uint64_t kSlotProcType         = 0x0B0; // PROCESSOR TYPE (major/minor)
constexpr uint64_t kSlotProcVariation    = 0x0B8; // PROCESSOR VARIATION (PE=bit2, IEEE=1, VAX=0)
constexpr uint64_t kSlotProcRevision     = 0x0C0; // PROCESSOR REVISION (4 ASCII)
constexpr uint64_t kSlotProcSerial       = 0x0C8; // PROCESSOR SERIAL (octaword)
constexpr uint64_t kSlotLogoutPa         = 0x0D8; // PA of logout area
constexpr uint64_t kSlotLogoutLen        = 0x0E0; // logout area length
constexpr uint64_t kSlotHaltPcbb         = 0x0E8; // HALT PCBB
constexpr uint64_t kSlotHaltPc           = 0x0F0; // HALT PC
constexpr uint64_t kSlotHaltPs           = 0x0F8; // HALT PS
constexpr uint64_t kSlotHaltArgList      = 0x100; // HALT ARGUMENT LIST (R25)
constexpr uint64_t kSlotHaltRetAddr      = 0x108; // HALT RETURN ADDRESS (R26)
constexpr uint64_t kSlotHaltProcValue    = 0x110; // HALT PROCEDURE VALUE (R27)
constexpr uint64_t kSlotReasonForHalt    = 0x118; // REASON FOR HALT
constexpr uint64_t kSlotReservedSw       = 0x120; // reserved for software
constexpr uint64_t kSlotRxtxBuffer       = 0x128; // RXTX BUFFER AREA (168 bytes)
constexpr uint64_t kSlotPalcodeAvailable = 0x1D0; // PALCODE AVAILABLE (16 qwords; [0]=fw rev)
constexpr uint64_t kSlotSwCompat         = 0x250; // +592 processor SW-compatibility
constexpr uint64_t kSlotConsoleLogPa     = 0x258; // +600 console data-log PA
constexpr uint64_t kSlotConsoleLogLen    = 0x260; // +608 console data-log length
constexpr uint64_t kSlotCacheDescriptor  = 0x268; // +616 cache descriptor
constexpr uint64_t kSlotCycleCounterFreq = 0x270; // +624 per-CPU cycle-counter freq
constexpr uint64_t kSlotSize             = 0x280; // total slot stride (640 bytes)

// ---- DSRDB section field offsets (hex; validate vs live 0x2ac0 dump) ----
constexpr uint64_t kDsrdbSmm             = 0x00;  // system marketing-model code
constexpr uint64_t kDsrdbLurtOffset      = 0x08;
constexpr uint64_t kDsrdbNameOffset      = 0x10;
constexpr uint64_t kDsrdbLurtCount       = 0x18;
constexpr uint64_t kDsrdbLurtTable       = 0x20;
constexpr uint64_t kDsrdbNameCount       = 0xC0;
constexpr uint64_t kDsrdbName            = 0xC8;
constexpr uint64_t kDsrdbSize            = 0x100;

// ----------------------------------------------------------------------------
// View unions -- overlay a raw guest-memory byte window with the typed struct.
// Host (x86_64) and guest (Alpha) are both little-endian and the structs are
// #pragma pack(1), so a direct overlay is byte-faithful (this matches what
// HwrpbBuilder already does with reinterpret_cast).  Reach a field either by
// typed member (view.fields.system_type) or by offset key over the bytes
// (peek<uint64_t>(view.raw, kHwrpbSystemType)).
// ----------------------------------------------------------------------------
union HwrpbHeaderView {
    uint8_t     raw[kHwrpbHeaderSize];   // 320 bytes
    HwrpbHeader fields;
};
static_assert(sizeof(HwrpbHeaderView) == 320, "HwrpbHeaderView overlays the 320-byte header");

union PerCpuSlotView {
    uint8_t    raw[kSlotSize];           // 640 bytes (0x280)
    PerCpuSlot fields;
};
static_assert(sizeof(PerCpuSlotView) == 0x280, "PerCpuSlotView overlays the 0x280 slot");

union DsrdbView {
    uint8_t raw[kDsrdbSize];             // 256 bytes (0x100)
    Dsrdb   fields;
};
static_assert(sizeof(DsrdbView) == 0x100, "DsrdbView overlays the 256-byte DSRDB");

// ----------------------------------------------------------------------------
// peek / poke -- typed read/write at an offset key from a region base pointer.
// memcpy-based, so they are alignment- and strict-aliasing-safe regardless of
// where the base points in guest memory.
//   uint64_t st = peek<uint64_t>(slotBase, kSlotProcVariation);
//   poke<uint64_t>(hdrBase, kHwrpbSystemVariation, 0x1805);  // DS20 SYSVAR
// ----------------------------------------------------------------------------
template <class T>
inline T peek(const uint8_t* base, uint64_t keyOffset) noexcept {
    T v{};
    std::memcpy(&v, base + keyOffset, sizeof(T));
    return v;
}
template <class T>
inline void poke(uint8_t* base, uint64_t keyOffset, T value) noexcept {
    std::memcpy(base + keyOffset, &value, sizeof(T));
}

// ---- Pin every key to its struct field so the two access paths never drift --
static_assert(kHwrpbId          == offsetof(HwrpbHeader, identifier),     "key/id");
static_assert(kHwrpbRevision    == offsetof(HwrpbHeader, revision),       "key/revision");
static_assert(kHwrpbSystemType  == offsetof(HwrpbHeader, system_type),    "key/system_type");
static_assert(kHwrpbSystemVariation == offsetof(HwrpbHeader, system_variation), "key/sysvar");
static_assert(kHwrpbCpuSlotSize == offsetof(HwrpbHeader, cpu_slot_size),  "key/cpu_slot_size");
static_assert(kHwrpbCpuSlotOffset == offsetof(HwrpbHeader, cpu_slot_offset), "key/cpu_slot_offset");
static_assert(kHwrpbDsrdbOffset == offsetof(HwrpbHeader, dsrdb_offset),   "key/dsrdb_offset");
static_assert(kSlotStateFlags   == offsetof(PerCpuSlot, state),          "key/state");
static_assert(kSlotProcVariation == offsetof(PerCpuSlot, cpu_var),       "key/proc_variation");
static_assert(kSlotReasonForHalt == offsetof(PerCpuSlot, halt_code),     "key/reason_for_halt");
static_assert(kSlotCycleCounterFreq == offsetof(PerCpuSlot, cycle_count_freq), "key/cycle_count_freq");
static_assert(kDsrdbLurtTable   == offsetof(Dsrdb, lurt_table),          "key/dsrdb_lurt_table");

}  // namespace hwrpb
}  // namespace deviceLib

#endif  // EMULATR_DEVICELIB_HWRPB_H
