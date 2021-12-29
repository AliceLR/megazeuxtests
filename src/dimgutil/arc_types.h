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

#ifndef DIMGUTIL_ARC_TYPES_H
#define DIMGUTIL_ARC_TYPES_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h> /* size_t */

#include <stdint.h>
typedef int8_t   arc_int8;
typedef int16_t  arc_int16;
typedef int32_t  arc_int32;
typedef uint8_t  arc_uint8;
typedef uint16_t arc_uint16;
typedef uint32_t arc_uint32;

#ifdef __cplusplus
}
#endif

#endif /* DIMGUTIL_ARC_TYPES_H */
