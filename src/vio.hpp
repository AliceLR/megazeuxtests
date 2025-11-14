/**
 * Copyright (C) 2025 Lachesis <petrifiedrowan@gmail.com>
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

#ifndef MODDIAG_VIO_HPP
#define MODDIAG_VIO_HPP

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <type_traits>

class vio
{
protected:
  int eof_value = 0;
  int err_value = 0;

public:
  virtual size_t read(void *dest, size_t num) noexcept = 0;
  virtual size_t write(const void *src, size_t num) noexcept = 0;
  virtual char *gets(char *dest, size_t num) noexcept = 0;
  virtual int seek(int64_t offset, int whence) noexcept = 0;
  virtual int64_t tell() noexcept = 0;
  virtual int64_t length() noexcept = 0;

  /* FIXME: remove! */
  virtual FILE *unwrap() noexcept { return nullptr; }

  inline int eof() const noexcept
  {
    return eof_value;
  }

  inline int error() noexcept
  {
    int v = err_value;
    err_value = 0;
    return v;
  }

  /* Read wrappers */

  inline uint8_t u8() noexcept
  {
    uint8_t v[1];
    if(read(v, 1) < 1)
      return static_cast<uint8_t>(-1);

    return v[0];
  }

  inline int8_t s8() noexcept
  {
    return static_cast<int8_t>(u8());
  };

  inline uint16_t u16le() noexcept
  {
    uint8_t v[2];
    if(read(v, 2) < 2)
      return static_cast<uint16_t>(-1);

    return v[0] | (v[1] << 8u);
  }

  inline uint16_t u16be() noexcept
  {
    uint8_t v[2];
    if(read(v, 2) < 2)
      return static_cast<uint16_t>(-1);

    return (v[0] << 8u) | v[1];
  }

  inline int16_t s16le() noexcept
  {
    return static_cast<int16_t>(u16le());
  }

  inline int16_t s16be() noexcept
  {
    return static_cast<int16_t>(u16be());
  }

  inline uint32_t u24le() noexcept
  {
    uint8_t v[3];
    if(read(v, 3) < 3)
      return static_cast<uint32_t>(-1);

    return v[0] | (v[1] << 8u) | (v[2] << 16u);
  }

  inline uint32_t u24be() noexcept
  {
    uint8_t v[3];
    if(read(v, 3) < 3)
      return static_cast<uint32_t>(-1);

    return (v[0] << 16u) | (v[1] << 8u) | v[2];
  }

  inline uint32_t u32le() noexcept
  {
    uint8_t v[4];
    if(read(v, 4) < 4)
      return static_cast<uint32_t>(-1);

    return v[0] | (v[1] << 8u) | (v[2] << 16u) | (v[3] << 24u);
  }

  inline uint32_t u32be() noexcept
  {
    uint8_t v[4];
    if(read(v, 4) < 4)
      return static_cast<uint32_t>(-1);

    return (v[0] << 24u) | (v[1] << 16u) | (v[2] << 8u) | v[3];
  }

  template<typename T, int N,
    typename E = typename std::enable_if<sizeof(T) == 1>::type>
  size_t read_buffer(T (&buffer)[N])
  {
    return read(buffer, N);
  }

  template<size_t N>
  inline size_t read_asciiz(char (&buffer)[N], size_t max_in_file)
  {
    size_t i;
    for(i = 0; i < max_in_file; i++)
    {
      int val = u8();
      if(val == 0 || eof_value || err_value)
      {
        if(i < N)
          buffer[i] = '\0';
        break;
      }
      if(i < N)
        buffer[i] = (char)val;
    }
    buffer[N - 1] = '\0';
    return i;
  }

  template<int N>
  inline char *gets_safe(char (&buffer)[N])
  {
    char *retval = gets(buffer, N);
    if(!retval)
      return NULL;

    size_t len = strlen(buffer);
    while(len && (buffer[len - 1] == '\r' || buffer[len - 1] == '\n'))
      buffer[--len] = '\0';

    return retval;
  }

  /* Write wrappers */

  inline uint8_t u8(uint8_t val) noexcept
  {
    if(write(&val, 1) < 1)
      return static_cast<uint8_t>(-1);

    return val;
  }

  inline uint16_t u16le(uint16_t val) noexcept
  {
    uint8_t v[2];
    v[0] = val;
    v[1] = val >> 8;
    if(write(v, 2) < 2)
      return static_cast<uint16_t>(-1);

    return val;
  }

  inline uint16_t u16be(uint16_t val) noexcept
  {
    uint8_t v[2];
    v[0] = val >> 8;
    v[1] = val;
    if(write(v, 2) < 2)
      return static_cast<uint16_t>(-1);

    return val;
  }

  inline uint32_t u24le(uint32_t val) noexcept
  {
    uint8_t v[3];
    v[0] = val;
    v[1] = val >> 8;
    v[2] = val >> 16;
    if(write(v, 3) < 3)
      return static_cast<uint32_t>(-1);

    return val;
  }

  inline uint32_t u24be(uint32_t val) noexcept
  {
    uint8_t v[3];
    v[0] = val >> 16;
    v[1] = val >> 8;
    v[2] = val;
    if(write(v, 3) < 3)
      return static_cast<uint32_t>(-1);

    return val;
  }

  inline uint32_t u32le(uint32_t val) noexcept
  {
    uint8_t v[4];
    v[0] = val;
    v[1] = val >> 8;
    v[2] = val >> 16;
    v[3] = val >> 24;
    if(write(v, 4) < 4)
      return static_cast<uint32_t>(-1);

    return val;
  }

  inline uint32_t u32be(uint32_t val) noexcept
  {
    uint8_t v[4];
    v[0] = val >> 24;
    v[1] = val >> 16;
    v[2] = val >> 8;
    v[3] = val;
    if(write(v, 4) < 4)
      return static_cast<uint32_t>(-1);

    return val;
  }

  template<typename T, int N,
    typename E = typename std::enable_if<sizeof(T) == 1>::type>
  size_t write_buffer(const T (&buffer)[N])
  {
    return write(buffer, N);
  }

  inline int puts(const char *string) noexcept
  {
    size_t len = strlen(string);
    if(write(string, len) < len)
      return -1;
    return 0;
  }
};


class vio_file : public vio
{
  FILE *f;
  int64_t saved_length;

public:
  vio_file(const char *filename, const char *mode);
  ~vio_file() noexcept;

  size_t read(void *dest, size_t num) noexcept override;
  size_t write(const void *src, size_t num) noexcept override;
  char *gets(char *dest, size_t num) noexcept override;
  int seek(int64_t offset, int whence) noexcept override;
  int64_t tell() noexcept override;
  int64_t length() noexcept override;

  /* FIXME: remove! */
  FILE *unwrap() noexcept override { return f; }
};

class vio_buffer : public vio
{
  const uint8_t *src_buffer;
  uint8_t *dest_buffer;
  size_t pos;
  size_t len;

public:
  vio_buffer(void *d, size_t d_len) noexcept;
  vio_buffer(const void *s, size_t s_len) noexcept;
  ~vio_buffer() noexcept {}

  size_t read(void *dest, size_t num) noexcept override;
  size_t write(const void *src, size_t num) noexcept override;
  char *gets(char *dest, size_t num) noexcept override;
  int seek(int64_t offset, int whence) noexcept override;
  int64_t tell() noexcept override;
  int64_t length() noexcept override;
};

#endif /* MODDIAG_VIO_HPP */
