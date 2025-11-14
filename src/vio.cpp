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

#include "vio.hpp"

#include <sys/stat.h>

static bool is_read(const char *mode)
{
  switch(mode[0])
  {
    case 'r':
      return mode[1] == '+' || (mode[1] == 'b' && mode[2] == '+');
    case 'w':
    case 'a':
      return true;
  }
  return false;
}

vio_file::vio_file(const char *filename, const char *mode)
{
  f = fopen(filename, mode);
  if(!f)
    throw "failed to open file";

  saved_length = -1;
  if(is_read(mode))
  {
    setvbuf(f, NULL, _IOFBF, 8192);
    saved_length = length();
  }
}

vio_file::~vio_file() noexcept
{
  fclose(f);
}

size_t vio_file::read(void *dest, size_t num) noexcept
{
  size_t n = fread(dest, 1, num, f);
  if(n < num)
  {
    eof_value = feof(f);
    err_value = ferror(f);
  }
  return n;
}

size_t vio_file::write(const void *src, size_t num) noexcept
{
  size_t n = fwrite(src, 1, num, f);
  if(n < num)
  {
    eof_value = feof(f);
    err_value = ferror(f);
  }
  return n;
}

char *vio_file::gets(char *dest, size_t num) noexcept
{
  char *v = fgets(dest, num, f);
  eof_value = feof(f);
  err_value = ferror(f);
  return v;
}

int vio_file::seek(int64_t pos, int whence) noexcept
{
  int ret = fseek(f, pos, whence);
  if(ret < 0)
  {
    eof_value = feof(f);
    err_value = ferror(f);
  }
  else
  {
    eof_value = 0;
    err_value = 0;
  }
  return ret;
}

int64_t vio_file::tell() noexcept
{
  return ftell(f);
}

int64_t vio_file::length() noexcept
{
  /* Read-only--the length should not change. */
  if(saved_length >= 0)
    return saved_length;

  struct stat st;
  int fd = fileno(f);
  long ret;

  if(fd >= 0)
  {
#ifdef _WIN32
    ret = _filelength(fd);
    if(ret >= 0)
      return ret;
#endif
    ret = fstat(fd, &st);
    if(ret == 0)
      return st.st_size;
  }
  long pos = ftell(f);
  if(pos >= 0 && !fseek(f, 0, SEEK_END))
  {
    ret = ftell(f);
    fseek(f, pos, SEEK_SET);
    return ret;
  }
  return -1;
}


vio_buffer::vio_buffer(void *dest, size_t dest_len) noexcept
{
  src_buffer = reinterpret_cast<const uint8_t *>(dest);
  dest_buffer = reinterpret_cast<uint8_t *>(dest);
  pos = 0;
  len = dest_len;
}

vio_buffer::vio_buffer(const void *src, size_t src_len) noexcept
{
  src_buffer = reinterpret_cast<const uint8_t *>(src);
  dest_buffer = nullptr;
  pos = 0;
  len = src_len;
}

size_t vio_buffer::read(void *dest, size_t num) noexcept
{
  if(num > len - pos)
  {
    num = len - pos;
    eof_value = 1;
  }

  memcpy(dest, src_buffer, num);
  return num;
}

size_t vio_buffer::write(const void *src, size_t num) noexcept
{
  if(!dest_buffer)
  {
    err_value = 1;
    return 0;
  }
  if(num > len - pos)
  {
    num = len - pos;
    eof_value = 1;
  }

  memcpy(dest_buffer, src, num);
  return num;
}

char *vio_buffer::gets(char *dest, size_t num) noexcept
{
  // FIXME:
  err_value = 1;
  return NULL;
}

int vio_buffer::seek(int64_t offset, int whence) noexcept
{
  switch(whence)
  {
    case SEEK_SET:
      break;
    case SEEK_CUR:
      offset += static_cast<int64_t>(pos);
      break;
    case SEEK_END:
      offset = static_cast<int64_t>(len) - offset;
      break;
  }
#if SIZE_MAX < INT64_MAX
  if(offset > SIZE_MAX)
    offset = -1;
#endif
  if(offset < 0)
  {
    eof_value = 1;
    return -1;
  }
  if(static_cast<size_t>(offset) > len)
    offset = len;

  pos = static_cast<size_t>(offset);
  eof_value = 0;
  err_value = 0;
  return 0;
}

int64_t vio_buffer::tell() noexcept
{
  return static_cast<int64_t>(pos);
}

int64_t vio_buffer::length() noexcept
{
  return static_cast<int64_t>(len);
}
