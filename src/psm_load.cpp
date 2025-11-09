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

/**
 * Protracker Studio Module / Epic MegaGames MASI "new format" handler.
 * See ps16_load.cpp for the older format.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "IFF.hpp"
#include "modutil.hpp"

static int total_psm = 0;


enum PSM_features
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

//static const int MAX_SAMPLES  = 256;
static const int MAX_PATTERNS = 256;
//static const int MAX_ORDERS   = 256;

struct PSM_pattern
{
  char id[9];
  uint16_t num_rows;
  // TODO: pattern data.
};

struct PSM_data
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
  PSM_pattern patterns[MAX_PATTERNS];

  bool uses[NUM_FEATURES];

  ~PSM_data()
  {
    delete[] name;
  }
};

class TITL_handler
{
public:
  static constexpr IFFCode id = IFFCode("TITL");

  static modutil::error parse(FILE *fp, size_t len, PSM_data &m)
  {
    m.name = new char[len + 1];

    if(!fread(m.name, len, 1, fp))
      return modutil::READ_ERROR;

    m.name[len] = '\0';
    return modutil::SUCCESS;
  }
};

class SDFT_handler
{
public:
  static constexpr IFFCode id = IFFCode("SDFT");

  static modutil::error parse(FILE *fp, size_t len, PSM_data &m)
  {
    if(len < 8 || !fread(m.song_type, 8, 1, fp))
      return modutil::READ_ERROR;

    m.song_type[8] = '\0';
    return modutil::SUCCESS;
  }
};

class PBOD_handler
{
public:
  static constexpr IFFCode id = IFFCode("PBOD");

  static modutil::error parse(FILE *fp, size_t len, PSM_data &m)
  {
    if(m.num_patterns >= MAX_PATTERNS)
    {
      format::warning("ignoring pattern %zu", m.current_patt++);
      return modutil::SUCCESS;
    }

    /* Ignore duplicate pattern length dword (???) */
    fget_u32le(fp);

    PSM_pattern &p = m.patterns[m.current_patt++];
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
};

class SONG_handler
{
public:
  static constexpr IFFCode id = IFFCode("SONG");

  static modutil::error parse(FILE *fp, size_t len, PSM_data &m)
  {
    // FIXME
    return modutil::SUCCESS;
  }
};

class DSMP_handler
{
public:
  static constexpr IFFCode id = IFFCode("DSMP");

  static modutil::error parse(FILE *fp, size_t len, PSM_data &m)
  {
    // FIXME
    return modutil::SUCCESS;
  }
};

static const IFF<
  PSM_data,
  TITL_handler,
  SDFT_handler,
  PBOD_handler,
  SONG_handler,
  DSMP_handler> PSM_parser(Endian::LITTLE, IFFPadding::BYTE);


class PSM_loader : modutil::loader
{
public:
  PSM_loader(): modutil::loader("PSM", "masi", "Protracker Studio Module / Epic MegaGames MASI") {}

  virtual modutil::error load(modutil::data state) const override
  {
    FILE *fp = state.reader.unwrap(); /* FIXME: */

    PSM_data m{};
    auto parser = PSM_parser;
    parser.max_chunk_length = 0;

    if(!fread(m.magic, 4, 1, fp))
      return modutil::FORMAT_ERROR;

    m.filesize = fget_u32le(fp);

    if(!fread(m.magic2, 4, 1, fp))
      return modutil::FORMAT_ERROR;

    if(strncmp(m.magic, "PSM ", 4) || strncmp(m.magic2, "FILE", 4))
      return modutil::FORMAT_ERROR;

    total_psm++;
    modutil::error err = parser.parse_iff(fp, 0, m);
    if(err)
      return err;

    if(parser.max_chunk_length > 4*1024*1024)
      m.uses[FT_CHUNK_OVER_4_MIB] = true;

    if(m.name)
      format::line("Name", "%s", m.name);
    if(strcmp(m.song_type, "MAINSONG"))
      format::line("Type", "MASI PSM / %s", m.song_type);
    else
      format::line("Type", "MASI PSM");

//    format::line("Samples",  "%u", m.num_samples);
//    format::line("Channels", "%u", m.num_channels);
    format::line("Patterns", "%zu", m.num_patterns);
    format::line("Max rows", "%zu", m.max_rows);
    format::line("MaxChunk", "%zu", parser.max_chunk_length);
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

        PSM_pattern &p = m.patterns[i];

        // FIXME
        if(!Config.quiet)
          O_("Pat. %02x : '%s', %u rows\n", i, p.id, p.num_rows);
      }

      // FIXME Config.dump_pattern_rows
    }
    return modutil::SUCCESS;
  }

  virtual void report() const override
  {
    if(!total_psm)
      return;

    format::report("Total PSMs", total_psm);
  }
};

static const PSM_loader loader;
