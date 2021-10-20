/**
 * Copyright (C) 2021 Lachesis <petrifiedrowan@gmail.com>
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

#ifndef MZXTEST_DIMGUTIL_ARC_UNPACK_H
#define MZXTEST_DIMGUTIL_ARC_UNPACK_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* ARC method 0x08: read maximum code width from stream, but ignore it. */
#define ARC_IGNORE_CODE_IN_STREAM 0x7ffe
/* Spark method 0xff: read maximum code width from stream. */
#define ARC_MAX_CODE_IN_STREAM 0x7fff

#define ARC_RESTRICT __restrict__

int arc_unpack_rle90(uint8_t * ARC_RESTRICT dest, size_t dest_len,
 const uint8_t *src, size_t src_len);

int arc_unpack_lzw(uint8_t * ARC_RESTRICT dest, size_t dest_len,
 const uint8_t *src, size_t src_len, int init_width, int max_width);

int arc_unpack_lzw_rle90(uint8_t * ARC_RESTRICT dest, size_t dest_len,
 const uint8_t *src, size_t src_len, int init_width, int max_width);

int arc_unpack_huffman_rle90(uint8_t * ARC_RESTRICT dest, size_t dest_len,
 const uint8_t *src, size_t src_len);

#ifdef __cplusplus
}
#endif

#endif /* MZXTEST_DIMGUTIL_ARC_UNPACK_H */
