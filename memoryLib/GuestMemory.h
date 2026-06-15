// ============================================================================
// memoryLib/GuestMemory.h -- sparse paged guest physical memory backing
// ============================================================================
// DESIGN: 
//   GuestMemory is now a strictly passive byte-store. It no longer contains
//   MMIO routing, PAL-scratch logic, or range-check faults. It assumes that
//   the calling Arbiter (TsunamiChipset) has already verified the address 
//   range and access validity.
// ============================================================================

#ifndef MEMORYLIB_GUESTMEMORY_H
#define MEMORYLIB_GUESTMEMORY_H

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include "coreLib/VA_types.h"

namespace memoryLib {

    // ---------------------------------------------------------------------------
    // MemStatus -- read / write outcome.
    // ---------------------------------------------------------------------------
    enum class MemStatus : uint8_t {
        Ok = 0,
        // Note: OutOfRange and BusError are deprecated here; they are 
        // now the responsibility of the SystemBus Arbiter.
        OutOfRange,
    };

    // ---------------------------------------------------------------------------
    // LockMonitor -- per-CPU LDQ_L / STQ_C reservation tracking.
    // ---------------------------------------------------------------------------
    class LockMonitor {
    public:
        static constexpr int      kMaxCPUs = 4;
        static constexpr uint64_t kCacheLineSize = 64;
        static constexpr uint64_t kCacheLineMask = ~(kCacheLineSize - 1);

        void set(int cpuId, uint64_t pa) noexcept;
        bool check(int cpuId, uint64_t pa) const noexcept;
        void clear(int cpuId) noexcept;
        void clearLine(uint64_t pa, int exceptCpu = -1) noexcept;

    private:
        struct Reservation { uint64_t pa{ 0 }; bool valid{ false }; };
        Reservation m_cpu[kMaxCPUs];
    };

    // ---------------------------------------------------------------------------
    // GuestMemory -- Dumb Byte Store (Sparse Backing)
    // ---------------------------------------------------------------------------
    class GuestMemory {
    public:
        static constexpr uint64_t  kPageSize = 64ULL * 1024ULL;
        static constexpr uint64_t  kPageMask = kPageSize - 1ULL;
        static constexpr uint32_t  kPageShift = 16;
        static constexpr uint32_t  kPagesPerDirty = 64;

        explicit GuestMemory(uint64_t sizeBytes = 64ULL * 1024ULL * 1024ULL);
        ~GuestMemory() noexcept;

        // -----------------------------------------------------------------------
        // ACCESS API: Trusted direct access.
        // Callers MUST ensure PA is within configured DRAM bounds before call.
        // -----------------------------------------------------------------------

        [[nodiscard]] MemStatus read1(coreLib::PAType pa, uint8_t& out) const noexcept;
        [[nodiscard]] MemStatus read2(coreLib::PAType pa, uint16_t& out) const noexcept;
        [[nodiscard]] MemStatus read4(coreLib::PAType pa, uint32_t& out) const noexcept;
        [[nodiscard]] MemStatus read8(coreLib::PAType pa, uint64_t& out) const noexcept;

        MemStatus write1(coreLib::PAType pa, uint8_t value) noexcept;
        MemStatus write2(coreLib::PAType pa, uint16_t value) noexcept;
        MemStatus write4(coreLib::PAType pa, uint32_t value) noexcept;
        MemStatus write8(coreLib::PAType pa, uint64_t value) noexcept;

        // Returns the total configured size of the guest physical memory in bytes.
        [[nodiscard]] uint64_t sizeBytes() const noexcept { return m_size; }

        // -----------------------------------------------------------------------
        // LockMonitor access.
        // -----------------------------------------------------------------------
        LockMonitor& lockMonitor()       noexcept { return m_locks; }

        // ---------------------------------------------------------------------------
        // forEachPage -- Iterate over all allocated pages and execute callback
        // ---------------------------------------------------------------------------
        template <typename Fn>
        void forEachPage(Fn&& cb) const noexcept {
            for (uint32_t i = 0; i < m_pageCount; ++i) {
                if (m_pages[i]) {
                    cb(i, m_pages[i]);
                }
            }
        }

        [[nodiscard]] uint8_t*      ensurePage(uint32_t pidx) noexcept;
        bool                        writeBlock(uint64_t pa, void const* src, uint64_t len) noexcept;
        bool                        readBlock(uint64_t pa, void* dst, uint64_t len) const noexcept;
        [[nodiscard]] bool          isDirty(uint32_t pidx) const noexcept;
        void                        clearDirty() const noexcept;

    private:
        inline void markDirty(uint32_t pidx) const noexcept {
            m_dirtyBitmap[pidx / kPagesPerDirty] |= (1ULL << (pidx % kPagesPerDirty));
        }

        // Counter of allocated pages -- atomic because ensurePage is
        // CAS-based and may be called from multiple threads in a future
        // SMP world.  Read by `allocatedPages()` for diagnostics.
        std::atomic<uint32_t> m_allocatedPages{ 0 };
        uint64_t    m_size = 0;
        uint8_t** m_pages = nullptr;
        uint32_t    m_pageCount = 0;
        uint8_t* m_zeroSentinel = nullptr;
        // Dirty bitmap: one bit per page, packed into uint64s.  Size
        // m_dirtyWordCount uint64s = ceil(m_pageCount / 64).
        uint64_t* m_dirtyBitmap = nullptr;
        uint32_t  m_dirtyWordCount = 0;
        LockMonitor m_locks;
    };

} // namespace memoryLib

#endif