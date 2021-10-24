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

#include "modutil.hpp"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <memory>

static size_t num_dtts = 0;


static constexpr char MAGIC_DSKT[] = "DskT";
static constexpr char MAGIC_ESKT[] = "EskT";

/* Implied in various places in the format and documentation. */
static constexpr size_t MAX_PATTERNS = 256;
static constexpr size_t MAX_CHANNELS = 16;
static constexpr size_t MAX_SAMPLES  = 63;
static constexpr size_t MAX_ROWS     = 256;
/* Sane upper bound ;-( */
static constexpr size_t MAX_ORDERS   = 65536;

struct DTT_header
{
  /*   0 */ char     magic[4]; /* DskT or EskT */
  /*   4 */ char     name[64];
  /*  68 */ char     author[64];
  /* 132 */ uint32_t flags;
  /* 136 */ uint32_t num_channels;
  /* 140 */ uint32_t num_orders;
  /* 144 */ uint8_t  panning[8];
  /* 152 */ uint32_t initial_speed;
  /* 156 */ uint32_t restart_pos;
  /* 160 */ uint32_t num_patterns;
  /* 164 */ uint32_t num_samples;
  /* 168 */
};

struct DTT_sample
{
  /*  0 */ uint8_t note; /* transpose? */
  /*  1 */ uint8_t volume;
  /*  2 */ uint16_t unused;
  /*  4 */ uint32_t period;
  /*  8 */ uint32_t sustain_start;
  /* 12 */ uint32_t sustain_length;
  /* 16 */ uint32_t loop_start;
  /* 20 */ uint32_t loop_length;
  /* 24 */ uint32_t length;
  /* 28 */ char     name[32];
  /* 60 */ uint32_t offset;
  /* 64 */

  /* Compressed samples only. */
  uint32_t uncompressed_size;
  uint32_t compressed_size;
  uint32_t compression_flags;
  bool is_compressed;
};

struct DTT_event
{
  uint8_t note = 0;
  uint8_t sample = 0;
  uint8_t effect[4] = { 0 };
  uint8_t param[4] = { 0 };

  DTT_event() {}

  DTT_event(uint32_t a)
  {
    sample    = (a & 0x0000002fUL);
    note      = (a & 0x00000fc0UL) >> 6;
    effect[0] = (a & 0x0001f000UL) >> 12;
    param[0]  = (a & 0xff00000000) >> 24;
  }

  DTT_event(uint32_t a, uint32_t b)
  {
    sample    = (a & 0x0000001fUL);
    note      = (a & 0x00000fc0UL) >> 6;
    effect[0] = (a & 0x0001f000UL) >> 12;
    effect[1] = (a & 0x003e0000UL) >> 17;
    effect[2] = (a & 0x07c00000UL) >> 22;
    effect[3] = (a & 0xf1000000UL) >> 27;
    param[0]  = (b & 0x000000ffUL);
    param[1]  = (b & 0x0000ff00UL) >> 8;
    param[2]  = (b & 0x00ff0000UL) >> 16;
    param[3]  = (b & 0xff000000UL) >> 24;
  }

  static bool is_multieffect(uint32_t a)
  {
    return (a & 0x00fe0000UL) != 0;
  }
};

struct DTT_pattern
{
  DTT_event *events = nullptr;

  /* Stored in header. */
  uint32_t offset;
  uint16_t num_rows; /* stored as a byte, minus 1 */

  /* Compressed patterns only. */
  uint32_t uncompressed_size;
  uint32_t compressed_size;
  uint32_t compression_flags;
  bool is_compressed;

  ~DTT_pattern()
  {
    delete[] events;
  }

  void allocate(uint32_t num_channels)
  {
    events = new DTT_event[num_rows * num_channels]{};
  }
};

struct DTT_data
{
  DTT_header  header;
  DTT_sample  samples[MAX_SAMPLES];
  DTT_pattern patterns[MAX_PATTERNS];
  uint8_t     *orders = nullptr;

  bool compression = false;
  bool any_compressed_patterns = false;
  bool any_compressed_samples = false;
  char name[65];
  char author[65];

  ~DTT_data()
  {
    delete[] orders;
  }

  void allocate()
  {
    orders = new uint8_t[header.num_orders];
  }
};

/* Compressed offsets are stored negated (two's-complement). */
static bool is_compressed_offset(uint32_t off)
{
  return (off & 0x80000000UL) != 0;
}

static bool DTT_uncompress(uint8_t *dest, size_t dest_len, const uint8_t *src, size_t src_len)
{
  return false;
}


class DTT_loader: public modutil::loader
{
public:
  DTT_loader(): modutil::loader("-", "dtt", "Desktop Tracker") {}

  virtual modutil::error load(FILE *fp, long file_length) const override
  {
    DTT_data m{};
    DTT_header &h = m.header;

    if(!fread(h.magic, sizeof(h.magic), 1, fp))
      return modutil::FORMAT_ERROR;

    if(memcmp(h.magic, MAGIC_DSKT, sizeof(h.magic)) &&
       memcmp(h.magic, MAGIC_ESKT, sizeof(h.magic)))
      return modutil::FORMAT_ERROR;

    if(h.magic[0] == MAGIC_ESKT[0])
      m.compression = true;

    num_dtts++;

    /* Header. */
    if(!fread(h.name, sizeof(h.name), 1, fp) ||
       !fread(h.author, sizeof(h.author), 1, fp))
      return modutil::READ_ERROR;

    memcpy(m.name, h.name, sizeof(h.name));
    memcpy(m.author, h.author, sizeof(h.author));
    m.name[sizeof(h.name)] = '\0';
    m.author[sizeof(h.author)] = '\0';
    strip_module_name(m.name, sizeof(m.name));
    strip_module_name(m.author, sizeof(m.author));

    h.flags         = fget_u32le(fp);
    h.num_channels  = fget_u32le(fp);
    h.num_orders    = fget_u32le(fp);

    if(!fread(h.panning, sizeof(h.panning), 1, fp))
      return modutil::READ_ERROR;

    h.initial_speed = fget_u32le(fp);
    h.restart_pos   = fget_u32le(fp);
    h.num_patterns  = fget_u32le(fp);
    h.num_samples   = fget_u32le(fp);
    if(feof(fp))
      return modutil::READ_ERROR;

    if(h.num_channels > MAX_CHANNELS)
    {
      format::error("invalid channel count %" PRIu32 " >= %zu", h.num_channels, MAX_CHANNELS);
      return modutil::INVALID;
    }
    if(h.num_patterns > MAX_PATTERNS)
    {
      format::error("invalid pattern count %" PRIu32 " >= %zu", h.num_patterns, MAX_PATTERNS);
      return modutil::INVALID;
    }
    if(h.num_samples > MAX_SAMPLES)
    {
      format::error("invalid sample count %" PRIu32 " >= %zu", h.num_samples, MAX_SAMPLES);
      return modutil::INVALID;
    }
    if(h.num_orders > MAX_ORDERS)
    {
      format::error("invalid order count %" PRIu32 " >= %zu", h.num_orders, MAX_ORDERS);
      return modutil::INVALID;
    }

    m.allocate();

    if(!fread(m.orders, h.num_orders, 1, fp))
      return modutil::READ_ERROR;

    if(h.num_orders & 3)
      if(fseek(fp, 4 - (h.num_orders & 3), SEEK_CUR))
        return modutil::SEEK_ERROR;

    for(size_t i = 0; i < h.num_patterns; i++)
      m.patterns[i].offset = fget_u32le(fp);

    for(size_t i = 0; i < h.num_patterns; i++)
      m.patterns[i].num_rows = fgetc(fp);

    if(feof(fp))
      return modutil::READ_ERROR;

    if(h.num_patterns & 3)
      if(fseek(fp, 4 - (h.num_patterns & 3), SEEK_CUR))
        return modutil::SEEK_ERROR;

    /* Samples. */
    for(size_t i = 0; i < h.num_samples; i++)
    {
      uint8_t buffer[64];
      if(!fread(buffer, 64, 1, fp))
        return modutil::READ_ERROR;

      DTT_sample &s = m.samples[i];
      s.note           = buffer[0];
      s.volume         = buffer[1];
      s.unused         = mem_u16le(buffer + 2);
      s.period         = mem_u32le(buffer + 4);
      s.sustain_start  = mem_u32le(buffer + 8);
      s.sustain_length = mem_u32le(buffer + 12);
      s.loop_start     = mem_u32le(buffer + 16);
      s.loop_length    = mem_u32le(buffer + 20);
      s.length         = mem_u32le(buffer + 24);
      s.offset         = mem_u32le(buffer + 60);

      memcpy(s.name, buffer + 28, 32);
      s.name[sizeof(s.name) - 1] = '\0';
    }

    /* Patterns. */
    for(size_t i = 0; i < h.num_patterns; i++)
    {
      DTT_pattern &p = m.patterns[i];
      p.allocate(h.num_channels);

      uint32_t real_offset = p.offset;
      if(is_compressed_offset(p.offset))
      {
        real_offset = ~p.offset + 1;
        p.is_compressed = true;
      }

      if(fseek(fp, real_offset, SEEK_SET))
      {
        format::warning("failed to seek to pattern %zu", i);
        continue;
      }

      std::unique_ptr<uint8_t> u_data(nullptr);

      if(p.is_compressed)
      {
        p.uncompressed_size = fget_u32le(fp);
        p.compressed_size   = fget_u32le(fp);
        p.compression_flags = fget_u32le(fp);

        std::unique_ptr<uint8_t> c_data(new uint8_t[p.compressed_size]);
        u_data = std::unique_ptr<uint8_t>(new uint8_t[p.uncompressed_size]);

        if(!fread(c_data.get(), p.compressed_size, 1, fp))
          return modutil::READ_ERROR;

        if(!DTT_uncompress(u_data.get(), p.uncompressed_size, c_data.get(), p.compressed_size))
        {
          format::warning("error depacking pattern %zu", i);
          continue;
        }
      }
      else
        p.compressed_size = 0;

      DTT_event *current = p.events;
      uint8_t *pos = u_data.get();
      for(size_t row = 0; row < p.num_rows; row++)
      {
        for(size_t track = 0; track < h.num_channels; track++, current++)
        {
          uint32_t a, b;
          if(!p.is_compressed)
          {
            a = fget_u32le(fp);
            p.compressed_size += 4;
          }
          else
            a = mem_u32le((pos += 4));

          if(DTT_event::is_multieffect(a))
          {
            if(!p.is_compressed)
            {
              b = fget_u32le(fp);
              p.compressed_size += 4;
            }
            else
              b = mem_u32le((pos += 4));

            *current = DTT_event(a, b);
          }
          else
            *current = DTT_event(a);
        }
      }
      if(feof(fp))
        return modutil::READ_ERROR;
    }

    /* Sample data (get uncompressed/compressed sizes only). */
    for(size_t i = 0; i < h.num_samples; i++)
    {
      DTT_sample &s = m.samples[i];
      if(is_compressed_offset(s.offset))
      {
        uint32_t real_offset = ~s.offset + 1;
        if(fseek(fp, real_offset, SEEK_SET))
        {
          format::warning("failed to seek to sample %zu", i);
          continue;
        }
        s.uncompressed_size = fget_u32le(fp);
        s.compressed_size   = fget_u32le(fp);
        s.compression_flags = fget_u32le(fp);
        s.is_compressed = true;
        m.any_compressed_samples = true;
      }
    }


    /* Print information. */

    format::line("Name",     "%s", m.name);
    format::line("Author",   "%s", m.author);
    format::line("Type",     "Desktop Tracker");
    format::line("Samples",  "%" PRIu32, h.num_samples);
    format::line("Channels", "%" PRIu32, h.num_channels);
    format::line("Patterns", "%" PRIu32, h.num_patterns);
    format::line("Orders",   "%" PRIu32 " (r:%" PRIu32 ")", h.num_orders, h.restart_pos);

    if(Config.dump_samples)
    {
      namespace table = format::table;

      static constexpr const char *s_labels[] =
      {
        "Name", "Length", "LoopStart", "LoopLen", "SusStart", "SusLen", "Vol", "Tr.", "Period"
      };

      table::table<
        table::string<32>,
        table::spacer,
        table::number<10>,
        table::number<10>,
        table::number<10>,
        table::number<10>,
        table::number<10>,
        table::spacer,
        table::number<4>,
        table::number<4>,
        table::number<10>> s_table;

      format::line();
      s_table.header("Samples", s_labels);

      for(size_t i = 0; i < h.num_samples; i++)
      {
        DTT_sample &s = m.samples[i];
        s_table.row(i + 1, s.name, {},
          s.length, s.loop_start, s.loop_length, s.sustain_start, s.sustain_length, {},
          s.volume, s.note, s.period);
      }
      if(m.any_compressed_samples)
      {
        static constexpr const char *c_labels[] =
        {
          "Uncmp.Sz.", "Cmp.Sz.", "Flags"
        };
        table::table<
          table::number<10>,
          table::number<10>,
          table::number<8, table::RIGHT | table::HEX>> c_table;

        format::line();
        c_table.header("Samples", c_labels);

        for(size_t i = 0; i < h.num_samples; i++)
        {
          DTT_sample &s = m.samples[i];
          if(s.is_compressed)
            c_table.row(i + 1, s.uncompressed_size, s.compressed_size, s.compression_flags);
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
        DTT_pattern &p = m.patterns[i];

        using EVENT = format::event<format::note, format::sample, format::effectWide,
                                    format::effectWide, format::effectWide, format::effectWide>;
        format::pattern<EVENT> pattern(i, h.num_channels, p.num_rows, p.compressed_size);

        if(!Config.dump_pattern_rows)
        {
          pattern.summary();
          continue;
        }

        DTT_event *current = p.events;
        for(size_t row = 0; row < p.num_rows; row++)
        {
          for(size_t track = 0; track < h.num_channels; track++, current++)
          {
            format::note       a{ current->note };
            format::sample     b{ current->sample };
            format::effectWide c{ current->effect[0], current->param[0] };
            format::effectWide d{ current->effect[1], current->param[1] };
            format::effectWide e{ current->effect[2], current->param[2] };
            format::effectWide f{ current->effect[3], current->param[3] };

            pattern.insert(EVENT(a, b, c, d, e, f));
          }
        }
        pattern.print();
      }
    }

    return modutil::SUCCESS;
  }

  virtual void report() const override
  {
    if(!num_dtts)
      return;

    format::report("Total DTTs", num_dtts);
  }
};

static const DTT_loader loader;
