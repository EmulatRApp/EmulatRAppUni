#ifndef PROCESSORVARIANT_H
#define PROCESSORVARIANT_H
#include <cstdint>
struct AlphaProcessorVariant
{
    const char* name;       // "EV6", "EV67", "EV68"
    const char* chipId;     // "21264", "21264A", "21264C"
    uint64_t implver;        // IMPLVER instruction result
    uint64_t amask;          // supported feature mask (AMASK clears these)
    uint64_t cpuType;        // HWRPB per-CPU descriptor type field
};
#endif // PROCESSORVARIANT_H
