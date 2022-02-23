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
