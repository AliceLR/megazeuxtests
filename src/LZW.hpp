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
 * Simple LZW decoder for Digital Symphony.
 * This does not handle the hacks required for ARC or UnShrink.
 *
 * Adapted from the Digital Symphony LZW decoder in libxmp, which in turn was
 * adapted from the ZIP Shrink decoder in MegaZeux.
 */

#ifndef MZXTEST_LZW_HPP
#define MZXTEST_LZW_HPP

#include <stdio.h>

#define LZW_FLAG_MAXBITS(x)	((x) & 15)
#define LZW_FLAG_SYMQUIRKS	0x100
#define LZW_FLAGS_SYM		LZW_FLAG_MAXBITS(13) | LZW_FLAG_SYMQUIRKS

int LZW_read(void *dest, size_t dest_len, size_t max_read_len, int flags, FILE *fp);

#endif /* MZXTEST_LZW_HPP */
