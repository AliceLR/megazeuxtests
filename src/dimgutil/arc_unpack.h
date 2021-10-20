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

#include <stdlib.h>

#define ARC_RESTRICT __restrict__

const char *arc_unpack(unsigned char * ARC_RESTRICT dest, size_t dest_len,
 const unsigned char *src, size_t src_len, int method);

#ifdef __cplusplus
}
#endif

#endif /* MZXTEST_DIMGUTIL_ARC_UNPACK_H */
