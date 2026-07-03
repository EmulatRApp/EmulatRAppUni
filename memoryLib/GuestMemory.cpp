// ============================================================================
// memoryLib/GuestMemory.cpp -- sparse paged backing implementation
// ============================================================================
// DESIGN:
//   This class now acts strictly as a "Dumb Byte Store". It assumes the 
//   calling TsunamiChipset (or Arbiter) has performed all range checking 
//   and MMIO/PAL dispatch logic. PAL scratchpad and MMIO hook logic have 
//   been removed to fulfill the strict architectural layering.
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

        uint8_t* allocPage() noexcept {
#if defined(EMULATR_USE_OS_PAGES)
#  if defined(_WIN32)
            return static_cast<uint8_t*>(
                VirtualAlloc(nullptr, static_cast<SIZE_T>(GuestMemory::kPageSize),
                    MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
#  else
            void* p = mmap(nullptr, static_cast<size_t>(GuestMemory::kPageSize),
                PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            return (p == MAP_FAILED) ? nullptr : static_cast<uint8_t*>(p);
#  endif
#else
            return static_cast<uint8_t*>(std::calloc(1, static_cast<size_t>(GuestMemory::kPageSize)));
#endif
        }

        void freePage(uint8_t* p) noexcept {
            if (!p) return;
#if defined(EMULATR_USE_OS_PAGES)
#  if defined(_WIN32)
            VirtualFree(p, 0, MEM_RELEASE);
#  else
            munmap(p, static_cast<size_t>(GuestMemory::kPageSize));
#  endif
#else
            std::free(p);
#endif
        }

    } // anonymous namespace

    GuestMemory::GuestMemory(uint64_t sizeBytes) : m_size(sizeBytes) {
        if (m_size == 0) return;

        m_pageCount = static_cast<uint32_t>((m_size + kPageMask) / kPageSize);
        m_pages = static_cast<uint8_t**>(std::calloc(m_pageCount, sizeof(uint8_t*)));

        if (!m_pages) throw std::bad_alloc{};

        m_zeroSentinel = allocPage();
        if (!m_zeroSentinel) {
            std::free(m_pages);
            throw std::bad_alloc{};
        }

        m_dirtyWordCount = (m_pageCount + kPagesPerDirty - 1) / kPagesPerDirty;
        m_dirtyBitmap = static_cast<uint64_t*>(std::calloc(m_dirtyWordCount, sizeof(uint64_t)));
        if (!m_dirtyBitmap) {
            freePage(m_zeroSentinel);
            std::free(m_pages);
            throw std::bad_alloc{};
        }
    }

    GuestMemory::~GuestMemory() noexcept {
        if (m_pages) {
            for (uint32_t i = 0; i < m_pageCount; ++i) {
                if (m_pages[i] && m_pages[i] != m_zeroSentinel) freePage(m_pages[i]);
            }
            std::free(m_pages);
        }
        if (m_zeroSentinel) freePage(m_zeroSentinel);
        if (m_dirtyBitmap) std::free(m_dirtyBitmap);
    }

    // ensurePage implementation remains mostly unchanged, but note that 
    // it no longer needs to worry about PAL or MMIO regions.
    uint8_t* GuestMemory::ensurePage(uint32_t pidx) noexcept {
        if (pidx >= m_pageCount) return nullptr;
        if (uint8_t* existing = m_pages[pidx]) return existing;

        uint8_t* newPage = allocPage();
        if (!newPage) return nullptr;

        auto* slot = reinterpret_cast<std::atomic<uint8_t*>*>(&m_pages[pidx]);
        uint8_t* expected = nullptr;
        if (slot->compare_exchange_strong(expected, newPage, std::memory_order_release, std::memory_order_acquire)) {
            m_allocatedPages.fetch_add(1, std::memory_order_relaxed);
            return newPage;
        }
        freePage(newPage);
        return m_pages[pidx];
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

    bool GuestMemory::writeBlock(uint64_t pa, void const* src, uint64_t len) noexcept {
        uint8_t const* sp = static_cast<uint8_t const*>(src);
        uint64_t remaining = len;
        uint64_t addr = pa;

        while (remaining > 0) {
            uint32_t const pidx = static_cast<uint32_t>(addr >> kPageShift);
            uint64_t const offset = addr & kPageMask;
            uint64_t const avail = kPageSize - offset;
            uint64_t const chunk = (remaining < avail) ? remaining : avail;

            uint8_t* page = ensurePage(pidx);
            if (!page) return false;
            std::memcpy(page + offset, sp, chunk);
            markDirty(pidx);
#if defined(EMULATR_DIAGNOSTIC_LOGGING)
            gmemDiagOnStore(addr, 0ULL, static_cast<unsigned>(chunk));
#endif

            sp += chunk; addr += chunk; remaining -= chunk;
        }
        return true;
    }

    bool GuestMemory::readBlock(uint64_t pa, void* dst, uint64_t len) const noexcept {
        uint8_t* dp = static_cast<uint8_t*>(dst);
        uint64_t remaining = len;
        uint64_t addr = pa;

        while (remaining > 0) {
            uint32_t const pidx = static_cast<uint32_t>(addr >> kPageShift);
            uint64_t const offset = addr & kPageMask;
            uint64_t const avail = kPageSize - offset;
            uint64_t const chunk = (remaining < avail) ? remaining : avail;

            uint8_t const* page = m_pages[pidx];
            if (page) std::memcpy(dp, page + offset, chunk);
            else std::memset(dp, 0, chunk);

            dp += chunk; addr += chunk; remaining -= chunk;
        }
        return true;
    }

    // --- Reads ---

    MemStatus GuestMemory::read1(coreLib::PAType pa, uint8_t& out) const noexcept {
        uint32_t pidx = static_cast<uint32_t>(pa >> kPageShift);
        uint32_t offset = static_cast<uint32_t>(pa & kPageMask);

        if (pidx >= m_pageCount || !m_pages[pidx]) { out = 0; return MemStatus::Ok; }
        out = m_pages[pidx][offset];
        return MemStatus::Ok;
    }

    MemStatus GuestMemory::read2(coreLib::PAType pa, uint16_t& out) const noexcept {
        if ((pa & kPageMask) > kPageMask - 1u) {   // crosses a 64KB page boundary
            uint16_t v = 0;
            for (unsigned i = 0; i < 2u; ++i) {
                uint8_t b = 0; (void)read1(pa + i, b);
                v |= static_cast<uint16_t>(static_cast<uint16_t>(b) << (8u * i));
            }
            out = v;
            return MemStatus::Ok;
        }
        uint32_t pidx = static_cast<uint32_t>(pa >> kPageShift);
        uint32_t offset = static_cast<uint32_t>(pa & kPageMask);

        if (pidx >= m_pageCount || !m_pages[pidx]) { out = 0; return MemStatus::Ok; }
        std::memcpy(&out, &m_pages[pidx][offset], 2);
        return MemStatus::Ok;
    }

    MemStatus GuestMemory::read4(coreLib::PAType pa, uint32_t& out) const noexcept {
        if ((pa & kPageMask) > kPageMask - 3u) {   // crosses a 64KB page boundary
            uint32_t v = 0;
            for (unsigned i = 0; i < 4u; ++i) {
                uint8_t b = 0; (void)read1(pa + i, b);
                v |= static_cast<uint32_t>(b) << (8u * i);
            }
            out = v;
            return MemStatus::Ok;
        }
        uint32_t pidx = static_cast<uint32_t>(pa >> kPageShift);
        uint32_t offset = static_cast<uint32_t>(pa & kPageMask);

        if (pidx >= m_pageCount || !m_pages[pidx]) { out = 0; return MemStatus::Ok; }
        std::memcpy(&out, &m_pages[pidx][offset], 4);
        return MemStatus::Ok;
    }

    MemStatus GuestMemory::read8(coreLib::PAType pa, uint64_t& out) const noexcept {
        if ((pa & kPageMask) > kPageMask - 7u) {   // crosses a 64KB page boundary
            uint64_t v = 0;
            for (unsigned i = 0; i < 8u; ++i) {
                uint8_t b = 0; (void)read1(pa + i, b);
                v |= static_cast<uint64_t>(b) << (8u * i);
            }
            out = v;
            return MemStatus::Ok;
        }
        uint32_t pidx = static_cast<uint32_t>(pa >> kPageShift);
        uint32_t offset = static_cast<uint32_t>(pa & kPageMask);

        if (pidx >= m_pageCount || !m_pages[pidx]) { out = 0; return MemStatus::Ok; }
        std::memcpy(&out, &m_pages[pidx][offset], 8);
        return MemStatus::Ok;
    }

    // --- Writes ---

    MemStatus GuestMemory::write1(coreLib::PAType pa, uint8_t value) noexcept {
        uint32_t pidx = static_cast<uint32_t>(pa >> kPageShift);
        uint8_t* page = ensurePage(pidx);
        if (!page) return MemStatus::OutOfRange;

        page[pa & kPageMask] = value;
        markDirty(pidx);
#if defined(EMULATR_DIAGNOSTIC_LOGGING)
        gmemDiagOnStore(pa, value, 1u);
#endif
        return MemStatus::Ok;
    }

    MemStatus GuestMemory::write2(coreLib::PAType pa, uint16_t value) noexcept {
        if ((pa & kPageMask) > kPageMask - 1u) {   // crosses a 64KB page boundary
            for (unsigned i = 0; i < 2u; ++i)
                (void)write1(pa + i, static_cast<uint8_t>(value >> (8u * i)));
            return MemStatus::Ok;
        }
        uint32_t pidx = static_cast<uint32_t>(pa >> kPageShift);
        uint8_t* page = ensurePage(pidx);
        if (!page) return MemStatus::OutOfRange;

        std::memcpy(&page[pa & kPageMask], &value, 2);
        markDirty(pidx);
#if defined(EMULATR_DIAGNOSTIC_LOGGING)
        gmemDiagOnStore(pa, value, 2u);
#endif
        return MemStatus::Ok;
    }

    MemStatus GuestMemory::write4(coreLib::PAType pa, uint32_t value) noexcept {
        if ((pa & kPageMask) > kPageMask - 3u) {   // crosses a 64KB page boundary
            for (unsigned i = 0; i < 4u; ++i)
                (void)write1(pa + i, static_cast<uint8_t>(value >> (8u * i)));
            return MemStatus::Ok;
        }
        uint32_t pidx = static_cast<uint32_t>(pa >> kPageShift);
        uint8_t* page = ensurePage(pidx);
        if (!page) return MemStatus::OutOfRange;

        std::memcpy(&page[pa & kPageMask], &value, 4);
        markDirty(pidx);
#if defined(EMULATR_DIAGNOSTIC_LOGGING)
        gmemDiagOnStore(pa, value, 4u);
#endif
        return MemStatus::Ok;
    }

    MemStatus GuestMemory::write8(coreLib::PAType pa, uint64_t value) noexcept {
        if ((pa & kPageMask) > kPageMask - 7u) {   // crosses a 64KB page boundary
            for (unsigned i = 0; i < 8u; ++i)
                (void)write1(pa + i, static_cast<uint8_t>(value >> (8u * i)));
            return MemStatus::Ok;
        }
        uint32_t pidx = static_cast<uint32_t>(pa >> kPageShift);
        uint8_t* page = ensurePage(pidx);
        if (!page) return MemStatus::OutOfRange;

        std::memcpy(&page[pa & kPageMask], &value, 8);
        markDirty(pidx);
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