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
 * Protracker Studio 16 / Epic MegaGames MASI "old format" handler.
 * See psm_load.cpp for the newer format.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

#include "modutil.hpp"

static int total_ps16 = 0;


enum PS16_features
{
  FT_ROWS_OVER_64,
  FT_SAMPLE_OVER_64K,
  NUM_FEATURES
};

static const char *FEATURE_STR[NUM_FEATURES] =
{
  "P:>64Rows",
  "S:>64k",
};

static const int MAX_SAMPLES  = 256;
static const int MAX_PATTERNS = 256;
static const int MAX_ORDERS   = 256;
static const int MAX_CHANNELS = 32;

static constexpr char MAGIC[] = "PSM\xfe";
//static constexpr char MAGIC_PORD[] = "PORD";
//static constexpr char MAGIC_PPAN[] = "PPAN";
//static constexpr char MAGIC_PSAH[] = "PSAH";
//static constexpr char MAGIC_PSAM[] = "PSAM";


struct PS16_instrument
{
  enum
  {
    SYNTH    = (1 << 0),
    _16BIT   = (1 << 2),
    UNSIGNED = (1 << 3),
    RAW      = (1 << 4),
    BIDI     = (1 << 5),
    GRAVIS   = (1 << 6), // unsupported?
    LOOP     = (1 << 7),
  };

  /*  0 */ char     filename[13];
  /* 13 */ char     name[24];
  /* 37 */ uint32_t data_offset;
  /* 41 */ uint32_t ram_offset; /* runtime only? */
  /* 45 */ uint16_t id;
  /* 47 */ uint8_t  type;
  /* 48 */ uint32_t length;
  /* 52 */ uint32_t loop_start;
  /* 56 */ uint32_t loop_end;
  /* 60 */ uint8_t  finetune;
  /* 61 */ uint8_t  default_volume;
  /* 62 */ uint16_t c2_speed;
  /* 64 */
};

struct PS16_event
{
  enum
  {
    NOTE    = (1 << 7),
    VOLUME  = (1 << 6),
    EFFECT  = (1 << 5),
    CHANNEL = 0x1f
  };

  uint8_t note = 0;
  uint8_t instrument = 0;
  uint8_t volume = 0;
  uint8_t effect = 0;
  uint8_t param = 0;

  PS16_event() {}
  PS16_event(uint8_t flags, const uint8_t *&pos, const uint8_t *end)
  {
    if(flags & NOTE)
    {
      if(pos <= end - 2)
      {
        note = *pos++;
        instrument = *pos++;
      }
      else
        pos = end;
    }

    if(flags & VOLUME)
    {
      if(pos <= end - 1)
      {
        volume = *pos++;
      }
      else
        pos = end;
    }

    if(flags & EFFECT)
    {
      if(pos <= end - 2)
      {
        effect = *pos++;
        param = *pos++;
      }
      else
        pos = end;
    }
  }
};

struct PS16_pattern
{
  PS16_event *events = nullptr;
  uint16_t raw_size;
  uint8_t num_rows;
  uint8_t num_channels;

  ~PS16_pattern()
  {
    delete[] events;
  }

  void allocate()
  {
    events = new PS16_event[num_rows * num_channels]{};
  }
};

struct PS16_header
{
  /*   0 */ uint8_t  magic[4];
  /*   4 */ char     name[59];
  /*  63 */ uint8_t  eof;
  /*  64 */ uint8_t  type;
  /*  65 */ uint8_t  version; /* high nibble: major; low nibble: minor*/
  /*  66 */ uint8_t  pattern_version; /* 1 seems to mean it uses a "255 channel format" and isn't used?? */
  /*  67 */ uint8_t  init_speed;
  /*  68 */ uint8_t  init_bpm;
  /*  69 */ uint8_t  global_volume;
  /*  70 */ uint16_t num_orders;
  /*  72 */ uint16_t num_orders2; /* The same as num_orders; this format was originally meant to store multiple sequences. */
  /*  74 */ uint16_t num_patterns;
  /*  76 */ uint16_t num_samples;
  /*  78 */ uint16_t num_channels_play; /* Number of channels to play. */
  /*  80 */ uint16_t num_channels; /* Number of channels to process. */
  /*  82 */ uint32_t orders_offset;
  /*  86 */ uint32_t panning_offset;
  /*  90 */ uint32_t patterns_offset;
  /*  94 */ uint32_t samples_offset;
  /*  98 */ uint32_t comments_offset;
  /* 102 */ uint32_t total_pattern_size;
  /* 106 */ uint8_t  reserved[40];
  /* 146 */
};

struct PS16_data
{
  PS16_header header;
  PS16_pattern patterns[MAX_PATTERNS];
  PS16_instrument instruments[MAX_SAMPLES];

  uint8_t orders[MAX_ORDERS];
  uint8_t panning[MAX_CHANNELS];

  bool uses[NUM_FEATURES];
};

class PS16_loader : modutil::loader
{
public:
  PS16_loader(): modutil::loader("PSM", "ps16", "Protracker Studio 16 / Epic MegaGames MASI") {}

  virtual modutil::error load(FILE *fp, long file_length) const override
  {
    PS16_data m{};
    PS16_header &h = m.header;
    uint8_t buf[256];

    if(!fread(h.magic, 4, 1, fp))
      return modutil::FORMAT_ERROR;

    if(memcmp(h.magic, MAGIC, 4))
      return modutil::FORMAT_ERROR;

    total_ps16++;

    /* Header */

    if(!fread(buf + 4, 142, 1, fp))
      return modutil::READ_ERROR;

    memcpy(h.name, buf + 4, sizeof(h.name));
    h.name[sizeof(h.name) - 1] = '\0';
    strip_module_name(h.name, sizeof(h.name));

    h.eof                = buf[63];
    h.type               = buf[64];
    h.version            = buf[65];
    h.pattern_version    = buf[66];
    h.init_speed         = buf[67];
    h.init_bpm           = buf[68];
    h.global_volume      = buf[69];
    h.num_orders         = mem_u16le(buf + 70);
    h.num_orders2        = mem_u16le(buf + 72);
    h.num_patterns       = mem_u16le(buf + 74);
    h.num_samples        = mem_u16le(buf + 76);
    h.num_channels_play  = mem_u16le(buf + 78);
    h.num_channels       = mem_u16le(buf + 80);
    h.orders_offset      = mem_u32le(buf + 82);
    h.panning_offset     = mem_u32le(buf + 86);
    h.patterns_offset    = mem_u32le(buf + 90);
    h.samples_offset     = mem_u32le(buf + 94);
    h.comments_offset    = mem_u32le(buf + 98);
    h.total_pattern_size = mem_u32le(buf + 102);
    //memcpy(reserved, buf + 106, 40);

    if(h.num_orders > MAX_ORDERS)
    {
      format::error("invalid order count %u", h.num_orders);
      return modutil::INVALID;
    }
    if(h.num_patterns > MAX_PATTERNS)
    {
      format::error("invalid pattern count %u", h.num_patterns);
      return modutil::INVALID;
    }
    if(h.num_samples > MAX_SAMPLES)
    {
      format::error("invalid sample count %u", h.num_samples);
      return modutil::INVALID;
    }
    if(h.num_channels > MAX_CHANNELS)
    {
      format::error("invalid channel count %u", h.num_channels);
      return modutil::INVALID;
    }

    /* Orders */

    if(fseek(fp, h.orders_offset, SEEK_SET))
    {
      format::error("error seeking to orders");
      return modutil::SEEK_ERROR;
    }

    /* This actually goes 4 bytes BEFORE the offset!
    // Optional magic
    if(fread(buf, 4, 1, fp))
      if(memcmp(buf, MAGIC_PORD, 4))
        fseek(fp, -4, SEEK_CUR);
    */

    if(fread(m.orders, 1, h.num_orders, fp) != h.num_orders)
    {
      format::error("read error at order list");
      return modutil::READ_ERROR;
    }

    /* Panning */

    if(!fseek(fp, h.panning_offset, SEEK_SET))
    {
      /* This actually goes 4 bytes BEFORE the offset!
      // Optional(?) magic
      if(fread(buf, 4, 1, fp))
        if(memcmp(buf, MAGIC_PPAN, 4))
          fseek(fp, -4, SEEK_CUR);
      */

      if(fread(m.panning, 1, h.num_channels, fp) != h.num_channels)
      {
        format::error("read error at panning table");
        return modutil::READ_ERROR;
      }
    }
    else
    {
      format::warning("error seeking to panning");
      memset(m.panning, 0x80, sizeof(m.panning));
    }

    /* Patterns */

    if(fseek(fp, h.patterns_offset, SEEK_SET))
    {
      format::error("error seeking to patterns");
      return modutil::SEEK_ERROR;
    }

    std::vector<uint8_t> patbuf(65536);

    for(size_t i = 0; i < h.num_patterns; i++)
    {
      PS16_pattern &p = m.patterns[i];

      p.raw_size     = fget_u16le(fp);
      p.num_rows     = fgetc(fp);
      p.num_channels = fgetc(fp);

      if(p.raw_size < 4 || p.num_rows == 0 || p.num_channels == 0)
        continue;

      if(!fread(patbuf.data(), p.raw_size - 4, 1, fp))
      {
        format::warning("read error at pattern %zu", i);
        break;
      }

      p.allocate();

      PS16_event dummy;
      const uint8_t *pos = patbuf.data();
      const uint8_t *end = pos + p.raw_size - 4;

      for(size_t row = 0; pos < end && row < p.num_rows;)
      {
        uint8_t flags = *pos++;
        if(!flags)
        {
          row++;
          continue;
        }

        uint8_t channel = flags & PS16_event::CHANNEL;
        PS16_event &ev = channel < p.num_channels ? p.events[row * p.num_channels + channel] : dummy;

        ev = PS16_event(flags, pos, end);
      }
    }

    /* Samples */

    if(fseek(fp, h.samples_offset, SEEK_SET))
    {
      format::error("error seeking to samples");
      return modutil::SEEK_ERROR;
    }

    /* This actually goes 4 bytes BEFORE the offset!
    // Optional(?) magic
    if(fread(buf, 4, 1, fp))
      if(memcmp(buf, MAGIC_PSAH, 4))
        fseek(fp, -4, SEEK_CUR);
    */

    for(size_t i = 0; i < h.num_samples; i++)
    {
      PS16_instrument &ins = m.instruments[i];

      if(!fread(buf, 64, 1, fp))
      {
        format::error("read error at sample %zu", i);
        return modutil::READ_ERROR;
      }

      memcpy(ins.filename, buf, 13);
      ins.filename[12] = '\0';
      memcpy(ins.name, buf + 13, sizeof(ins.name) - 1);
      ins.name[sizeof(ins.name) - 1] = '\0';

      ins.data_offset    = mem_u32le(buf + 37);
      ins.ram_offset     = mem_u32le(buf + 41);
      ins.id             = mem_u16le(buf + 45);
      ins.type           = buf[47];
      ins.length         = mem_u32le(buf + 48);
      ins.loop_start     = mem_u32le(buf + 52);
      ins.loop_end       = mem_u32le(buf + 56);
      ins.finetune       = buf[60];
      ins.default_volume = buf[61];
      ins.c2_speed       = mem_u16le(buf + 62);
    }

    /* Comment */
    /* TODO: I don't think any of the original PS16 modules have this. */


    /* Print information */

    format::line("Name", "%s", h.name);
    format::line("Type", "MASI PS16 v%d.%02d", h.version >> 4, h.version & 0xf);

    format::line("Samples",  "%u", h.num_samples);
    format::line("Channels", "%u", h.num_channels);
    format::line("Patterns", "%u", h.num_patterns);
    format::line("Orders",   "%u", h.num_orders);
    format::line("Tempo",    "%u/%u", h.init_speed, h.init_bpm);
    format::uses(m.uses, FEATURE_STR);

    if(Config.dump_samples)
    {
      namespace table = format::table;

      static constexpr const char *labels[] =
      {
        "Name", "Filename", "Offset", "Length", "LoopStart", "LoopEnd", "ID", "Type", "Vol", "Fine", "C2Spd"
      };

      table::table<
        table::string<23>,
        table::string<12>,
        table::spacer,
        table::number<10>,
        table::number<10>,
        table::number<10>,
        table::number<10>,
        table::spacer,
        table::number<5>,
        table::number<5>,
        table::number<4>,
        table::number<4>,
        table::number<6>> i_table;

      format::line();
      i_table.header("Samples", labels);

      for(size_t i = 0; i < h.num_samples; i++)
      {
        PS16_instrument &ins = m.instruments[i];
        i_table.row(i + 1, ins.name, ins.filename, {},
          ins.data_offset, ins.length, ins.loop_start, ins.loop_end, {},
          ins.id, ins.type, ins.default_volume, ins.finetune, ins.c2_speed);
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
        PS16_pattern &p = m.patterns[i];

        using EVENT = format::event<format::note, format::sample, format::volume, format::effectWide>;
        format::pattern<EVENT> pattern(i, p.num_channels, p.num_rows, p.raw_size);

        if(!Config.dump_pattern_rows)
        {
          pattern.summary();
          continue;
        }
        if(!p.events)
        {
          pattern.print();
          continue;
        }

        PS16_event *current = p.events;
        for(size_t row = 0; row < p.num_rows; row++)
        {
          for(size_t track = 0; track < p.num_channels; track++, current++)
          {
            format::note       a{ current->note };
            format::sample     b{ current->instrument };
            format::volume     c{ current->volume };
            format::effectWide d{ current->effect, current->param };

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
    if(!total_ps16)
      return;

    format::report("Total PS16s", total_ps16);
  }
};

static const PS16_loader loader;
