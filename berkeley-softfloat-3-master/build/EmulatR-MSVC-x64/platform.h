
/*============================================================================

This C header file is part of the SoftFloat IEEE Floating-Point Arithmetic
Package, Release 3e, by John R. Hauser.

Copyright 2011, 2012, 2013, 2014, 2015, 2016, 2017 The Regents of the
University of California.  All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

 1. Redistributions of source code must retain the above copyright notice,
    this list of conditions, and the following disclaimer.

 2. Redistributions in binary form must reproduce the above copyright notice,
    this list of conditions, and the following disclaimer in the documentation
    and/or other materials provided with the distribution.

 3. Neither the name of the University nor the names of its contributors may
    be used to endorse or promote products derived from this software without
    specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS "AS IS", AND ANY
EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE, ARE
DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

=============================================================================*/

/*----------------------------------------------------------------------------
| EmulatR portable host-config for SoftFloat 3e -- PORTABLE across ALL
| toolchains (MSVC, clang, gcc), NOT MSVC-specific despite the directory name.
|
| Deliberately omits opts-GCC.h and the __int128 / __builtin_clz intrinsics:
| their extern-inline primitives emit no linkable symbol under a plain build
| (the .c versions are guarded out, the inlines never instantiated) -> link
| failure.  Without those defines SoftFloat compiles its own software
| primitives instead -- bit-deterministic and identical across compilers.
| The `__inline` keyword is accepted by MSVC, clang, and gcc alike and uses
| gnu89 inline semantics, so it always emits an out-of-line copy when needed
| (avoiding the C99 extern-inline link trap above).
*----------------------------------------------------------------------------*/
#define LITTLEENDIAN 1

/*----------------------------------------------------------------------------
*----------------------------------------------------------------------------*/
#define INLINE __inline
