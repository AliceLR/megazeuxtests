/**
 * dimgutil: disk image and archive utility
 * Copyright (C) 2022 Alice Rowan <petrifiedrowan@gmail.com>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/**
 * Unpacker for LZX compressed streams.
 */

#ifndef DIMGUTIL_LZX_UNPACK_H
#define DIMGUTIL_LZX_UNPACK_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h> /* size_t */

#include <stdint.h>
typedef uint8_t  lzx_uint8;
typedef uint16_t lzx_uint16;
typedef uint32_t lzx_uint32;
typedef int32_t  lzx_int32;

#define LZX_RESTRICT __restrict__

enum lzx_method
{
  LZX_M_UNPACKED = 0,
  LZX_M_PACKED   = 2,
};

/*
static inline lzx_uint16 lzx_mem_u16(const unsigned char *buf)
{
  return (buf[1] << 8) | buf[0];
}
*/

static inline lzx_uint32 lzx_mem_u32(const unsigned char *buf)
{
  return (buf[3] << 24UL) | (buf[2] << 16UL) | (buf[1] << 8UL) | buf[0];
}

/**
 * Determine if a given LZX method is supported.
 *
 * @param method    compression method to test.
 *
 * @return          0 if a method is supported, otherwise -1.
 */
static inline int lzx_method_is_supported(int method)
{
  switch(method)
  {
    case LZX_M_UNPACKED:
    case LZX_M_PACKED:
      return 0;
  }
  return -1;
}

/**
 * Unpack a buffer containing an LZX compressed stream into an uncompressed
 * representation of the stream. The unpacked method should be handled
 * separately from this function since it doesn't need a second output buffer
 * for the uncompressed data.
 *
 * @param dest        destination buffer for the uncompressed stream.
 * @param dest_len    destination buffer size.
 * @param src         buffer containing the compressed stream.
 * @param src_len     size of the compressed stream.
 * @param method      LZX compression method (should be 2).
 * @param windowbits  LZX sliding window bits.
 *
 * @return          `NULL` on success, otherwise a static const string
 *                  containing a short error message.
 */
const char *lzx_unpack(unsigned char * LZX_RESTRICT dest, size_t dest_len,
 const unsigned char *src, size_t src_len, int method, int windowbits);

#ifdef __cplusplus
}
#endif

#endif /* DIMGUTIL_LZX_UNPACK_H */
