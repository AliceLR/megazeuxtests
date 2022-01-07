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
 * Unpacker for Amiga LZX compressed streams.
 * Based primarily on the LZX compression documentation from MSDN:
 *
 *   https://docs.microsoft.com/en-us/previous-versions/bb417343(v=msdn.10)?redirectedfrom=MSDN#microsoft-lzx-data-compression-format
 *
 * The following changes are required from the MSDN documentation for this
 * to work correctly:
 *
 *   * CAB LZX changed the block type values:
 *     1 is uncompressed in classic LZX, but is verbatim in CAB LZX.
 *     2 is verbatim in classic LZX, but is aligned offsets in CAB LZX.
 *     3 is aligned offsets in classic LZX, but is uncompressed in CAB LZX.
 *
 *   * The bitstream description is wrong for classic LZX. Amiga LZX reads
 *     big endian 16-bit codes into a little endian bitstream, but the MSDN
 *     documentation claims the opposite. This might have been a deliberate
 *     change since the MDSN version makes more sense for Huffman codes.
 *
 *   * Amiga LZX seems to use a fixed window size.
 *
 *   * The documentation states block lengths are a 24-bit field but fails to
 *     clarify that they're read in three 8-bit chunks big endian style.
 *
 *   * The documentation kind of forgot that uncompressed blocks have a length.
 *     It is three 8-bit fields following the block type, like verbatim.
 *
 *   * The aligned offset tree header documentation is totally wrong, even for
 *     CAB LZX: in CAB LZX, the aligned offset tree is after the length and
 *     before the main/lengths trees. In classic LZX, it's BEFORE the length.
 *     The documentation also forgot the lengths tree.
 *
 *   * FIXME even more things probably!!!!!
 *
 * The correct documentation for these features is in the MSDN LZX DELTA
 * specification (except where classic LZX differences are noted).
 */

#include "lzx_unpack.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

#define LZX_DEBUG

#define LZX_LOOKUP_BITS  11
#define LZX_LOOKUP_MASK  ((1 << LZX_CODES_LOOKUP_BITS) - 1)

#define LZX_MAX_CODES    (LZX_NUM_CHARS + 8 * 50)
#define LZX_MAX_ALIGNED  8
#define LZX_MAX_PRETREE  20

#define LZX_MAX_BINS     17
#define LZX_CODE_BINS    17
#define LZX_LENGTH_BINS  17
#define LZX_ALIGNED_BINS 8
#define LZX_PRETREE_BINS 16

/* MSDN constants. */
#define LZX_MIN_MATCH    2
#define LZX_MAX_MATCH    257
#define LZX_NUM_CHARS    256
#define LZX_MAX_LENGTHS  249

#ifdef LZX_DEBUG
#include <stdio.h>
#define debug(...) do{ fprintf(stderr, "" __VA_ARGS__); fflush(stderr); }while(0)
#endif

static const lzx_uint8 lzx_reverse8[] =
{
  0x00,0x80,0x40,0xc0,0x20,0xa0,0x60,0xe0,0x10,0x90,0x50,0xd0,0x30,0xb0,0x70,0xf0,
  0x08,0x88,0x48,0xc8,0x28,0xa8,0x68,0xe8,0x18,0x98,0x58,0xd8,0x38,0xb8,0x78,0xf8,
  0x04,0x84,0x44,0xc4,0x24,0xa4,0x64,0xe4,0x14,0x94,0x54,0xd4,0x34,0xb4,0x74,0xf4,
  0x0c,0x8c,0x4c,0xcc,0x2c,0xac,0x6c,0xec,0x1c,0x9c,0x5c,0xdc,0x3c,0xbc,0x7c,0xfc,
  0x02,0x82,0x42,0xc2,0x22,0xa2,0x62,0xe2,0x12,0x92,0x52,0xd2,0x32,0xb2,0x72,0xf2,
  0x0a,0x8a,0x4a,0xca,0x2a,0xaa,0x6a,0xea,0x1a,0x9a,0x5a,0xda,0x3a,0xba,0x7a,0xfa,
  0x06,0x86,0x46,0xc6,0x26,0xa6,0x66,0xe6,0x16,0x96,0x56,0xd6,0x36,0xb6,0x76,0xf6,
  0x0e,0x8e,0x4e,0xce,0x2e,0xae,0x6e,0xee,0x1e,0x9e,0x5e,0xde,0x3e,0xbe,0x7e,0xfe,
  0x01,0x81,0x41,0xc1,0x21,0xa1,0x61,0xe1,0x11,0x91,0x51,0xd1,0x31,0xb1,0x71,0xf1,
  0x09,0x89,0x49,0xc9,0x29,0xa9,0x69,0xe9,0x19,0x99,0x59,0xd9,0x39,0xb9,0x79,0xf9,
  0x05,0x85,0x45,0xc5,0x25,0xa5,0x65,0xe5,0x15,0x95,0x55,0xd5,0x35,0xb5,0x75,0xf5,
  0x0d,0x8d,0x4d,0xcd,0x2d,0xad,0x6d,0xed,0x1d,0x9d,0x5d,0xdd,0x3d,0xbd,0x7d,0xfd,
  0x03,0x83,0x43,0xc3,0x23,0xa3,0x63,0xe3,0x13,0x93,0x53,0xd3,0x33,0xb3,0x73,0xf3,
  0x0b,0x8b,0x4b,0xcb,0x2b,0xab,0x6b,0xeb,0x1b,0x9b,0x5b,0xdb,0x3b,0xbb,0x7b,0xfb,
  0x07,0x87,0x47,0xc7,0x27,0xa7,0x67,0xe7,0x17,0x97,0x57,0xd7,0x37,0xb7,0x77,0xf7,
  0x0f,0x8f,0x4f,0xcf,0x2f,0xaf,0x6f,0xef,0x1f,0x9f,0x5f,0xdf,0x3f,0xbf,0x7f,0xff
};

static lzx_uint16 lzx_reverse16(lzx_uint16 v)
{
  return (lzx_reverse8[v & 0xff] << 8) | lzx_reverse8[v >> 8];
}

typedef size_t bitcount;

/* In CAB LZX verbatim is 1, aligned offsets is 2, and uncompressed is 3.
 * In Amiga LZX uncompressed is 1, verbatim is 2, and aligned offsets is 3.
 * How helpful! */
enum lzx_block_type
{
  LZX_B_UNCOMPRESSED = 1,
  LZX_B_VERBATIM     = 2,
  LZX_B_ALIGNED      = 3,
};

struct lzx_lookup
{
  lzx_uint16 value;
  lzx_uint8  length;
};

struct lzx_bin
{
  lzx_uint16 offset; /* Translate code to its position in the values list. */
  lzx_uint16 last;   /* Position after last valid position in this bin. */
};

struct lzx_tree
{
  lzx_uint16 *values;
  struct lzx_lookup *lookup;
  unsigned num_values;
  unsigned num_bins;
  struct lzx_bin bins[LZX_MAX_BINS];
};

struct lzx_data
{
  bitcount bits_in;
  size_t in;
  size_t out;
  lzx_uint32 prev_offsets[3];
  unsigned block_type;
  size_t window_bits;
  size_t num_codes;

  struct lzx_tree codes;
  struct lzx_tree lengths;
  struct lzx_tree aligned;
  struct lzx_tree pretree;

  lzx_uint16 code_values[LZX_MAX_CODES];
  lzx_uint16 length_values[LZX_MAX_LENGTHS];
  lzx_uint16 aligned_values[LZX_MAX_ALIGNED];
  lzx_uint16 pretree_values[LZX_MAX_PRETREE];

  /* LZX stores delta widths for codes and lengths between blocks. */
  lzx_uint8 code_widths[LZX_MAX_CODES];
  lzx_uint8 length_widths[LZX_MAX_LENGTHS];
};

static struct lzx_data *lzx_unpack_init(int windowbits)
{
  struct lzx_data *lzx;

  if(windowbits < 15 || windowbits > 21)
    return NULL;

  lzx = calloc(1, sizeof(struct lzx_data));

  lzx->prev_offsets[0] = 1;
  lzx->prev_offsets[1] = 1;
  lzx->prev_offsets[2] = 1;
  lzx->window_bits = windowbits;
  lzx->num_codes = LZX_NUM_CHARS + 8 * (lzx->window_bits << 1);

  lzx->codes.values   = lzx->code_values;
  lzx->lengths.values = lzx->length_values;
  lzx->aligned.values = lzx->aligned_values;
  lzx->pretree.values = lzx->pretree_values;
  return lzx;
}

static int lzx_unpack_init_lookups(struct lzx_data *lzx)
{
  if(lzx->codes.lookup)
    return 0;

/* TODO after everything else is tested.
  lzx->codes.lookup = (struct lzx_lookup *)malloc((1 << LZX_LOOKUP_BITS) * sizeof(struct lzx_lookup));
  if(!lzx->codes.lookup)
    return -1;
  lzx->lengths.lookup = (struct lzx_lookup *)malloc((1 << LZX_LOOKUP_BITS) * sizeof(struct lzx_lookup));
  if(!lzx->lengths.lookup)
    return -1;
*/
  return 0;
}

static void lzx_unpack_free(struct lzx_data *lzx)
{
  free(lzx->codes.lookup);
  free(lzx->lengths.lookup);
  free(lzx);
}

static int lzx_align(struct lzx_data * LZX_RESTRICT lzx, size_t src_len)
{
  /* Align to 2-byte boundary. */
  if(lzx->bits_in & 0x0F)
  {
    lzx->bits_in += 0x10 - (lzx->bits_in & 0x0F);
    if((lzx->bits_in >> 3) >= src_len)
      return -1;
  }
  return 0;
}

/* Amiga LZX uses a little endian (right shift) bitstream
 * but rather than appending bytes, it appends 16-bit big endian words.
 */
static lzx_uint32 lzx_get_bytes(const unsigned char *pos, unsigned num)
{
  switch(num)
  {
    case 0:
    case 1:  return 0;
    case 2:
    case 3:  return (pos[0] << 8UL) | pos[1];
    default: return (pos[0] << 8UL) | pos[1] | (pos[2] << 24UL) | (pos[3] << 16UL);
  }
}

static lzx_uint32 lzx_peek_bits(struct lzx_data * LZX_RESTRICT lzx,
 const unsigned char *src, size_t src_len, unsigned num)
{
  static const lzx_uint16 BIT_MASKS[17] =
  {
    0, 0x1, 0x3, 0x7, 0xf, 0x1f, 0x3f, 0x7f, 0xff, 0x1ff,
    0x3ff, 0x7ff, 0xfff, 0x1fff, 0x3fff, 0x7fff, 0xffff
  };

  lzx_uint32 bits;
  size_t in;

  assert(num <= 16); /* Should never happen. */

  in   = (lzx->bits_in >> 3) & ~1;
  bits = lzx_get_bytes(src + in, src_len - in) >> (lzx->bits_in & 15);
  bits &= BIT_MASKS[num];
  return bits;
}

static lzx_int32 lzx_get_bits(struct lzx_data * LZX_RESTRICT lzx,
 const unsigned char *src, size_t src_len, unsigned num)
{
  lzx_uint32 bits;
  if(lzx->bits_in + num > (bitcount)src_len << 3)
    return -1;

  bits = lzx_peek_bits(lzx, src, src_len, num);
  lzx->bits_in += num;
  return bits;
}

/*
 * Huffman decoder.
 *
 * Since LZX uses canonical Huffman, the Huffman tree can be optimized out
 * entirely. All that is required is a set of bins for all of the bit widths
 * and a list of values in the order they appear in the tree, from left to
 * right. To get the list index, subtract bin.offset from a code. If the
 * index is less than bin.last, it is a valid code for that width.
 *
 * A lookup table can be used on top of this as with usual Huffman trees.
 * And if the bitstream thing above wasn't bad enough, the codes are reversed.
 */

static lzx_int32 lzx_get_huffman(struct lzx_data * LZX_RESTRICT lzx,
 const struct lzx_tree *tree, const unsigned char *src, size_t src_len)
{
  lzx_uint32 peek;
  unsigned pos = 1;

  peek = lzx_peek_bits(lzx, src, src_len, 16);
  peek = lzx_reverse16(peek);
  if(tree->lookup)
  {
    struct lzx_lookup e = tree->lookup[peek >> (16 - LZX_LOOKUP_BITS)];
    if(e.length)
    {
      if((lzx->bits_in >> 3) > src_len)
        return -1;

      lzx->bits_in += e.length;
      return e.value;
    }
    pos += LZX_LOOKUP_BITS;
  }

  for(; pos < tree->num_bins; pos++)
  {
    unsigned code = peek >> (16 - pos);
    code -= tree->bins[pos].offset;

    if(code < tree->bins[pos].last)
    {
      if((lzx->bits_in >> 3) > src_len)
        return -1;

      lzx->bits_in += pos;
      return tree->values[code];
    }
  }
  return -1;
}

static void lzx_prepare_huffman(struct lzx_tree * LZX_RESTRICT tree,
 const lzx_uint8 *counts, const lzx_uint8 *widths, unsigned max_codes,
 unsigned max_bins)
{
  lzx_uint8 offsets[LZX_CODE_BINS];
  unsigned pos = 0;
  unsigned first = 0;
  unsigned i;

  tree->num_values = max_codes - counts[0];
  tree->num_bins = 0;

  for(i = 1; i < max_bins; i++)
  {
    offsets[i] = pos;
    pos += counts[i];

    if(counts[i])
      tree->num_bins = i + 1;

    tree->bins[i].offset = first;
    tree->bins[i].last = pos;

    first = (first + counts[i]) << 1;
  }

  for(i = 0; i < max_codes; i++)
  {
    unsigned width = widths[i];
    unsigned offset = offsets[width]++;
    tree->values[offset] = i;
  }
}

static void lzx_prepare_lookup(struct lzx_tree * LZX_RESTRICT tree,
 const lzx_uint8 *counts)
{
  struct lzx_lookup *dest = tree->lookup;
  struct lzx_lookup e;
  unsigned bin = 1;
  unsigned j = 0;
  unsigned i;
  unsigned fill;

  if(!tree->lookup)
    return;

  for(i = 0; i < tree->num_values; i++)
  {
    while(j >= counts[bin])
    {
      bin++;
      j = 0;
      if(bin >= tree->num_bins || bin > LZX_LOOKUP_BITS)
        return;
    }
    j++;

    e.value = tree->values[i];
    e.length = bin;
    fill = 1 << (LZX_LOOKUP_BITS - bin);
    while(fill--)
      *(dest++) = e;
  }
}

static int lzx_read_aligned(struct lzx_data * LZX_RESTRICT lzx,
 const unsigned char *src, size_t src_len)
{
  struct lzx_tree *tree = &(lzx->aligned);
  lzx_uint8 widths[LZX_MAX_ALIGNED];
  lzx_uint8 counts[LZX_ALIGNED_BINS];
  unsigned i;

  memset(counts, 0, sizeof(counts));

  for(i = 0; i < LZX_MAX_ALIGNED; i++)
  {
    lzx_int32 w = lzx_get_bits(lzx, src, src_len, 3);
    if(w < 0)
      return -1;
    widths[i] = w;
    counts[w]++;
  }

  lzx_prepare_huffman(tree, counts, widths, LZX_MAX_ALIGNED, LZX_ALIGNED_BINS);
  return 0;
}

static int lzx_read_pretree(struct lzx_data * LZX_RESTRICT lzx,
 const unsigned char *src, size_t src_len)
{
  struct lzx_tree *tree = &(lzx->pretree);
  lzx_uint8 widths[LZX_MAX_PRETREE];
  lzx_uint8 counts[LZX_PRETREE_BINS];
  unsigned i;

  memset(counts, 0, sizeof(counts));

  for(i = 0; i < LZX_MAX_PRETREE; i++)
  {
    lzx_int32 w = lzx_get_bits(lzx, src, src_len, 4);
    if(w < 0)
      return -1;
    widths[i] = w;
    counts[w]++;
  }

  lzx_prepare_huffman(tree, counts, widths, LZX_MAX_PRETREE, LZX_PRETREE_BINS);
  return 0;
}

static int lzx_read_delta(struct lzx_data *lzx,
 lzx_uint8 * LZX_RESTRICT counts, lzx_uint8 * LZX_RESTRICT widths,
 int i, int max, const unsigned char *src, size_t src_len)
{
  while(i < max)
  {
    lzx_int32 w = lzx_get_huffman(lzx, &(lzx->pretree), src, src_len);
    lzx_int32 num;
    if(w < 0 || w >= 20)
      return -1;

    switch(w)
    {
      default:
        widths[i] = (widths[i] + w) % 17;
        counts[widths[i]]++;
        i++;
        break;

      case 17:
        num = lzx_get_bits(lzx, src, src_len, 4) + 4;
        if(num < 4 || num > max - i)
          return -1;

        memset(widths + i, 0, num);
        counts[0] += num;
        i += num;
        break;

      case 18:
        num = lzx_get_bits(lzx, src, src_len, 5) + 20;
        if(num < 20 || num > max - i)
          return -1;

        memset(widths + i, 0, num);
        counts[0] += num;
        i += num;
        break;

      case 19:
        num = lzx_get_bits(lzx, src, src_len, 1) + 4;
        if(num < 4 || num > max - i)
          return -1;

        w = lzx_get_huffman(lzx, &(lzx->pretree), src, src_len);
        if(w < 0)
          return -1;

        w = (widths[i] + w) % 17;
        memset(widths + i, w, num);
        counts[w] += num;
        i += num;
        break;
    }
  }
  return 0;
}

static int lzx_read_lengths(struct lzx_data * LZX_RESTRICT lzx,
 const unsigned char *src, size_t src_len)
{
  struct lzx_tree *tree = &(lzx->lengths);
  lzx_uint8 *widths = lzx->length_widths;
  lzx_uint8 counts[LZX_LENGTH_BINS];

  memset(counts, 0, sizeof(counts));

  /* Read pretree and lengths. */
  if(lzx_read_pretree(lzx, src, src_len) < 0)
    return -1;
  if(lzx_read_delta(lzx, counts, widths, 0, LZX_MAX_LENGTHS, src, src_len) < 0)
    return -1;

  lzx_prepare_huffman(tree, counts, lzx->length_widths, LZX_MAX_LENGTHS, LZX_LENGTH_BINS);
  lzx_prepare_lookup(tree, counts);
  return 0;
}

static int lzx_read_codes(struct lzx_data * LZX_RESTRICT lzx,
 const unsigned char *src, size_t src_len)
{
  struct lzx_tree *tree = &(lzx->codes);
  lzx_uint8 *widths = lzx->code_widths;
  lzx_uint8 counts[LZX_CODE_BINS];

  memset(counts, 0, sizeof(counts));

  /* Read pretree and first 256 codes. */
  if(lzx_read_pretree(lzx, src, src_len) < 0)
    return -1;
  if(lzx_read_delta(lzx, counts, widths, 0, 256, src, src_len) < 0)
    return -1;

  /* Read pretree and offset codes. */
  if(lzx_read_pretree(lzx, src, src_len) < 0)
    return -1;
  if(lzx_read_delta(lzx, counts, widths, 256, lzx->num_codes, src, src_len) < 0)
    return -1;

  lzx_prepare_huffman(tree, counts, widths, LZX_MAX_CODES, LZX_CODE_BINS);
  lzx_prepare_lookup(tree, counts);
  return 0;
}

/*
 * Sliding dictionary support.
 *
 * LZX uses some unique hacks on top of its otherwise typical LZ77 sliding
 * dictionary. FIXME document them here.
 */

static lzx_uint32 lzx_translate_offset(struct lzx_data *lzx, lzx_uint32 offset_in)
{
  if(offset_in >= 3)
  {
    lzx->prev_offsets[2] = lzx->prev_offsets[1];
    lzx->prev_offsets[1] = lzx->prev_offsets[0];
    lzx->prev_offsets[0] = offset_in - 2;
    return lzx->prev_offsets[0];
  }

  if(offset_in >= 1)
  {
    lzx_uint32 tmp = lzx->prev_offsets[offset_in];
    lzx->prev_offsets[offset_in] = lzx->prev_offsets[0];
    lzx->prev_offsets[0] = tmp;
    return tmp;
  }

  return lzx->prev_offsets[0];
}









static int lzx_unpack_normal(unsigned char * LZX_RESTRICT dest, size_t dest_len,
 const unsigned char *src, size_t src_len, int windowbits)
{
  struct lzx_data *lzx;
  lzx_int32 bytes_out;
  lzx_int32 bytes_in;

  lzx = lzx_unpack_init(windowbits);
  if(!lzx)
    return -1;

  /* NOTE: CAB LZX extension header for x86 machine code goes here. */

  while(lzx->out < dest_len)
  {
    unsigned block_type = lzx_get_bits(lzx, src, src_len, 3);

    /* For some reason that I'm SURE made sense, the
     * aligned offsets tree goes here in Amiga LZX. */
    if(block_type == LZX_B_ALIGNED)
      if(lzx_read_aligned(lzx, src, src_len) < 0)
        goto err;

    bytes_out = lzx_get_bits(lzx, src, src_len, 8) << 16;
    bytes_out |= lzx_get_bits(lzx, src, src_len, 8) << 8;
    bytes_out |= lzx_get_bits(lzx, src, src_len, 8);
    if(bytes_out < 0)
      goto err;

    #ifdef LZX_DEBUG
    debug("block type:%u uncompr.size:%zu (%06zx)\n", block_type,
     (size_t)bytes_out, (size_t)bytes_out);
    #endif

    switch(block_type)
    {
      default:
        goto err;

      case LZX_B_VERBATIM:
        if(lzx_unpack_init_lookups(lzx) < 0)
          goto err;
        if(lzx_read_codes(lzx, src, src_len) < 0)
          goto err;
        if(lzx_read_lengths(lzx, src, src_len) < 0)
          goto err;

        #ifdef LZX_DEBUG
        debug("verbatim block not implemented :(\n");
        #endif

        // FIXME! read codes
        goto err;

      case LZX_B_ALIGNED:
        if(lzx_unpack_init_lookups(lzx) < 0)
          goto err;
        /* NOTE: in CAB LZX, the aligned offsets tree goes here. */
        if(lzx_read_codes(lzx, src, src_len) < 0)
          goto err;
        if(lzx_read_lengths(lzx, src, src_len) < 0)
          goto err;

        #ifdef LZX_DEBUG
        debug("aligned offset block not implemented :(\n");
        #endif

        // FIXME! read codes
        goto err;

      case LZX_B_UNCOMPRESSED:
        /* Align to 16-bit boundary. */
        if(lzx_align(lzx, src_len) < 0)
          goto err;

        bytes_in = 12 + bytes_out;
        lzx->in = lzx->bits_in >> 3;

        if(lzx->in >= src_len || (size_t)bytes_in > src_len - lzx->in ||
           lzx->out >= dest_len || (size_t)bytes_out > dest_len - lzx->out)
          goto err;

        lzx->prev_offsets[0] = lzx_mem_u32(src + lzx->in + 0);
        lzx->prev_offsets[1] = lzx_mem_u32(src + lzx->in + 4);
        lzx->prev_offsets[2] = lzx_mem_u32(src + lzx->in + 8);

        memcpy(dest + lzx->out, src + lzx->in + 12, bytes_out);

        lzx->bits_in += ((bitcount)bytes_in << 3);
        lzx->out += bytes_out;
        break;
    }
  }

  lzx_unpack_free(lzx);
  return 0;

err:
  lzx_unpack_free(lzx);
  return -1;
}

const char *lzx_unpack(unsigned char * LZX_RESTRICT dest, size_t dest_len,
 const unsigned char *src, size_t src_len, int method, int windowbits)
{
  switch(method)
  {
    case LZX_M_PACKED:
      if(lzx_unpack_normal(dest, dest_len, src, src_len, windowbits) < 0)
        return "unpack failed";
      return NULL;
  }
  return "unsupported method";
}
