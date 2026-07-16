/* ==========================================================================
 * firmwareLib/decompressFirmware.h -- clean-room host-side SRM firmware
 * decompressor (WimC + DEFLATE).
 * --------------------------------------------------------------------------
 * Project: EmulatR -- Alpha AXP / EV6 (21264) Emulator (V4)
 * Copyright (C) 2025, 2026 eNVy Systems, Inc.  All rights reserved.
 * Licensed under the eNVy Systems Non-Commercial License v1.1
 * Project Architect: Timothy Peer.  AI Collaboration: Claude (Anthropic).
 * Contact: peert@envysys.com | https://envysys.com
 * --------------------------------------------------------------------------
 * PROVENANCE / LICENSING (see firmwareLib/NOTICE.md):
 *   - The DEFLATE engine is inflate.c, Mark Adler's PUBLIC-DOMAIN reference
 *     inflate ("Not copyrighted 1992 by Mark Adler").  It is an independent
 *     standard-DEFLATE implementation, NOT derived from any DEC source.
 *   - This wrapper is original eNVy/EmulatR code.
 *   - NO DEC-copyrighted source (ev6_huf_decom, decompress.c) is used or
 *     shipped.  The compressed firmware is the USER's own file, supplied at
 *     runtime and transformed locally -- never redistributed.
 *
 * The EV6 SRM firmware ships as a "WimC"-headered, standard-DEFLATE-compressed
 * image.  This decompresses it on the HOST so a module reset can re-seed the
 * decompressed console image directly (deterministic, cached) instead of
 * re-running the multi-billion-cycle guest SROM decompressor every reboot.
 * ========================================================================== */
#ifndef EMULATR_FIRMWARELIB_DECOMPRESSFIRMWARE_H
#define EMULATR_FIRMWARELIB_DECOMPRESSFIRMWARE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Decompress a WimC-wrapped, DEFLATE-compressed firmware image.
 *
 *   compressed        : the raw firmware file bytes (e.g. es40_v7_3.exe)
 *   compressedFileSize : length of `compressed`
 *   out               : caller-owned output buffer for the decompressed image
 *   outCap            : capacity of `out` (must be >= the decompressed size;
 *                       16 MB is the SROM working cap and is always sufficient)
 *   targetBaseOut     : if non-NULL, receives the WimC "target" load base
 *                       (the guest PA the decompressed image is meant to run at)
 *
 * Returns the decompressed length in bytes on success (> 0), or -1 on error
 * (bad args, no WimC magic).  Single-threaded: uses file-static inflate state;
 * call once at a time (init / reset), which is the only usage.
 */
long emulatrDecompressFirmware(const unsigned char* compressed,
                               long                 compressedFileSize,
                               unsigned char*       out,
                               long                 outCap,
                               unsigned*            targetBaseOut);

#ifdef __cplusplus
}
#endif

#endif /* EMULATR_FIRMWARELIB_DECOMPRESSFIRMWARE_H */
