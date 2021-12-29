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

/**
 * Unpack a buffer containing an ARC/ArcFS/Spark compressed stream
 * into an uncompressed representation of the stream.
 *
 * Supported methods are 3/packed (RLE), 4/squeezed (RLE + Huffman),
 * 8/crunched (RLE + dynamic LZW <=12), 9/squashed (dynamic LZW <=13),
 * FF/compressed (dynamic LZW <=16). The unpacked methods should be handled
 * separately from this function.
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
