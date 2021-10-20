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

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//#define ARC_DEBUG

/* ARC method 0x08: read maximum code width from stream, but ignore it. */
#define ARC_IGNORE_CODE_IN_STREAM 0x7ffe
/* Spark method 0xff: read maximum code width from stream. */
#define ARC_MAX_CODE_IN_STREAM 0x7fff

#define ARC_NO_CODE 0xffff
#define ARC_RESET_CODE 256

struct arc_code
{
  uint16_t prev;
  uint16_t length;
  uint8_t value;
};

struct arc_lookup
{
  uint16_t value;
  uint8_t length;
};

struct arc_huffman_index
{
  int16_t value[2];
};

struct arc_unpack
{
  // RLE90.
  size_t rle_in;
  size_t rle_out;
  int in_rle_code;
  int last_byte;

  // LZW and huffman.
  uint16_t codes_buffered[8];
  unsigned buffered_pos;
  unsigned buffered_width;
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
  unsigned kwkwk;
  uint8_t last_first_value;

  struct arc_code *tree;
  struct arc_lookup *huffman_lookup;
  struct arc_huffman_index *huffman_tree;
  uint16_t num_huffman;
};

static int arc_unpack_init(struct arc_unpack *arc, int init_width, int max_width, int is_dynamic)
{
  arc->rle_out = 0;
  arc->rle_in = 0;
  arc->in_rle_code = 0;
  arc->last_byte = 0;

  arc->buffered_pos = 0;
  arc->buffered_width = 0;
  arc->lzw_bits_in = 0;
  arc->lzw_in = 0;
  arc->lzw_out = 0;
  arc->max_code = (1 << max_width);
  arc->first_code = is_dynamic ? 257 : 256;
  arc->current_width = init_width;
  arc->init_width = init_width;
  arc->max_width = max_width;
  arc->continue_left = 0;
  arc->continue_code = 0;
  arc->last_code = ARC_NO_CODE;
  arc->last_first_value = 0;
  arc->kwkwk = 0;
  arc->tree = NULL;
  arc->huffman_lookup = NULL;
  arc->huffman_tree = NULL;
  arc->num_huffman = 0;

  if(max_width)
  {
    size_t i;

    arc->tree = (struct arc_code *)calloc(1 << max_width, sizeof(struct arc_code));
    if(!arc->tree)
      return -1;

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

static uint32_t arc_get_bytes(const uint8_t *pos, int num)
{
  switch(num)
  {
    case 1:
      return pos[0];
    case 2:
      return pos[0] | (pos[1] << 8UL);
    case 3:
      return pos[0] | (pos[1] << 8UL) | (pos[2] << 16UL);
    default:
      return pos[0] | (pos[1] << 8UL) | (pos[2] << 16UL) | (pos[3] << 24UL);
  }
}

static int arc_read_bits(struct arc_unpack *arc, const uint8_t *src, size_t src_len, unsigned int num_bits)
{
  uint32_t ret;

  if(arc->lzw_bits_in + num_bits > (src_len << 3))
  {
    arc->lzw_bits_in = src_len << 3;
    arc->lzw_in = src_len;
    return -1;
  }

  ret = arc_get_bytes(src + arc->lzw_in, src_len - arc->lzw_in);

  ret = (ret >> (arc->lzw_bits_in & 7)) & (0xffffUL << num_bits >> 16);

  arc->lzw_bits_in += num_bits;
  arc->lzw_in = arc->lzw_bits_in >> 3;
  return ret;
}

static uint16_t arc_next_code(struct arc_unpack *arc, const uint8_t *src, size_t src_len)
{
  /**
   * Codes are read 8 at a time in the original ARC/ArcFS/Spark software,
   * presumably to simplify file IO. This buffer needs to be simulated.
   *
   * When the code width changes, the extra buffered codes are discarded.
   * Despite this, the final number of codes won't always be a multiple of 8.
   */
  if(arc->buffered_pos >= 8 || arc->buffered_width != arc->current_width)
  {
    size_t i;
    for(i = 0; i < 8; i++)
    {
      int value = arc_read_bits(arc, src, src_len, arc->current_width);
      if(value < 0)
        break;

      arc->codes_buffered[i] = value;
    }
    for(; i < 8; i++)
      arc->codes_buffered[i] = ARC_NO_CODE;

    arc->buffered_pos = 0;
    arc->buffered_width = arc->current_width;
  }
  return arc->codes_buffered[arc->buffered_pos++];
}

static void arc_unlzw_add(struct arc_unpack *arc)
{
  if(arc->last_code != ARC_NO_CODE && arc->next_code < arc->max_code)
  {
    uint16_t len = arc->tree[arc->last_code].length;
    struct arc_code *e;

    e = &(arc->tree[arc->next_code++]);
    e->prev = arc->last_code;
    e->length = len ? len + 1 : 0;
    e->value = arc->last_first_value;

    // Automatically expand width.
    if(arc->next_code >= (1U << arc->current_width) && arc->current_width < arc->max_width)
    {
      arc->current_width++;
      #ifdef ARC_DEBUG
      fprintf(stderr, "width expanded to %u\n", arc->current_width);
      #endif
    }
  }
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
  uint16_t start_code;
  uint16_t code;
  uint16_t len;
  int set_last_first;

  #ifdef ARC_DEBUG
  int num_debug = 0;
  #endif

  while(arc->lzw_out < dest_len)
  {
    // Interrupted while writing out code? Resume output...
    if(arc->continue_code)
    {
      code = arc->continue_code;
      set_last_first = 0;
      goto continue_code;
    }

    code = arc_next_code(arc, src, src_len);
    if(code >= arc->max_code)
      break;

    #ifdef ARC_DEBUG
    fprintf(stderr, "%04x ", code);
    num_debug++;
    if(!(num_debug & 15))
      fprintf(stderr, "\n");
    #endif

    if(code == ARC_RESET_CODE && arc->first_code == 257)
    {
      size_t i;
      // Reset width for dynamic modes 8, 9, and 255.
      #ifdef ARC_DEBUG
      fprintf(stderr, "reset at size = %u codes\n", arc->next_code);
      #endif
      arc->next_code = arc->first_code;
      arc->current_width = arc->init_width;
      arc->last_code = ARC_NO_CODE;

      for(i = 256; i < arc->max_code; i++)
        arc->tree[i].length = 0;

      continue;
    }

    // Add next code first to avoid KwKwK problem.
    if((unsigned)code == arc->next_code)
    {
      arc_unlzw_add(arc);
      arc->kwkwk = 1;
    }

    // Emit code.
    set_last_first = 1;

continue_code:
    start_code = code;
    e = &(arc->tree[code]);

    if(!arc->continue_code)
    {
      len = arc_unlzw_get_length(arc, e);
      if(!len)
      {
        #ifdef ARC_DEBUG
        fprintf(stderr, "failed to get length for %04xh (code count is %04xh)\n",
         code, arc->next_code);
        #endif
        return -1;
      }
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

    pos = dest + arc->lzw_out + len - 1;
    arc->lzw_out += len;
    for(; len > 0; len--)
    {
      code = e->value;
      *(pos--) = code;
      e = &(arc->tree[e->prev]);
    }
    // Only set this if this is the tail end of the chain,
    // i.e., the first section written.
    if(set_last_first)
      arc->last_first_value = code;

    if(arc->continue_code)
      return 0;

    if(!arc->kwkwk)
      arc_unlzw_add(arc);

    arc->last_code = start_code;
    arc->kwkwk = 0;
  }
  return 0;
}

static int arc_unrle90_block(struct arc_unpack * ARC_RESTRICT arc,
 uint8_t * ARC_RESTRICT dest, size_t dest_len, const uint8_t *src, size_t src_len)
{
  size_t start;
  size_t len;
  size_t i;

  for(i = 0; i < src_len;)
  {
    if(arc->in_rle_code)
    {
      arc->in_rle_code = 0;
      if(i >= src_len)
      {
        #ifdef ARC_DEBUG
        fprintf(stderr, "end of input stream mid-code @ %zu\n", i);
        #endif
        return -1;
      }

      if(src[i] == 0)
      {
        if(arc->rle_out >= dest_len)
        {
          #ifdef ARC_DEBUG
          fprintf(stderr, "end of output stream @ %zu emitting 0x90\n", i);
          #endif
          return -1;
        }

        #ifdef ARC_DEBUG
        fprintf(stderr, "@ %zu: literal 0x90\n", i);
        #endif
        dest[arc->rle_out++] = 0x90;
        arc->last_byte = 0x90;
      }
      else
      {
        len = src[i] - 1;
        if(arc->rle_out + len > dest_len)
        {
          #ifdef ARC_DEBUG
          fprintf(stderr, "end of output stream @ %zu: run of %02xh times %zu\n",
           i, arc->last_byte, len);
          #endif
          return -1;
        }

        #ifdef ARC_DEBUG
        fprintf(stderr, "@ %zu: run of %02xh times %zu\n", i, arc->last_byte, len);
        #endif
        memset(dest + arc->rle_out, arc->last_byte, len);
        arc->rle_out += len;
      }
      i++;
    }

    start = i;
    while(i < src_len && src[i] != 0x90)
      i++;

    if(i > start)
    {
      len = i - start;
      if(len + arc->rle_out > dest_len)
      {
        #ifdef ARC_DEBUG
        fprintf(stderr, "end of output_stream @ %zu: block of length %zu\n",
          i, len);
        #endif
        return -1;
      }

      #ifdef ARC_DEBUG
      fprintf(stderr, "@ %zu: block of length %zu\n", i, len);
      #endif
      memcpy(dest + arc->rle_out, src + start, len);
      arc->rle_out += len;
      arc->last_byte = src[i - 1];
    }

    if(i < src_len && src[i] == 0x90)
    {
      arc->in_rle_code = 1;
      i++;
    }
  }
  arc->rle_in += i;
  return 0;
}

static int arc_unpack_rle90(uint8_t * ARC_RESTRICT dest, size_t dest_len,
 const uint8_t *src, size_t src_len)
{
  struct arc_unpack arc;
  if(arc_unpack_init(&arc, 0, 0, 0) != 0)
    return -1;

  if(arc_unrle90_block(&arc, dest, dest_len, src, src_len) != 0)
  {
    #ifdef ARC_DEBUG
    fprintf(stderr, "arc_unrle90_block failed\n");
    #endif
    goto err;
  }

  if(arc.rle_out != dest_len)
  {
    #ifdef ARC_DEBUG
    fprintf(stderr, "out %zu != buffer size %zu\n", arc.rle_out, dest_len);
    #endif
    goto err;
  }

  arc_unpack_free(&arc);
  return 0;

err:
  arc_unpack_free(&arc);
  return -1;
}

static int arc_unpack_lzw(uint8_t * ARC_RESTRICT dest, size_t dest_len,
 const uint8_t *src, size_t src_len, int init_width, int max_width)
{
  struct arc_unpack arc;
  int is_dynamic = (init_width != max_width);

  if(max_width == ARC_MAX_CODE_IN_STREAM)
  {
    if(src_len < 2)
      return -1;

    max_width = src[0];
    src++;
    src_len--;
    if(max_width < 9 || max_width > 16)
      return -1;
  }

  if(arc_unpack_init(&arc, init_width, max_width, is_dynamic) != 0)
    return -1;

  if(arc_unlzw_block(&arc, dest, dest_len, src, src_len))
  {
    #ifdef ARC_DEBUG
    fprintf(stderr, "arc_unlzw_block failed (%zu in, %zu out)\n",
     arc.lzw_in, arc.lzw_out);
    #endif
    goto err;
  }

  if(arc.lzw_out != dest_len)
  {
    #ifdef ARC_DEBUG
    fprintf(stderr, "out %zu != buffer size %zu\n", arc.lzw_out, dest_len);
    #endif
    goto err;
  }

  arc_unpack_free(&arc);
  return 0;

err:
  arc_unpack_free(&arc);
  return -1;
}

static int arc_unpack_lzw_rle90(uint8_t * ARC_RESTRICT dest, size_t dest_len,
 const uint8_t *src, size_t src_len, int init_width, int max_width)
{
  struct arc_unpack arc;
  uint8_t buffer[4096];
  int is_dynamic = (init_width != max_width);

  // This is only used for Spark method 0xff, which doesn't use RLE.
  if(max_width == ARC_MAX_CODE_IN_STREAM)
    return -1;
  if(max_width == ARC_IGNORE_CODE_IN_STREAM)
  {
    src++;
    src_len--;
    max_width = 12;
  }
  if(max_width < 9 || max_width > 16)
    return -1;

  if(arc_unpack_init(&arc, init_width, max_width, is_dynamic) != 0)
    return -1;

  while(arc.lzw_in < src_len)
  {
    arc.lzw_out = 0;
    if(arc_unlzw_block(&arc, buffer, sizeof(buffer), src, src_len) || !arc.lzw_out)
    {
      #ifdef ARC_DEBUG
      fprintf(stderr, "arc_unlzw_block failed "
       "(%zu in, %zu out in buffer, %zu out in stream)\n",
       arc.lzw_in, arc.lzw_out, arc.rle_out);
      #endif
      goto err;
    }

    if(arc_unrle90_block(&arc, dest, dest_len, buffer, arc.lzw_out))
    {
      #ifdef ARC_DEBUG
      fprintf(stderr, "arc_unrle90_block failed (%zu in, %zu out)\n",
       arc.lzw_in, arc.rle_out);
      #endif
      goto err;
    }
  }

  if(arc.rle_out != dest_len)
  {
    #ifdef ARC_DEBUG
    fprintf(stderr, "out %zu != buffer size %zu\n", arc.rle_out, dest_len);
    #endif
    goto err;
  }

  arc_unpack_free(&arc);
  return 0;

err:
  arc_unpack_free(&arc);
  return -1;
}

/**
 * Huffman decoding based on this blog post by Phaeron.
 * https://www.virtualdub.org/blog2/entry_345.html
 */
#define LOOKUP_BITS 10
#define LOOKUP_MASK ((1 << LOOKUP_BITS) - 1)

static int arc_huffman_init(struct arc_unpack *arc, const uint8_t *src, size_t src_len)
{
  size_t table_size = 1 << LOOKUP_BITS;
  size_t iter;
  size_t i;
  size_t j;

  arc->num_huffman = src[0] | (src[1] << 8);
  if(!arc->num_huffman || arc->num_huffman > 256)
    return -1;

  arc->lzw_in = 2UL + 4UL * arc->num_huffman;
  arc->lzw_bits_in = (arc->lzw_in << 3);
  if(arc->lzw_in > src_len)
    return -1;

  // Precompute huffman tree and lookup table.
  arc->huffman_lookup = (struct arc_lookup *)calloc(table_size, sizeof(struct arc_lookup));
  arc->huffman_tree = (struct arc_huffman_index *)malloc(arc->num_huffman * sizeof(struct arc_huffman_index));

  for(i = 0; i < arc->num_huffman; i++)
  {
    struct arc_huffman_index *e = &(arc->huffman_tree[i]);
    e->value[0] = src[i * 4 + 2] | (src[i * 4 + 3] << 8);
    e->value[1] = src[i * 4 + 4] | (src[i * 4 + 5] << 8);
    if(e->value[0] >= arc->num_huffman || e->value[1] >= arc->num_huffman)
      return -1;
  }

  for(i = 0; i < table_size; i++)
  {
    int index = 0;
    int value = i;
    int bits;
    if(arc->huffman_lookup[i].length)
      continue;

    for(bits = 0; index >= 0 && bits < LOOKUP_BITS; bits++)
    {
      index = arc->huffman_tree[index].value[value & 1];
      value >>= 1;
    }
    if(index >= 0)
      continue;

    iter = 1 << bits;
    for(j = i; j < table_size; j += iter)
    {
      arc->huffman_lookup[j].value = ~index;
      arc->huffman_lookup[j].length = bits;
    }
  }
  return 0;
}

static int arc_huffman_read_bits(struct arc_unpack *arc, const uint8_t *src, size_t src_len)
{
  struct arc_huffman_index *tree = arc->huffman_tree;
  struct arc_lookup *e;
  uint32_t peek;
  size_t bits_end;
  int index;

  // Optimize short values with precomputed table.
  peek = arc_get_bytes(src + arc->lzw_in, src_len - arc->lzw_in) >> (arc->lzw_bits_in & 7);

  e = &(arc->huffman_lookup[peek & LOOKUP_MASK]);
  if(e->length)
  {
    arc->lzw_bits_in += e->length;
    arc->lzw_in = arc->lzw_bits_in >> 3;
    return e->value;
  }

  bits_end = (src_len << 3);
  index = 0;

  while(index >= 0 && arc->lzw_bits_in < bits_end)
  {
    int bit = src[arc->lzw_bits_in >> 3] >> (arc->lzw_bits_in & 7);
    arc->lzw_bits_in++;

    index = tree[index].value[bit & 1];
  }
  if(index < 0)
  {
    arc->lzw_in = arc->lzw_bits_in >> 3;
    return ~index;
  }
  return -1;
}

static int arc_unhuffman_block(struct arc_unpack * ARC_RESTRICT arc,
 uint8_t * ARC_RESTRICT dest, size_t dest_len, const uint8_t *src, size_t src_len)
{
  while(arc->lzw_in < src_len && arc->lzw_out < dest_len)
  {
    int value = arc_huffman_read_bits(arc, src, src_len);
    if(value >= 256)
    {
      // End of stream code.
      arc->lzw_in = src_len;
      return 0;
    }
    if(value < 0)
      return -1;

    dest[arc->lzw_out++] = value;
  }
  return 0;
}

static int arc_unpack_huffman_rle90(uint8_t * ARC_RESTRICT dest, size_t dest_len,
 const uint8_t *src, size_t src_len)
{
  struct arc_unpack arc;
  uint8_t buffer[4096];

  if(arc_unpack_init(&arc, 0, 0, 0) != 0)
    return -1;

  if(arc_huffman_init(&arc, src, src_len) != 0)
    goto err;

  while(arc.lzw_in < src_len)
  {
    arc.lzw_out = 0;
    if(arc_unhuffman_block(&arc, buffer, sizeof(buffer), src, src_len) || !arc.lzw_out)
    {
      #ifdef ARC_DEBUG
      fprintf(stderr, "arc_unhuffman_block failed "
       "(%zu in, %zu out in buffer, %zu out in stream)\n",
       arc.lzw_in, arc.lzw_out, arc.rle_out);
      #endif
      goto err;
    }

    if(arc_unrle90_block(&arc, dest, dest_len, buffer, arc.lzw_out))
    {
      #ifdef ARC_DEBUG
      fprintf(stderr, "arc_unrle90_block failed (%zu in, %zu out)\n",
       arc.lzw_in, arc.rle_out);
      #endif
      goto err;
    }
  }

  if(arc.rle_out != dest_len)
  {
    #ifdef ARC_DEBUG
    fprintf(stderr, "out %zu != buffer size %zu\n", arc.rle_out, dest_len);
    #endif
    goto err;
  }

  arc_unpack_free(&arc);
  return 0;

err:
  arc_unpack_free(&arc);
  return -1;
}

const char *arc_unpack(unsigned char * ARC_RESTRICT dest, size_t dest_len,
 const unsigned char *src, size_t src_len, int method)
{
  switch(method)
  {
    case 1: // unpacked (old)
    case 2: // unpacked
      break; // Shouldn't be handled here.

    case 3: // packed (RLE)
      if(arc_unpack_rle90(dest, dest_len, src, src_len) < 0)
        return "failed unpack";
      break;

    case 4: // squeezed (RLE, huffman)
      if(arc_unpack_huffman_rle90(dest, dest_len, src, src_len) < 0)
        return "failed unsqueeze";
      break;

    case 8: // crunched (RLE, dynamic LZW 9 to 12)
      if(arc_unpack_lzw_rle90(dest, dest_len, src, src_len, 9, ARC_IGNORE_CODE_IN_STREAM))
        return "failed uncrunch";
      break;

    case 9: // PK squashed (dynamic LZW 9 to 13)
      if(arc_unpack_lzw(dest, dest_len, src, src_len, 9, 13))
        return "failed unsquash";
      break;

    case 0xff: // Spark compressed (dynamic LZW 9 to 16)
      if(arc_unpack_lzw(dest, dest_len, src, src_len, 9, ARC_MAX_CODE_IN_STREAM))
        return "failed uncompress";
      break;

    default:
      return "unsupported method";
  }
  return NULL;
}
