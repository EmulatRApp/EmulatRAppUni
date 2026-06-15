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
        uint32_t pidx = static_cast<uint32_t>(pa >> kPageShift);
        uint32_t offset = static_cast<uint32_t>(pa & kPageMask);

        if (pidx >= m_pageCount || !m_pages[pidx]) { out = 0; return MemStatus::Ok; }
        std::memcpy(&out, &m_pages[pidx][offset], 2);
        return MemStatus::Ok;
    }

    MemStatus GuestMemory::read4(coreLib::PAType pa, uint32_t& out) const noexcept {
        uint32_t pidx = static_cast<uint32_t>(pa >> kPageShift);
        uint32_t offset = static_cast<uint32_t>(pa & kPageMask);

        if (pidx >= m_pageCount || !m_pages[pidx]) { out = 0; return MemStatus::Ok; }
        std::memcpy(&out, &m_pages[pidx][offset], 4);
        return MemStatus::Ok;
    }

    MemStatus GuestMemory::read8(coreLib::PAType pa, uint64_t& out) const noexcept {
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
        return MemStatus::Ok;
    }

    MemStatus GuestMemory::write2(coreLib::PAType pa, uint16_t value) noexcept {
        uint32_t pidx = static_cast<uint32_t>(pa >> kPageShift);
        uint8_t* page = ensurePage(pidx);
        if (!page) return MemStatus::OutOfRange;

        std::memcpy(&page[pa & kPageMask], &value, 2);
        markDirty(pidx);
        return MemStatus::Ok;
    }

    MemStatus GuestMemory::write4(coreLib::PAType pa, uint32_t value) noexcept {
        uint32_t pidx = static_cast<uint32_t>(pa >> kPageShift);
        uint8_t* page = ensurePage(pidx);
        if (!page) return MemStatus::OutOfRange;

        std::memcpy(&page[pa & kPageMask], &value, 4);
        markDirty(pidx);
        return MemStatus::Ok;
    }

    MemStatus GuestMemory::write8(coreLib::PAType pa, uint64_t value) noexcept {
        uint32_t pidx = static_cast<uint32_t>(pa >> kPageShift);
        uint8_t* page = ensurePage(pidx);
        if (!page) return MemStatus::OutOfRange;

        std::memcpy(&page[pa & kPageMask], &value, 8);
        markDirty(pidx);
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

    

} // namespace memoryLib