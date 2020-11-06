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
#include "common.hpp"

static const char USAGE[] =
  "A utility to dump DBM metadata and patterns.\n"
  "Usage:\n"
  "  dbmutil [options] [filenames...]\n\n"
  "Options:\n"
  "  -s[=N]    Dump sample info. N=1 (optional) enables, N=0 disables (default).\n"
  "  -p[=N]    Dump patterns. N=1 (optional) enables, N=0 disables (default).\n"
  "            N=2 additionally dumps the entire pattern as raw data.\n"
  "  -         Read filenames from stdin. Useful when there are too many files\n"
  "            for argv. Place after any other options if applicable.\n\n";

static bool dump_samples      = false;
static bool dump_patterns     = false;
static bool dump_pattern_rows = false;

enum DBM_error
{
  DBM_SUCCESS,
  DBM_READ_ERROR,
  DBM_SEEK_ERROR,
  DBM_NOT_A_DBM,
  DBM_INVALID,
};

static const char *DBM_strerror(int err)
{
  switch(err)
  {
    case DBM_SUCCESS:           return "no error";
    case DBM_READ_ERROR:        return "read error";
    case DBM_SEEK_ERROR:        return "seek error";
    case DBM_NOT_A_DBM:         return "not a DigiBooster Pro module";
    case DBM_INVALID:           return "invalid DBM";
  }
  return IFF_strerror(err);
}

enum DBM_features
{
  FT_MULTIPLE_SONGS,
  FT_ROWS_OVER_256,
  FT_CHUNK_OVER_4_MIB,
  NUM_FEATURES
};

static const char *FEATURE_STR[NUM_FEATURES] =
{
  ">1Song",
  ">256Rows",
  ">4MBChunk",
};

static const int MAX_SONGS = 2;
static const int MAX_INSTRUMENTS = 256;
static const int MAX_SAMPLES = 256;
static const int MAX_PATTERNS = 256;

struct DBM_song
{
  char name[45];
  uint16_t num_orders;
  uint16_t *order_list = nullptr;

  ~DBM_song()
  {
    delete[] order_list;
  }
};

struct DBM_instrument
{
  char name[33];
  uint16_t sample_id;
  uint16_t volume;
  uint32_t finetune_hz;
  uint32_t repeat_start;
  uint32_t repeat_length;
  uint16_t panning;
  uint16_t flags;
};

struct DBM_sample
{
  uint32_t flags;
  uint32_t length;
};

struct DBM_pattern
{
  uint16_t num_rows;
  uint32_t packed_data_size;
  // TODO: pattern data.
};

struct DBM_data
{
  /* Header (8) */

  char magic[4];
  uint16_t tracker_version;
  uint16_t reserved;

  /* NAME (44) */

  char name[45];
  bool read_name = false;

  /* INFO (10) */

  uint16_t num_instruments;
  uint16_t num_samples;
  uint16_t num_songs;
  uint16_t num_patterns;
  uint16_t num_channels;
  bool read_info = false;

  /* SONG */

  size_t current_song = 0;
  DBM_song songs[MAX_SONGS];

  /* INST */

  size_t current_inst = 0;
  DBM_instrument instruments[MAX_INSTRUMENTS];

  /* SMPL */

  size_t current_smpl = 0;
  DBM_sample samples[MAX_SAMPLES];

  /* PATT */

  size_t current_patt = 0;
  DBM_pattern patterns[MAX_PATTERNS];

  /* VENV - TODO */

  bool uses[NUM_FEATURES];
};

static const class DBM_NAME_Handler final: public IFFHandler<DBM_data>
{
public:
  DBM_NAME_Handler(const char *n, bool c): IFFHandler(n, c) {}

  int parse(FILE *fp, size_t len, DBM_data &m) const override
  {
    if(len < 44)
    {
      O_("Error     : NAME chunk length %zu, expected >=44.\n", len);
      return DBM_INVALID;
    }
    if(m.read_name)
    {
      O_("Error    : duplicate NAME.\n");
      return DBM_INVALID;
    }

    if(!fread(m.name, 44, 1, fp))
      return DBM_READ_ERROR;

    m.name[44] = '\0';
    m.read_name = true;
    return DBM_SUCCESS;
  }
} NAME_handler("NAME", false);

static const class DBM_INFO_Handler final: public IFFHandler<DBM_data>
{
public:
  DBM_INFO_Handler(const char *n, bool c): IFFHandler(n, c) {}

  int parse(FILE *fp, size_t len, DBM_data &m) const override
  {
    if(len < 10)
    {
      O_("Error     : INFO chunk length %zu, expected >=10.\n", len);
      return DBM_INVALID;
    }
    if(m.read_info)
    {
      O_("Error     : duplicate INFO.\n");
      return DBM_INVALID;
    }

    m.num_instruments = fget_u16be(fp);
    m.num_samples     = fget_u16be(fp);
    m.num_songs       = fget_u16be(fp);
    m.num_patterns    = fget_u16be(fp);
    m.num_channels    = fget_u16be(fp);

    if(feof(fp))
      return DBM_READ_ERROR;

    return DBM_SUCCESS;
  }
} INFO_handler("INFO", false);

static const class DBM_SONG_Handler final: public IFFHandler<DBM_data>
{
public:
  DBM_SONG_Handler(const char *n, bool c): IFFHandler(n, c) {}

  int parse(FILE *fp, size_t len, DBM_data &m) const override
  {
    // FIXME
    return 0;
  }
} SONG_handler("SONG", false);

static const class DBM_INST_Handler final: public IFFHandler<DBM_data>
{
public:
  DBM_INST_Handler(const char *n, bool c): IFFHandler(n, c) {}

  int parse(FILE *fp, size_t len, DBM_data &m) const override
  {
    // FIXME
    return 0;
  }
} INST_handler("INST", false);

static const class DBM_PATT_Handler final: public IFFHandler<DBM_data>
{
public:
  DBM_PATT_Handler(const char *n, bool c): IFFHandler(n, c) {}

  int parse(FILE *fp, size_t len, DBM_data &m) const override
  {
    if(len < 7)
    {
      O_("Error     : PATT chunk length < 7.\n");
      return DBM_INVALID;
    }
    if(m.current_patt >= MAX_PATTERNS)
    {
      O_("Warning   : ignoring pattern %zu.\n", m.current_patt);
      return DBM_SUCCESS;
    }

    DBM_pattern &p = m.patterns[m.current_patt++];

    p.num_rows         = fget_u16be(fp);
    p.packed_data_size = fget_u32be(fp);

    if(p.num_rows > 256)
      m.uses[FT_ROWS_OVER_256] = true;

    if(feof(fp))
      return DBM_READ_ERROR;

    return DBM_SUCCESS;
  }
} PATT_handler("PATT", false);

static const class DBM_SMPL_Handler final: public IFFHandler<DBM_data>
{
public:
  DBM_SMPL_Handler(const char *n, bool c): IFFHandler(n, c) {}

  int parse(FILE *fp, size_t len, DBM_data &m) const override
  {
    // FIXME
   return 0;
  }
} SMPL_handler("SMPL", false);

static const class DBM_VENV_Handler final: public IFFHandler<DBM_data>
{
public:
  DBM_VENV_Handler(const char *n, bool c): IFFHandler(n, c) {}

  int parse(FILE *fp, size_t len, DBM_data &m) const override
  {
    // FIXME
    return 0;
  }
} VENV_handler("VENV", false);

static const IFF<DBM_data> DBM_parser({
  &NAME_handler,
  &INFO_handler,
  &SONG_handler,
  &INST_handler,
  &PATT_handler,
  &SMPL_handler,
  &VENV_handler
});

int DBM_read(FILE *fp)
{
  DBM_data m{};
  DBM_parser.max_chunk_length = 0;

  if(!fread(m.magic, 4, 1, fp))
    return DBM_READ_ERROR;

  if(!strncmp(m.magic, "DBMO", 4))
    return DBM_NOT_A_DBM;

  m.tracker_version = fget_u16be(fp);
  fget_u16be(fp);

  int err = DBM_parser.parse_iff(fp, 0, m);
  if(err)
    return err;

  if(DBM_parser.max_chunk_length > 4*1024*1024)
    m.uses[FT_CHUNK_OVER_4_MIB] = true;

  O_("Name      : %s\n",  m.name);
  O_("Songs     : %u\n",  m.num_songs);
  O_("Instr.    : %u\n",  m.num_instruments);
  O_("Samples   : %u\n",  m.num_samples);
  O_("Channels  : %u\n",  m.num_channels);
  O_("Patterns  : %u\n",  m.num_patterns);
  O_("Max Chunk : %zu\n", DBM_parser.max_chunk_length);

  O_("Uses      :");
  for(int i = 0; i < NUM_FEATURES; i++)
    if(m.uses[i])
      fprintf(stderr, " %s", FEATURE_STR[i]);
  fprintf(stderr, "\n");

  if(dump_samples)
  {
    // FIXME
  }

  if(dump_patterns)
  {
    O_("          :\n");

    for(unsigned int i = 0; i < m.num_patterns; i++)
    {
      if(i >= MAX_PATTERNS)
        break;

      DBM_pattern &p = m.patterns[i];

      O_("Pattern %02x: %u rows, %u bytes\n", i, p.num_rows, p.packed_data_size);
    }

    // FIXME dump_pattern_rows
  }
  return DBM_SUCCESS;
}

void check_dbm(const char *filename)
{
  FILE *fp = fopen(filename, "rb");
  if(fp)
  {
    O_("File      : %s\n", filename);

    int err = DBM_read(fp);
    if(err)
      O_("Error     : %s\n\n", DBM_strerror(err));
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
    fprintf(stdout, "%s", USAGE);
    return 0;
  }

  for(int i = 1; i < argc; i++)
  {
    char *arg = argv[i];
    if(arg[0] == '-')
    {
      switch(arg[1])
      {
        case '\0':
          if(!read_stdin)
          {
            char buffer[1024];
            while(fgets_safe(buffer, stdin))
              check_dbm(buffer);

            read_stdin = true;
          }
          continue;

        case 'p':
          if(!arg[2] || !strcmp(arg + 2, "=1"))
          {
            dump_patterns = true;
            dump_pattern_rows = false;
            continue;
          }
          if(!strcmp(arg + 2, "=2"))
          {
            dump_patterns = true;
            dump_pattern_rows = true;
            continue;
          }
          if(!strcmp(arg + 2, "=0"))
          {
            dump_patterns = false;
            dump_pattern_rows = false;
            continue;
          }
          break;

        case 's':
          if(!arg[2] || !strcmp(arg + 2, "=1"))
          {
            dump_samples = true;
            continue;
          }
          if(!strcmp(arg + 2, "=0"))
          {
            dump_samples = false;
            continue;
          }
          break;
      }
    }
    check_dbm(arg);
  }

  return 0;
}
