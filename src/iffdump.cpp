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

/**
 * Dump information from an IFF file.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "Config.hpp"
#include "IFF.hpp"
#include "common.hpp"

struct IFFDumpConfig
{
  size_t offset        = 0;
  Endian endian        = Endian::BIG;
  IFFPadding padding   = IFFPadding::WORD;
  IFFCodeSize codesize = IFFCodeSize::FOUR;
  bool full_chunk_lens = false;
};

static struct IFFDumpConfig IFFConfig{};

static bool config_handler(const char *arg, void *priv)
{
  struct IFFDumpConfig *conf = reinterpret_cast<struct IFFDumpConfig *>(priv);
  switch(arg[1])
  {
    case '2':
      conf->codesize = IFFCodeSize::TWO;
      break;

    case '4':
      conf->codesize = IFFCodeSize::FOUR;
      break;

    case 'o':
      conf->offset = strtoul(arg + 2, nullptr, 10);
      break;

    case 'B':
      conf->endian = Endian::BIG;
      break;

    case 'L':
      conf->endian = Endian::LITTLE;
      break;

    case 'b':
      conf->padding = IFFPadding::BYTE;
      break;

    case 'w':
      conf->padding = IFFPadding::WORD;
      break;

    case 'd':
      conf->padding = IFFPadding::DWORD;
      break;

    case 'f':
      conf->full_chunk_lens = true;
      break;
  }
  return true;
}

struct IFFDumpData
{
  const IFF<IFFDumpData> *current;
};

static const class IFFDumpHandler final: public IFFHandler<IFFDumpData>
{
  mutable bool print_hex = false;

public:
  modutil::error parse(FILE *fp, size_t len, IFFDumpData &m) const override
  {
    const auto *current = m.current;
    const char *current_id = current->current_id;
    size_t current_start = current->current_start;
    size_t codelen = static_cast<size_t>(IFFConfig.codesize);

    for(size_t i = 0; i < codelen; i++)
    {
      if(current_id[i] < 0x20 || current_id[i] > 0x7E)
      {
        print_hex = true;
        break;
      }
    }

    if(print_hex)
    {
      static const char hex[] = "0123456789abcdef";
      char hexbuf[9];
      size_t j = 0;

      for(size_t i = 0; i < codelen && j + 1 < (size_t)arraysize(hexbuf); i++)
      {
        hexbuf[j++] = hex[static_cast<uint8_t>(current_id[i]) >> 4];
        hexbuf[j++] = hex[current_id[i] & 0xF];
      }
      hexbuf[j] = '\0';

      O_("%-8s : pos=%zu, len=%zu\n", hexbuf, current_start, len);
    }
    else
      O_("%-8s : pos=%zu, len=%zu\n", current_id, current_start, len);

    return modutil::SUCCESS;
  }
} iff_handler{};


static modutil::error IFF_dump(FILE *fp)
{
  if(fseek(fp, IFFConfig.offset, SEEK_SET))
    return modutil::SEEK_ERROR;

  IFF<IFFDumpData> iff(IFFConfig.endian, IFFConfig.padding, IFFConfig.codesize, &iff_handler);
  iff.full_chunk_lengths = IFFConfig.full_chunk_lens;
  IFFDumpData data{};
  data.current = &iff;
  return iff.parse_iff(fp, 0, data);
}

static void check_iff(const char *filename)
{
  FILE *fp = fopen(filename, "rb");
  if(fp)
  {
    O_("File     : %s\n", filename);

    modutil::error err = IFF_dump(fp);
    if(err)
      O_("Error    : %s\n\n", modutil::strerror(err));
    else
      fprintf(stderr, "\n");

    fclose(fp);
  }
  else
    O_("Error    : failed to open '%s'.\n", filename);
}

int main(int argc, char *argv[])
{
  bool read_stdin = false;

  if(!argv || argc < 2)
  {
    fprintf(stdout, "Usage: iffdump [options] [filenames...]\n");
    return 0;
  }

  if(!Config.init(&argc, argv, config_handler, &IFFConfig))
    return -1;

  for(int i = 1; i < argc; i++)
  {
    if(!strcmp(argv[i], "-"))
    {
      if(!read_stdin)
      {
        char buffer[1024];
        while(fgets_safe(buffer, stdin))
          check_iff(buffer);

        read_stdin = true;
      }
      continue;
    }
    check_iff(argv[i]);
  }

  return 0;
}
