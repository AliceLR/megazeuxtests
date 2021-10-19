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

#include "arc_unpack.h"

#include <stdlib.h>
#include <string.h>

#define ARC_NO_CODE 0xffff
#define ARC_RESET_CODE 256

struct arc_code
{
  uint16_t prev;
  uint16_t length;
  uint8_t value;
};

struct arc_unpack
{
  // RLE90.
  size_t rle_in;
  size_t rle_out;
  int in_rle_code;
  int last_byte;

  // LZW.
  size_t lzw_bits_in;
  size_t lzw_in;
  size_t lzw_out;
  unsigned max_code;
  unsigned first_code;
  unsigned next_code;
  unsigned current_width;
  unsigned init_width;
  unsigned max_width;
  unsigned continue_left;
  unsigned continue_code;
  unsigned last_code;
  uint8_t last_first_value;

  struct arc_code *tree;
};

static int arc_unpack_init(struct arc_unpack *arc, int init_width, int max_width, int is_spark)
{
  arc->rle_out = 0;
  arc->rle_in = 0;
  arc->in_rle_code = 0;
  arc->last_byte = 0;

  arc->lzw_bits_in = 0;
  arc->lzw_in = 0;
  arc->lzw_out = 0;
  arc->max_code = (1 << max_width);
  arc->first_code = 257;
  arc->current_width = init_width;
  arc->init_width = init_width;
  arc->max_width = max_width;
  arc->continue_left = 0;
  arc->continue_code = 0;
  arc->last_code = ARC_NO_CODE;
  arc->last_first_value = 0;
  arc->tree = NULL;

  if(max_width)
  {
    size_t i;

    arc->tree = (struct arc_code *)calloc(1 << max_width, sizeof(struct arc_code));
    if(!arc->tree)
      return -1;

    if(init_width == max_width && !is_spark)
      arc->first_code = 256;

    for(i = 0; i < 256; i++)
    {
      struct arc_code *c = &(arc->tree[i]);
      c->prev = ARC_NO_CODE;
      c->length = 1;
      c->value = i;
    }
    arc->next_code = arc->first_code;
  }
  return 0;
}

static void arc_unpack_free(struct arc_unpack *arc)
{
  free(arc->tree);
}

static int arc_read_bits(struct arc_unpack *arc, const uint8_t *src, size_t src_len, unsigned int num_bits)
{
  const uint8_t *pos = src + arc->lzw_in;
  uint32_t ret = 0;

  if(arc->lzw_in >= src_len)
    return -1;

  switch(src_len - arc->lzw_in)
  {
    case 1:
      ret = pos[0];
      break;
    case 2:
      ret = pos[0] | (pos[1] << 8UL);
      break;
    default:
      ret = pos[0] | (pos[1] << 8UL) | (pos[2] << 16UL);
  }

  ret = (ret >> (arc->lzw_bits_in & 7)) & (0xffffUL << num_bits >> 16);

  arc->lzw_bits_in += num_bits;
  arc->lzw_in = arc->lzw_bits_in >> 3;
  return ret;
}

static uint16_t arc_unlzw_get_length(const struct arc_unpack *arc,
 const struct arc_code *e)
{
  uint32_t length = 1;
  int code;

  if(e->length)
    return e->length;

  do
  {
    if(length >= arc->max_code)
      return 0;

    length++;
    code = e->prev;
    e = &(arc->tree[code]);
  }
  while(code >= 256);
  return length;
}

static int arc_unlzw_block(struct arc_unpack * ARC_RESTRICT arc,
 uint8_t * ARC_RESTRICT dest, size_t dest_len, const uint8_t *src, size_t src_len)
{
  uint8_t *pos;
  struct arc_code *e;
  int code;
  int len;
  int set_last_first;

  while(arc->lzw_in < src_len && arc->lzw_out < dest_len)
  {
    // Interrupted while writing out code? Resume output...
    if(arc->continue_code)
    {
      code = arc->continue_code;
      set_last_first = 0;
      goto continue_code;
    }

    code = arc_read_bits(arc, src, src_len, arc->current_width);
    if(code < 0)
      break;

    if(code == ARC_RESET_CODE && arc->first_code == 257)
    {
      // Reset width for dynamic modes 8 and 255.
      arc->next_code = arc->first_code;
      arc->current_width = arc->init_width; // FIXME does ARC do this?
      // FIXME reset cached lengths?
      continue;
    }

    // Add next code first to avoid KwKwK problem.
    if(arc->next_code < arc->max_code)
    {
      if(arc->last_code != ARC_NO_CODE)
      {
        arc->last_first_value = code;
        len = 1;
      }
      else
        len = arc->tree[arc->last_code].length;

      e = &(arc->tree[arc->next_code]);
      e->prev = arc->last_code;
      e->length = len ? len + 1 : 0;
      e->value = arc->last_first_value;
      arc->last_code = (arc->next_code++);

      // Automatically expand width.
      if(arc->next_code >= (1U << arc->current_width))
        arc->current_width++;
    }

    // Emit code.
    set_last_first = 1;

continue_code:
    e = &(arc->tree[code]);

    if(!arc->continue_code)
    {
      len = arc_unlzw_get_length(arc, e);
      if(!len)
        return -1;
    }
    else
      len = arc->continue_left;

    if((unsigned)len > dest_len - arc->lzw_out)
    {
      // Calculate arc->continue_left, skip arc->continue_left,
      // emit remaining len from end of dest.
      int32_t num_emit = dest_len - arc->lzw_out;

      arc->continue_left = len - num_emit;
      arc->continue_code = code;

      for(; len > num_emit; len--)
        e = &(arc->tree[e->prev]);
    }
    else
      arc->continue_code = 0;

    for(pos = dest + len - 1; len >= 0; len--)
    {
      code = e->value;
      *(pos--) = code;
      e = &(arc->tree[e->prev]);
    }
    if(set_last_first)
      arc->last_first_value = code;
  }
  return 0;
}

static int arc_unrle90_block(struct arc_unpack * ARC_RESTRICT arc,
 uint8_t * ARC_RESTRICT dest, size_t dest_len, const uint8_t *src, size_t src_len)
{
  size_t start;
  size_t len;
  size_t i;

  for(i = 0; i < src_len; i++)
  {
    if(arc->in_rle_code)
    {
      arc->in_rle_code = 0;
      if(i >= src_len)
        return -1;

      if(src[i] == 0)
      {
        if(arc->rle_out >= dest_len)
          return -1;

        dest[arc->rle_out++] = 0x90;
        arc->last_code = 0x90;
      }
      else
      {
        if(arc->rle_out + src[i] > dest_len)
          return -1;

        memset(dest + arc->rle_out, arc->last_code, src[i]);
        arc->rle_out += src[i];
      }
    }

    start = i;
    while(i < src_len && src[i] != 0x90)
      i++;

    if(i > start)
    {
      len = i - start;
      if(len + arc->rle_out > dest_len)
        return -1;

      memcpy(dest + arc->rle_out, src + start, len);
      arc->rle_out += len;
      arc->last_code = src[i - 1];
    }

    if(src[i] == 0x90)
      arc->in_rle_code = 1;
  }
  arc->rle_in += i;
  return 0;
}

int arc_unpack_rle90(uint8_t * ARC_RESTRICT dest, size_t dest_len,
 const uint8_t *src, size_t src_len)
{
  struct arc_unpack arc;
  if(arc_unpack_init(&arc, 0, 0, 0) != 0)
    return -1;

  if(arc_unrle90_block(&arc, dest, dest_len, src, src_len) != 0)
    goto err;

  if(arc.rle_out != dest_len)
    goto err;

  arc_unpack_free(&arc);
  return 0;

err:
  arc_unpack_free(&arc);
  return -1;
}

int arc_unpack_lzw(uint8_t * ARC_RESTRICT dest, size_t dest_len,
 const uint8_t *src, size_t src_len, int init_width, int max_width)
{
  struct arc_unpack arc;
  int is_spark = 0;

  if(max_width == ARC_MAX_CODE_IN_STREAM)
  {
    if(src_len < 2)
      return -1;

    max_width = src[0];
    src++;
    src_len--;
    is_spark = 1;
    if(max_width < 9 || max_width > 16)
      return -1;
  }

  if(arc_unpack_init(&arc, init_width, max_width, is_spark) != 0)
    return -1;

  if(arc_unlzw_block(&arc, dest, dest_len, src, src_len))
    goto err;

  if(arc.lzw_out != dest_len)
    goto err;

  arc_unpack_free(&arc);
  return 0;

err:
  arc_unpack_free(&arc);
  return -1;
}

int arc_unpack_lzw_rle90(uint8_t * ARC_RESTRICT dest, size_t dest_len,
 const uint8_t *src, size_t src_len, int init_width, int max_width)
{
  struct arc_unpack arc;
  uint8_t buffer[4096];

  // This is only used for Spark method 0xff, which doesn't use RLE.
  if(max_width == ARC_MAX_CODE_IN_STREAM)
    return -1;
  if(max_width < 9 || max_width > 16)
    return -1;

  if(arc_unpack_init(&arc, init_width, max_width, 0) != 0)
    return -1;

  while(arc.lzw_in < src_len)
  {
    arc.lzw_out = 0;
    if(arc_unlzw_block(&arc, buffer, sizeof(buffer), src, src_len))
      goto err;

    if(arc_unrle90_block(&arc, dest, dest_len, buffer, arc.lzw_out))
      goto err;
  }

  if(arc.rle_out != dest_len)
    goto err;

  arc_unpack_free(&arc);
  return 0;

err:
  arc_unpack_free(&arc);
  return -1;
}
