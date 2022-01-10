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
 * Based primarily on the LZX compression documentation from MSDN, with
 * further reference and corrections based on temisu's Ancient decompressor:
 *
 *   https://docs.microsoft.com/en-us/previous-versions/bb417343(v=msdn.10)?redirectedfrom=MSDN#microsoft-lzx-data-compression-format
 *   https://github.com/temisu/ancient/blob/master/src/LZXDecompressor.cpp
 *
 * The following changes are required from the MSDN documentation for this
 * to work correctly:
 *
 *   * CAB LZX changed the block type values:
 *     1 is verbatim but reuses the previous tree in classic LZX.
 *     2 is verbatim in classic LZX, but is aligned offsets in CAB LZX.
 *     3 is aligned offsets in classic LZX, but is uncompressed in CAB LZX.
 *
 *   * The bitstream description is wrong for classic LZX. Amiga LZX reads
 *     big endian 16-bit codes into a little endian bitstream, but CAB LZX
 *     appears to have been updated to do the opposite.
 *
 *   * Amiga LZX uses a fixed 64k window and 512 distance+length codes. It
 *     does not have a separate lengths tree. The distance slot is determined
 *     by (symbol - 256) & 0x1f and the length slot is determined by
 *     (symbol - 256) >> 5. Both use the same set of slots, which are the same
 *     as the first 32 CAB LZX position slots.
 *
 *   * The documentation states block lengths are a 24-bit field but fails to
 *     clarify that they're read in three 8-bit chunks big endian style. This
 *     is corrected in the LZX DELTA specification.
 *
 *   * The aligned offset tree header documentation is wrong, even for CAB:
 *     in CAB LZX, the aligned offset tree is after the block length, but in
 *     Amiga LZX, it's BEFORE the block length.
 *
 *   * The code tree width delta algorithm is incorrectly documented as
 *     (prev_len[x] + code) % 17 instead of (prev_len[x] - code + 17) % 17.
 *     This is corrected in the LZX DELTA specification. The Amiga LZX delta
 *     RLE codes also have separate behavior for the two main tree blocks.
 *
 *   * In CAB LZX the aligned offsets tree is only used for >3 bit distances,
 *     but Amiga LZX also uses it for 3 bit distances.
 */

#include "lzx_unpack.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

/* #define LZX_DEBUG */

#define LZX_LOOKUP_BITS  11
#define LZX_LOOKUP_MASK  ((1 << LZX_CODES_LOOKUP_BITS) - 1)

#define LZX_NUM_CHARS    256
#define LZX_MAX_CODES    (LZX_NUM_CHARS + 512)
#define LZX_MAX_ALIGNED  8
#define LZX_MAX_PRETREE  20

#define LZX_MAX_BINS     17
#define LZX_CODE_BINS    17
#define LZX_ALIGNED_BINS 8
#define LZX_PRETREE_BINS 16

/* This is 2 in CAB LZX, but using 2 in Amiga LZX results in short reads. */
#define LZX_MIN_MATCH    3

#ifdef LZX_DEBUG
#include <stdio.h>
#define debug(...) do{ fprintf(stderr, "" __VA_ARGS__); fflush(stderr); }while(0)
#endif

/* Position slot base positions table from MSDN documentation. */
static const unsigned lzx_slot_base[32] =
{
     0,    1,    2,     3,     4,     6,     8,    12,
    16,   24,   32,    48,    64,    96,   128,   192,
   256,  384,  512,   768,  1024,  1536,  2048,  3072,
  4096, 6144, 8192, 12288, 16384, 24576, 32768, 49152
};

/* Position slot footer bits table from MSDN documentation. */
static const unsigned lzx_slot_bits[32] =
{
   0,  0,  0,  0,  1,  1,  2,  2,
   3,  3,  4,  4,  5,  5,  6,  6,
   7,  7,  8,  8,  9,  9, 10, 10,
  11, 11, 12, 12, 13, 13, 14, 14
};

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

static lzx_uint16 lzx_mem_u16be(const unsigned char *buf)
{
  return (buf[0] << 8) | buf[1];
}

/* In CAB LZX verbatim is 1, aligned offsets is 2, and uncompressed is 3.
 * In Amiga LZX 1 is verbatim without a stored tree, 2 is verbatim with a
 * stored tree, and 3 is aligned offsets. */
enum lzx_block_type
{
  LZX_B_VERBATIM_SAME = 1,
  LZX_B_VERBATIM      = 2,
  LZX_B_ALIGNED       = 3,
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
  unsigned min_bin;
  struct lzx_bin bins[LZX_MAX_BINS];
};

struct lzx_data
{
  size_t in;
  size_t out;

  /* The unusual bit packing scheme makes a bit buffer faster than direct reads... */
  size_t buffer;
  size_t buffer_left;

  struct lzx_tree codes;
  struct lzx_tree aligned;
  struct lzx_tree pretree;

  lzx_uint16 code_values[LZX_MAX_CODES];
  lzx_uint16 aligned_values[LZX_MAX_ALIGNED];
  lzx_uint16 pretree_values[LZX_MAX_PRETREE];

  /* LZX stores delta widths for codes between blocks. */
  lzx_uint8 code_widths[LZX_MAX_CODES];
};

static struct lzx_data *lzx_unpack_init()
{
  struct lzx_data *lzx = (struct lzx_data *)calloc(1, sizeof(struct lzx_data));
  if(!lzx)
    return NULL;

  lzx->codes.values   = lzx->code_values;
  lzx->aligned.values = lzx->aligned_values;
  lzx->pretree.values = lzx->pretree_values;

/* TODO after everything else is tested.
  lzx->codes.lookup = (struct lzx_lookup *)malloc((1 << LZX_LOOKUP_BITS) * sizeof(struct lzx_lookup));
  if(!lzx->codes.lookup)
  {
    free(lzx);
    return NULL;
  }
*/
  return lzx;
}

static void lzx_unpack_free(struct lzx_data *lzx)
{
  free(lzx->codes.lookup);
  free(lzx);
}

/* Amiga LZX uses a little endian (right shift) bitstream
 * but rather than appending bytes, it appends 16-bit big endian words.
 */
static void lzx_word_in(struct lzx_data * LZX_RESTRICT lzx,
 const unsigned char *src, size_t src_len)
{
  if(src_len - lzx->in >= 2)
  {
    lzx->buffer |= (size_t)lzx_mem_u16be(src + lzx->in) << lzx->buffer_left;
    lzx->buffer_left += 16;
    lzx->in += 2;
  }
}

/* Not guaranteed to return the requested number of bits! */
static lzx_uint32 lzx_peek_bits(struct lzx_data * LZX_RESTRICT lzx,
 const unsigned char *src, size_t src_len, unsigned num)
{
  static const lzx_uint16 BIT_MASKS[17] =
  {
        0,
      0x1,   0x3,   0x7,   0xf,   0x1f,   0x3f,   0x7f,   0xff,
    0x1ff, 0x3ff, 0x7ff, 0xfff, 0x1fff, 0x3fff, 0x7fff, 0xffff
  };
  #ifdef LZX_DEBUG
  /* It is currently impossible for >16 to reach here but
   * this assert might be useful for debug. */
  assert(num <= 16);
  #endif

  if(lzx->buffer_left < num)
  {
    /* Minor optimization for 64-bit systems:
     * buffer_left < 16, so 3 words can be read into the buffer. */
    if(sizeof(size_t) >= 8)
    {
      lzx_word_in(lzx, src, src_len);
      lzx_word_in(lzx, src, src_len);
    }
    lzx_word_in(lzx, src, src_len);
  }
  return lzx->buffer & BIT_MASKS[num];
}

/* Bounds check and discard bits from lzx_peek_bits. */
static int lzx_skip_bits(struct lzx_data * LZX_RESTRICT lzx,
 unsigned num)
{
  if(lzx->buffer_left < num)
    return -1;

  lzx->buffer >>= num;
  lzx->buffer_left -= num;
  return 0;
}

/* Read and remove bits from the bitstream (effectively peek + skip). */
static lzx_int32 lzx_get_bits(struct lzx_data * LZX_RESTRICT lzx,
 const unsigned char *src, size_t src_len, unsigned num)
{
  lzx_uint32 peek = lzx_peek_bits(lzx, src, src_len, num);
  if(lzx_skip_bits(lzx, num) < 0)
    return -1;

  return peek;
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

static int lzx_get_huffman(struct lzx_data * LZX_RESTRICT lzx,
 const struct lzx_tree *tree, const unsigned char *src, size_t src_len)
{
  lzx_uint32 peek;
  unsigned pos = tree->min_bin;

  peek = lzx_peek_bits(lzx, src, src_len, 16);
  peek = lzx_reverse16(peek);

  if(tree->lookup)
  {
    struct lzx_lookup e = tree->lookup[peek >> (16 - LZX_LOOKUP_BITS)];
    if(e.length)
    {
      if(lzx_skip_bits(lzx, e.length) < 0)
        return -1;

      return e.value;
    }
    pos = LZX_LOOKUP_BITS + 1;
  }

  for(; pos < tree->num_bins; pos++)
  {
    unsigned code = peek >> (16 - pos);
    code -= tree->bins[pos].offset;

    if(code < tree->bins[pos].last)
    {
      if(lzx_skip_bits(lzx, pos) < 0)
        return -1;

      return tree->values[code];
    }
  }
  return -1;
}

static int lzx_prepare_huffman(struct lzx_tree * LZX_RESTRICT tree,
 const lzx_uint8 *counts, const lzx_uint8 *widths, unsigned max_codes,
 unsigned max_bins)
{
  unsigned offsets[LZX_CODE_BINS];
  unsigned pos = 0;
  unsigned first = 0;
  unsigned i;

  tree->num_values = 0;
  tree->num_bins = 0;
  tree->min_bin = 0;

  for(i = 1; i < max_bins; i++)
  {
    offsets[i] = pos;
    pos += counts[i];

    if(counts[i])
    {
      if(!tree->min_bin)
        tree->min_bin = i;
      tree->num_bins = i + 1;
      tree->num_values = pos;
    }

    tree->bins[i].offset = first - offsets[i];
    tree->bins[i].last = pos;
    first = (first + counts[i]) << 1;

    #ifdef LZX_DEBUG
    if(tree->min_bin)
      debug("bin %u: %04x %u\n", i, tree->bins[i].offset, tree->bins[i].last);
    #endif
  }

  /* The "first" value after all of the bins are generated should be the
   * theoretical maximum number of codes that can be stored in the tree.
   * If these aren't the same, the Huffman tree is under/over-specified.
   * (These values are actually both twice the maximum number of codes.) */
  #ifdef LZX_DEBUG
  debug("Huffman tree: sum=%u expected=%u\n", first, 1 << max_bins);
  #endif
  if(first != 1U << max_bins)
    return -1;

  for(i = 0; i < max_codes; i++)
  {
    if(widths[i] > 0)
    {
      unsigned offset = offsets[widths[i]]++;
      tree->values[offset] = i;
    }
  }
  #ifdef LZX_DEBUG
  if(max_codes <= 20)
    for(i = 0; i < tree->num_values; i++)
      debug("code %u: %u\n", i, tree->values[i]);
  #endif
  return 0;
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

  // FIXME remove
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

  #ifdef LZX_DEBUG
  debug("aligned offsets\n");
  #endif
  for(i = 0; i < LZX_MAX_ALIGNED; i++)
  {
    lzx_int32 w = lzx_get_bits(lzx, src, src_len, 3);
    if(w < 0)
      return -1;

    widths[i] = w;
    counts[w]++;
  }
  return lzx_prepare_huffman(tree, counts, widths, LZX_MAX_ALIGNED, LZX_ALIGNED_BINS);
}

static int lzx_read_pretree(struct lzx_data * LZX_RESTRICT lzx,
 const unsigned char *src, size_t src_len)
{
  struct lzx_tree *tree = &(lzx->pretree);
  lzx_uint8 widths[LZX_MAX_PRETREE];
  lzx_uint8 counts[LZX_PRETREE_BINS];
  unsigned i;

  memset(counts, 0, sizeof(counts));

  #ifdef LZX_DEBUG
  debug("pretree\n");
  #endif
  for(i = 0; i < LZX_MAX_PRETREE; i++)
  {
    lzx_int32 w = lzx_get_bits(lzx, src, src_len, 4);
    if(w < 0)
      return -1;

    widths[i] = w;
    counts[w]++;
  }
  return lzx_prepare_huffman(tree, counts, widths, LZX_MAX_PRETREE, LZX_PRETREE_BINS);
}

static int lzx_read_delta(struct lzx_data *lzx,
 lzx_uint8 * LZX_RESTRICT counts, lzx_uint8 * LZX_RESTRICT widths,
 int i, int max, const unsigned char *src, size_t src_len)
{
  /* In Amiga LZX (but not CAB LZX) the RLE bit reads and repeat count
   * values vary depending on which section of the tree is being read.
   * The changes for this were found by experimenting with LZX files and
   * then confirming against other Amiga LZX decompressors. */
  int is_dists = (i >= LZX_NUM_CHARS);
  #ifdef LZX_DEBUG
  debug("code deltas %d through %d\n", i, max);
  #endif

  while(i < max)
  {
    lzx_int32 w = lzx_get_huffman(lzx, &(lzx->pretree), src, src_len);
    lzx_int32 bits;
    lzx_int32 num;
    if(w < 0 || w >= 20)
      return -1;

    switch(w)
    {
      default:
        widths[i] = (widths[i] + 17 - w) % 17;
        counts[widths[i]]++;
        i++;
        break;

      case 17: /* Short run of 0. */
        bits = lzx_get_bits(lzx, src, src_len, 4);
        num = bits + 4 - is_dists;
        if(bits < 0 || num > max - i)
          return -1;

        memset(widths + i, 0, num);
        counts[0] += num;
        i += num;
        break;

      case 18: /* Long run of 0. */
        bits = lzx_get_bits(lzx, src, src_len, 5 + is_dists);
        num = bits + 20 - is_dists;
        if(bits < 0 || num > max - i)
          return -1;

        memset(widths + i, 0, num);
        counts[0] += num;
        i += num;
        break;

      case 19: /* Short run of same value. */
        bits = lzx_get_bits(lzx, src, src_len, 1);
        num = bits + 4 - is_dists;
        if(bits < 0 || num > max - i)
          return -1;

        w = lzx_get_huffman(lzx, &(lzx->pretree), src, src_len);
        if(w < 0 || w > 16)
          return -1;

        w = (widths[i] + 17 - w) % 17;
        memset(widths + i, w, num);
        counts[w] += num;
        i += num;
        break;
    }
  }
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
  if(lzx_read_delta(lzx, counts, widths, 0, LZX_NUM_CHARS, src, src_len) < 0)
    return -1;

  /* Read pretree and distance codes. */
  if(lzx_read_pretree(lzx, src, src_len) < 0)
    return -1;
  if(lzx_read_delta(lzx, counts, widths, LZX_NUM_CHARS, LZX_MAX_CODES, src, src_len) < 0)
    return -1;

  if(lzx_prepare_huffman(tree, counts, widths, LZX_MAX_CODES, LZX_CODE_BINS) < 0)
    return -1;
  lzx_prepare_lookup(tree, counts);
  return 0;
}

/*
 * LZX unpacking.
 */

static void lzx_copy_dictionary(struct lzx_data * LZX_RESTRICT lzx,
 unsigned char * LZX_RESTRICT dest, ptrdiff_t distance, size_t length)
{
  ptrdiff_t offset = (ptrdiff_t)lzx->out - distance;

  if(offset < 0)
  {
    size_t count = -offset;
    if(count > length)
      count = length;

    if(count > 0)
    {
      memset(dest + lzx->out, 0, count);
      lzx->out += count;
      length -= count;
      offset = 0;
    }
  }

  if(length > 0)
  {
    if((size_t)offset + length > lzx->out)
    {
      unsigned char *pos = dest + offset;
      unsigned char *end = pos + length;
      dest += lzx->out;
      while(pos < end)
        *(dest++) = *(pos++);
    }
    else
      memcpy(dest + lzx->out, dest + offset, length);

    lzx->out += length;
  }
}

static int lzx_unpack_normal(unsigned char * LZX_RESTRICT dest, size_t dest_len,
 const unsigned char *src, size_t src_len)
{
  struct lzx_data *lzx;
  lzx_int32 bytes_out;
  /* NOTE: CAB LZX stores three previous distances. */
  size_t prev_distance = 1;

  lzx = lzx_unpack_init();
  if(!lzx)
    return -1;

  /* NOTE: CAB LZX extension header for x86 machine code goes here. */

  while(lzx->out < dest_len)
  {
    unsigned block_type = lzx_get_bits(lzx, src, src_len, 3);
    #ifdef LZX_DEBUG
    debug("\nblock type:%u\n", block_type);
    #endif

    /* For some reason that I'm SURE made sense, the
     * aligned offsets tree goes here in Amiga LZX. */
    if(block_type == LZX_B_ALIGNED)
      if(lzx_read_aligned(lzx, src, src_len) < 0)
        goto err;

    bytes_out = lzx_get_bits(lzx, src, src_len, 8) << 16;
    bytes_out |= lzx_get_bits(lzx, src, src_len, 8) << 8;
    bytes_out |= lzx_get_bits(lzx, src, src_len, 8);
    if(bytes_out < 0 || (size_t)bytes_out > dest_len - lzx->out)
      goto err;

    #ifdef LZX_DEBUG
    debug("uncompr.size:%zu (%06zx)\n", (size_t)bytes_out, (size_t)bytes_out);
    #endif

    switch(block_type)
    {
      /* NOTE: CAB LZX has an uncompressed block type and gets rid of
       * the verbatim with same tree block type. */
      default:
        goto err;

      case LZX_B_ALIGNED:
        /* NOTE: in CAB LZX, the aligned offsets tree goes here. */
        /* fall-through */

      case LZX_B_VERBATIM:
        if(lzx_read_codes(lzx, src, src_len) < 0)
          goto err;
        /* NOTE: CAB LZX reads a dedicated lengths tree here. */
        break;

      case LZX_B_VERBATIM_SAME:
        /* Should never be the first block type. */
        if(!lzx->codes.num_values)
          goto err;
        break;
    }

    while(bytes_out)
    {
      int slot, bits;
      lzx_int32 distance, length;

      int code = lzx_get_huffman(lzx, &(lzx->codes), src, src_len);
      if(code < 0)
      {
        #ifdef LZX_DEBUG
        debug("failed to read code (in:%zu out:%zu)\n", lzx->in, lzx->out);
        #endif
        goto err;
      }

      if(code < LZX_NUM_CHARS)
      {
        dest[lzx->out++] = code;
        bytes_out--;
        continue;
      }

      slot     = (code - LZX_NUM_CHARS) & 0x1f;
      distance = lzx_slot_base[slot];
      bits     = lzx_slot_bits[slot];
      if(bits)
      {
        if(block_type == LZX_B_ALIGNED && bits >= 3)
        {
          distance += lzx_get_bits(lzx, src, src_len, bits - 3) << 3;
          distance += lzx_get_huffman(lzx, &(lzx->aligned), src, src_len);
        }
        else
          distance += lzx_get_bits(lzx, src, src_len, bits);
      }
      else

      if(!distance)
        distance = prev_distance;

      prev_distance = distance;

      slot   = (code - LZX_NUM_CHARS) >> 5;
      length = lzx_slot_base[slot] + LZX_MIN_MATCH;
      bits   = lzx_slot_bits[slot];
      if(bits)
        length += lzx_get_bits(lzx, src, src_len, bits);

      if(length > bytes_out)
      {
        #ifdef LZX_DEBUG
        debug("invalid length %d (in:%zu out:%zu)\n", length, lzx->in, lzx->out);
        #endif
        goto err;
      }

      lzx_copy_dictionary(lzx, dest, distance, length);
      bytes_out -= length;
    }
  }

  lzx_unpack_free(lzx);
  return 0;

err:
  lzx_unpack_free(lzx);
  return -1;
}

const char *lzx_unpack(unsigned char * LZX_RESTRICT dest, size_t dest_len,
 const unsigned char *src, size_t src_len, int method)
{
  switch(method)
  {
    case LZX_M_PACKED:
      if(lzx_unpack_normal(dest, dest_len, src, src_len) < 0)
        return "unpack failed";
      return NULL;
  }
  return "unsupported method";
}
