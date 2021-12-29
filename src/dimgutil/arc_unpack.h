/**
 * dimgutil: disk image and archive utility
 * Copyright (C) 2021 Alice Rowan <petrifiedrowan@gmail.com>
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
 * Unpacker for ARC/ArcFS/Spark compressed streams.
 */

#ifndef DIMGUTIL_ARC_UNPACK_H
#define DIMGUTIL_ARC_UNPACK_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h> /* size_t */

#define ARC_RESTRICT __restrict__

enum arc_method
{
  ARC_M_UNPACKED_OLD = 0x01,
  ARC_M_UNPACKED     = 0x02,
  ARC_M_PACKED       = 0x03, /* RLE90 */
  ARC_M_SQUEEZED     = 0x04, /* RLE90 + Huffman coding */
  ARC_M_CRUNCHED_5   = 0x05, /* LZW 12-bit static (old hash) */
  ARC_M_CRUNCHED_6   = 0x06, /* RLE90 + LZW 12-bit static (old hash) */
  ARC_M_CRUNCHED_7   = 0x07, /* RLE90 + LZW 12-bit static (new hash) */
  ARC_M_CRUNCHED     = 0x08, /* RLE90 + LZW 9-12 bit dynamic */
  ARC_M_SQUASHED     = 0x09, /* LZW 9-13 bit dynamic (PK extension)*/
  ARC_M_TRIMMED      = 0x0a, /* RLE90 + LZH with adaptive Huffman coding */
  ARC_M_COMPRESSED   = 0x7f, /* LZW 9-16 bit dynamic (Spark extension) */
};

/**
 * Determine if a given ARC/ArcFS/Spark method is supported.
 *
 * Almost all methods found in ArcFS and Spark archives in practice are
 * supported. The rare methods 5-7 are not supported. Method 10 was added
 * in later versions of ARC and is not supported here. Other higher method
 * values are used to encode archive info and other things that can be
 * safely ignored.
 *
 * @param method    compression method to test. All but the lowest seven bits
 *                  will be masked away from this value.
 *
 * @return          0 if a method is supported, otherwise -1.
 */
static inline int arc_method_is_supported(int method)
{
  switch(method & 0x7f)
  {
    case ARC_M_UNPACKED_OLD:
    case ARC_M_UNPACKED:
    case ARC_M_PACKED:
    case ARC_M_SQUEEZED:
    case ARC_M_CRUNCHED:
    case ARC_M_SQUASHED:
    case ARC_M_COMPRESSED:
      return 0;
  }
  return -1;
}

/**
 * Unpack a buffer containing an ARC/ArcFS/Spark compressed stream
 * into an uncompressed representation of the stream. The unpacked methods
 * should be handled separately from this function since they don't need
 * a second output buffer for the uncompressed data.
 *
 * @param dest      destination buffer for the uncompressed stream.
 * @param dest_len  destination buffer size.
 * @param src       buffer containing the compressed stream.
 * @param src_len   size of the compressed stream.
 * @param method    ARC/ArcFS/Spark compression method. All but the lowest
 *                  seven bits will be masked away from this value.
 * @param max_width Specifies the maximum bit width for the crunched and
 *                  compressed (Spark) methods. This value is stored in the
 *                  compressed stream in the ARC/Spark formats but is NOT
 *                  stored in the compressed stream in the ArcFS format.
 *                  If <=0, the value is read from the stream instead.
 *                  For all other methods, this field is ignored.
 *
 * @return          `NULL` on success, otherwise a static const string
 *                  containing a short error message.
 */
const char *arc_unpack(unsigned char * ARC_RESTRICT dest, size_t dest_len,
 const unsigned char *src, size_t src_len, int method, int max_width);

#ifdef __cplusplus
}
#endif

#endif /* DIMGUTIL_ARC_UNPACK_H */
