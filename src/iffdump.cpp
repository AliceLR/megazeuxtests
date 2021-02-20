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
  size_t offset;
  Endian endian;
  IFFPadding padding;
};

static struct IFFDumpConfig IFFConfig = { 0, Endian::BIG, IFFPadding::WORD };

static bool config_handler(const char *arg, void *priv)
{
  struct IFFDumpConfig *conf = reinterpret_cast<struct IFFDumpConfig *>(priv);
  switch(arg[1])
  {
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
  }
  return true;
}

struct IFFDumpData
{
  const IFF<IFFDumpData> *current;
};

static const class IFFDumpHandler final: public IFFHandler<IFFDumpData>
{
public:
  int parse(FILE *fp, size_t len, IFFDumpData &m) const override
  {
    O_("%-7s : pos=%zu, len=%zu\n", m.current->current_id, m.current->current_start, len);
    return 0;
  }
} iff_handler{};


static int IFF_dump(FILE *fp)
{
  if(fseek(fp, IFFConfig.offset, SEEK_SET))
    return IFF_SEEK_ERROR;

  IFF<IFFDumpData> iff(IFFConfig.endian, IFFConfig.padding, &iff_handler);
  IFFDumpData data{};
  data.current = &iff;
  return iff.parse_iff(fp, 0, data);
}

static void check_iff(const char *filename)
{
  FILE *fp = fopen(filename, "rb");
  if(fp)
  {
    O_("File    : %s\n", filename);

    int err = IFF_dump(fp);
    if(err)
      O_("Error   : %s\n\n", IFF_strerror(err));
    else
      fprintf(stderr, "\n");

    fclose(fp);
  }
  else
    O_("Error     : failed to open '%s'.\n", filename);
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
