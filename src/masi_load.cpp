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

#include "IFF.hpp"
#include "modutil.hpp"

static int total_masi = 0;


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

  modutil::error parse(FILE *fp, size_t len, MASI_data &m) const override
  {
    m.name = new char[len + 1];

    if(!fread(m.name, len, 1, fp))
      return modutil::READ_ERROR;

    m.name[len] = '\0';
    return modutil::SUCCESS;
  }
} TITL_handler("TITL", false);

static const class MASI_SDFT_Handler final: public IFFHandler<MASI_data>
{
public:
  MASI_SDFT_Handler(const char *n, bool c): IFFHandler(n, c) {}

  modutil::error parse(FILE *fp, size_t len, MASI_data &m) const override
  {
    if(len < 8 || !fread(m.song_type, 8, 1, fp))
      return modutil::READ_ERROR;

    m.song_type[8] = '\0';
    return modutil::SUCCESS;
  }
} SDFT_handler("SDFT", false);

static const class MASI_PBOD_Handler final: public IFFHandler<MASI_data>
{
public:
  MASI_PBOD_Handler(const char *n, bool c): IFFHandler(n, c) {}

  modutil::error parse(FILE *fp, size_t len, MASI_data &m) const override
  {
    if(m.num_patterns >= MAX_PATTERNS)
    {
      format::warning("ignoring pattern %zu", m.current_patt++);
      return modutil::SUCCESS;
    }

    /* Ignore duplicate pattern length dword (???) */
    fget_u32le(fp);

    MASI_pattern &p = m.patterns[m.current_patt++];
    m.num_patterns = m.current_patt;

    if(!fread(p.id, 4, 1, fp))
      return modutil::READ_ERROR;

    if(!strncmp(p.id, "PATT", 4))
    {
      /* Older format has 8 char long pattern IDs. */
      if(!fread(p.id + 4, 4, 1, fp))
        return modutil::READ_ERROR;

      p.id[8] = '\0';
    }
    else
      p.id[4] = '\0';

    p.num_rows = fget_u16le(fp);
    if(feof(fp))
      return modutil::READ_ERROR;

    if(p.num_rows > 64)
      m.uses[FT_ROWS_OVER_64] = true;
    if(p.num_rows > m.max_rows)
      m.max_rows = p.num_rows;

    // TODO pattern data.
    return modutil::SUCCESS;
  }
} PBOD_handler("PBOD", false);

static const class MASI_SONG_Handler final: public IFFHandler<MASI_data>
{
public:
  MASI_SONG_Handler(const char *n, bool c): IFFHandler(n, c) {}

  modutil::error parse(FILE *fp, size_t len, MASI_data &m) const override
  {
    // FIXME
    return modutil::SUCCESS;
  }
} SONG_handler("SONG", false);

static const class MASI_DSMP_Handler final: public IFFHandler<MASI_data>
{
public:
  MASI_DSMP_Handler(const char *n, bool c): IFFHandler(n, c) {}

  modutil::error parse(FILE *fp, size_t len, MASI_data &m) const override
  {
    // FIXME
    return modutil::SUCCESS;
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


class MASI_loader : modutil::loader
{
public:
  MASI_loader(): modutil::loader("PSM", "Epic MegaGames MASI") {}

  virtual modutil::error load(FILE *fp, long file_length) const override
  {
    MASI_data m{};
    MASI_parser.max_chunk_length = 0;

    if(!fread(m.magic, 4, 1, fp))
      return modutil::READ_ERROR;

    m.filesize = fget_u32le(fp);

    if(!fread(m.magic2, 4, 1, fp))
      return modutil::READ_ERROR;

    if(!strncmp(m.magic, "PSM\xFE", 4))
    {
      format::warning("ignoring old-format MASI.");
      total_masi++;
      return modutil::SUCCESS;
    }

    if(strncmp(m.magic, "PSM ", 4) || strncmp(m.magic2, "FILE", 4))
      return modutil::FORMAT_ERROR;

    total_masi++;
    modutil::error err = MASI_parser.parse_iff(fp, 0, m);
    if(err)
      return err;

    if(MASI_parser.max_chunk_length > 4*1024*1024)
      m.uses[FT_CHUNK_OVER_4_MIB] = true;

    if(m.name)
      format::line("Name", "%s", m.name);
    if(strcmp(m.song_type, "MAINSONG"))
      format::line("Type", "MASI / %s", m.song_type);
    else
      format::line("Type", "MASI");

//    format::line("Samples",  "%u", m.num_samples);
//    format::line("Channels", "%u", m.num_channels);
    format::line("Patterns", "%zu", m.num_patterns);
    format::line("Max rows", "%zu", m.max_rows);
    format::line("MaxChunk", "%zu", MASI_parser.max_chunk_length);
    format::uses(m.uses, FEATURE_STR);

    if(Config.dump_samples)
    {
      // FIXME
    }

    if(Config.dump_patterns)
    {
      format::line();

      for(unsigned int i = 0; i < m.num_patterns; i++)
      {
        if(i >= MAX_PATTERNS)
          break;

        MASI_pattern &p = m.patterns[i];

        // FIXME
        O_("Pat. %02x : '%s', %u rows\n", i, p.id, p.num_rows);
      }

      // FIXME Config.dump_pattern_rows
    }
    return modutil::SUCCESS;
  }

  virtual void report() const override
  {
    if(!total_masi)
      return;

    format::report("Total MASIs", total_masi);
  }
};

static const MASI_loader loader;
