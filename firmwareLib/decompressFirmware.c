/* ==========================================================================
 * firmwareLib/decompressFirmware.c -- clean-room WimC + DEFLATE host
 * decompressor wrapper (see decompressFirmware.h for provenance/licensing).
 * --------------------------------------------------------------------------
 * Copyright (C) 2025, 2026 eNVy Systems, Inc.  All rights reserved.
 * Licensed under the eNVy Systems Non-Commercial License v1.1
 * Project Architect: Timothy Peer.  AI Collaboration: Claude (Anthropic).
 * --------------------------------------------------------------------------
 * Adapted from the reference host harness (tools/host_decompressor/src/
 * oracle.c, eNVy) to operate on caller buffers instead of files.  The DEFLATE
 * engine is Mark Adler's public-domain inflate.c.  NO DEC source.
 * ========================================================================== */
#include "decompressFirmware.h"
#include "decomp.h"

#include <string.h>

/* --------------------------------------------------------------------------
 * inflate() globals.  inflate.c (Mark Adler PD) declares these `extern` via
 * decomp.h and reads/advances them; the reference build defined them in
 * oracle.c.  We define them here (oracle.c is NOT part of this build).  All
 * file-static-lifetime; a single decompression uses them at a time.
 * -------------------------------------------------------------------------- */
PUCHAR compressed       = 0;
LONG   compressedSize   = 0;
PUCHAR decompressed     = 0;
LONG   decompressedSize = 0;
PUCHAR inptr            = 0;
PUCHAR outptr           = 0;
INT    bits_left        = 0;
INT    verbose          = 0;

extern int inflate(void);   /* firmwareLib/inflate.c, Mark Adler public domain */

long emulatrDecompressFirmware(const unsigned char* comp,
                               long                 fsz,
                               unsigned char*       out,
                               long                 outCap,
                               unsigned*            targetBaseOut)
{
    long     w = -1;
    long     i;
    unsigned csize  = 0;
    unsigned target = 0;

    if (comp == 0 || out == 0 || fsz < 20 || outCap <= 0) {
        return -1;
    }

    /* Find the "WimC" magic (0x57 0x69 0x6D 0x43) that heads the compressed
     * payload.  Header layout (from the reference harness): magic at +0,
     * compressedSize at +4, target load base at +16, DEFLATE data at +20. */
    for (i = 0; i + 4 <= fsz; i++) {
        if (comp[i]   == 0x57 && comp[i+1] == 0x69 &&
            comp[i+2] == 0x6D && comp[i+3] == 0x43) {
            w = i;
            break;
        }
    }
    if (w < 0) {
        return -1;   /* no WimC magic -- not a compressed SRM image */
    }

    memcpy(&csize,  comp + w + 4,  4);
    memcpy(&target, comp + w + 16, 4);

    /* Set up inflate: read from the DEFLATE data, write into the caller's
     * buffer.  outlen is how far outptr advanced.  compressedSize is inflate's
     * ReadByte end-of-input guard. */
    decompressed   = outptr = (PUCHAR)out;
    inptr          = (PUCHAR)(comp + w + 20);
    compressed     = 0;              /* unused by inflate (inptr drives reads) */
    compressedSize = (LONG)csize;
    bits_left      = 0;

    (void)outCap;   /* SROM 16 MB cap; caller sizes `out` (see header) */
    (void)inflate();

    if (targetBaseOut != 0) {
        *targetBaseOut = target;
    }
    return (long)(outptr - decompressed);
}
