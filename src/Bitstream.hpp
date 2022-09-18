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
#include <type_traits>
#include <vector>

#include "common.hpp"

/* Generic little endian bitstream. */
template<typename SRC=FILE *>
struct Bitstream
{
  using BUFFERTYPE = std::conditional<(sizeof(size_t) > sizeof(uint32_t)), size_t, uint32_t>::type;

  SRC &fp;
  BUFFERTYPE buf = 0;
  size_t pos = 0;
  size_t num_read = 0;
  size_t max_read;
  int buf_bits = 0;

  Bitstream(SRC &_fp): fp(_fp), max_read(_fp.size()) {}
  Bitstream(SRC &_fp, size_t _max_read): fp(_fp), max_read(_max_read) {}

  inline int read(int bits_to_read)
  {
    int ret;

    if(buf_bits < bits_to_read)
      if(!fill(bits_to_read))
        return -1;

    ret = buf & ((1 << bits_to_read) - 1);
    buf >>= bits_to_read;
    buf_bits -= bits_to_read;
    return ret;
  }

private:
  inline int read_byte();
  inline bool fill(int bits_to_read);
};

template<>
inline int Bitstream<FILE *>::read_byte()
{
  return fgetc(fp);
}

/*
template<>
inline int Bitstream<std::vector<uint8_t>>::read_byte()
{
  if(pos < fp.size())
    return fp[pos++];

  return EOF;
}

template<>
inline int Bitstream<const std::vector<uint8_t>>::read_byte()
{
  if(pos < fp.size())
    return fp[pos++];

  return EOF;
}
*/

/* TODO: the main user of this is Digital Symphony, which fills
 * by reading four new bytes at a time. */
template<>
inline bool Bitstream<FILE *>::fill(int bits_to_read)
{
  while(buf_bits < bits_to_read)
  {
    if(num_read >= max_read)
      return false;

    int byte = read_byte();
    if(byte < 0)
      return false;

    buf |= byte << buf_bits;
    buf_bits += 8;
    num_read++;
  }
  return true;
}

template<>
inline bool Bitstream<std::vector<uint8_t>>::fill(int bits_to_read)
{
  if(num_read >= fp.size())
    return false;

  unsigned bytes = MIN(fp.size() - num_read, ((sizeof(BUFFERTYPE)<<3) - buf_bits)>>3);
  const uint8_t *data = fp.data() + num_read;

  num_read += bytes;

  /* 4 bytes is sufficient and it's much faster to stop without filling any further. */
  if(bytes >= 4)
  {
    BUFFERTYPE t = data[0] | (data[1] << 8) | (data[2] << 16) | ((BUFFERTYPE)data[3] << 24);
    buf |= t << buf_bits;
    buf_bits += 32;
    return (buf_bits >= bits_to_read);
  }

  /* 2 bytes isn't necessarily enough, so handle the extra byte after if needed. */
  if(bytes & 2)
  {
    BUFFERTYPE t = data[0] | (data[1] << 8);
    buf |= t << buf_bits;
    buf_bits += 16;
    data += 2;
  }
  if(bytes & 1)
  {
    BUFFERTYPE t = data[0];
    buf |= t << buf_bits;
    buf_bits += 8;
  }
  return (buf_bits >= bits_to_read);
}

#endif /* MZXTEST_BITSTREAM_HPP */
