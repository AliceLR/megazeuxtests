/**
 * Copyright (C) 2021 Lachesis <petrifiedrowan@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "Bitstream.hpp"
#include "LZW.hpp"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*#define LZW_DEBUG*/

#define LZW_NO_CODE		((uint16_t)-1)
#define LZW_CODE_CLEAR		256
#define LZW_CODE_SYM_EOF	257


struct LZW_code
{
  uint16_t prev;
  uint16_t length;
  uint8_t  value;
};

struct LZW_tree
{
  struct LZW_code *codes;
  unsigned int bits;
  unsigned int length;
  unsigned int maxlength;
  unsigned int defaultlength;
  unsigned int alloclength;
  unsigned int previous_code;
  int new_inc;
  int flags;
  uint8_t previous_first_char;
};


static int LZW_init_tree(LZW_tree *lzw, int flags)
{
  unsigned int maxbits = LZW_FLAG_MAXBITS(flags);
  unsigned int i;

  lzw->bits = 9;
  if(maxbits < lzw->bits || maxbits > 16)
    return -1;

  lzw->defaultlength = 258; /* 256 chars + clear + EOF. */
  lzw->maxlength = 1 << lzw->bits;
  lzw->alloclength = 1 << maxbits;

  lzw->codes = (LZW_code *)calloc(lzw->alloclength, sizeof(LZW_code));
  if(lzw->codes == NULL)
    return -1;

  lzw->length = lzw->defaultlength;
  lzw->previous_code = LZW_NO_CODE;
  lzw->new_inc = 0;
  lzw->flags = flags;
  lzw->previous_first_char = 0;

  for(i = 0; i < 256; i++)
  {
    lzw->codes[i].length = 1;
    lzw->codes[i].value = i;
    lzw->codes[i].prev = LZW_NO_CODE;
  }
  return 0;
}

static void LZW_free(LZW_tree *lzw)
{
  free(lzw->codes);
  return;
}

static void LZW_add(LZW_tree *lzw)
{
  LZW_code *current;
  uint16_t prev_length;

  if(lzw->length >= lzw->alloclength)
     return;

  current = &(lzw->codes[lzw->length++]);

  /* Increase bitwidth if the NEXT code would be maxlength. */
  if(lzw->length >= lzw->maxlength && lzw->length < lzw->alloclength)
  {
    lzw->maxlength <<= 1;
    lzw->bits++;
    lzw->new_inc = 1;
    #ifdef LZW_DEBUG
    printf("I: bitwidth increased to %d\n", lzw->bits);
    #endif
  }

  current->prev = lzw->previous_code;
  current->value = lzw->previous_first_char;

  /* NOTE: when the length cache deadcode below is enabled, this may
   * intentionally be set to or overflow to 0, in which case the length
   * will be computed as-needed by iterating the tree. */
  prev_length = lzw->codes[lzw->previous_code].length;
  current->length = prev_length ? prev_length + 1 : 0;
}

/**
 * Reset the LZW tree length.
 */
static void LZW_clear(LZW_tree *lzw)
{
  lzw->bits = 9;
  lzw->maxlength = (1 << lzw->bits);
  lzw->length = lzw->defaultlength;
  lzw->previous_code = LZW_NO_CODE;
#if 0
{
  int i;
  for(i = lzw->defaultlength; i < lzw->alloclength; i++)
    lzw->codes[i].length = 0;
}
#endif
}

/**
 * Get the length of an LZW code, or compute it if it isn't currently stored.
 * This happens when one or mode codes in the sequence are marked for reuse.
 */
static uint16_t LZW_get_length(const LZW_tree *lzw, const LZW_code *c)
{
#if 0
  uint16_t code;
  uint16_t length = 1;

  if(c->length)
    return c->length;

  do
  {
    /* Shouldn't happen, but... */
    if(length >= lzw->maxlength)
      return 0;

    length++;
    code = c->prev;
    c = &(lzw->codes[code]);
  }
  while(code >= lzw->defaultlength);
  return length;
#endif
  return c->length;
}

/**
 * Output an LZW code.
 */
static int LZW_output(LZW_tree *lzw, uint16_t code, uint8_t **_pos, size_t *left)
{
  uint8_t *pos = *_pos;

  LZW_code *codes = lzw->codes;
  LZW_code *current = &(codes[code]);
  unsigned int length = LZW_get_length(lzw, current);
  unsigned int i;

  if(length == 0 || length > *left)
    return -1;

  for(i = length - 1; i > 0; i--)
  {
    pos[i] = current->value;
    code = current->prev;
    current = &(codes[code]);
  }
  *pos = code;
  *_pos += length;
  *left -= length;

  lzw->previous_first_char = code;
  return 0;
}

/**
 * Decode an LZW code and create the next code from known data.
 */
static int LZW_decode(LZW_tree *lzw, uint16_t code, uint8_t **_pos, size_t *left)
{
  int kwkwk = 0;
  int result;

  /* Digital Symphony LZW never seems to reference cleared codes,
   * which allows some assumptions to be made (like never clearing the
   * cached code lengths). If this decoder needs to support those, the
   * cached length handling deadcode above needs to be uncommented. */
  if(code > lzw->length)
    return -1;

  /* This is a special case--the current code is the previous code with the
   * first character of the previous code appended, and needs to be added
   * before the output occurs (instead of after). */
  if(code == lzw->length)
  {
    if(lzw->previous_code == LZW_NO_CODE)
      return -1;

    LZW_add(lzw);
    lzw->previous_code = code;
    kwkwk = 1;
  }

  /* Otherwise, output first, and then add a new code, which is the previous
   * code with the first character of the current code appended. */
  result = LZW_output(lzw, code, _pos, left);
  if(result == 0 && !kwkwk)
  {
    if(lzw->previous_code != LZW_NO_CODE)
      LZW_add(lzw);

    lzw->previous_code = code;
  }
  return result;
}

int LZW_read(void *dest, size_t dest_len, size_t max_read_len, int flags, FILE *fp)
{
  LZW_tree lzw{};
  Bitstream bs(fp, max_read_len);

  uint8_t *start = (uint8_t *)dest;
  uint8_t *pos = start;
  size_t left = dest_len;
  int result;
  int code;

  if(LZW_init_tree(&lzw, flags) != 0)
    return -1;

  #ifdef LZW_DEBUG
  printf("S: %zu\n", dest_len);
  #endif

  while(left > 0)
  {
    code = bs.read(lzw.bits);
    #ifdef LZW_DEBUG
    printf(" : %x\n", code);
    #endif
    if(code < 0)
      break;

    if(code == LZW_CODE_CLEAR)
    {
      #ifdef LZW_DEBUG
      printf(" : >>> CLEAR <<<\n");
      #endif
      LZW_clear(&lzw);
      continue;
    }
    else

    if((flags & LZW_FLAG_SYMQUIRKS) && code == LZW_CODE_SYM_EOF)
      break;

    lzw.new_inc = 0;
    result = LZW_decode(&lzw, code, &pos, &left);
    if(result)
      break;
  }

  if(left > 0)
  {
    //D_(D_WARN "encountered error in stream or early EOF");
    memset(pos, 0, left);
  }
  else

  if(flags & LZW_FLAG_SYMQUIRKS)
  {
    /* Digital Symphony - read final EOF code. */
    if(lzw.new_inc)
    {
      /* If the final code prior to EOF should have increased
       * the bitwidth, read the EOF with the old bitwidth
       * instead of the new one.
       *
       * This anomaly exists in FULLEFFECT, NARCOSIS and
       * NEWDANCE. In NEWDANCE (libxmp's test file for this),
       * it occurs specifically in the LZW-compressed sequence.
       * https://github.com/libxmp/libxmp/issues/347
       */
      lzw.bits--;
    }

    code = bs.read(lzw.bits);
    #ifdef LZW_DEBUG
    printf("E: %x\n", code);
    #endif
    /*
    if(code < 0)
    {
      D_(D_WARN "missing LZW EOF code!");
    }
    else

    if(code != LZW_CODE_SYM_EOF)
      D_(D_WARN "LZW stream is longer than the provided buffer!");
    */
  }

  if(flags & LZW_FLAG_SYMQUIRKS)
  {
    /* Digital Symphony LZW compressed stream size is 4 aligned. */
    size_t pos = bs.num_read;
    while(pos & 3)
    {
      #ifdef LZW_DEBUG
      printf("A: align byte\n");
      #endif
      fgetc(fp);
      pos++;
    }
  }
  #ifdef LZW_DEBUG
  printf("I: stream end position: %ld\n", hio_tell(f));
  #endif

  LZW_free(&lzw);
  return 0;
}
