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

#ifndef MZXTEST_BITSTREAM_HPP
#define MZXTEST_BITSTREAM_HPP

#include <stdio.h>
#include <stdint.h>

struct Bitstream
{
  FILE *fp;
  uint32_t buf = 0;
  size_t num_read = 0;
  size_t max_read;
  int buf_bits = 0;

  Bitstream(FILE *_fp, size_t _max_read): fp(_fp), max_read(_max_read) {}

  int read(int bits_to_read)
  {
    int byte;
    int ret;

    if(buf_bits < bits_to_read)
    {
      while(buf_bits < bits_to_read)
      {
        if(num_read >= max_read)
          return -1;

        byte = fgetc(fp);
        if(byte < 0)
          return -1;

        buf |= byte << buf_bits;
        buf_bits += 8;
        num_read++;
      }
    }
    ret = buf & ((1 << bits_to_read) - 1);
    buf >>= bits_to_read;
    buf_bits -= bits_to_read;
    return ret;
  }
};

#endif /* MZXTEST_BITSTREAM_HPP */
