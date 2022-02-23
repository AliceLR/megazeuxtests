/**
 * Copyright (C) 2020 Lachesis <petrifiedrowan@gmail.com>
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

#ifndef MZXTEST_IFF_HPP
#define MZXTEST_IFF_HPP

#include <stdio.h>
#include <vector>
#include "common.hpp"
#include "error.hpp"
#include "format.hpp"

enum class IFFPadding
{
  BYTE,
  WORD,
  DWORD,
};

enum class IFFCodeSize
{
  TWO  = 2,
  FOUR = 4,
};

class IFFCode
{
public:
  static constexpr uint64_t NO_CODE = 0x7f7f7f7f7f7f7f7fULL;
  uint64_t value;
  bool is_container;

  constexpr IFFCode(): value(NO_CODE), is_container(false) {}

  constexpr IFFCode(char a, char b, bool is_c = false):
    value((uint64_t)a | ((uint64_t)b << 8) | (NO_CODE << 16)), is_container(is_c) {}

  constexpr IFFCode(char a, char b, char c, char d, bool is_c = false):
    value((uint64_t)a | ((uint64_t)b << 8) | ((uint64_t)c << 16) | ((uint64_t)d << 24) | (NO_CODE << 32)),
    is_container(is_c) {}

  constexpr IFFCode(const char (&arr)[2], bool is_c = false): IFFCode(arr[0], arr[1], is_c) {}
  constexpr IFFCode(const char (&arr)[3], bool is_c = false): IFFCode(arr[0], arr[1], is_c) {}
  constexpr IFFCode(const char (&arr)[4], bool is_c = false): IFFCode(arr[0], arr[1], arr[2], arr[3], is_c) {}
  constexpr IFFCode(const char (&arr)[5], bool is_c = false): IFFCode(arr[0], arr[1], arr[2], arr[3], is_c) {}

  static constexpr IFFCode ANY_CODE() { return IFFCode(); }
};

inline bool operator==(const IFFCode &lhs, const IFFCode &rhs)
{
  return lhs.value == rhs.value;
}

template<class T>
class IFFHandler
{
public:
  const char *id;
  bool is_container;

  virtual modutil::error parse(FILE *fp, size_t len, T &m) const = 0;

  IFFHandler():
    id("IGNORE"), is_container(false) {}
  IFFHandler(const char *id, bool is_container):
    id(id), is_container(is_container) {}
};

template<class T, class... HANDLERS>
class IFF
{
private:
  std::vector<const IFFHandler<T> *> handlers;
  bool use_generic = false;
  Endian endian = Endian::BIG;
  IFFPadding padding = IFFPadding::WORD;
  IFFCodeSize codesize = IFFCodeSize::FOUR;

  /**
   * Attempt to execute a static IFF handler. Most of the time dynamically
   * changing handlers isn't necessary, and these are cleaner to write in loaders.
   */
  template<int I>
  modutil::error exec_static_handler(FILE *fp, size_t len, T &m, IFFCode &id)
  {
    return modutil::IFF_NO_HANDLER;
  }

  template<int I, class H, class... REST>
  modutil::error exec_static_handler(FILE *fp, size_t len, T &m, IFFCode &id)
  {
    if(I < sizeof...(HANDLERS))
    {
      if(H::id == IFFCode::ANY_CODE() || H::id == id)
      {
        if(!H::id.is_container)
          return H::parse(fp, len, m);
        else
          return parse_iff(fp, len, m);
      }

      return exec_static_handler<I + 1, REST...>(fp, len, m, id);
    }
    return modutil::IFF_NO_HANDLER;
  }

  modutil::error exec_static_handler(FILE *fp, size_t len, T &m, IFFCode &id)
  {
    return exec_static_handler<0, HANDLERS...>(fp, len, m, id);
  }

  /**
   * Attempt to execute a dynamic IFF handler.
   */
  modutil::error exec_dynamic_handler(FILE *fp, size_t len, T &m, const char *id)
  {
    for(const IFFHandler<T> *h : handlers)
    {
      if(use_generic || !memcmp(h->id, id, static_cast<size_t>(codesize)))
      {
        if(!h->is_container)
          return h->parse(fp, len, m);
        else
          return parse_iff(fp, len, m);
      }
    }
    return modutil::IFF_NO_HANDLER;
  }

public:
  size_t max_chunk_length = 0;
  bool full_chunk_lengths = false;
  char current_id[5];
  size_t current_start;

  IFF(Endian e, IFFPadding p, IFFCodeSize c, const IFFHandler<T> *generic_handler):
   endian(e), padding(p), codesize(c)
  {
    handlers.push_back(generic_handler);
    use_generic = true;
  }
  IFF(Endian e, IFFPadding p, const IFFHandler<T> *generic_handler):
   IFF(e, p, IFFCodeSize::FOUR, generic_handler) {}
  IFF(const IFFHandler<T> *generic_handler):
   IFF(Endian::BIG, IFFPadding::WORD, IFFCodeSize::FOUR, generic_handler) {}

  template<int N>
  IFF(Endian e, IFFPadding p, IFFCodeSize c, const IFFHandler<T> *(&&handlers_in)[N]):
   endian(e), padding(p), codesize(c)
  {
    for(int i = 0; i < N; i++)
      handlers.push_back(handlers_in[i]);
  }

  template<int N>
  IFF(Endian e, IFFPadding p, const IFFHandler<T> *(&&handlers_in)[N]):
   endian(e), padding(p)
  {
    for(int i = 0; i < N; i++)
      handlers.push_back(handlers_in[i]);
  }

  template<int N>
  IFF(const IFFHandler<T> *(&&handlers_in)[N])
  {
    for(int i = 0; i < N; i++)
      handlers.push_back(handlers_in[i]);
  }

  IFF(Endian e, IFFPadding p, IFFCodeSize c): endian(e), padding(p), codesize(c) {}
  IFF(Endian e, IFFPadding p): endian(e), padding(p) {}
  IFF() {}

  modutil::error parse_iff(FILE *fp, size_t container_len, T &m)
  {
    size_t start_pos = ftell(fp);
    size_t current_pos = start_pos;
    size_t end_pos = start_pos;
    int codelen = static_cast<int>(codesize);
    IFFCode id_code;
    char id[5];

    switch(codesize)
    {
      case IFFCodeSize::TWO:
      case IFFCodeSize::FOUR:
        break;

      default:
        return modutil::IFF_CONFIG_ERROR;
    }
    if(codelen + 1 > arraysize(id))
      return modutil::IFF_CONFIG_ERROR;

    while(!container_len || current_pos < start_pos + container_len)
    {
      size_t len;

      current_start = ftell(fp);

      if(!fread(id, codelen, 1, fp))
        break;

      switch(codesize)
      {
        case IFFCodeSize::TWO:
          id_code = IFFCode(id[0], id[1]);
          break;
        case IFFCodeSize::FOUR:
          id_code = IFFCode(id[0], id[1], id[2], id[3]);
          break;
      }

      memcpy(current_id, id, codelen);
      current_id[codelen] = '\0';

      if(endian == Endian::BIG)
        len = fget_u32be(fp);
      else
        len = fget_u32le(fp);

      /* Length may be optional on the final code in some formats... */
      if(feof(fp))
        len = 0;

      if(len > max_chunk_length)
        max_chunk_length = len;

      /* Annoying hack required for Protracker 3.6 modules. */
      if(full_chunk_lengths)
      {
        if(len >= (size_t)codelen + 4)
          len -= codelen + 4;
        else
          len = 0;
      }

      end_pos = ftell(fp) + len;
      switch(padding)
      {
        case IFFPadding::BYTE:
          break;

        case IFFPadding::WORD:
          if(len & 1)
            end_pos++;
          break;

        case IFFPadding::DWORD:
          if(len & 3)
            end_pos = (end_pos + 3) & ~3;
          break;
      }

      /* Attempt static handlers first. */
      modutil::error result = exec_static_handler(fp, len, m, id_code);
      if(result == modutil::IFF_NO_HANDLER)
      {
        result = exec_dynamic_handler(fp, len, m, id);
        if(result == modutil::IFF_NO_HANDLER)
        {
          format::warning("ignoring unknown IFF tag '%*.*s' @ %#lx.\n",
           codelen, codelen, id, ftell(fp) - 8);
          result = modutil::SUCCESS;
        }
      }
      if(result)
        return result;

      if(fseek(fp, end_pos, SEEK_SET))
        return modutil::SEEK_ERROR;

      current_pos = ftell(fp);
    }

    if(container_len && current_pos > start_pos + container_len)
      return modutil::IFF_CONTAINER_ERROR;

    return modutil::SUCCESS;
  }
};

#endif /* MZXTEST_IFF_HPP */
