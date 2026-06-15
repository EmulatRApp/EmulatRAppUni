/* oracle.c -- host harness for the DEC SRM reference decompressor.
   Compile with inflate.c (Mark Adler c10p1). decom.c is reference only.
   Portable: Linux gcc and MSYS2 MinGW64 (produces oracle.exe).
   Usage: oracle <compressed-firmware.exe> [output.bin]
   Finds the WimC header, runs inflate(), writes the decompressed image,
   and prints the hw_ret(R2)/hw_ret(R6) regression signature. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "decomp.h"

/* globals declared extern in decomp.h (decom.c excluded from this build) */
PUCHAR compressed = 0;
LONG   compressedSize = 0;
PUCHAR decompressed = 0;
LONG   decompressedSize = 0;
PUCHAR inptr = 0;
PUCHAR outptr = 0;
INT    verbose = 0;
INT    bits_left = 0;

int inflate(void);

#define OUTCAP (16*1024*1024)

static long count_sig(unsigned char *d, long n, const unsigned char *s, int sl){
    long c = 0; long i;
    for (i = 0; i + sl <= n; i++) if (memcmp(d+i, s, sl) == 0) c++;
    return c;
}

int main(int argc, char **argv){
    const char *path, *outpath;
    FILE *f, *o;
    long fsz, w, outlen, i;
    unsigned char *buf;
    unsigned csize, target;
    int r;
    unsigned char r2[8] = {0x30,0x00,0xde,0xb4,0x00,0xa0,0xe2,0x7b};
    unsigned char r6[8] = {0x30,0x00,0xde,0xb4,0x00,0xa0,0xe6,0x7b};

    if (argc < 2){ fprintf(stderr,"usage: %s <compressed.exe> [output.bin]\n", argv[0]); return 1; }
    path = argv[1];
    outpath = (argc >= 3) ? argv[2] : "ref_decompressed.bin";

    f = fopen(path,"rb"); if(!f){ perror("open input"); return 2; }
    fseek(f,0,SEEK_END); fsz = ftell(f); fseek(f,0,SEEK_SET);
    buf = (unsigned char*)malloc(fsz);
    fread(buf,1,fsz,f); fclose(f);

    w = -1;
    for (i = 0; i + 4 <= fsz; i++)
        if (buf[i]==0x57 && buf[i+1]==0x69 && buf[i+2]==0x6D && buf[i+3]==0x43){ w = i; break; }
    if (w < 0){ fprintf(stderr,"no WimC magic found\n"); return 3; }

    memcpy(&csize, buf+w+4, 4);
    memcpy(&target, buf+w+16, 4);
    fprintf(stderr,"WimC@0x%lx  compressedSize=0x%x  target=0x%x  dataoff=0x%lx\n",
            w, csize, target, w+20);

    decompressed = outptr = (PUCHAR)calloc(1, OUTCAP);
    inptr = buf + w + 20;
    compressedSize = csize;          /* ReadByte guard */

    r = inflate();
    outlen = (long)(outptr - decompressed);
    fprintf(stderr,"inflate ret=%d  output=0x%lx (%ld) bytes\n", r, outlen, outlen);

    o = fopen(outpath,"wb"); fwrite(decompressed,1,outlen,o); fclose(o);
    fprintf(stderr,"wrote %s\n", outpath);

    fprintf(stderr,"signature: hw_ret(R2) x%ld , hw_ret(R6) x%ld   (expect R2=3, R6=0)\n",
            count_sig(decompressed, outlen, r2, 8),
            count_sig(decompressed, outlen, r6, 8));
    return 0;
}
