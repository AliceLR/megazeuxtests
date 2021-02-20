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

enum IFF_error
{
  IFF_SUCCESS,
  IFF_READ_ERROR      = 0x1000,
  IFF_SEEK_ERROR      = 0x1001,
  IFF_CONTAINER_ERROR = 0x1002,
  IFF_NO_HANDLER      = 0x1003,
};

enum class IFFPadding
{
  BYTE,
  WORD,
  DWORD,
};

const char *IFF_strerror(int err);

template<class T>
class IFFHandler
{
public:
  const char *id;
  bool is_container;

  virtual int parse(FILE *fp, size_t len, T &m) const = 0;

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

  const IFFHandler<T> *find_handler(char (&id)[4]) const
  {
    for(const IFFHandler<T> *h : handlers)
    {
      if(use_generic || !strncmp(h->id, id, 4))
        return h;
    }
    return nullptr;
  }

public:
  mutable size_t max_chunk_length = 0;
  mutable char current_id[5];
  mutable size_t current_start;

  IFF(Endian e, IFFPadding p, const IFFHandler<T> *generic_handler):
   endian(e), padding(p)
  {
    handlers.push_back(generic_handler);
    use_generic = true;
  }
  IFF(const IFFHandler<T> *generic_handler): IFF(Endian::BIG, IFFPadding::WORD, generic_handler) {}

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

  int parse_iff(FILE *fp, size_t container_len, T &m) const
  {
    size_t start_pos = ftell(fp);
    size_t current_pos = start_pos;
    size_t end_pos = start_pos;

    while(!container_len || current_pos < start_pos + container_len)
    {
      char id[4];
      size_t len;

      current_start = ftell(fp);
      if(!fread(id, 4, 1, fp))
        break;

      memcpy(current_id, id, 4);
      current_id[4] = '\0';

      if(endian == Endian::BIG)
        len = fget_u32be(fp);
      else
        len = fget_u32le(fp);

      if(feof(fp))
        return IFF_READ_ERROR;

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
        O_("Warning   : ignoring unknown IFF tag '%4.4s' @ %#lx.\n", id, ftell(fp) - 8);
      }
      else

      if(!handler->is_container)
      {
        int result = handler->parse(fp, len, m);
        if(result)
          return result;
      }
      else
      {
        int result = parse_iff(fp, len, m);
        if(result)
          return result;
      }

      if(fseek(fp, end_pos, SEEK_SET))
        return IFF_SEEK_ERROR;

      current_pos = ftell(fp);
    }

    if(container_len && current_pos > start_pos + container_len)
      return IFF_CONTAINER_ERROR;

    return IFF_SUCCESS;
  }
};

#endif /* MZXTEST_IFF_HPP */
