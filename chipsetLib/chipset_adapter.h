#ifndef _EMULATR_EMULATRPOC_GSEA_POC_SRC_CHIPSET_CHIPSET_ADAPTER_H
#define _EMULATR_EMULATRPOC_GSEA_POC_SRC_CHIPSET_CHIPSET_ADAPTER_H

// ===========================================================================
// chipset_adapter.h - Bridges GSEA ExecContext to v1 TsunamiChipset
// ===========================================================================

#include "TsunamiChipset.h"
#include "coreLib/ExecCtx.h"
#include "chipsetLib/TsunamiChipset.h"

namespace gsea {

static constexpr uint64_t kTsunamiBase = 0x80000000000ULL;

inline uint64_t tsunami_mmio_read(uint64_t pa, uint8_t width, void* dev) {
    auto* chip = static_cast<TsunamiChipset*>(dev);
    uint64_t offset = pa - kTsunamiBase;
    uint64_t val = TsunamiChipset::mmioRead(chip, offset, width);
        fprintf(stderr, "MMIO READ: PA=0x%012llx val=0x%016llx\n",
            static_cast<unsigned long long>(pa),
            static_cast<unsigned long long>(val));

        if (val < 0x800) {
            fprintf(stderr,
                "MMIO READ: routing error chip=0x%03x PA=0x%012llx\n",
                val, static_cast<unsigned long long>(pa));
            return 0ULL;
        }

        return val;
}

inline void tsunami_mmio_write(uint64_t pa, uint64_t val, uint8_t width, void* dev) {
    auto* chip = static_cast<TsunamiChipset*>(dev);
    uint64_t offset = pa - kTsunamiBase;
    TsunamiChipset::mmioWrite(chip, offset, val, width);
}

/*  
 *  TODO Call once during initialization
inline void attach_chipset(coreLib::ExecCtx& ctx, TsunamiChipset* chip) {
    ctx.mmioRead   = tsunami_mmio_read;
    ctx.mmioWrite  = tsunami_mmio_write;
    ctx.mmioDevice = chip;

    // GuestMemory wiring (new)
    ctx.shared_mem.mmio_read = tsunami_mmio_read;
    ctx.shared_mem.mmio_write = tsunami_mmio_write;
    ctx.shared_mem.mmio_device = chip;
}
*/

} // namespace gsea

#endif
