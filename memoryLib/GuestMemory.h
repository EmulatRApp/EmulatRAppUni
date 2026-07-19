// ============================================================================
// memoryLib/GuestMemory.h -- contiguous guest physical memory backing
// ============================================================================
// DESIGN:
//   GuestMemory is a strictly passive byte-store over a SINGLE contiguous,
//   eagerly-reserved host region (VirtualAlloc / mmap / calloc).  It contains
//   no MMIO routing or PAL-scratch logic; the calling Arbiter (TsunamiChipset)
//   routes MMIO and PAL scratch before ever reaching here.  In-range accesses
//   hit committed memory directly; an access whose PA lies at or beyond
//   sizeBytes() faults with MemStatus::OutOfRange so a page-table walk or a
//   pointer that runs off the end SURFACES instead of silently reading zero.
//
// CHANGE 2026-07-19 (T. Peer decision; Claude / Cowork):
//   FILE: memoryLib/GuestMemory.h + memoryLib/GuestMemory.cpp
//   FUNCTION: whole backing class (ctor/dtor, read*/write*, ensurePage,
//             forEachPage, block helpers)
//   CHANGE: Rip out the sparse, lazily-allocated 64 KiB page table
//     (m_pages / ensurePage CAS install / zero sentinel) and replace it with
//     one contiguous VirtualAlloc(MEM_RESERVE|MEM_COMMIT) / mmap / calloc
//     region of the full configured size.  The OS still lazily backs untouched
//     zero pages physically, so RAM footprint is unchanged; the app-level lazy
//     machinery -- and its silent-zero-on-unallocated behaviour plus the
//     64 KiB "seam" byte-wise special-casing on multi-byte access -- is gone.
//     Out-of-range reads now FAULT (MemStatus::OutOfRange) instead of returning
//     a silent zero.  ensurePage() and forEachPage() are retained as thin shims
//     over the flat region so Snapshot and the HWRPB scan compile unchanged.
//   RATIONALE: eliminate a whole class of lazy-allocation footguns (a walk
//     reading an unwritten page saw a zero, hence invalid, PTE) and reduce the
//     hot path to a bounds check + memcpy.  Best-effort deterministic: no
//     timing/order change; single-threaded.
// ============================================================================

#ifndef MEMORYLIB_GUESTMEMORY_H
#define MEMORYLIB_GUESTMEMORY_H

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include "coreLib/VA_types.h"

namespace memoryLib {

    // ---------------------------------------------------------------------------
    // MemStatus -- read / write outcome.
    // ---------------------------------------------------------------------------
    enum class MemStatus : uint8_t {
        Ok = 0,
        // OutOfRange: the PA (or PA + access width) lies at or beyond
        // sizeBytes().  With the contiguous backing this is a real fault, not
        // a silent zero: reads set out = 0 and return OutOfRange; writes are
        // dropped and return OutOfRange.
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
    // GuestMemory -- Dumb Byte Store (Contiguous Backing)
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
        // ACCESS API: direct access to the contiguous region.  In-range PA hits
        // committed memory; PA at/beyond sizeBytes() returns MemStatus::OutOfRange.
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

        // -----------------------------------------------------------------------
        // forEachPage -- iterate every 64 KiB chunk of the contiguous region.
        // Post-rip-out every chunk is present (there are no sparse holes), so the
        // callback is invoked for all m_pageCount chunks in ascending order.
        // -----------------------------------------------------------------------
        template <typename Fn>
        void forEachPage(Fn&& cb) const noexcept {
            if (!m_base) return;
            for (uint32_t i = 0; i < m_pageCount; ++i) {
                cb(i, m_base + (static_cast<uint64_t>(i) << kPageShift));
            }
        }

        // ensurePage -- compatibility shim.  Every page in [0, pageCount) is
        // backed by the eager region; returns its chunk pointer, or nullptr past
        // the end.  No allocation happens here anymore.
        [[nodiscard]] uint8_t*      ensurePage(uint32_t pidx) noexcept;
        bool                        writeBlock(uint64_t pa, void const* src, uint64_t len) noexcept;
        bool                        readBlock(uint64_t pa, void* dst, uint64_t len) const noexcept;
        [[nodiscard]] bool          isDirty(uint32_t pidx) const noexcept;
        void                        clearDirty() const noexcept;

    private:
        inline void markDirty(uint32_t pidx) const noexcept {
            m_dirtyBitmap[pidx / kPagesPerDirty] |= (1ULL << (pidx % kPagesPerDirty));
        }

        uint64_t    m_size = 0;          // logical guest RAM size -- the access bound
        uint64_t    m_allocBytes = 0;    // reserved region size (m_size rounded up to 64 KiB)
        uint8_t*    m_base = nullptr;    // single contiguous committed region [0, m_allocBytes)
        uint32_t    m_pageCount = 0;     // ceil(m_size / kPageSize); dirty + forEachPage span
        // Dirty bitmap: one bit per 64 KiB page, packed into uint64s.  Size
        // m_dirtyWordCount uint64s = ceil(m_pageCount / 64).
        uint64_t*   m_dirtyBitmap = nullptr;
        uint32_t    m_dirtyWordCount = 0;
        LockMonitor m_locks;
    };

} // namespace memoryLib

#endif
