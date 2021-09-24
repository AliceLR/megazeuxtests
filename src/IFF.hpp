/**
 * Copyright (C) 2020 Lachesis <petrifiedrowan@gmail.com>
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

#ifndef MZXTEST_IFF_HPP
#define MZXTEST_IFF_HPP

#include <stdio.h>
#include <vector>
#include "common.hpp"

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

template<class T>
class IFF
{
private:
  std::vector<const IFFHandler<T> *> handlers;
  bool use_generic = false;
  Endian endian = Endian::BIG;
  IFFPadding padding = IFFPadding::WORD;
  IFFCodeSize codesize = IFFCodeSize::FOUR;

  const IFFHandler<T> *find_handler(const char *id) const
  {
    for(const IFFHandler<T> *h : handlers)
    {
      if(use_generic || !memcmp(h->id, id, static_cast<size_t>(codesize)))
        return h;
    }
    return nullptr;
  }

public:
  mutable size_t max_chunk_length = 0;
  mutable char current_id[5];
  mutable size_t current_start;

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

  modutil::error parse_iff(FILE *fp, size_t container_len, T &m) const
  {
    size_t start_pos = ftell(fp);
    size_t current_pos = start_pos;
    size_t end_pos = start_pos;
    size_t codelen = static_cast<size_t>(codesize);
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

      const IFFHandler<T> *handler = find_handler(id);
      if(!handler)
      {
        O_("Warning   : ignoring unknown IFF tag '%*.*s' @ %#lx.\n",
         (int)codelen, (int)codelen, id, ftell(fp) - 8);
      }
      else

      if(!handler->is_container)
      {
        modutil::error result = handler->parse(fp, len, m);
        if(result)
          return result;
      }
      else
      {
        modutil::error result = parse_iff(fp, len, m);
        if(result)
          return result;
      }

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
