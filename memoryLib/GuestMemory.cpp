// ============================================================================
// memoryLib/GuestMemory.cpp -- contiguous backing implementation
// ============================================================================
// DESIGN:
//   This class acts strictly as a "Dumb Byte Store" over ONE contiguous,
//   eagerly-reserved host region (VirtualAlloc / mmap / calloc).  The calling
//   TsunamiChipset (or Arbiter) performs MMIO/PAL dispatch and DRAM routing;
//   this layer only reads/writes bytes and enforces the sizeBytes() bound.
//
//   2026-07-19 (T. Peer decision; Claude / Cowork): the sparse, lazily-
//   allocated 64 KiB page table was ripped out in favour of a single
//   contiguous region.  See GuestMemory.h header block for the full rationale.
//   Out-of-range reads now fault (MemStatus::OutOfRange) instead of returning a
//   silent zero, and the 64 KiB "seam" byte-wise special-casing is gone.
// ============================================================================

#include "memoryLib/GuestMemory.h"
#include <cstdio>
#include <cstring>

#if defined(EMULATR_DIAGNOSTIC_LOGGING)
#  include <cstdlib>                       // std::getenv / std::strtoull
#  include "traceLib/DecListingSink.h"     // setTraceWindowCountdown (retire-window arm)
#endif

#if defined(EMULATR_USE_OS_PAGES)
#  if defined(_WIN32)
#    ifndef WIN32_LEAN_AND_MEAN
#      define WIN32_LEAN_AND_MEAN
#    endif
#    include <windows.h>
#  else
#    include <sys/mman.h>
#  endif
#endif

namespace memoryLib {

    namespace {

        // allocRegion / freeRegion -- one contiguous, zero-filled host region
        // of `bytes`.  VirtualAlloc(MEM_RESERVE|MEM_COMMIT) and mmap
        // (MAP_ANONYMOUS) both guarantee zero pages that the OS lazily backs
        // physically on first touch; the calloc fallback zeroes eagerly.
        uint8_t* allocRegion(uint64_t bytes) noexcept {
#if defined(EMULATR_USE_OS_PAGES)
#  if defined(_WIN32)
            return static_cast<uint8_t*>(
                VirtualAlloc(nullptr, static_cast<SIZE_T>(bytes),
                    MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
#  else
            void* p = mmap(nullptr, static_cast<size_t>(bytes),
                PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            return (p == MAP_FAILED) ? nullptr : static_cast<uint8_t*>(p);
#  endif
#else
            return static_cast<uint8_t*>(std::calloc(1, static_cast<size_t>(bytes)));
#endif
        }

        void freeRegion(uint8_t* p, uint64_t bytes) noexcept {
            if (!p) return;
#if defined(EMULATR_USE_OS_PAGES)
#  if defined(_WIN32)
            (void)bytes;                    // MEM_RELEASE requires size 0
            VirtualFree(p, 0, MEM_RELEASE);
#  else
            munmap(p, static_cast<size_t>(bytes));
#  endif
#else
            (void)bytes;
            std::free(p);
#endif
        }

    } // anonymous namespace

    GuestMemory::GuestMemory(uint64_t sizeBytes) : m_size(sizeBytes) {
        if (m_size == 0) return;

        m_pageCount = static_cast<uint32_t>((m_size + kPageMask) / kPageSize);
        // Reserve+commit ONE contiguous region, rounded up to a whole 64 KiB
        // page so forEachPage/ensurePage chunk pointers are always valid.  The
        // OS zero-fills and lazily backs untouched pages, so this is not a 4 GiB
        // eager physical allocation -- only the touched pages consume RAM.
        m_allocBytes = static_cast<uint64_t>(m_pageCount) * kPageSize;
        m_base = allocRegion(m_allocBytes);
        if (!m_base) throw std::bad_alloc{};

        m_dirtyWordCount = (m_pageCount + kPagesPerDirty - 1) / kPagesPerDirty;
        m_dirtyBitmap = static_cast<uint64_t*>(std::calloc(m_dirtyWordCount, sizeof(uint64_t)));
        if (!m_dirtyBitmap) {
            freeRegion(m_base, m_allocBytes);
            m_base = nullptr;
            throw std::bad_alloc{};
        }
    }

    GuestMemory::~GuestMemory() noexcept {
        if (m_base) freeRegion(m_base, m_allocBytes);
        if (m_dirtyBitmap) std::free(m_dirtyBitmap);
    }

    // ensurePage -- compatibility shim over the contiguous region.  Every page
    // in [0, m_pageCount) is backed; return its chunk pointer, or nullptr past
    // the end.  No allocation happens here anymore (the region is eager).
    uint8_t* GuestMemory::ensurePage(uint32_t pidx) noexcept {
        if (pidx >= m_pageCount || !m_base) return nullptr;
        return m_base + (static_cast<uint64_t>(pidx) << kPageShift);
    }

    // Bulk memory operations now trust that the caller is operating within 
    // valid DRAM bounds.
#if defined(EMULATR_DIAGNOSTIC_LOGGING)
    // ------------------------------------------------------------------------
    // gmemDiagOnStore -- observe-only diagnostic hook at the universal DRAM
    // commit sink.  Catches stores that bypass the MemDrainer drain-watch
    // (e.g. the HWRPB build at PA 0x2000).  Runtime default-OFF; absent in
    // Release (guarded).  See journals/20260629_guestmemory_diag_instrumentation.md.
    //   EMULATR_GMEM_WATCH=<pa>        log every store overlapping that quadword
    //   EMULATR_GMEM_WATCH_LO=<pa>     with _HI: log every store landing in the
    //   EMULATR_GMEM_WATCH_HI=<pa>     half-open PA range [LO,HI) (page/table watch;
    //                                  2026-07-07, ES40 SCB-page install probe)
    //   EMULATR_TRACE_ARM_PA=<pa>      arm the retire .trc window on a store to <pa>
    //   EMULATR_TRACE_ARM_VAL=<v>      (optional) only when the stored value == v
    //   EMULATR_TRACE_ARM_INSTRS=<n>   window length in retired instrs (default 8M)
    //   EMULATR_TRACE_DISARM_PA=<pa>   disarm (close) the window on a store to <pa>
    //   EMULATR_TRACE_DISARM_VAL=<v>   (optional) only when the stored value == v
    // The disarm is a hard backstop bound for a window armed elsewhere (e.g. the
    // IIC-model arm, EMULATR_TRACE_ARM_ON_IIC): a store to <pa> sets the retire
    // countdown to 0 so the .trc stops at, e.g., the HWRPB base store (0x2000).
    // ------------------------------------------------------------------------
    static inline void gmemDiagOnStore(uint64_t pa, uint64_t value,
                                       unsigned size) noexcept {
        static uint64_t const s_watchPa = []() -> uint64_t {
            char const* e = std::getenv("EMULATR_GMEM_WATCH");
            return (e && *e) ? std::strtoull(e, nullptr, 0) : 0ULL; }();
        static uint64_t const s_armPa = []() -> uint64_t {
            char const* e = std::getenv("EMULATR_TRACE_ARM_PA");
            return (e && *e) ? std::strtoull(e, nullptr, 0) : 0ULL; }();
        static bool const s_armValSet =
            (std::getenv("EMULATR_TRACE_ARM_VAL") != nullptr);
        static uint64_t const s_armVal = []() -> uint64_t {
            char const* e = std::getenv("EMULATR_TRACE_ARM_VAL");
            return (e && *e) ? std::strtoull(e, nullptr, 0) : 0ULL; }();
        static int64_t const s_armInstrs = []() -> int64_t {
            char const* e = std::getenv("EMULATR_TRACE_ARM_INSTRS");
            return (e && *e) ? std::strtoll(e, nullptr, 0) : 8000000LL; }();
        static uint64_t const s_disarmPa = []() -> uint64_t {
            char const* e = std::getenv("EMULATR_TRACE_DISARM_PA");
            return (e && *e) ? std::strtoull(e, nullptr, 0) : 0ULL; }();
        static bool const s_disarmValSet =
            (std::getenv("EMULATR_TRACE_DISARM_VAL") != nullptr);
        static uint64_t const s_disarmVal = []() -> uint64_t {
            char const* e = std::getenv("EMULATR_TRACE_DISARM_VAL");
            return (e && *e) ? std::strtoull(e, nullptr, 0) : 0ULL; }();

        if (s_watchPa != 0ULL) {
            uint64_t const qw  = s_watchPa & ~7ULL;
            uint64_t const sHi = pa + (size ? size : 1u);
            if (pa < qw + 8ULL && sHi > qw) {
                std::fprintf(stderr,
                    "GMEM-WATCH(0x%llx) STORE pa=0x%llx sz=%u v=0x%llx\n",
                    (unsigned long long)s_watchPa, (unsigned long long)pa,
                    size, (unsigned long long)value);
                std::fflush(stderr);
            }
        }
        // 2026-07-07: half-open PA range watch (ES40 SCB-page install probe).
        // Logs every store landing in [LO,HI); catches an install store at ANY
        // offset in the table page without needing to know the stored value.
        static uint64_t const s_watchLo = []() -> uint64_t {
            char const* e = std::getenv("EMULATR_GMEM_WATCH_LO");
            return (e && *e) ? std::strtoull(e, nullptr, 0) : 0ULL; }();
        static uint64_t const s_watchHi = []() -> uint64_t {
            char const* e = std::getenv("EMULATR_GMEM_WATCH_HI");
            return (e && *e) ? std::strtoull(e, nullptr, 0) : 0ULL; }();
        if (s_watchHi != 0ULL) {
            uint64_t const sHi = pa + (size ? size : 1u);
            if (pa < s_watchHi && sHi > s_watchLo) {
                std::fprintf(stderr,
                    "GMEM-WATCH-RANGE[0x%llx..0x%llx) STORE pa=0x%llx sz=%u v=0x%llx\n",
                    (unsigned long long)s_watchLo, (unsigned long long)s_watchHi,
                    (unsigned long long)pa, size, (unsigned long long)value);
                std::fflush(stderr);
            }
        }
        // 2026-07-08: VALUE-keyed store probe (ES40 SCB vector-install split,
        // task #29 Step 1).  The range watch above keys on the target PA; this
        // keys on the stored VALUE so it catches the vector-install store at ANY
        // PA the kseg/superpage decode lands it on (splits Q2a "never installed"
        // vs Q2b-far "installed at a PA outside the watched page").  Two run keys:
        //   EMULATR_VECWATCH_VAL=0x1038000  -> whoever caches the SCB base
        //   EMULATR_VECWATCH_VAL=<CLK_ISR>  -> the install store itself
        // The (v & ~3) mask mirrors the console PAL's BIC Rx,#3 PAL-bit tagging
        // (the low 2 bits carry the PAL/mode flag, not part of the target PC).
        // Env-gated, zero-cost when unset; guarded by EMULATR_DIAGNOSTIC_LOGGING.
        static bool const s_vecValSet =
            (std::getenv("EMULATR_VECWATCH_VAL") != nullptr);
        static uint64_t const s_vecVal = []() -> uint64_t {
            char const* e = std::getenv("EMULATR_VECWATCH_VAL");
            return (e && *e) ? std::strtoull(e, nullptr, 0) : 0ULL; }();
        if (s_vecValSet && ((value & ~3ULL) == (s_vecVal & ~3ULL))) {
            std::fprintf(stderr,
                "VECWATCH-VAL(0x%llx) STORE pa=0x%llx sz=%u v=0x%llx\n",
                (unsigned long long)s_vecVal, (unsigned long long)pa,
                size, (unsigned long long)value);
            std::fflush(stderr);
        }
        if (s_armPa != 0ULL && pa == s_armPa &&
            (!s_armValSet || value == s_armVal)) {
            traceLib::DecListingSink::setTraceWindowCountdown(s_armInstrs);
            std::fprintf(stderr,
                "GMEM-TRACE-ARM pa=0x%llx v=0x%llx -> retire window %lld instrs\n",
                (unsigned long long)pa, (unsigned long long)value,
                (long long)s_armInstrs);
            std::fflush(stderr);
        }
        if (s_disarmPa != 0ULL && pa == s_disarmPa &&
            (!s_disarmValSet || value == s_disarmVal)) {
            traceLib::DecListingSink::setTraceWindowCountdown(0);
            std::fprintf(stderr,
                "GMEM-TRACE-DISARM pa=0x%llx v=0x%llx -> retire window closed\n",
                (unsigned long long)pa, (unsigned long long)value);
            std::fflush(stderr);
        }
    }
#endif

    // Bulk helpers over the contiguous region.  writeBlock enforces the size
    // bound; readBlock zero-fills any portion at/after m_size so the snapshot
    // save (which walks the full logical span) never faults on the rounded tail.
    bool GuestMemory::writeBlock(uint64_t pa, void const* src, uint64_t len) noexcept {
        if (len == 0) return true;
        if (!m_base || pa >= m_size || len > m_size - pa) return false;
        std::memcpy(m_base + pa, src, static_cast<size_t>(len));
        uint32_t const first = static_cast<uint32_t>(pa >> kPageShift);
        uint32_t const last  = static_cast<uint32_t>((pa + len - 1) >> kPageShift);
        for (uint32_t p = first; p <= last; ++p) markDirty(p);
#if defined(EMULATR_DIAGNOSTIC_LOGGING)
        gmemDiagOnStore(pa, 0ULL, static_cast<unsigned>(len < 8 ? len : 8));
#endif
        return true;
    }

    bool GuestMemory::readBlock(uint64_t pa, void* dst, uint64_t len) const noexcept {
        if (len == 0) return true;
        uint8_t* dp = static_cast<uint8_t*>(dst);
        uint64_t const inRange =
            (!m_base || pa >= m_size) ? 0ULL
                                      : ((len < m_size - pa) ? len : m_size - pa);
        if (inRange) std::memcpy(dp, m_base + pa, static_cast<size_t>(inRange));
        if (inRange < len)
            std::memset(dp + inRange, 0, static_cast<size_t>(len - inRange));
        return true;
    }

    // --- Reads (contiguous backing; PA at/beyond m_size faults OutOfRange) ---
    // A multi-byte access no longer needs the 64 KiB "seam" byte-wise fallback:
    // the region is contiguous, so a single memcpy spans any in-range offset.

    MemStatus GuestMemory::read1(coreLib::PAType pa, uint8_t& out) const noexcept {
        if (pa >= m_size) { out = 0; return MemStatus::OutOfRange; }
        out = m_base[pa];
        return MemStatus::Ok;
    }

    MemStatus GuestMemory::read2(coreLib::PAType pa, uint16_t& out) const noexcept {
        if (pa >= m_size || 2u > m_size - pa) { out = 0; return MemStatus::OutOfRange; }
        std::memcpy(&out, m_base + pa, 2);
        return MemStatus::Ok;
    }

    MemStatus GuestMemory::read4(coreLib::PAType pa, uint32_t& out) const noexcept {
        if (pa >= m_size || 4u > m_size - pa) { out = 0; return MemStatus::OutOfRange; }
        std::memcpy(&out, m_base + pa, 4);
        return MemStatus::Ok;
    }

    MemStatus GuestMemory::read8(coreLib::PAType pa, uint64_t& out) const noexcept {
        if (pa >= m_size || 8u > m_size - pa) { out = 0; return MemStatus::OutOfRange; }
        std::memcpy(&out, m_base + pa, 8);
        return MemStatus::Ok;
    }

    // --- Writes (contiguous backing; PA at/beyond m_size faults OutOfRange) ---

    MemStatus GuestMemory::write1(coreLib::PAType pa, uint8_t value) noexcept {
        if (pa >= m_size) return MemStatus::OutOfRange;
        m_base[pa] = value;
        markDirty(static_cast<uint32_t>(pa >> kPageShift));
#if defined(EMULATR_DIAGNOSTIC_LOGGING)
        gmemDiagOnStore(pa, value, 1u);
#endif
        return MemStatus::Ok;
    }

    MemStatus GuestMemory::write2(coreLib::PAType pa, uint16_t value) noexcept {
        if (pa >= m_size || 2u > m_size - pa) return MemStatus::OutOfRange;
        std::memcpy(m_base + pa, &value, 2);
        markDirty(static_cast<uint32_t>(pa >> kPageShift));
        markDirty(static_cast<uint32_t>((pa + 1) >> kPageShift));
#if defined(EMULATR_DIAGNOSTIC_LOGGING)
        gmemDiagOnStore(pa, value, 2u);
#endif
        return MemStatus::Ok;
    }

    MemStatus GuestMemory::write4(coreLib::PAType pa, uint32_t value) noexcept {
        if (pa >= m_size || 4u > m_size - pa) return MemStatus::OutOfRange;
        std::memcpy(m_base + pa, &value, 4);
        markDirty(static_cast<uint32_t>(pa >> kPageShift));
        markDirty(static_cast<uint32_t>((pa + 3) >> kPageShift));
#if defined(EMULATR_DIAGNOSTIC_LOGGING)
        gmemDiagOnStore(pa, value, 4u);
#endif
        return MemStatus::Ok;
    }

    MemStatus GuestMemory::write8(coreLib::PAType pa, uint64_t value) noexcept {
        if (pa >= m_size || 8u > m_size - pa) return MemStatus::OutOfRange;
        std::memcpy(m_base + pa, &value, 8);
        markDirty(static_cast<uint32_t>(pa >> kPageShift));
        markDirty(static_cast<uint32_t>((pa + 7) >> kPageShift));
#if defined(EMULATR_DIAGNOSTIC_LOGGING)
        gmemDiagOnStore(pa, value, 8u);
#endif
        return MemStatus::Ok;
    }


    // ---------------------------------------------------------------------------
    // isDirty -- Check if a specific page index is marked as modified
    // ---------------------------------------------------------------------------
    [[nodiscard]] bool GuestMemory::isDirty(uint32_t pidx) const noexcept {
        if (pidx >= m_pageCount) return false;
        uint64_t word = m_dirtyBitmap[pidx / kPagesPerDirty];
        return (word & (1ULL << (pidx % kPagesPerDirty))) != 0;
    }

    // ---------------------------------------------------------------------------
    // clearDirty -- Reset the entire dirty bitmap
    // ---------------------------------------------------------------------------
    void GuestMemory::clearDirty() const noexcept {
        std::memset(m_dirtyBitmap, 0, m_dirtyWordCount * sizeof(uint64_t));
    }

    // ===========================================================================
    // LockMonitor -- per-CPU LDx_L / STx_C reservation table (SSOT).
    // ===========================================================================
    // Semantics ported from the validated schedLib::LockArbiter (Phase 3) and
    // promoted here because this is the memory boundary every writer crosses --
    // CPU stores AND device/DMA writes -- so cross-CPU invalidation can be hooked
    // in one place.  Real LL/SC: multiple CPUs may hold a reservation on the SAME
    // cache line at once; a LOAD never clears another CPU's reservation, only a
    // STORE does.  Granularity is the 64-byte EV6 cache line: LDx_L of any byte in
    // a line reserves the whole line; STx_C to any byte of that line matches.
    //
    //   set(cpuId, pa)        : arm ONLY cpuId's reservation on pa's line.
    //   check(cpuId, pa)      : true iff cpuId's reservation is valid and names
    //                           pa's line.  Pure query; does not mutate.
    //   clear(cpuId)          : drop cpuId's reservation (e.g. a failed STx_C, or
    //                           the self-clear after a successful one).
    //   clearLine(pa, except) : drop EVERY CPU's reservation on pa's line except
    //                           `except` (a CPU's own plain store passes its id so
    //                           it does not self-invalidate; device/DMA writes pass
    //                           the default -1 to clear all).  This is the cross-CPU
    //                           invalidation hook.
    //
    // Out-of-range cpuId is ignored (set/clear) or reported false (check) so a
    // mis-wired caller can never index out of bounds.
    // ---------------------------------------------------------------------------
    void LockMonitor::set(int cpuId, uint64_t pa) noexcept {
        if (cpuId < 0 || cpuId >= kMaxCPUs) return;
        m_cpu[cpuId].pa    = pa & kCacheLineMask;
        m_cpu[cpuId].valid = true;
    }

    bool LockMonitor::check(int cpuId, uint64_t pa) const noexcept {
        if (cpuId < 0 || cpuId >= kMaxCPUs) return false;
        return m_cpu[cpuId].valid
            && m_cpu[cpuId].pa == (pa & kCacheLineMask);
    }

    void LockMonitor::clear(int cpuId) noexcept {
        if (cpuId < 0 || cpuId >= kMaxCPUs) return;
        m_cpu[cpuId].valid = false;
    }

    void LockMonitor::clearLine(uint64_t pa, int exceptCpu) noexcept {
        uint64_t const line = pa & kCacheLineMask;
        for (int i = 0; i < kMaxCPUs; ++i) {
            if (i == exceptCpu) continue;
            if (m_cpu[i].valid && m_cpu[i].pa == line)
                m_cpu[i].valid = false;
        }
    }

} // namespace memoryLib