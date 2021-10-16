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

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "modutil.hpp"

static int total_s3ms = 0;


enum S3M_features
{
  FT_OVER_255_INSTRUMENTS,
  FT_OVER_256_PATTERNS,
  FT_OVER_256_ORDERS,
  FT_ADLIB,
  FT_ADLIB_CHANNELS,
  FT_ADLIB_INSTRUMENTS,
  FT_INTGP_UNKNOWN,
  FT_INTGP_SOUNDBLASTER,
  FT_INTGP_GRAVIS_ULTRASOUND,
  FT_SAMPLE_SEGMENT_HI,
  FT_SAMPLE_STEREO,
  FT_SAMPLE_16,
  FT_SAMPLE_ADPCM,
  NUM_FEATURES
};

static const char *FEATURE_STR[NUM_FEATURES] =
{
  "I>255",
  "P>256",
  "O>256",
  "AdLib",
  "AdLib(C)",
  "AdLib(I)",
  "Gp:?",
  "Gp:SB",
  "Gp:GUS",
  "S:HiSeg",
  "S:Stereo",
  "S:16",
  "S:ADPCM",
};

static const char S3M_MAGIC[] = "SCRM";
static const char SAMPLE_MAGIC[] = "SCRS";
static const char ADLIB_MAGIC[] = "SCRI";

static const char SCREAMTRACKER3[] = "Scrm";
static const char BEROTRACKER[] = "BeRo";
static const char MODPLUG_TRACKER[] = "Modplug";

static const char *TRACKERS[16] =
{
  "?",
  SCREAMTRACKER3, /* Scream Tracker 3 */
  "Orpheus",      /* IMAGO Orpheus */
  "IT",           /* Impulse Tracker */
  "Schism",       /* Schism Tracker. Apparently some BeRoTracker modules use 0x4100. */
  "OpenMPT",      /* OpenMPT */
  BEROTRACKER,    /* BeRoTracker */
  "?",
  "?",
  "?",
  "?",
  "?",
  "?",
  "?",
  "?",
  "?",
};

static const unsigned int MAX_CHANNELS = 32;
static const unsigned int HAS_PANNING_TABLE = 252;

enum S3M_flags
{
  ST2_VIBRATO        = (1<<0),
  ST2_TEMPO          = (1<<1),
  AMIGA_SLIDES       = (1<<2),
  ZVOL_OPTIMIZATIONS = (1<<3),
  AMIGA_LIMITS       = (1<<4),
  /* */
  ST300_VOLSLIDES    = (1<<6),
  SPECIAL_DATA       = (1<<7),
};

struct S3M_header
{
  /*  0 */ char     name[28];
  /* 28 */ uint8_t  eof;
  /* 29 */ uint8_t  type;
  /* 30 */ uint16_t reserved;
  /* 32 */ uint16_t num_orders;
  /* 34 */ uint16_t num_instruments;
  /* 36 */ uint16_t num_patterns;
  /* 38 */ uint16_t flags;
  /* 40 */ uint16_t cwtv; /* "Created with tracker/version" */
  /* 42 */ uint16_t ffi; /* "File format information" */
  /* 44 */ char     magic[4]; /* SCRM */
  /* 48 */ uint8_t  global_volume;
  /* 49 */ uint8_t  initial_speed;
  /* 50 */ uint8_t  initial_tempo;
  /* 51 */ uint8_t  master_volume; /* bit 7: stereo(1) or mono(0) */
  /* 52 */ uint8_t  click_removal; /* Gravis Ultrasound click removal */
  /* 53 */ uint8_t  has_panning_table; /* must be 252 for panning table to be present */
  /* 54 */ uint8_t  reserved2[8];
  /* 62 */ uint16_t special_segment; /* paragraph pointer to special custom data */
  /* 64 */ uint8_t  channel_settings[32];
  /* 96 */

  uint8_t panning_table[32];
};

struct S3M_instrument
{
  enum
  {
    UNUSED      = 0,
    SAMPLE      = 1,
    ADLIB       = 2,
    ADLIB_BD    = 3,
    ADLIB_SNARE = 4,
    ADLIB_TOM   = 5,
    ADLIB_CYM   = 6,
    ADLIB_HIHAT = 7,
  };

  enum S3M_sample_flags
  {
    LOOP   = (1<<0),
    STEREO = (1<<1),
    S16    = (1<<2),
    // Stored in the packing field when Modplug ADPCM is present.
    ADPCM  = 4,
  };

  /*  0 */ uint8_t  type;
  /*  1 */ char     filename[12];
  /* 13 */ uint8_t  _sample_segment[3];/* sample only, paragraph pointer to sample data, 24-bits */
  /* 16 */ uint32_t length;       /* sample only, bytes */
  /* 20 */ uint32_t loop_start;   /* sample only, bytes */
  /* 24 */ uint32_t loop_end;     /* sample only, bytes */
  /* 28 */ uint8_t  default_volume;
  /* 29 */ uint8_t  dsk;          /* AdLib only; not specified by the format documentation. */
  /* 30 */ uint8_t  packing;      /* sample only */
  /* 31 */ uint8_t  flags;        /* sample only */
  /* 32 */ uint32_t c2speed;
  /* 36 */ uint32_t reserved;
  /* 40 */ uint16_t int_gp;       /* sample only. internal: address of sample in Gravis memory */
  /* 42 */ uint16_t int_512;      /* sample only. internal: flags for SoundBlaster loop expansion */
  /* 44 */ uint32_t int_lastpos;  /* sample only. internal: last used position for SoundBlaster */
  /* 48 */ char     name[28];
  /* 76 */ char     magic[4];
  /* 80 */

  /* AdLib instruments store these where length/loopstart/loopend go. */
  /* 20 */ uint8_t  operators[12];

  uint16_t instrument_segment; /* paragraph pointer to this instrument */

  uint32_t sample_segment()
  {
    // Stored in WTF endian
    return (_sample_segment[0] << 16) | mem_u16le(_sample_segment + 1);
  }
};

struct S3M_event
{
  uint8_t note = 0;
  uint8_t instrument = 0;
  uint8_t volume = 0;
  uint8_t effect = 0;
  uint8_t param = 0;

  S3M_event() {}
  S3M_event(uint8_t flg, const uint8_t *&stream, const uint8_t *end)
  {
    if(flg & 0x20)
    {
      if(stream < end)
        note = stream[0];
      if(stream + 1 < end)
        instrument = stream[1];

      stream += 2;
    }
    if(flg & 0x40)
    {
      if(stream < end)
        volume = stream[0];

      stream++;
    }
    if(flg & 0x80 && stream + 2 <= end)
    {
      if(stream < end)
        effect = stream[0];
      if(stream + 1 < end)
        param = stream[1];

      stream += 2;
    }
  }
};

struct S3M_pattern
{
  S3M_event *events = nullptr;
  uint16_t packed_size;
  uint16_t pattern_segment; /* paragraph pointer to this pattern */

  ~S3M_pattern()
  {
    delete[] events;
  }

  void allocate(uint8_t channels, uint8_t rows)
  {
    events = new S3M_event[channels * rows]{};
  }
};

struct S3M_data
{
  S3M_header     header;
  uint8_t        *orders = nullptr;
  S3M_instrument *instruments = nullptr;
  S3M_pattern    *patterns = nullptr;
  uint8_t        *buffer = nullptr;

  char name[29];
  const char *tracker_string;
  unsigned int max_channel;
  unsigned int num_channels;
  unsigned int num_samples;
  unsigned int num_adlib;
  bool uses[NUM_FEATURES];

  ~S3M_data()
  {
    delete[] orders;
    delete[] instruments;
    delete[] patterns;
    delete[] buffer;
  }

  void allocate()
  {
    orders = new uint8_t[header.num_orders]{};
    patterns = new S3M_pattern[header.num_patterns]{};
    instruments = new S3M_instrument[header.num_instruments]{};
    buffer = new uint8_t[1 << 16];
  }
};


class S3M_loader : public modutil::loader
{
public:
  S3M_loader(): modutil::loader("S3M", "s3m", "Scream Tracker 3") {}

  virtual modutil::error load(FILE *fp, long file_length) const override
  {
    S3M_data m{};
    S3M_header &h = m.header;
    uint8_t buffer[96];

    if(!fread(buffer, 96, 1, fp))
      return modutil::FORMAT_ERROR;

    if(memcmp(buffer + 44, "SCRM", 4))
      return modutil::FORMAT_ERROR;

    total_s3ms++;

    /* Header. */

    memcpy(h.name, buffer, sizeof(h.name));
    memcpy(m.name, h.name, sizeof(h.name));
    m.name[sizeof(h.name)] = '\0';
    strip_module_name(m.name, sizeof(m.name));

    h.eof               = buffer[28];
    h.type              = buffer[29];
    h.reserved          = mem_u16le(buffer + 30);
    h.num_orders        = mem_u16le(buffer + 32);
    h.num_instruments   = mem_u16le(buffer + 34);
    h.num_patterns      = mem_u16le(buffer + 36);
    h.flags             = mem_u16le(buffer + 38);
    h.cwtv              = mem_u16le(buffer + 40);
    h.ffi               = mem_u16le(buffer + 42);

    memcpy(h.magic, buffer + 44, sizeof(h.magic));

    h.global_volume     = buffer[48];
    h.initial_speed     = buffer[49];
    h.initial_tempo     = buffer[50];
    h.master_volume     = buffer[51];
    h.click_removal     = buffer[52];
    h.has_panning_table = buffer[53];

    memcpy(h.reserved2, buffer + 54, sizeof(h.reserved2));

    h.special_segment   = mem_u16le(buffer + 62);

    memcpy(h.channel_settings, buffer + 64, MAX_CHANNELS);
    // Now synchronized with the FILE stream.

    if(h.num_instruments > 255)
      m.uses[FT_OVER_255_INSTRUMENTS] = true;
    if(h.num_patterns > 256)
      m.uses[FT_OVER_256_PATTERNS] = true;
    if(h.num_orders > 256)
      m.uses[FT_OVER_256_ORDERS] = true;

    m.allocate();

    // Order list.
    // Standard Scream Tracker 3 S3Ms are saved with this padded to
    // a multiple of 4 (to keep the segment pointers aligned?), but
    // other trackers (IT) seem to ignore that when saving.
    if(!fread(m.orders, h.num_orders, 1, fp))
      return modutil::READ_ERROR;

    // Instrument parapointers.
    for(size_t i = 0; i < h.num_instruments; i++)
      m.instruments[i].instrument_segment = fget_u16le(fp);

    // Pattern parapointers.
    for(size_t i = 0; i < h.num_patterns; i++)
      m.patterns[i].pattern_segment = fget_u16le(fp);

    // Panning table.
    if(h.has_panning_table == HAS_PANNING_TABLE)
    {
      if(!fread(h.panning_table, MAX_CHANNELS, 1, fp))
        return modutil::READ_ERROR;
    }
    if(feof(fp))
      return modutil::READ_ERROR;

    // Get channel count.
    bool adlib_channels = false;
    for(size_t i = 0; i < MAX_CHANNELS; i++)
    {
      if(~h.channel_settings[i] & (1<<7))
      {
        m.num_channels++;
        m.max_channel = i + 1;
        if((h.channel_settings[i] & 0x7f) >= 16)
          adlib_channels = true;
      }
    }

    // Get printable tracker.
    if(h.cwtv == 0x4100)
      m.tracker_string = BEROTRACKER;
    else
      m.tracker_string = TRACKERS[h.cwtv >> 12];


    /* Instruments. */
    int intgp_min = 65536;
    int intgp_max = 0;
    for(size_t i = 0; i < h.num_instruments; i++)
    {
      S3M_instrument &ins = m.instruments[i];
      if(!ins.instrument_segment)
        continue;

      if(fseek(fp, ins.instrument_segment << 4, SEEK_SET))
        return modutil::SEEK_ERROR;

      if(!fread(buffer, 80, 1, fp))
      {
        format::warning("read error at instrument %zu : segment %u", i, ins.instrument_segment);
        return modutil::READ_ERROR;
      }

      ins.type = buffer[0];
      memcpy(ins.magic, buffer + 76, sizeof(ins.magic));

      if(ins.type == S3M_instrument::UNUSED)
      {
        m.num_samples++;
      }
      else

      if(ins.type == S3M_instrument::SAMPLE && !memcmp(ins.magic, SAMPLE_MAGIC, sizeof(ins.magic)))
      {
        m.num_samples++;
      }
      else

      if(ins.type >= S3M_instrument::ADLIB && !memcmp(ins.magic, ADLIB_MAGIC, sizeof(ins.magic)))
      {
        m.num_adlib++;

        memcpy(ins.operators, buffer + 16, sizeof(ins.operators));
      }
      else
      {
        format::warning("skipping invalid instrument %zu: %u / %4.4s", i, ins.type, ins.magic);
        continue;
      }

      memcpy(ins.filename, buffer + 1, sizeof(ins.filename));
      memcpy(ins._sample_segment, buffer + 13, sizeof(ins._sample_segment));

      ins.length         = mem_u32le(buffer + 16);
      ins.loop_start     = mem_u32le(buffer + 20);
      ins.loop_end       = mem_u32le(buffer + 24);
      ins.default_volume = buffer[28];
      ins.dsk            = buffer[29];
      ins.packing        = buffer[30];
      ins.flags          = buffer[31];
      ins.c2speed        = mem_u32le(buffer + 32);
      ins.reserved       = mem_u32le(buffer + 36);
      ins.int_gp         = mem_u16le(buffer + 40);
      ins.int_512        = mem_u16le(buffer + 42);
      ins.int_lastpos    = mem_u32le(buffer + 44);

      memcpy(ins.name, buffer + 48, sizeof(ins.name));

      if(ins.type == S3M_instrument::SAMPLE && ins.length > 0)
      {
        if(ins.int_gp < intgp_min)
          intgp_min = ins.int_gp;
        if(ins.int_gp > intgp_max)
          intgp_max = ins.int_gp;

        if(ins.flags & S3M_instrument::STEREO)
          m.uses[FT_SAMPLE_STEREO] = true;

        if(ins.flags & S3M_instrument::S16)
          m.uses[FT_SAMPLE_16] = true;

        if(ins.packing == S3M_instrument::ADPCM)
          m.uses[FT_SAMPLE_ADPCM] = true;

        if(ins._sample_segment[0])
          m.uses[FT_SAMPLE_SEGMENT_HI] = true;

        // TODO: not sure if this MPT fingerprinting is correct.
        if(h.cwtv == 0x1320 && (ins.packing == 4 || ins.int_gp == 0))
          m.tracker_string = MODPLUG_TRACKER;
      }
    }

    // Experimental ST3 SoundBlaster and Gravis Ultrasound fingerprinting.
    // See: https://github.com/libxmp/libxmp/issues/357
    if(m.tracker_string == SCREAMTRACKER3 && m.num_samples)
    {
      if(intgp_min >= 1)
      {
        if(intgp_max == 1)
          m.uses[FT_INTGP_SOUNDBLASTER] = true;
        else
          m.uses[FT_INTGP_GRAVIS_ULTRASOUND] = true;
      }
      else

      // Early ST 3.00 versions don't support GUS.
      if(h.cwtv == 0x1300)
      {
        m.uses[FT_INTGP_SOUNDBLASTER] = true;
      }
      else
        m.uses[FT_INTGP_UNKNOWN] = true;
    }

    if(adlib_channels && m.num_adlib)
    {
      m.uses[FT_ADLIB] = true;
    }
    else

    if(m.num_adlib)
    {
      m.uses[FT_ADLIB_INSTRUMENTS] = true;
    }
    else

    if(adlib_channels)
      m.uses[FT_ADLIB_CHANNELS] = true;


    /* Patterns. */
    for(size_t i = 0; i < h.num_patterns; i++)
    {
      S3M_pattern &p = m.patterns[i];
      if(!p.pattern_segment)
        continue;

      if(fseek(fp, p.pattern_segment << 4, SEEK_SET))
        return modutil::SEEK_ERROR;

      p.allocate(MAX_CHANNELS, 64);

      p.packed_size = fget_u16le(fp);
      if(!p.packed_size)
        continue;

      if(!fread(m.buffer, p.packed_size, 1, fp))
      {
        format::warning("read error at pattern %zu : segment %u", i, p.pattern_segment);
        return modutil::READ_ERROR;
      }

      const uint8_t *pos = m.buffer;
      const uint8_t *end = m.buffer + p.packed_size;
      size_t row = 0;
      while(pos < end && row < 64)
      {
        uint8_t flg = *(pos++);
        if(!flg)
        {
          row++;
          continue;
        }

        uint8_t chn = flg & 0x1f;
        S3M_event *ev = &(p.events[row * MAX_CHANNELS + chn]);
        *ev = S3M_event(flg, pos, end);

        if(pos > end)
        {
          format::warning("invalid pattern stream for %zu", i);
          break;
        }
      }
    }


    /* Print information. */

    format::line("Name",     "%s", m.name);
    format::line("Type",     "S3M v%d %s(%d:%d.%02X)", h.ffi, m.tracker_string, h.cwtv >> 12, (h.cwtv & 0xf00) >> 8, h.cwtv & 0xff);
    if(m.num_samples)
      format::line("Samples", "%u", m.num_samples);
    if(m.num_adlib)
      format::line("Instr.",  "%u", m.num_adlib);
    format::line("Channels", "%u", m.num_channels);
    format::line("Patterns", "%u", h.num_patterns);
    format::line("Orders",   "%u", h.num_orders);
    format::uses(m.uses, FEATURE_STR);

    if(Config.dump_samples)
    {
      namespace table = format::table;

      static const char *s_labels[] =
      {
        "Name", "Filename", "T", "Length", "LoopStart", "LoopEnd", "Vol", "Pck", "Flg", "C2Speed", "IntGp", "Int512", "ISeg", "SSeg"
      };
      static const char *a_labels[] =
      {
        "Name", "Filename", "T", "mCH", "cCH", "mLV", "cLV", "mAD", "cAD", "mSR", "cSR", "mWV", "cWV", "Alg", "Vol", "Dsk", "C2Speed", "ISeg"
      };

      table::table<
        table::string<28>,
        table::string<12>,
        table::spacer,
        table::number<1>,
        table::number<10>,
        table::number<10>,
        table::number<10>,
        table::spacer,
        table::number<4>,
        table::number<4>,
        table::number<4>,
        table::number<10>,
        table::spacer,
        table::number<6>,
        table::number<6>,
        table::number<6>,
        table::number<10>> s_table;

      constexpr int OP_FLG = table::RIGHT | table::HEX;
      table::table<
        table::string<28>,
        table::string<12>,
        table::spacer,
        table::number<1>,
        table::number<3, OP_FLG>,
        table::number<3, OP_FLG>,
        table::number<3, OP_FLG>,
        table::number<3, OP_FLG>,
        table::number<3, OP_FLG>,
        table::number<3, OP_FLG>,
        table::number<3, OP_FLG>,
        table::number<3, OP_FLG>,
        table::number<3, OP_FLG>,
        table::number<3, OP_FLG>,
        table::number<3, OP_FLG>,
        table::spacer,
        table::number<4>,
        table::number<4>,
        table::number<10>,
        table::spacer,
        table::number<6>> a_table;

      if(m.num_samples)
      {
        format::line();
        s_table.header("Samples", s_labels);
        for(size_t i = 0; i < h.num_instruments; i++)
        {
          S3M_instrument &ins = m.instruments[i];
          if(ins.type <= S3M_instrument::SAMPLE)
          {
            s_table.row(i + 1, ins.name, ins.filename, {},
              ins.type, ins.length, ins.loop_start, ins.loop_end, {},
              ins.default_volume, ins.packing, ins.flags, ins.c2speed, {},
              ins.int_gp, ins.int_512, ins.instrument_segment, ins.sample_segment());
          }
        }
      }

      if(m.num_adlib)
      {
        format::line();
        a_table.header("AdLib", a_labels);
        for(size_t i = 0; i < h.num_instruments; i++)
        {
          S3M_instrument &ins = m.instruments[i];
          if(ins.type >= S3M_instrument::ADLIB)
          {
            a_table.row(i + 1, ins.name, ins.filename, {},
              ins.type,         ins.operators[0], ins.operators[1], ins.operators[2],
              ins.operators[3], ins.operators[4], ins.operators[5], ins.operators[6],
              ins.operators[7], ins.operators[8], ins.operators[9], ins.operators[10], {},
              ins.default_volume, ins.dsk, ins.c2speed, {},
              ins.instrument_segment);
          }
        }
      }
    }

    if(Config.dump_patterns)
    {
      format::line();
      format::orders("Orders", m.orders, h.num_orders);

      if(!Config.dump_pattern_rows)
        format::line();

      for(size_t i = 0; i < h.num_patterns; i++)
      {
        S3M_pattern &p = m.patterns[i];

        using EVENT = format::event<format::note, format::sample, format::volume, format::effectIT>;
        format::pattern<EVENT> pattern(i, MAX_CHANNELS, 64, p.packed_size);
        pattern.extra("PSeg: %u", p.pattern_segment);

        if(!Config.dump_pattern_rows)
        {
          pattern.summary();
          continue;
        }

        S3M_event *current = p.events;

        for(size_t row = 0; row < 64; row++)
        {
          for(size_t track = 0; track < MAX_CHANNELS; track++, current++)
          {
            format::note     a{ current->note };
            format::sample   b{ current->instrument };
            format::volume   c{ current->volume };
            format::effectIT d{ current->effect, current->param };
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
    if(!total_s3ms)
      return;

    format::report("Total S3Ms", total_s3ms);
  }
};

static const S3M_loader loader;
