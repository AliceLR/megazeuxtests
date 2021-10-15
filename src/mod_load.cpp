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

#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "modutil.hpp"

enum MOD_type
{
  MOD_PROTRACKER,          // M.K.
  MOD_PROTRACKER_EXT,      // M!K!
  MOD_NOISETRACKER_EXT,    // M&K!
  MOD_FASTTRACKER_XCHN,    // 2CHN, 6CHN, 8CHN, etc.
  MOD_FASTTRACKER_XXCH,    // 10CH, 16CH, 32CH, etc.
  MOD_TAKETRACKER_TDZX,    // TDZ1, TDZ2, TDZ3
  MOD_OCTALYSER_CD61,      // CD61
  MOD_OCTALYSER_CD81,      // CD81
  MOD_OKTA,                // OKTA (Oktalyzer?)
  MOD_OCTA,                // OCTA (OctaMED?)
  MOD_STARTREKKER_EXO4,    // EXO4
  MOD_STARTREKKER_FLT4,    // FLT4
  MOD_STARTREKKER_FLT8,    // FLT8
  MOD_DIGITALTRACKER_FA04, // FA04
  MOD_DIGITALTRACKER_FA06, // FA06
  MOD_DIGITALTRACKER_FA08, // FA08
  MOD_HMN,                 // His Master's Noise FEST
  MOD_LARD,                // Signature found in judgement_day_gvine.mod. It's a normal 4-channel MOD.
  MOD_NSMS,                // Signature found in kingdomofpleasure.mod. It's a normal 4-channel MOD.
  WOW,                     // Mod's Grave M.K.
  MOD_SOUNDTRACKER,        // ST 15-instrument .MOD, no signature.
  MOD_SOUNDTRACKER_26,     // Soundtracker 2.6 MTN\0
  MOD_ICETRACKER_IT10,     // Icetracker 1.x IT10
  MOD_UNKNOWN,             // ?
  NUM_MOD_TYPES
};

struct MOD_type_info
{
  const char magic[5];
  const char * const source;
  int channels; // -1=ignore this type ;-(
  bool print_channel_count;
};

static const struct MOD_type_info TYPES[NUM_MOD_TYPES] =
{
  { "M.K.", "ProTracker",      4,  false },
  { "M!K!", "ProTracker",      4,  false },
  { "M&K!", "NoiseTracker",    4,  false },
  { "xCHN", "FastTracker",     0,  false },
  { "xxCH", "FastTracker",     0,  false },
  { "TDZx", "TakeTracker",     0,  false },
  { "CD61", "Octalyser",       6,  false },
  { "CD81", "Octalyser",       8,  false },
  { "OKTA", "Oktalyzer?",      8,  true  },
  { "OCTA", "OctaMED?",        8,  true  },
  { "EXO4", "StarTrekker",     4,  false },
  { "FLT4", "StarTrekker",     4,  false },
  { "FLT8", "StarTrekker",     8,  false },
  { "FA04", "Digital Tracker", 4,  false },
  { "FA06", "Digital Tracker", 6,  false },
  { "FA08", "Digital Tracker", 8,  false },
  { "FEST", "HMN",             4,  true  },
  { "LARD", "Unknown 4ch",     4,  false },
  { "NSMS", "Unknown 4ch",     4,  false },
  { "M.K.", "Mod's Grave",     8,  true  },
  { "",     "Soundtracker",    4,  false },
  { "",     "ST 2.6",          -1, false },
  { "",     "IceTracker",      -1, false },
  { "",     "unknown",         -1, false },
};

static int total_files;
static int total_files_nonzero_diff;
static int total_files_wow_fp_diff;
static int type_count[NUM_MOD_TYPES];

static constexpr uint32_t pattern_size(uint32_t num_channels)
{
  return num_channels * 4 * 64;
}

enum MOD_features
{
  FT_SAMPLE_ADPCM,
  FT_RETRIGGER_NO_NOTE,
  FT_RETRIGGER_ZERO,
  NUM_FEATURES
};

static const char *FEATURE_STR[NUM_FEATURES] =
{
  "S:ADPCM",
  "RetrigNoNote",
  "Retrig0",
};

enum MOD_effects
{
  E_ARPEGGIO,
  E_PORTAMENTO_UP,
  E_PORTAMENTO_DOWN,
  E_TONE_PORTAMENTO,
  E_VIBRATO,
  E_TONE_PORTAMENTO_VOLSLIDE,
  E_VIBRATO_VOLSLIDE,
  E_TREMOLO,
  E_SET_PANNING,
  E_OFFSET,
  E_VOLSLIDE,
  E_POSITION_JUMP,
  E_SET_VOLUME,
  E_PATTERN_BREAK,
  E_EXTENDED,
  E_SPEED,
  EX_SET_FILTER           = 0x00,
  EX_FINE_PORTAMENTO_UP   = 0x10,
  EX_FINE_PORTAMENTO_DOWN = 0x20,
  EX_GLISSANDO_CONTROL    = 0x30,
  EX_SET_VIBRATO_WAVEFORM = 0x40,
  EX_SET_FINETUNE         = 0x50,
  EX_LOOP                 = 0x60,
  EX_SET_TREMOLO_WAVEFORM = 0x70,
  EX_RETRIGGER_NOTE       = 0x90,
  EX_FINE_VOLSLIDE_UP     = 0xA0,
  EX_FINE_VOLSLIDE_DOWN   = 0xB0,
  EX_NOTE_CUT             = 0xC0,
  EX_NOTE_DELAY           = 0xD0,
  EX_PATTERN_DELAY        = 0xE0,
  EX_INVERT_LOOP          = 0xF0
};


struct MOD_sample
{
  /*  0 */ char name[22];             // NOTE: null-padded, but not necessarily null-terminated.
  /* 22 */ uint16_t half_length;      // Half the actual length.
  /* 24 */ uint8_t finetune;
  /* 25 */ uint8_t volume;
  /* 26 */ uint16_t half_loop_start;  // Half the actual repeat start.
  /* 28 */ uint16_t half_loop_length; // Half the actual repeat length.
  /* 30 */

  uint32_t length;
  uint32_t loop_start;
  uint32_t loop_length;
};

struct MOD_header
{
  /*    0 */ char name[20]; // NOTE: space-padded, not null-terminated.
  /*   20 */ MOD_sample samples[31];
  /*  950 */ uint8_t num_orders;
  /*  951 */ uint8_t restart_byte;
  /*  952 */ uint8_t orders[128];
  /* 1080 */ unsigned char magic[4];
  /* 1084 */
};

/* For reference only. Load data to the regular MOD_header. */
struct ST_header
{
  /*   0 */ char name[20];
  /*  20 */ MOD_sample samples[15];
  /* 470 */ uint8_t num_orders;
  /* 471 */ uint8_t song_speed;
  /* 472 */ uint8_t orders[128];
  /* 600 */
};

struct MOD_note
{
  uint16_t note;
  uint8_t sample;
  uint8_t effect;
  uint8_t param;
};

struct MOD_data
{
  char name[21];
  MOD_type type;
  int type_channels;
  int type_instruments;
  int pattern_count;
  ssize_t real_length;
  ssize_t expected_length;
  ssize_t samples_length;

  MOD_header header;
  MOD_note *patterns[256];
  uint8_t *pattern_buffer;

  bool uses[NUM_FEATURES];

  ~MOD_data()
  {
    for(int i = 0; i < arraysize(patterns); i++)
      delete[] patterns[i];
    delete[] pattern_buffer;
  }

  void use(MOD_features f)
  {
    uses[f] = true;
  }
};

static modutil::error MOD_ST_check(MOD_data &m)
{
  MOD_header &h = m.header;
  // Try to filter out ST mods based on sample data bounding.
  for(int i = 0; i < m.type_instruments; i++)
  {
    MOD_sample &ins = h.samples[i];
    uint16_t sample_length = ins.length;

    if(ins.finetune || ins.volume > 64 || sample_length > 32768)
      return modutil::FORMAT_ERROR;
  }
  // Make sure the order count and pattern numbers aren't nonsense.
  if(!h.num_orders || h.num_orders > 128)
    return modutil::FORMAT_ERROR;

  for(int i = 0; i < 128; i++)
    if(h.orders[i] >= 0x80)
      return modutil::FORMAT_ERROR;

  return modutil::SUCCESS;
}

static modutil::error MOD_check_format(MOD_data &m, FILE *fp)
{
  unsigned char magic[4];

  /* Normal MOD magic is located at 1080. */
  if(fseek(fp, 1080, SEEK_SET))
    return modutil::FORMAT_ERROR;

  if(!fread(magic, 4, 1, fp))
    return modutil::FORMAT_ERROR;

  // FIXME global :(
  memcpy(modutil::loaded_mod_magic, magic, 4);

  // Determine initial guess for what the mod type is.
  for(int i = 0; i < MOD_UNKNOWN; i++)
  {
    if(TYPES[i].magic[0] && !memcmp(magic, TYPES[i].magic, 4) && TYPES[i].channels)
    {
      m.type = static_cast<MOD_type>(i);
      m.type_channels = TYPES[i].channels;
      m.type_instruments = 31;
      return modutil::SUCCESS;
    }
  }

  // Check for FastTracker xCHN and xxCH magic formats.
  if(isdigit(magic[0]) && !memcmp(magic + 1, "CHN", 3))
  {
    m.type = MOD_FASTTRACKER_XCHN;
    m.type_channels = magic[0] - '0';
    m.type_instruments = 31;
    return modutil::SUCCESS;
  }
  else

  if(isdigit(magic[0]) && isdigit(magic[1]) && magic[2] == 'C' && magic[3] == 'H')
  {
    m.type = MOD_FASTTRACKER_XXCH;
    m.type_channels = (magic[0] - '0') * 10 + (magic[1] - '0');
    m.type_instruments = 31;
    return modutil::SUCCESS;
  }
  else

  // TakeTracker uses a unique magic for modules with 1-3 channels.
  if(isdigit(magic[3]) && !memcmp(magic, "TDZ", 3))
  {
    m.type = MOD_TAKETRACKER_TDZX;
    m.type_channels = magic[3] - '0';
    m.type_instruments = 31;
    return modutil::SUCCESS;
  }

  // Check for Soundtracker 2.6 and IceTracker modules.
  if(fseek(fp, 1464, SEEK_SET))
    return modutil::SEEK_ERROR;

  if(fread(magic, 4, 1, fp))
  {
    if(!memcmp(magic, "MTN\x00", 4))
    {
      type_count[MOD_SOUNDTRACKER_26]++;
      return modutil::MOD_IGNORE_ST26;
    }
    if(!memcmp(magic, "IT10", 4))
    {
      type_count[MOD_ICETRACKER_IT10]++;
      return modutil::MOD_IGNORE_IT10;
    }
  }

  // Isn't a MOD, or maybe is a Soundtracker 15-instrument MOD.
  // Assume the latter. If it isn't correct it will be detected early during load.
  m.type = MOD_SOUNDTRACKER;
  m.type_channels = 4;
  m.type_instruments = 15;

  //format::error("unknown/invalid magic %2x %2x %2x %2x", h.magic[0], h.magic[1], h.magic[2], h.magic[3]);
  //type_count[MOD_UNKNOWN]++;
  return modutil::SUCCESS;
}

static modutil::error MOD_read_sample(MOD_data &m, size_t sample_num, FILE *fp)
{
  MOD_sample &ins = m.header.samples[sample_num];
  if(!fread(ins.name, sizeof(ins.name), 1, fp))
    return m.type == MOD_SOUNDTRACKER ? modutil::FORMAT_ERROR : modutil::READ_ERROR;

  ins.half_length      = fget_u16be(fp);
  ins.finetune         = fgetc(fp);
  ins.volume           = fgetc(fp);
  ins.half_loop_start  = fget_u16be(fp);
  ins.half_loop_length = fget_u16be(fp);

  ins.length = ins.half_length << 1;
  ins.loop_start = ins.half_loop_start << 1;
  ins.loop_length = ins.half_loop_length << 1;

  return modutil::SUCCESS;
}

static modutil::error MOD_read_pattern(MOD_data &m, size_t pattern_num, FILE *fp)
{
  if(!m.pattern_buffer)
    m.pattern_buffer = new uint8_t[m.type_channels * 64 * 4];

  if(!fread(m.pattern_buffer, m.type_channels * 64 * 4, 1, fp))
    return modutil::READ_ERROR;

  m.patterns[pattern_num] = new MOD_note[m.type_channels * 64]{};

  MOD_note *note = m.patterns[pattern_num];
  uint8_t *current = m.pattern_buffer;
  for(int row = 0; row < 64; row++)
  {
    for(int ch = 0; ch < m.type_channels; ch++)
    {
      note->note   = ((current[0] & 0x0F) << 8) | current[1];
      note->sample = (current[0] & 0xF0) | ((current[2] & 0xF0) >> 4);
      note->effect = (current[2] & 0x0F);
      note->param  = current[3];

      if(note->effect == E_EXTENDED &&
       (note->param & 0xF0) == EX_RETRIGGER_NOTE)
      {
        if(!note->note && (note->param & 0x0F))
          m.use(FT_RETRIGGER_NO_NOTE);
        if(!(note->param & 0xF))
          m.use(FT_RETRIGGER_ZERO);
      }

      current += 4;
      note++;
    }
  }
  return modutil::SUCCESS;
}

static modutil::error MOD_read(FILE *fp, long file_length)
{
  MOD_data m{};
  MOD_header &h = m.header;
  bool maybe_wow = true;
  ssize_t samples_length = 0;
  ssize_t running_length;
  int i;

  modutil::error ret = MOD_check_format(m, fp);
  if(ret != modutil::SUCCESS)
    return ret;

  m.real_length = file_length;
  errno = 0;
  rewind(fp);
  if(errno)
    return modutil::SEEK_ERROR;

  if(!fread(h.name, sizeof(h.name), 1, fp))
    return modutil::READ_ERROR;

  for(i = 0; i < m.type_instruments; i++)
  {
    modutil::error ret = MOD_read_sample(m, i, fp);
    if(ret != modutil::SUCCESS)
      return ret;
  }
  h.num_orders = fgetc(fp);
  h.restart_byte = fgetc(fp);

  if(!fread(h.orders, sizeof(h.orders), 1, fp))
    return modutil::READ_ERROR;

  // If this was "detected" as Soundtracker, make sure it actually is one...
  if(m.type == MOD_SOUNDTRACKER)
  {
    ret = MOD_ST_check(m);
    if(ret != modutil::SUCCESS)
      return ret;

    maybe_wow = false;
    running_length = 600;
  }
  else
  {
    if(!fread(h.magic, 4, 1, fp))
      return modutil::READ_ERROR;

    running_length = 1084;

    if(m.type == MOD_DIGITALTRACKER_FA04 ||
       m.type == MOD_DIGITALTRACKER_FA06 ||
       m.type == MOD_DIGITALTRACKER_FA08)
    {
      /**
       * Digital Tracker MODs have extra unused bytes after the magic.
       * The intent of these seems to have been as follows, but MOD verisons
       * of Digital Tracker don't allow changing these fields, and post-MOD
       * versions of Digital Tracker have been confirmed to ignore them:
       *
       * rows_per_pattern = fget_u16be(fp);
       * sample_bits      = fgetc(fp); // 0=8-bit, 1=16-bit
       * sample_rate      = fgetc(fp); // 0=8363Hz, 1=12500Hz, 2=25000Hz
       */
      fget_u32be(fp);
      running_length += 4;
    }
  }

  total_files++;

  memcpy(m.name, h.name, arraysize(h.name));
  m.name[arraysize(m.name) - 1] = '\0';

  if(!strip_module_name(m.name, arraysize(m.name)))
    m.name[0] = '\0';

  if(m.type_channels <= 0 || m.type_channels > 32)
  {
    format::error("unsupported .MOD variant: %s %4.4s.", TYPES[m.type].source, h.magic);
    type_count[m.type]++;
    return modutil::MOD_IGNORE_MAGIC;
  }

  if(!h.num_orders || h.num_orders > 128)
  {
    format::error("valid magic %4.4s but invalid order count %u", h.magic, h.num_orders);
    type_count[MOD_UNKNOWN]++;
    return modutil::MOD_INVALID_ORDER_COUNT;
  }

  // Get sample info.
  for(i = 0; i < m.type_instruments; i++)
  {
    MOD_sample &ins = h.samples[i];

    samples_length += ins.length;
    running_length += ins.length;

    /**
     * .669s don't have sample volume or finetune, so every .WOW has
     * 0x00 and 0x40 for these bytes when the sample exists.
     */
    if(ins.length && (ins.finetune != 0x00 || ins.volume != 0x40))
      maybe_wow = false;
  }

  /**
   * Determine pattern count.
   * This can be dependent on orders outside of the order count
   * (observed with converting 'final vision.669' to .WOW). This
   * is consistent with how libmodplug and libxmp determine the
   * pattern count as well (including the 0x80 check).
   */
  uint8_t max_pattern = 0;
  for(i = 0; i < 128; i++)
  {
    if(h.orders[i] < 0x80 && h.orders[i] > max_pattern)
      max_pattern = h.orders[i];
  }
  m.pattern_count = max_pattern + 1;

  // Calculate expected length.
  m.expected_length = running_length + m.pattern_count * pattern_size(m.type_channels);
  m.samples_length = samples_length;

  /**
   * Calculate expected length of a Mod's Grave .WOW to see if a M.K. file
   * is actually a stealth .WOW. .WOW files always have a restart byte of 0x00.
   * (the .669 restart byte is handled by inserting a pattern break).
   *
   * Also, require exactly the length that the .WOW would be because
   * 1) when 6692WOW.EXE doesn't make a corrupted .WOW it's always exactly that long;
   * 2) apparently some .MOD authors like to append junk to their .MODs that are
   * otherwise regular 4 channel MODs (nightshare_-_heaven_hell.mod).
   *
   * Finally, 6692WOW rarely likes to append an extra byte for some reason, so
   * round the length down.
   */
  if(m.type == MOD_PROTRACKER && h.restart_byte == 0x00 && maybe_wow)
  {
    ssize_t wow_length = running_length + m.pattern_count * pattern_size(8);
    if((m.real_length & ~1) == wow_length)
    {
      m.type = WOW;
      m.type_channels = TYPES[WOW].channels;
      m.expected_length = wow_length;
    }
  }

  /* Load patterns. */
  for(i = 0; i < m.pattern_count; i++)
    MOD_read_pattern(m, i, fp);

  /* As if everything else wasn't enough, samples with data starting with "ADPCM" are
   * Modplug ADPCM4 compressed, and the expected length needs to be adjusted accordingly. */
  bool has_adpcm = false;
  for(i = 0; i < m.type_instruments; i++)
  {
    MOD_sample &ins = h.samples[i];
    char tmp[5];

    if(ins.length)
    {
      long offset = ins.length - 5;
      if(!fread(tmp, 5, 1, fp))
        break;

      if(!memcmp(tmp, "ADPCM", 5))
      {
        ssize_t stored_length = ((ins.length + 1) >> 1) /* compressed size */ + 16 /* ADPCM table */;
        m.expected_length += (stored_length - ins.length + 5);

        has_adpcm = true;
        m.uses[FT_SAMPLE_ADPCM] = true;

        fseek(fp, stored_length, SEEK_CUR);
      }
      else
        fseek(fp, offset, SEEK_CUR);
    }
  }

  /**
   * Check for .MODs with lengths that would be a potential false positive for
   * .WOW detection.
   */
  ssize_t difference = m.real_length - m.expected_length;
  ssize_t threshold = m.pattern_count * pattern_size(4);
  bool wow_fp_diff = (m.type != WOW) && !has_adpcm && (difference > 0) && ((difference & ~1) == threshold);

  if(wow_fp_diff)
    total_files_wow_fp_diff++;
  if(difference)
    total_files_nonzero_diff++;


  /**
   * Print summary.
   */

  if(strlen(m.name))
    format::line("Name",   "%s", m.name);
  if(TYPES[m.type].print_channel_count)
    format::line("Type",   "%s %4.4s %d ch.", TYPES[m.type].source, h.magic, m.type_channels);
  else
    format::line("Type",   "%s %4.4s", TYPES[m.type].source, h.magic);
  format::line("Patterns", "%u", m.pattern_count);
  format::line("Orders",   "%u (0x%02x)", h.num_orders, h.restart_byte);
  format::line("Filesize", "%zd", m.real_length);
  if(difference)
  {
    format::line("Expected", "%zd", m.expected_length);
    format::line("SampleSz", "%zd", m.samples_length);
    format::line("Diff.",    "%zd%s", difference, wow_fp_diff ? " (WOW fp!)" : "");
  }
  format::uses(m.uses, FEATURE_STR);
  type_count[m.type]++;

  if(Config.dump_samples)
  {
    namespace table = format::table;

    static const char *labels[] =
    {
      "Name", "Length", "LoopSt", "LoopLn", "Vol", "Fine"
    };

    format::line();
    table::table<
      table::string<22>,
      table::spacer,
      table::number<6>,
      table::number<6>,
      table::number<6>,
      table::spacer,
      table::number<4>,
      table::number<4>> s_table;

    s_table.header("Samples", labels);

    for(i = 0; i < m.type_instruments; i++)
    {
      MOD_sample &ins = h.samples[i];
      s_table.row(i + 1, ins.name, {}, ins.length, ins.loop_start, ins.loop_length, {}, ins.volume, ins.finetune);
    }
  }

  if(Config.dump_patterns)
  {
    format::line();
    format::orders("Orders", h.orders, h.num_orders);

    if(!Config.dump_pattern_rows)
      format::line();

    for(i = 0; i < m.pattern_count; i++)
    {
      using EVENT = format::event<format::periodMOD, format::sample, format::effect>;
      format::pattern<EVENT> pattern(i, m.type_channels, 64);

      if(!Config.dump_pattern_rows)
      {
        pattern.summary();
        continue;
      }

      MOD_note *current = m.patterns[i];
      for(int row = 0; row < 64; row++)
      {
        for(int track = 0; track < m.type_channels; track++, current++)
        {
          format::periodMOD a{ current->note };
          format::sample    b{ current->sample };
          format::effect    c{ current->effect, current->param };

          pattern.insert(EVENT(a, b, c));
        }
      }
      pattern.print();
    }
  }

  return modutil::SUCCESS;
}

/**
 * MOD loader class.
 */
class MOD_loader : public modutil::loader
{
public:
  MOD_loader() : modutil::loader("MOD", "Protracker and Soundtracker compatible modules") {}

  virtual modutil::error load(FILE *fp, long file_length) const override
  {
    return MOD_read(fp, file_length);
  };

  virtual void report() const override
  {
    if(!total_files)
      return;

    format::report("Total MODs", total_files);
    if(total_files_nonzero_diff)
      format::reportline("Nonzero difference", "%d", total_files_nonzero_diff);
    if(total_files_wow_fp_diff)
      format::reportline("WOW false positive?", "%d", total_files_wow_fp_diff);
    if(total_files_nonzero_diff || total_files_wow_fp_diff)
      format::reportline();

    for(int i = 0; i < NUM_MOD_TYPES; i++)
    {
      char label[23];
      if(type_count[i])
      {
        snprintf(label, sizeof(label), "%-16s %4.4s", TYPES[i].source, TYPES[i].magic);
        format::reportline(label, "%d", type_count[i]);
      }
    }
  }
};

static const MOD_loader loader;
