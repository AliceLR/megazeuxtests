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

static int total_dbm = 0;


enum DBM_features
{
  FT_MULTIPLE_SONGS,
  FT_ROWS_OVER_64,
  FT_ROWS_OVER_256,
  FT_CHUNK_ORDER,
  FT_CHUNK_OVER_4_MIB,
  FT_VENV_CHUNK,
  FT_PENV_CHUNK,
  FT_DSPE_CHUNK,
  FT_BAD_VOLUME_ENVELOPE,
  FT_BAD_PAN_ENVELOPE,
  FT_NEGATIVE_ENVELOPE_VALUE,
  FT_HIGH_ENVELOPE_VALUE,
  NUM_FEATURES
};

static const char *FEATURE_STR[NUM_FEATURES] =
{
  ">1Song",
  ">64Rows",
  ">256Rows",
  "Misordered",
  ">4MBChunk",
  "VENV",
  "PENV",
  "DSPE",
  "BadVolEnv",
  "BadPanEnv",
  "EnvPt<0",
  "EnvPt>64",
};

static const int MAX_SONGS = 16;
static const int MAX_INSTRUMENTS = 256;
static const int MAX_SAMPLES = 256;
static const int MAX_PATTERNS = 256;

struct DBM_song
{
  char name[45];
  uint16_t num_orders;
  uint16_t *orders = nullptr;

  ~DBM_song()
  {
    delete[] orders;
  }
};

struct DBM_instrument
{
  enum flags
  {
    FORWARD_LOOP = (1 << 0),
    BIDI_LOOP    = (1 << 1),
  };

  char name[31];
  uint16_t sample_id;
  uint16_t volume;
  uint32_t finetune_hz;
  uint32_t repeat_start; /* in samples(?) */
  uint32_t repeat_length; /* in samples(?) */
  int16_t panning;
  uint16_t flags;
};

struct DBM_sample
{
  enum flags
  {
    S_8_BIT  = (1 << 0),
    S_16_BIT = (1 << 1),
    S_32_BIT = (1 << 2),
  };

  const char *type_str() const
  {
    if(flags & S_8_BIT)
      return "8-bit";
    if(flags & S_16_BIT)
      return "16-bit";
    if(flags & S_32_BIT)
      return "32-bit";
    return "?";
  }

  uint32_t flags;
  uint32_t length; /* in samples. */
};

struct DBM_pattern
{
  enum flags
  {
    NOTE       = (1 << 0),
    INSTRUMENT = (1 << 1),
    EFFECT_1   = (1 << 2),
    PARAM_1    = (1 << 3),
    EFFECT_2   = (1 << 4),
    PARAM_2    = (1 << 5)
  };

  struct note
  {
    uint8_t note;
    uint8_t instrument;
    uint8_t effect_1;
    uint8_t param_1;
    uint8_t effect_2;
    uint8_t param_2;
  };

  uint16_t num_rows;
  uint32_t packed_data_size;
  note     *data;
  char     *name; /* From PNAM. */

  ~DBM_pattern()
  {
    delete[] data;
    delete[] name;
  }
};

struct DBM_envelope
{
  static const size_t MAX_POINTS = 32;

  enum flags
  {
    ENABLED   = (1 << 0),
    SUSTAIN_1 = (1 << 1),
    LOOP      = (1 << 2),
    SUSTAIN_2 = (1 << 3),
  };

  struct point
  {
    uint16_t time;
    int16_t value;
  };

  uint16_t instrument_id;
  uint8_t flags;
  uint8_t num_points;
  uint8_t sustain_1_point;
  uint8_t loop_start_point;
  uint8_t loop_end_point;
  uint8_t sustain_2_point;
  DBM_envelope::point points[32];
};

struct DBM_data
{
  /* Header (8) */

  char magic[4];
  uint16_t tracker_version;
  uint16_t reserved;

  /* NAME (44) */

  char name[45];
  char name_stripped[45];
  bool read_name = false;

  /* INFO (10) */

  uint16_t num_instruments;
  uint16_t num_samples;
  uint16_t num_songs;
  uint16_t num_patterns;
  uint16_t num_channels;
  bool read_info = false;

  /* SONG */

  DBM_song songs[MAX_SONGS];

  /* PATT and PNAM */

  DBM_pattern patterns[MAX_PATTERNS];
  uint16_t pattern_name_encoding;
  bool pattern_names;

  /* INST */

  DBM_instrument instruments[MAX_INSTRUMENTS];

  /* SMPL */

  DBM_sample samples[MAX_SAMPLES];

  /* VENV */

  uint16_t num_volume_envelopes;
  DBM_envelope *volume_envelopes;

  /* PENV */

  uint16_t num_pan_envelopes;
  DBM_envelope *pan_envelopes;

  /* DSPE */

  uint16_t dspe_mask_length;
  uint8_t *dspe_mask;
  uint16_t dspe_global_echo_delay;
  uint16_t dspe_global_echo_feedback;
  uint16_t dspe_global_echo_mix;
  uint16_t dspe_cross_channel_echo;

  bool uses[NUM_FEATURES];

  ~DBM_data()
  {
    delete[] volume_envelopes;
    delete[] pan_envelopes;
    delete[] dspe_mask;
  }
};

static const class DBM_NAME_Handler final: public IFFHandler<DBM_data>
{
public:
  DBM_NAME_Handler(const char *n, bool c): IFFHandler(n, c) {}

  modutil::error parse(FILE *fp, size_t len, DBM_data &m) const override
  {
    if(len < 44)
    {
      format::error("NAME chunk length %zu, expected >=44.", len);
      return modutil::INVALID;
    }
    if(m.read_name)
    {
      format::error("duplicate NAME.");
      return modutil::INVALID;
    }

    if(!fread(m.name, 44, 1, fp))
      return modutil::READ_ERROR;

    m.name[44] = '\0';
    m.read_name = true;

    memcpy(m.name_stripped, m.name, arraysize(m.name));
    strip_module_name(m.name_stripped, arraysize(m.name_stripped));

    return modutil::SUCCESS;
  }
} NAME_handler("NAME", false);

static const class DBM_INFO_Handler final: public IFFHandler<DBM_data>
{
public:
  DBM_INFO_Handler(const char *n, bool c): IFFHandler(n, c) {}

  modutil::error parse(FILE *fp, size_t len, DBM_data &m) const override
  {
    if(len < 10)
    {
      format::error("INFO chunk length %zu, expected >=10.", len);
      return modutil::INVALID;
    }
    if(m.read_info)
    {
      format::error("duplicate INFO.");
      return modutil::INVALID;
    }
    m.read_info = true;

    m.num_instruments = fget_u16be(fp);
    m.num_samples     = fget_u16be(fp);
    m.num_songs       = fget_u16be(fp);
    m.num_patterns    = fget_u16be(fp);
    m.num_channels    = fget_u16be(fp);

    if(feof(fp))
      return modutil::READ_ERROR;

    return modutil::SUCCESS;
  }
} INFO_handler("INFO", false);

static const class DBM_SONG_Handler final: public IFFHandler<DBM_data>
{
public:
  DBM_SONG_Handler(const char *n, bool c): IFFHandler(n, c) {}

  modutil::error parse(FILE *fp, size_t len, DBM_data &m) const override
  {
    if(len < 46 * m.num_songs)
    {
      format::error("SONG chunk length < %u", 46 * m.num_songs);
      return modutil::INVALID;
    }

    for(size_t i = 0; i < m.num_songs; i++)
    {
      if(i >= MAX_SONGS)
      {
        format::warning("ignoring SONG %zu.", i);
        continue;
      }

      DBM_song &sng = m.songs[i];

      if(!fread(sng.name, 44, 1, fp))
        return modutil::READ_ERROR;
      sng.name[44] = '\0';

      sng.num_orders = fget_u16be(fp);
      if(feof(fp))
        return modutil::READ_ERROR;

      sng.orders = new uint16_t[sng.num_orders];

      for(size_t i = 0; i < sng.num_orders; i++)
        sng.orders[i] = fget_u16be(fp);

      if(feof(fp))
        return modutil::READ_ERROR;
    }
    return modutil::SUCCESS;
  }
} SONG_handler("SONG", false);

static const class DBM_PATT_Handler final: public IFFHandler<DBM_data>
{
public:
  DBM_PATT_Handler(const char *n, bool c): IFFHandler(n, c) {}

  modutil::error parse(FILE *fp, size_t len, DBM_data &m) const override
  {
    if(!m.read_info)
      m.uses[FT_CHUNK_ORDER] = true;

    for(size_t i = 0; i < m.num_patterns; i++)
    {
      if(i >= MAX_PATTERNS)
      {
        format::warning("ignoring pattern %zu", i);
        continue;
      }
      if(len < 6)
      {
        format::error("pattern %zu header truncated.", i);
        return modutil::READ_ERROR;
      }

      DBM_pattern &p = m.patterns[i];

      p.num_rows         = fget_u16be(fp);
      p.packed_data_size = fget_u32be(fp);
      len -= 6;

      if(p.num_rows > 64)
        m.uses[FT_ROWS_OVER_64] = true;
      if(p.num_rows > 256)
        m.uses[FT_ROWS_OVER_256] = true;

      if(feof(fp))
        return modutil::READ_ERROR;

      if(len < p.packed_data_size)
      {
        format::error("pattern %zu truncated (left=%zu, expected>=%u).",
          i, len, p.packed_data_size);
        return modutil::READ_ERROR;
      }

      if(!p.num_rows)
      {
        if(p.packed_data_size)
          if(fseek(fp, p.packed_data_size, SEEK_CUR))
            return modutil::SEEK_ERROR;
        continue;
      }

      size_t num_notes = m.num_channels * p.num_rows;
      p.data = new DBM_pattern::note[num_notes]{};

      DBM_pattern::note *row = p.data;
      DBM_pattern::note *end = p.data + num_notes;
      ssize_t left = p.packed_data_size;

      while(left > 0 && row < end)
      {
        uint8_t channel = fgetc(fp);
        left--;

        if(!channel)
        {
          row += m.num_channels;
          continue;
        }

        uint8_t flags = fgetc(fp);
        left--;

        channel--;
        if(channel >= m.num_channels)
        {
          format::error("invalid pattern data.");
          return modutil::INVALID;
        }

        if(flags & DBM_pattern::NOTE)
        {
          row[channel].note = fgetc(fp);
          left--;
        }
        if(flags & DBM_pattern::INSTRUMENT)
        {
          row[channel].instrument = fgetc(fp);
          left--;
        }
        if(flags & DBM_pattern::EFFECT_1)
        {
          row[channel].effect_1 = fgetc(fp);
          left--;
        }
        if(flags & DBM_pattern::PARAM_1)
        {
          row[channel].param_1 = fgetc(fp);
          left--;
        }
        if(flags & DBM_pattern::EFFECT_2)
        {
          row[channel].effect_2 = fgetc(fp);
          left--;
        }
        if(flags & DBM_pattern::PARAM_2)
        {
          row[channel].param_2 = fgetc(fp);
          left--;
        }

        if(feof(fp))
          return modutil::READ_ERROR;
      }
      if(left)
      {
        if(left < 0)
          format::warning("read %zd past end of packed data for pattern %zu.", -left, i);
        /* Don't print for 1 byte, this seems to be common... */
        if(left > 1)
          format::warning("%zd of packed data remaining for pattern %zu.", left, i);
        if(fseek(fp, left, SEEK_CUR))
          return modutil::SEEK_ERROR;
      }

      len -= p.packed_data_size;
    }
    return modutil::SUCCESS;
  }
} PATT_handler("PATT", false);

static const class DBM_PNAM_Handler final: public IFFHandler<DBM_data>
{
public:
  DBM_PNAM_Handler(const char *n, bool c): IFFHandler(n, c) {}

  modutil::error parse(FILE *fp, size_t len, DBM_data &m) const override
  {
    if(!m.read_info)
      m.uses[FT_CHUNK_ORDER] = true;

    m.pattern_names = true;
    m.pattern_name_encoding = fget_u16be(fp);

    ssize_t left = len;
    for(size_t i = 0; i < m.num_patterns; i++)
    {
      if(!left)
        break;

      uint8_t length = fgetc(fp);
      left--;

      if(left < length)
        break;

      DBM_pattern &p = m.patterns[i];

      p.name = new char[length + 1];
      if(!fread(p.name, length, 1, fp))
        return modutil::READ_ERROR;
      p.name[length] = '\0';
      left -= length;
    }
    return modutil::SUCCESS;
  }
} PNAM_handler("PNAM", false);

static const class DBM_INST_Handler final: public IFFHandler<DBM_data>
{
public:
  DBM_INST_Handler(const char *n, bool c): IFFHandler(n, c) {}

  modutil::error parse(FILE *fp, size_t len, DBM_data &m) const override
  {
    if(!m.read_info)
      m.uses[FT_CHUNK_ORDER] = true;

    if(len < 50 * m.num_instruments)
    {
      format::error("INST chunk length < %u", 50 * m.num_instruments);
      return modutil::INVALID;
    }

    for(size_t i = 0; i < m.num_instruments; i++)
    {
      if(i > MAX_INSTRUMENTS)
      {
        format::warning("ignoring instrument %zu.", i);
        continue;
      }

      DBM_instrument &is = m.instruments[i];

      if(!fread(is.name, 30, 1, fp))
        return modutil::READ_ERROR;
      is.name[30] = '\0';

      is.sample_id     = fget_u16be(fp);
      is.volume        = fget_u16be(fp);
      is.finetune_hz   = fget_u32be(fp);
      is.repeat_start  = fget_u32be(fp);
      is.repeat_length = fget_u32be(fp);
      is.panning       = fget_s16be(fp);
      is.flags         = fget_u16be(fp);
    }

    if(feof(fp))
      return modutil::READ_ERROR;

    return modutil::SUCCESS;
  }
} INST_handler("INST", false);

static const class DBM_SMPL_Handler final: public IFFHandler<DBM_data>
{
public:
  DBM_SMPL_Handler(const char *n, bool c): IFFHandler(n, c) {}

  modutil::error parse(FILE *fp, size_t len, DBM_data &m) const override
  {
    if(!m.read_info)
      m.uses[FT_CHUNK_ORDER] = true;

    if(len < 8 * m.num_samples)
    {
      format::error("SMPL chunk length < %u.", 8 * m.num_samples);
      return modutil::INVALID;
    }

    for(size_t i = 0; i < m.num_samples; i++)
    {
      if(i >= MAX_SAMPLES)
      {
        format::warning("ignoring sample %zu.", i);
        continue;
      }

      DBM_sample &s = m.samples[i];

      s.flags  = fget_u32be(fp);
      s.length = fget_u32be(fp);

      size_t byte_length = s.length;
      if(s.flags & DBM_sample::S_16_BIT)
        byte_length <<= 1;
      else
      if(s.flags & DBM_sample::S_32_BIT)
        byte_length <<= 2;

      /* Ignore the sample data... */
      if(fseek(fp, byte_length, SEEK_CUR))
        return modutil::SEEK_ERROR;
    }
    return modutil::SUCCESS;
  }
} SMPL_handler("SMPL", false);

static modutil::error read_envelope(DBM_data &m, DBM_envelope &env, size_t env_num, FILE *fp)
{
  env.instrument_id    = fget_u16be(fp);
  env.flags            = fgetc(fp);
  env.num_points       = fgetc(fp) + 1;
  env.sustain_1_point  = fgetc(fp);
  env.loop_start_point = fgetc(fp);
  env.loop_end_point   = fgetc(fp);
  env.sustain_2_point  = fgetc(fp);

  for(size_t i = 0; i < DBM_envelope::MAX_POINTS; i++)
  {
    DBM_envelope::point &p = env.points[i];
    p.time  = fget_u16be(fp);
    p.value = fget_s16be(fp);
    if(p.value < 0)
      m.uses[FT_NEGATIVE_ENVELOPE_VALUE] = true;
    if(p.value > 64)
      m.uses[FT_HIGH_ENVELOPE_VALUE] = true;
  }

  if(feof(fp))
    return modutil::READ_ERROR;

  if(env.instrument_id > m.num_instruments)
  {
    format::warning("envelope %zu for invalid instrument %u",
      env_num, env.instrument_id);
    return modutil::INVALID;
  }

  if(env.num_points > DBM_envelope::MAX_POINTS)
  {
    format::warning("envelope %zu for instrument %u contains too many points (%zu)",
      env_num, env.instrument_id, (size_t)env.num_points);
    return modutil::INVALID;
  }

  if(env.sustain_1_point >= DBM_envelope::MAX_POINTS)
  {
    format::warning("envelope %zu sustain 1 (%u) >= max points (32)",
      env_num, env.sustain_1_point);
    return modutil::INVALID;
  }

  if(env.sustain_2_point >= DBM_envelope::MAX_POINTS)
  {
    format::warning("envelope %zu sustain 2 (%u) >= max points (32)",
      env_num, env.sustain_2_point);
    return modutil::INVALID;
  }

  if(env.loop_start_point >= DBM_envelope::MAX_POINTS)
  {
    format::warning("envelope %zu loop start (%u) >= max points (32)",
      env_num, env.loop_start_point);
    return modutil::INVALID;
  }

  if(env.loop_end_point >= DBM_envelope::MAX_POINTS)
  {
    format::warning("envelope %zu loop end (%u) >= max points (32)",
      env_num, env.loop_end_point);
    return modutil::INVALID;
  }

  return modutil::SUCCESS;
}

static const class DBM_VENV_Handler final: public IFFHandler<DBM_data>
{
public:
  DBM_VENV_Handler(const char *n, bool c): IFFHandler(n, c) {}

  modutil::error parse(FILE *fp, size_t len, DBM_data &m) const override
  {
    if(!m.read_info)
      m.uses[FT_CHUNK_ORDER] = true;

    m.uses[FT_VENV_CHUNK] = true;

    if(len < 4)
    {
      format::error("VENV chunk length < 4.");
      return modutil::INVALID;
    }

    uint16_t num_envelopes = fget_u16be(fp);
    if(feof(fp))
      return modutil::READ_ERROR;

    if(!num_envelopes)
      return modutil::SUCCESS;

    m.num_volume_envelopes = num_envelopes;
    m.volume_envelopes = new DBM_envelope[num_envelopes];

    if(len < (size_t)(num_envelopes * 136 + 2))
    {
      format::warning("VENV chunk truncated (envelopes=%u, size=%zu, expected=%zu).",
        num_envelopes, len, (size_t)(2 + num_envelopes * 136));
      return modutil::SUCCESS;
    }

    for(size_t i = 0; i < num_envelopes; i++)
    {
      DBM_envelope &env = m.volume_envelopes[i];
      modutil::error result = read_envelope(m, env, i, fp);
      if(result == modutil::INVALID)
      {
        m.uses[FT_BAD_VOLUME_ENVELOPE] = true;
      }
      else

      if(result)
        return result;
    }
    return modutil::SUCCESS;
  }
} VENV_handler("VENV", false);

static const class DBM_PENV_Handler final: public IFFHandler<DBM_data>
{
public:
  DBM_PENV_Handler(const char *n, bool c): IFFHandler(n,c) {}

  modutil::error parse(FILE *fp, size_t len, DBM_data &m) const override
  {
    if(!m.read_info)
      m.uses[FT_CHUNK_ORDER] = true;

    m.uses[FT_PENV_CHUNK] = true;

    if(len < 4)
    {
      format::error("PENV chunk length < 4.");
      return modutil::INVALID;
    }

    uint16_t num_envelopes = fget_u16be(fp);
    if(feof(fp))
      return modutil::READ_ERROR;

    if(!num_envelopes)
      return modutil::SUCCESS;

    m.num_pan_envelopes = num_envelopes;
    m.pan_envelopes = new DBM_envelope[num_envelopes];

    if(len < (size_t)(num_envelopes * 136 + 2))
    {
      format::warning("PENV chunk truncated (envelopes=%u, size=%zu, expected=%zu).",
        num_envelopes, len, (size_t)(2 + num_envelopes * 136));
      return modutil::SUCCESS;
    }

    for(size_t i = 0; i < num_envelopes; i++)
    {
      DBM_envelope &env = m.pan_envelopes[i];
      modutil::error result = read_envelope(m, env, i, fp);
      if(result == modutil::INVALID)
      {
        m.uses[FT_BAD_PAN_ENVELOPE] = true;
      }
      else

      if(result)
        return result;
    }
    return modutil::SUCCESS;
  }
} PENV_handler("PENV", false);

static const class DBM_DSPE_Handler final: public IFFHandler<DBM_data>
{
public:
  DBM_DSPE_Handler(const char *n, bool c): IFFHandler(n,c) {}

  modutil::error parse(FILE *fp, size_t len, DBM_data &m) const override
  {
    m.uses[FT_DSPE_CHUNK] = true;

    if(len < 10)
    {
      format::error("DSPE chunk length < 10.");
      return modutil::INVALID;
    }

    m.dspe_mask_length = fget_u16be(fp);
    if(feof(fp))
      return modutil::READ_ERROR;

    m.dspe_mask = new uint8_t[m.dspe_mask_length];
    if(!fread(m.dspe_mask, m.dspe_mask_length, 1, fp))
      return modutil::READ_ERROR;

    m.dspe_global_echo_delay    = fget_u16be(fp);
    m.dspe_global_echo_feedback = fget_u16be(fp);
    m.dspe_global_echo_mix      = fget_u16be(fp);
    m.dspe_cross_channel_echo   = fget_u16be(fp);
    if(feof(fp))
      return modutil::READ_ERROR;

    return modutil::SUCCESS;
  }
} DSPE_handler("DSPE", false);

static const IFF<DBM_data> DBM_parser({
  &NAME_handler,
  &INFO_handler,
  &SONG_handler,
  &PATT_handler,
  &PNAM_handler,
  &INST_handler,
  &SMPL_handler,
  &VENV_handler,
  &PENV_handler,
  &DSPE_handler,
});

static void print_envelopes(const char *name, size_t num, DBM_envelope *envs)
{
  format::line();
  O_("%-6s  : Instr. #  Enabled : (...)=Loop  S=Sustain\n", name);
  O_("------  : --------  ------- : -------------------------\n");
  for(unsigned int i = 0; i < num; i++)
  {
    DBM_envelope &env = envs[i];
    size_t loop_start = (env.flags & DBM_envelope::LOOP)      ? env.loop_start_point : -1;
    size_t loop_end   = (env.flags & DBM_envelope::LOOP)      ? env.loop_end_point   : -1;
    size_t sustain_1  = (env.flags & DBM_envelope::SUSTAIN_1) ? env.sustain_1_point  : -1;
    size_t sustain_2  = (env.flags & DBM_envelope::SUSTAIN_2) ? env.sustain_2_point  : -1;

    O_("    %02x  : %-8u  %-7s : ",
      i + 1, env.instrument_id, (env.flags & DBM_envelope::ENABLED) ? "Yes" : "No"
    );
    for(size_t j = 0; j < env.num_points; j++)
    {
      fprintf(stderr, "%1s%-5u%1s ",
        (j == loop_start) ? "(" : "",
        env.points[j].time,
        (j == loop_end) ? ")" : ""
      );
    }
    fprintf(stderr, "\n");

    O_("        : %8s  %7s : ", "", "");
    for(size_t j = 0; j < env.num_points; j++)
    {
      fprintf(stderr, "%1s%-4d%1s%1s ",
        (j == loop_start) ? "(" : "",
        env.points[j].value,
        (j == sustain_1 || j == sustain_2) ? "S" : "",
        (j == loop_end) ? ")" : ""
      );
    }
    fprintf(stderr, "\n");
  }
}


class DBM_loader : public modutil::loader
{
public:
  DBM_loader(): modutil::loader("DBM", "DigiBooster Pro") {}

  virtual modutil::error load(FILE *fp, long file_length) const override
  {
    DBM_data m{};
    DBM_parser.max_chunk_length = 0;

    if(!fread(m.magic, 4, 1, fp))
      return modutil::READ_ERROR;

    if(strncmp(m.magic, "DBM0", 4))
      return modutil::FORMAT_ERROR;

    total_dbm++;

    m.tracker_version = fget_u16be(fp);
    fget_u16be(fp);

    modutil::error err = DBM_parser.parse_iff(fp, 0, m);
    if(err)
      return err;

    if(DBM_parser.max_chunk_length > 4*1024*1024)
      m.uses[FT_CHUNK_OVER_4_MIB] = true;

    format::line("Name",      "%s", m.name_stripped);
    format::line("Type",      "DBM %d.%02x", m.tracker_version >> 8, m.tracker_version & 0xFF);
    format::line("Songs",     "%u", m.num_songs);
    if(m.num_samples)
      format::line("Samples", "%u", m.num_samples);
    if(m.num_instruments)
      format::line("Instr.",  "%u", m.num_instruments);
    if(m.num_volume_envelopes)
      format::line("V.Envs.", "%u", m.num_volume_envelopes);
    if(m.num_pan_envelopes)
      format::line("P.Envs.", "%u", m.num_pan_envelopes);
    format::line("Channels",  "%u", m.num_channels);
    format::line("Patterns",  "%u", m.num_patterns);
    format::line("MaxChunk",  "%zu", DBM_parser.max_chunk_length);
    format::uses(m.uses, FEATURE_STR);

    if(Config.dump_samples)
    {
      namespace table = format::table;

      if(m.num_samples)
      {
        static const char *labels[] = { "Type", "Length (samples)" };

        format::line();
        table::table<
          table::string<6>,
          table::number<16>> s_table;

        s_table.header("Samples", labels);
        for(unsigned int i = 0; i < m.num_samples; i++)
        {
          DBM_sample &s = m.samples[i];
          s_table.row(i + 1, s.type_str(), s.length);
        }
      }

      if(m.num_instruments)
      {
        static const char *labels[] =
        {
          "Name", "Sample #", "Vol", "Pan", "C4 Rate", "LoopStart", "LoopLen",
        };
        format::line();
        table::table<
          table::string<30>,
          table::spacer,
          table::number<8>,
          table::number<4>,
          table::number<4>,
          table::number<10>,
          table::spacer,
          table::number<10>,
          table::number<10>> i_table;

        i_table.header("Instr.", labels);
        for(unsigned int i = 0; i < m.num_instruments; i++)
        {
          DBM_instrument &is = m.instruments[i];
          i_table.row(i + 1, is.name, {},
           is.sample_id, is.volume, is.panning, is.finetune_hz, {},
           is.repeat_start, is.repeat_length);
        }
      }

      if(m.num_volume_envelopes)
      {
        print_envelopes("V.Env.", m.num_volume_envelopes, m.volume_envelopes);
      }

      if(m.num_pan_envelopes)
      {
        print_envelopes("P.Env.", m.num_pan_envelopes, m.pan_envelopes);
      }
    }

    if(Config.dump_patterns)
    {
      format::line();

      /* Print each song + order list. */
      for(unsigned int i = 0; i < m.num_songs; i++)
      {
        if(i >= MAX_SONGS)
          break;

        DBM_song &sng = m.songs[i];

        format::song("Song", "Orders", i + 1, sng.name, sng.orders, sng.num_orders);
        format::line();
      }

      for(unsigned int i = 0; i < m.num_patterns; i++)
      {
        if(i >= MAX_PATTERNS)
          break;

        DBM_pattern &p = m.patterns[i];
        const char *name = m.pattern_names ? p.name : nullptr;

        using EVENT = format::event<format::note, format::sample, format::effectXM, format::effectXM>;
        format::pattern<EVENT> pattern(name, i, m.num_channels, p.num_rows, p.packed_data_size);

        if(!Config.dump_pattern_rows)
        {
          pattern.summary();
          continue;
        }

        DBM_pattern::note *current = p.data;

        for(size_t row = 0; row < p.num_rows; row++)
        {
          for(size_t track = 0; track < m.num_channels; track++, current++)
          {
            format::note     a{ current->note };
            format::sample   b{ current->instrument };
            format::effectXM c{ current->effect_1, current->param_1 };
            format::effectXM d{ current->effect_2, current->param_2 };

            pattern.insert(EVENT(a, b, c, d));
          }
        }
        pattern.print();
      }
    }
    return modutil::SUCCESS;
  }

  virtual void report() const override
  {
    if(!total_dbm)
      return;

    format::report("Total DBMs", total_dbm);
  }
};

static const DBM_loader loader;
