/**
 * Copyright (C) 2021-2025 Lachesis <petrifiedrowan@gmail.com>
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

#include "modutil.hpp"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

static size_t num_coconizer = 0;


static constexpr int MAX_ORDERS = 255;
static constexpr int MAX_PATTERNS = 256;
static constexpr int MAX_INSTRUMENTS = 255;
static constexpr size_t NUM_ROWS = 64;

struct Coconizer_header
{
  /*  0 */ uint8_t  info;
  /*  1 */ char     name[20];
  /* 21 */ uint8_t  num_instruments;
  /* 22 */ uint8_t  num_orders;
  /* 23 */ uint8_t  num_patterns;
  /* 24 */ uint32_t orders_offset;
  /* 28 */ uint32_t patterns_offset;
  /* 32 */
};

struct Coconizer_instrument
{
  /*  0 */ uint32_t sample_offset;
  /*  4 */ uint32_t length;
  /*  8 */ uint32_t volume;
  /* 12 */ uint32_t loop_start;
  /* 16 */ uint32_t loop_length;
  /* 20 */ char     name[11];
  /* 31 */ uint8_t  unused;
  /* 32 */
};

struct Coconizer_event
{
  uint8_t note;
  uint8_t instrument;
  uint8_t effect;
  uint8_t param;

  Coconizer_event() {}
  Coconizer_event(uint32_t tone_info)
  {
    note       = (tone_info & 0xff000000) >> 24;
    instrument = (tone_info & 0x00ff0000) >> 16;
    effect     = (tone_info & 0x0000ff00) >> 8;
    param      = (tone_info & 0x000000ff);
  }
};

struct Coconizer_pattern
{
  Coconizer_event *events = nullptr;

  ~Coconizer_pattern()
  {
    delete[] events;
  }

  void allocate(uint8_t channels, uint8_t rows)
  {
    events = new Coconizer_event[channels * rows]{};
  }
};

struct Coconizer_data
{
  Coconizer_header     header;
  Coconizer_instrument instruments[MAX_INSTRUMENTS];
  Coconizer_pattern    patterns[MAX_PATTERNS];
  uint8_t              orders[MAX_ORDERS];

  uint8_t num_channels;
  char name[21];
};


class Coconizer_loader: modutil::loader
{
  template<int N>
  constexpr bool test_lf(char (&name)[N]) const
  {
    for(int i = 0; i < N; i++)
    {
      if(name[i] == '\r')
      {
        name[i] = '\0';
        return true;
      }
    }
    return false;
  }

  modutil::error test_header(Coconizer_header &h, size_t file_length) const
  {
    /* Bit 7 indicates that this file is a module and not a song file.
     * libxmp requires it to be set, but this isn't actually necessary.
     * Only check the bottom nibble for 4 or 8 channels.
     */
    uint8_t num_channels = h.info & 0x0f;
    if(num_channels != 4 && num_channels != 8)
      return modutil::FORMAT_ERROR;

    /* Name should contain a 0x0d byte (document refers to this as "LF").
     */
    if(!test_lf(h.name))
      return modutil::FORMAT_ERROR;

    /* Order table and pattern offsets should exist within the constraints of
     * the file size.
     */
    size_t patterns_size = h.num_patterns * 4 * NUM_ROWS * num_channels;

    if(h.orders_offset > file_length ||
     h.patterns_offset > file_length ||
     h.num_orders > file_length ||
     patterns_size > file_length ||
     h.orders_offset > file_length - h.num_orders ||
     h.patterns_offset > file_length - patterns_size)
      return modutil::FORMAT_ERROR;

    return modutil::SUCCESS;
  }

  modutil::error test_instrument(Coconizer_header &h, Coconizer_instrument &ins,
   size_t file_length) const
  {
    /* Coconizer samples were expected to be on floppy disks
     * and shouldn't be larger than 1600k. This isn't foolproof
     * because samples could also be stored on hard disk (and
     * 3200k octa-density floppies also existed).
     *
     * (TODO what is the largest Coconizer supports?). */
    static constexpr size_t SAMPLE_MAX = 1600 * 1024;

    if(ins.length > SAMPLE_MAX ||
     ins.loop_start > SAMPLE_MAX ||
     ins.loop_length > SAMPLE_MAX)
      return modutil::FORMAT_ERROR;

    /* Volume should range between 0x00 (max) and 0xff (min).
     */
    if(ins.volume > 0xff)
      return modutil::FORMAT_ERROR;

    /* Name should contain a 0x0d byte (document refers to this as "LF").
     */
    if(!test_lf(ins.name))
      return modutil::FORMAT_ERROR;

    /* If this is a "trackfile" (module), the sample size should exist within
     * the constraints of the file size.
     */
    if(h.info & 0x80)
    {
      if(ins.sample_offset < 32u * (h.num_instruments + 1) ||
       ins.sample_offset > file_length ||
       ins.length > file_length ||
       ins.sample_offset > file_length - ins.length)
        return modutil::FORMAT_ERROR;
    }

    return modutil::SUCCESS;
  }

public:
  Coconizer_loader(): modutil::loader("-", "coco", "Coconizer") {}

  virtual modutil::error load(modutil::data state) const override
  {
    vio &vf = state.reader;
    int64_t file_length = vf.length();

    Coconizer_data m{};
    Coconizer_header &h = m.header;
    uint8_t buffer[32];
    uint8_t pattern[4 * NUM_ROWS * 8];
    modutil::error err;

    /* This format has no magic and must have its header and instruments
     * tested. These checks need to know the file size, so if it failed to
     * be detected, exit. */
    if(file_length <= 0)
      return modutil::FORMAT_ERROR;

    if(vf.read_buffer(buffer) < 32)
      return modutil::FORMAT_ERROR;

    h.info = buffer[0];
    memcpy(h.name, buffer + 1, sizeof(h.name));
    h.num_instruments = buffer[21];
    h.num_orders      = buffer[22];
    h.num_patterns    = buffer[23];
    h.orders_offset   = mem_u32le(buffer + 24);
    h.patterns_offset = mem_u32le(buffer + 28);

    err = test_header(h, file_length);
    if(err)
      return err;

    for(size_t i = 0; i < h.num_instruments; i++)
    {
      Coconizer_instrument &ins = m.instruments[i];

      if(vf.read_buffer(buffer) < 32)
        return modutil::FORMAT_ERROR;

      ins.sample_offset = mem_u32le(buffer +  0);
      ins.length        = mem_u32le(buffer +  4);
      ins.volume        = mem_u32le(buffer +  8);
      ins.loop_start    = mem_u32le(buffer + 12);
      ins.loop_length   = mem_u32le(buffer + 16);
      memcpy(ins.name, buffer + 20, sizeof(ins.name));

      err = test_instrument(h, ins, file_length);
      if(err)
        return err;
    }

    num_coconizer++;

    memcpy(m.name, h.name, sizeof(h.name));
    m.name[sizeof(h.name)] = '\0';
    strip_module_name(m.name, sizeof(m.name));

    m.num_channels = h.info & 0x0f;

    /* Orders. */
    if(vf.seek(h.orders_offset, SEEK_SET) < 0)
      return modutil::SEEK_ERROR;

    if(vf.read(m.orders, h.num_orders) < h.num_orders)
      return modutil::READ_ERROR;

    /* Patterns. */
    if(vf.seek(h.patterns_offset, SEEK_SET) < 0)
      return modutil::SEEK_ERROR;

    size_t pattern_size = 4 * NUM_ROWS * m.num_channels;
    for(size_t i = 0; i < h.num_patterns; i++)
    {
      Coconizer_pattern &p = m.patterns[i];
      p.allocate(m.num_channels, NUM_ROWS);

      if(vf.eof())
        continue;

      size_t num_in = vf.read(pattern, pattern_size);
      if(num_in < pattern_size)
      {
        /* Recover broken patterns by zeroing missing portion. */
        format::warning("read error in pattern %zu", i);
        memset(pattern + num_in, 0, pattern_size - num_in);
      }

      Coconizer_event *current = p.events;
      uint8_t *pos = pattern;

      for(size_t row = 0; row < NUM_ROWS; row++)
      {
        for(size_t track = 0; track < m.num_channels; track++)
        {
          *(current++) = Coconizer_event(mem_u32le(pos));
          pos += 4;
        }
      }
    }


    /* Print information. */
    format::line("Name",     "%s", m.name);
    format::line("Type",     "Coconizer (%02xh)", h.info);
    format::line("Instr.",   "%u", h.num_instruments);
    format::line("Channels", "%u", m.num_channels);
    format::line("Patterns", "%u", h.num_patterns);
    format::line("Orders",   "%u", h.num_orders);

    if(Config.dump_samples)
    {
      namespace table = format::table;

      static constexpr const char *labels[] =
      {
        "Name", "Length", "LoopStart", "LoopLen", "Vol"
      };

      table::table<
        table::string<10>,
        table::spacer,
        table::number<10>,
        table::number<10>,
        table::number<10>,
        table::spacer,
        table::number<4>> s_table;

      s_table.header("Instr.", labels);

      for(size_t i = 0; i < h.num_instruments; i++)
      {
        Coconizer_instrument &ins = m.instruments[i];
        s_table.row(i + 1, ins.name, {},
          ins.length, ins.loop_start, ins.loop_length, {},
          ins.volume);
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
        Coconizer_pattern &p = m.patterns[i];

        using EVENT = format::event<format::note<>, format::sample<>,
                                    format::effectWide>;
        format::pattern<EVENT> pattern(i, m.num_channels, NUM_ROWS);

        if(!Config.dump_pattern_rows)
        {
          pattern.summary();
          continue;
        }

        Coconizer_event *current = p.events;
        for(size_t row = 0; row < NUM_ROWS; row++)
        {
          for(size_t track = 0; track < m.num_channels; track++, current++)
          {
            format::note<>     a{ current->note };
            format::sample<>   b{ current->instrument };
            format::effectWide c{ current->effect, current->param };

            pattern.insert(EVENT(a, b, c));
          }
        }
        pattern.print();
      }
    }

    return modutil::SUCCESS;
  }

  virtual void report() const override
  {
    if(!num_coconizer)
      return;

    format::report("Total Coconizer", num_coconizer);
  }
};

static const Coconizer_loader loader;
