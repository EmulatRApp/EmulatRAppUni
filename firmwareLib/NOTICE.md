<!--
firmwareLib/NOTICE.md -- provenance and licensing for the host-side firmware
decompressor.  ASCII(128) only.
-->

# firmwareLib -- provenance / licensing

`firmwareLib` decompresses the SRM firmware image on the **host** so a module
reset can re-seed the decompressed console image directly, instead of re-running
the guest SROM decompressor every reboot. It contains only clean / public-domain
code and requires **no** DEC-copyrighted source.

## Files and their provenance

| File | Origin | License | Ships? |
|------|--------|---------|--------|
| `inflate.c` | Mark Adler's reference DEFLATE inflate ("Not copyrighted 1992 by Mark Adler") | **Public domain** | Yes |
| `decomp.h` | Generic typedefs + inflate-global decls | eNVy / no third-party copyright | Yes |
| `decompressFirmware.h` / `.c` | Original eNVy wrapper (WimC parse + inflate call over caller buffers) | eNVy Non-Commercial License v1.1 | Yes |

`inflate.c` is Mark Adler's canonical public-domain DEFLATE decoder, an
independent standard-format implementation. The EV6 SRM firmware is
standard-DEFLATE-compressed (PKZIP method 8) behind a small "WimC" header, so
this public-domain decoder decompresses it without any DEC-derived code.

## What is deliberately NOT here (and must not be shipped)

The reference tree `tools/host_decompressor/src/` also contains two
**DEC-copyrighted, reference-only** files that are **not compiled** and are
**excluded** from `firmwareLib`:

- `ev6_huf_decom.m64` -- DEC (C) 1994, guest-side PALcode decompressor. Its
  license forbids providing it "to any other person." It is a cross-check oracle
  only; the guest runs its own copy from the user's firmware at boot.
- `decompress.c` / `decom.c` -- DEC (Wim Colgate, 1992), reference only, never
  compiled.

Before any source distribution, ensure these two DEC files are excluded from the
shipped package (keep them in a private dev/reference location, or remove them
from the tree). The compressed firmware image itself is the **user's own file**,
supplied at runtime and transformed locally -- never redistributed.

## Not legal advice

This records technical provenance to support an IP/legal review; it is not legal
advice. Given DEC-copyrighted files exist in the wider repo, a sign-off on
"ship firmwareLib (inflate.c + wrapper), exclude the DEC references" is advised
before release.
