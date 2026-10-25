/* Wuffs (third_party/wuffs), configured for Tachyon: only the image
 * decoders it uses, and none of Wuffs' CPU-specific code, which would use
 * SSE (decision 9). Include this rather than wuffs-v0.4.c directly, so
 * every file sees the same configuration. */
#ifndef TC_WUFFS_H
#define TC_WUFFS_H

#define WUFFS_CONFIG__AVOID_CPU_ARCH
#define WUFFS_CONFIG__MODULES
#define WUFFS_CONFIG__MODULE__BASE
#define WUFFS_CONFIG__MODULE__ADLER32
#define WUFFS_CONFIG__MODULE__CRC32
#define WUFFS_CONFIG__MODULE__DEFLATE
#define WUFFS_CONFIG__MODULE__ZLIB
#define WUFFS_CONFIG__MODULE__PNG
#define WUFFS_CONFIG__MODULE__LZW
#define WUFFS_CONFIG__MODULE__GIF
#define WUFFS_CONFIG__MODULE__JPEG
#define WUFFS_CONFIG__MODULE__BMP
#define WUFFS_CONFIG__MODULE__VP8
#define WUFFS_CONFIG__MODULE__WEBP

#include "wuffs-v0.4.c"

#endif
