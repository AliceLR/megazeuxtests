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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "Config.hpp"
#include "IFF.hpp"
#include "common.hpp"

static const char USAGE[] =
  "A utility to dump Epic MegaGames MASI metadata and patterns.\n"
  "Usage:\n"
  "  masiutil [options] [filenames...]\n\n";

enum MASI_error
{
  MASI_SUCCESS,
  MASI_READ_ERROR,
  MASI_SEEK_ERROR,
  MASI_NOT_A_MASI,
  MASI_INVALID,
};

static const char *MASI_strerror(int err)
{
  switch(err)
  {
    case MASI_SUCCESS:           return "no error";
    case MASI_READ_ERROR:        return "read error";
    case MASI_SEEK_ERROR:        return "seek error";
    case MASI_NOT_A_MASI:        return "not an Epic MegaGames MASI module";
    case MASI_INVALID:           return "invalid MASI";
  }
  return IFF_strerror(err);
}

enum MASI_features
{
  FT_ROWS_OVER_64,
  FT_CHUNK_OVER_4_MIB,
  NUM_FEATURES
};

static const char *FEATURE_STR[NUM_FEATURES] =
{
  ">64Rows",
  ">4MBChunk",
};

static const int MAX_SAMPLES  = 256;
static const int MAX_PATTERNS = 256;
static const int MAX_ORDERS   = 256;

struct MASI_pattern
{
  char id[9];
  uint16_t num_rows;
  // TODO: pattern data.
};

struct MASI_data
{
  /* Header (12) */

  char magic[4];     /* PSM[space] */
  uint32_t filesize; /* filesize - 12 */
  char magic2[4];    /* FILE */

  /* TITL (?) */

  char *name = nullptr;
  uint32_t name_length;

  /* SDFT (8) */
  char song_type[9];

  /* PBOD (?) */

  size_t current_patt = 0;
  size_t num_patterns = 0;
  size_t max_rows = 0;
  MASI_pattern patterns[MAX_PATTERNS];

  bool uses[NUM_FEATURES];

  ~MASI_data()
  {
    delete[] name;
  }
};

static const class MASI_TITL_Handler final: public IFFHandler<MASI_data>
{
public:
  MASI_TITL_Handler(const char *n, bool c): IFFHandler(n, c) {}

  int parse(FILE *fp, size_t len, MASI_data &m) const override
  {
    m.name = new char[len + 1];

    if(!fread(m.name, len, 1, fp))
      return MASI_READ_ERROR;

    m.name[len] = '\0';
    return 0;
  }
} TITL_handler("TITL", false);

static const class MASI_SDFT_Handler final: public IFFHandler<MASI_data>
{
public:
  MASI_SDFT_Handler(const char *n, bool c): IFFHandler(n, c) {}

  int parse(FILE *fp, size_t len, MASI_data &m) const override
  {
    if(len < 8 || !fread(m.song_type, 8, 1, fp))
      return MASI_READ_ERROR;

    m.song_type[8] = '\0';
    return 0;
  }
} SDFT_handler("SDFT", false);

static const class MASI_PBOD_Handler final: public IFFHandler<MASI_data>
{
public:
  MASI_PBOD_Handler(const char *n, bool c): IFFHandler(n, c) {}

  int parse(FILE *fp, size_t len, MASI_data &m) const override
  {
    if(m.num_patterns >= MAX_PATTERNS)
    {
      O_("Warning   : ignoring pattern %zu\n", m.current_patt++);
      return 0;
    }

    /* Ignore duplicate pattern length dword (???) */
    fget_u32le(fp);

    MASI_pattern &p = m.patterns[m.current_patt++];
    m.num_patterns = m.current_patt;

    if(!fread(p.id, 4, 1, fp))
      return MASI_READ_ERROR;

    if(!strncmp(p.id, "PATT", 4))
    {
      /* Older format has 8 char long pattern IDs. */
      if(!fread(p.id + 4, 4, 1, fp))
        return MASI_READ_ERROR;

      p.id[8] = '\0';
    }
    else
      p.id[4] = '\0';

    p.num_rows = fget_u16le(fp);
    if(feof(fp))
      return MASI_READ_ERROR;

    if(p.num_rows > 64)
      m.uses[FT_ROWS_OVER_64] = true;
    if(p.num_rows > m.max_rows)
      m.max_rows = p.num_rows;

    // TODO pattern data.
    return 0;
  }
} PBOD_handler("PBOD", false);

static const class MASI_SONG_Handler final: public IFFHandler<MASI_data>
{
public:
  MASI_SONG_Handler(const char *n, bool c): IFFHandler(n, c) {}

  int parse(FILE *fp, size_t len, MASI_data &m) const override
  {
    // FIXME
    return 0;
  }
} SONG_handler("SONG", false);

static const class MASI_DSMP_Handler final: public IFFHandler<MASI_data>
{
public:
  MASI_DSMP_Handler(const char *n, bool c): IFFHandler(n, c) {}

  int parse(FILE *fp, size_t len, MASI_data &m) const override
  {
    // FIXME
    return 0;
  }
} DSMP_handler("DSMP", false);

static const IFF<MASI_data> MASI_parser(Endian::LITTLE, IFFPadding::BYTE,
{
  &TITL_handler,
  &SDFT_handler,
  &PBOD_handler,
  &SONG_handler,
  &DSMP_handler
});

int MASI_read(FILE *fp)
{
  MASI_data m{};
  MASI_parser.max_chunk_length = 0;

  if(!fread(m.magic, 4, 1, fp))
    return MASI_READ_ERROR;

  m.filesize = fget_u32le(fp);

  if(!fread(m.magic2, 4, 1, fp))
    return MASI_READ_ERROR;

  if(!strncmp(m.magic, "PSM\xFE", 4))
  {
    O_("Warning   : ignoring old-format MASI.\n");
    return MASI_SUCCESS;
  }

  if(strncmp(m.magic, "PSM ", 4) || strncmp(m.magic2, "FILE", 4))
    return MASI_NOT_A_MASI;

  int err = MASI_parser.parse_iff(fp, 0, m);
  if(err)
    return err;

  if(MASI_parser.max_chunk_length > 4*1024*1024)
    m.uses[FT_CHUNK_OVER_4_MIB] = true;

  if(m.name)
    O_("Name      : %s\n",  m.name);
  if(strcmp(m.song_type, "MAINSONG"))
    O_("Song type : %s\n",  m.song_type);

//  O_("Samples   : %u\n",  m.num_samples);
//  O_("Channels  : %u\n",  m.num_channels);
  O_("Patterns  : %zu\n",  m.num_patterns);
  O_("Max rows  : %zu\n",  m.max_rows);
  O_("Max Chunk : %zu\n",  MASI_parser.max_chunk_length);

  O_("Uses      :");
  for(int i = 0; i < NUM_FEATURES; i++)
    if(m.uses[i])
      fprintf(stderr, " %s", FEATURE_STR[i]);
  fprintf(stderr, "\n");

  if(Config.dump_samples)
  {
    // FIXME
  }

  if(Config.dump_patterns)
  {
    O_("          :\n");

    for(unsigned int i = 0; i < m.num_patterns; i++)
    {
      if(i >= MAX_PATTERNS)
        break;

      MASI_pattern &p = m.patterns[i];

      O_("Pattern %02x: '%s', %u rows\n", i, p.id, p.num_rows);
    }

    // FIXME Config.dump_pattern_rows
  }
  return MASI_SUCCESS;
}

void check_masi(const char *filename)
{
  FILE *fp = fopen(filename, "rb");
  if(fp)
  {
    O_("File      : %s\n", filename);

    int err = MASI_read(fp);
    if(err)
      O_("Error     : %s\n\n", MASI_strerror(err));
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
    fprintf(stdout, "%s%s", USAGE, Config.COMMON_FLAGS);
    return 0;
  }

  if(!Config.init(&argc, argv))
    return -1;

  for(int i = 1; i < argc; i++)
  {
    char *arg = argv[i];
    if(arg[0] == '-' && arg[1] == '\0')
    {
      if(!read_stdin)
      {
        char buffer[1024];
        while(fgets_safe(buffer, stdin))
          check_masi(buffer);

        read_stdin = true;
      }
      continue;
    }
    check_masi(arg);
  }

  return 0;
}
