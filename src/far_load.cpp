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

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "modutil.hpp"

static int total_far = 0;


enum FAR_feature
{
  FT_NONE,
  FT_E_RAMP_DELAY_ON,
  FT_E_RAMP_DELAY_OFF,
  FT_E_FULFILL_LOOP,
  FT_E_OLD_FAR_TEMPO,
  FT_E_NEW_FAR_TEMPO,
  FT_E_PORTA_UP,
  FT_E_PORTA_DN,
  FT_E_TONEPORTA,
  FT_E_RETRIGGER,
  FT_E_SET_VIBRATO_DEPTH,
  FT_E_VIBRATO_NOTE,
  FT_E_VOLSLIDE_UP,
  FT_E_VOLSLIDE_DN,
  FT_E_VIBRATO_SUSTAIN,
  FT_E_SLIDE_TO_VOLUME,
  FT_E_BALANCE,
  FT_E_NOTE_OFFSET,
  FT_E_FINE_TEMPO_DN,
  FT_E_FINE_TEMPO_UP,
  FT_E_TEMPO,
  NUM_FEATURES
};

static constexpr const char *FEATURE_STR[NUM_FEATURES] =
{
  "",
  "E:RampDelayOn",
  "E:RampDelayOff",
  "E:FulfillLoop",
  "E:OldTempo",
  "E:NewTempo",
  "E:PortaUp",
  "E:PortaDn",
  "E:TPorta",
  "E:Retrig",
  "E:VibDepth",
  "E:VibNote",
  "E:VSlideUp",
  "E:VSlideDn",
  "E:VibSustain",
  "E:Slide2Vol",
  "E:Balance",
  "E:NoteOffset",
  "E:FTempoDn",
  "E:FTempoUp",
  "E:Tempo",
};

static const char MAGIC[] = "FAR\xFE";
static const char MAGIC_EOF[] = "\x0d\x0a\x1a";

static constexpr const int MAX_ORDERS = 256;
static constexpr const int MAX_PATTERNS = 256;
static constexpr const int MAX_INSTRUMENTS = 64;

enum FAR_editor_memory
{
  CURRENT_OCTAVE,
  CURRENT_VOICE,
  CURRENT_ROW,
  CURRENT_PATTERN,
  CURRENT_ORDER,
  CURRENT_SAMPLE,
  CURRENT_VOLUME,
  CURRENT_DISPLAY,
  CURRENT_EDITING,
  CURRENT_TEMPO,
  MAX_EDITOR_MEMORY
};
enum FAR_editor_memory_2
{
  MARK_TOP,
  MARK_BOTTOM,
  GRID_SIZE,
  EDIT_MODE,
  MAX_EDITOR_MEMORY_2
};

enum FAR_effects
{
  E_GLOBAL_FUNCTION   = 0x00,
  E_RAMP_DELAY_ON     = 0x01,
  E_RAMP_DELAY_OFF    = 0x02,
  E_FULFILL_LOOP      = 0x03,
  E_OLD_FAR_TEMPO     = 0x04,
  E_NEW_FAR_TEMPO     = 0x05,
  E_PORTA_UP          = 0x10,
  E_PORTA_DN          = 0x20,
  E_TONEPORTA         = 0x30,
  E_RETRIGGER         = 0x40,
  E_SET_VIBRATO_DEPTH = 0x50,
  E_VIBRATO_NOTE      = 0x60,
  E_VOLSLIDE_UP       = 0x70,
  E_VOLSLIDE_DN       = 0x80,
  E_VIBRATO_SUSTAIN   = 0x90,
  E_SLIDE_TO_VOLUME   = 0xa0,
  E_BALANCE           = 0xb0,
  E_NOTE_OFFSET       = 0xc0,
  E_FINE_TEMPO_DN     = 0xd0,
  E_FINE_TEMPO_UP     = 0xe0,
  E_TEMPO             = 0xf0,
};

#define HAS_INSTRUMENT(x) (!!(h.sample_mask[x >> 3] & (1 << (x & 7))))

struct FAR_header
{
  char magic[4];
  char name[40];
  char eof[3];
  uint16_t header_length;
  uint8_t version;
  uint8_t track_enabled[16];
  uint8_t editor_memory[MAX_EDITOR_MEMORY];
  uint8_t track_panning[16];
  uint8_t editor_memory_2[MAX_EDITOR_MEMORY_2];
  uint16_t text_length;

  uint8_t orders[MAX_ORDERS];
  uint8_t num_patterns;
  uint8_t num_orders;
  uint8_t loop_to_position;
  uint16_t pattern_length[MAX_PATTERNS];

  uint8_t sample_mask[8];
};

struct FAR_event
{
  uint8_t note;
  uint8_t instrument;
  uint8_t volume;
  uint8_t effect; /* hi: effect, lo: param */

  FAR_event() {}
  FAR_event(uint8_t a, uint8_t b, uint8_t c, uint8_t d)
  {
    note = a;
    instrument = b;
    volume = c;
    effect = d;
  }
};

struct FAR_pattern
{
  FAR_event *events = nullptr;
  uint16_t columns;
  uint16_t rows;
  uint8_t break_location;
  uint8_t tempo;

  FAR_pattern(uint16_t c=0, uint16_t r=0): columns(c), rows(r)
  {
    if(c && r)
      events = new FAR_event[c * r];
  }
  ~FAR_pattern()
  {
    delete[] events;
  }
  FAR_pattern &operator=(FAR_pattern &&p)
  {
    columns = p.columns;
    rows = p.rows;
    events = p.events;
    p.events = nullptr;
    p.columns = 0;
    p.rows = 0;
    return *this;
  }
};

enum FAR_instrument_flags
{
  // Type flags.
  S_16BIT = (1<<0),
  // Loop flags.
  S_LOOP  = (1<<3),
};

struct FAR_instrument
{
  char name[32];
  uint32_t length;
  uint8_t finetune; // "not supported"
  uint8_t volume; // "yet another unsupported feature"
  uint32_t loop_start;
  uint32_t loop_end;
  uint8_t type_flags;
  uint8_t loop_flags;
};

struct FAR_data
{
  FAR_header     header;
  FAR_pattern    patterns[MAX_PATTERNS];
  FAR_instrument instruments[MAX_INSTRUMENTS];
  char *text = nullptr;

  char name[41];
  size_t num_instruments;
  bool uses[NUM_FEATURES];

  ~FAR_data()
  {
    delete[] text;
  }
};

static FAR_feature get_effect_feature(uint8_t effect)
{
  switch(effect & 0xf0)
  {
    case E_GLOBAL_FUNCTION:
      switch(effect)
      {
        case E_RAMP_DELAY_ON:  return FT_E_RAMP_DELAY_ON;
        case E_RAMP_DELAY_OFF: return FT_E_RAMP_DELAY_OFF;
        case E_FULFILL_LOOP:   return FT_E_FULFILL_LOOP;
        case E_OLD_FAR_TEMPO:  return FT_E_OLD_FAR_TEMPO;
        case E_NEW_FAR_TEMPO:  return FT_E_NEW_FAR_TEMPO;
      }
      break;
    case E_PORTA_UP:           return FT_E_PORTA_UP;
    case E_PORTA_DN:           return FT_E_PORTA_DN;
    case E_TONEPORTA:          return FT_E_TONEPORTA;
    case E_RETRIGGER:          return FT_E_RETRIGGER;
    case E_SET_VIBRATO_DEPTH:  return FT_E_SET_VIBRATO_DEPTH;
    case E_VIBRATO_NOTE:       return FT_E_VIBRATO_NOTE;
    case E_VOLSLIDE_UP:        return FT_E_VOLSLIDE_UP;
    case E_VOLSLIDE_DN:        return FT_E_VOLSLIDE_DN;
    case E_VIBRATO_SUSTAIN:    return FT_E_VIBRATO_SUSTAIN;
    case E_SLIDE_TO_VOLUME:    return FT_E_SLIDE_TO_VOLUME;
    case E_BALANCE:            return FT_E_BALANCE;
    case E_NOTE_OFFSET:        return FT_E_NOTE_OFFSET;
    case E_FINE_TEMPO_DN:      return FT_E_FINE_TEMPO_DN;
    case E_FINE_TEMPO_UP:      return FT_E_FINE_TEMPO_UP;
    case E_TEMPO:              return FT_E_TEMPO;
  }
  return FT_NONE;
}

static void check_event_features(FAR_data &m, FAR_event &ev)
{
  FAR_feature ft = get_effect_feature(ev.effect);
  if(ft)
    m.uses[ft] = true;
}


class FAR_loader : modutil::loader
{
public:
  FAR_loader(): modutil::loader("FAR", "far", "Farandole Composer") {}

  virtual modutil::error load(FILE *fp, long file_length) const override
  {
    FAR_data m{};
    FAR_header &h = m.header;
    size_t len;
    int num_patterns;
    int i;

    if(!fread(h.magic, sizeof(h.magic), 1, fp) ||
       !fread(h.name, sizeof(h.name), 1, fp) ||
       !fread(h.eof, sizeof(h.eof), 1, fp))
      return modutil::FORMAT_ERROR;

    if(memcmp(h.magic, MAGIC, sizeof(h.magic)))
      return modutil::FORMAT_ERROR;
    if(memcmp(h.eof, MAGIC_EOF, sizeof(h.eof)))
      format::warning("EOF area invalid!");

    total_far++;

    memcpy(m.name, h.name, sizeof(h.name));
    m.name[sizeof(h.name)] = '\0';
    strip_module_name(m.name, sizeof(m.name));

    h.header_length = fget_u16le(fp);
    h.version       = fgetc(fp);

    if(!fread(h.track_enabled, sizeof(h.track_enabled), 1, fp) ||
       !fread(h.editor_memory, sizeof(h.editor_memory), 1, fp) ||
       !fread(h.track_panning, sizeof(h.track_panning), 1, fp) ||
       !fread(h.editor_memory_2, sizeof(h.editor_memory_2), 1, fp))
      return modutil::READ_ERROR;

    if(h.version != 0x10)
    {
      format::error("unknown FAR version %02x", h.version);
      return modutil::BAD_VERSION;
    }

    h.text_length = fget_u16le(fp);
    if(feof(fp))
      return modutil::READ_ERROR;

    len = h.text_length;
    if(len)
    {
      m.text = new char[len + 1];
      if(!fread(m.text, len, 1, fp))
        return modutil::READ_ERROR;
      m.text[len] = '\0';
    }

    if(!fread(h.orders, sizeof(h.orders), 1, fp))
      return modutil::READ_ERROR;

    h.num_patterns     = fgetc(fp);
    h.num_orders       = fgetc(fp);
    h.loop_to_position = fgetc(fp);

    for(i = 0; i < MAX_PATTERNS; i++)
      h.pattern_length[i] = fget_u16le(fp);

    if(feof(fp))
      return modutil::READ_ERROR;

    /* The documentation claims h.num_patterns it the pattern count but
     * most commonly it seems to have a value of "1". The actual pattern
     * count needs to be calculated manually.
     */
    num_patterns = h.num_patterns;
    for(i = 0; i < MAX_PATTERNS; i++)
    {
      if(h.pattern_length[i])
      {
        size_t rows = (h.pattern_length[i] - 2) >> 6;
        if(i < num_patterns && rows > 256)
          format::warning("pattern %02x expects %zu rows >256", i, rows);

        m.patterns[i] = FAR_pattern(16, rows);

        if(num_patterns < i + 1)
          num_patterns = i + 1;
      }
    }

    /* Load patterns. */
    for(i = 0; i < num_patterns; i++)
    {
      if(!h.pattern_length[i])
        continue;

      FAR_pattern &p = m.patterns[i];

      // The break location is badly documented--it claims to be "length in rows",
      // but it's actually the last row to play MINUS 1 i.e. it is actually (length - 2) in rows.
      // Pattern tempo is unused like numerous other features.
      p.break_location = fgetc(fp);
      p.tempo = fgetc(fp);

      FAR_event *current = p.events;
      for(size_t row = 0; row < p.rows; row++)
      {
        for(size_t track = 0; track < p.columns; track++, current++)
        {
          uint8_t ev[4];
          if(!fread(ev, 4, 1, fp))
          {
            format::error("read error for pattern %02x", i);
            return modutil::READ_ERROR;
          }
          *current = FAR_event(ev[0], ev[1], ev[2], ev[3]);
          check_event_features(m, *current);
        }
      }
    }

    /* Load instruments. */
    if(!fread(h.sample_mask, sizeof(h.sample_mask), 1, fp))
      return modutil::READ_ERROR;

    for(i = 0; i < MAX_INSTRUMENTS; i++)
    {
      if(!HAS_INSTRUMENT(i))
        continue;

      FAR_instrument &ins = m.instruments[i];
      m.num_instruments++;

      if(!fread(ins.name, sizeof(ins.name), 1, fp))
      {
        format::error("read error at instrument %02x", i);
        return modutil::READ_ERROR;
      }

      ins.length     = fget_u32le(fp);
      ins.finetune   = fgetc(fp);
      ins.volume     = fgetc(fp);
      ins.loop_start = fget_u32le(fp);
      ins.loop_end   = fget_u32le(fp);
      ins.type_flags = fgetc(fp);
      ins.loop_flags = fgetc(fp);

      if(feof(fp))
      {
        format::error("read error at instrument %02x", i);
        return modutil::READ_ERROR;
      }

      // Skip sample.
      if(fseek(fp, ins.length, SEEK_CUR))
        return modutil::SEEK_ERROR;
    }

    /* Print summary. */
    format::line("Name",     "%s", m.name);
    format::line("Type",     "FAR %x", h.version);
    format::line("Instr.",   "%zu", m.num_instruments);
    format::line("Patterns", "%d (claims %d)", num_patterns, h.num_patterns);
    format::line("Orders",   "%d", h.num_orders);
    format::uses(m.uses, FEATURE_STR);

    format::description<132>("Desc.", m.text, h.text_length);

    if(Config.dump_samples)
    {
      namespace table = format::table;

      static const char *labels[] =
      {
        "Name", "Length", "LoopStart", "LoopEnd", "Vol", "Fine", "Type", "Mode",
      };

      format::line();
      table::table<
        table::string<32>,
        table::spacer,
        table::number<10>,
        table::number<10>,
        table::number<10>,
        table::spacer,
        table::number<4>,
        table::number<4>,
        table::number<4>,
        table::number<4>> s_table;

      s_table.header("Instr.", labels);

      for(size_t i = 0; i < MAX_INSTRUMENTS; i++)
      {
        if(!HAS_INSTRUMENT(i))
          continue;

        FAR_instrument &ins = m.instruments[i];
        s_table.row(i + 1, ins.name, {},
          ins.length, ins.loop_start, ins.loop_end, {},
          ins.volume, ins.finetune, ins.type_flags, ins.loop_flags);
      }
    }

    if(Config.dump_patterns)
    {
      format::line();
      format::orders("Orders", h.orders, h.num_orders);

      if(!Config.dump_pattern_rows)
        format::line();

      for(i = 0; i < num_patterns; i++)
      {
        FAR_pattern &p = m.patterns[i];
        int pattern_len = h.pattern_length[i];

        using EVENT = format::event<format::note, format::sample, format::volume, format::effect669>;
        format::pattern<EVENT, 16> pattern(i, p.columns, p.rows, pattern_len);
        // TODO also print break.

        if(!Config.dump_pattern_rows || !pattern_len)
        {
          pattern.summary(!pattern_len);
          continue;
        }

        FAR_event *current = p.events;
        for(size_t row = 0; row < p.rows; row++)
        {
          for(size_t track = 0; track < p.columns; track++, current++)
          {
            format::note      a{ current->note };
            format::sample    b{ current->instrument };
            format::volume    c{ current->volume };
            format::effect669 d{ current->effect };
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
    if(!total_far)
      return;

    format::report("Total FARs", total_far);
  }
};

static const FAR_loader loader;
