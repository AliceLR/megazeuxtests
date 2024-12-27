/**
 * Copyright (C) 2024 Lachesis <petrifiedrowan@gmail.com>
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

static int total_liq = 0;

enum LIQ_features
{
  MODE_LIQ,
  MODE_S3M,
  MODE_CUT_ON_LIMIT,
  SAMPLE_SIGNED,
  SAMPLE_UNSIGNED,
  SAMPLE_16BIT,
  SAMPLE_STEREO,
  NOTE_OCTAVE_8_9,
  NUM_FEATURES
};

static const char *FEATURE_STR[NUM_FEATURES] =
{
  "M:LIQ",
  "M:S3M",
  "M:CutOnLimit",
  "S:+",
  "S:U",
  "S:16",
  "S:Stereo",
  "N:Oct8-9",
};

static const char LIQ_MAGIC[]             = "Liquid Module:";
static const char LIQ_ECHO_MAGIC[]        = "POOL";
static const char LIQ_PATTERN_MAGIC[]     = "LP\0\0";
static const char LIQ_NO_PATTERN_MAGIC[]  = "!!!!";
static const char LIQ_LDSS_MAGIC[]        = "LDSS";
static const char LIQ_NO_LDSS_MAGIC[]     = "????";

static const unsigned int MAX_CHANNELS = 256; // todo: unknown
static const unsigned int MAX_INSTRUMENTS = 255;
static const unsigned int MAX_PATTERNS = 256;

enum LIQ_soundboards
{
  LIQ_SOUNDBOARD_SB_150           = 0,
  LIQ_SOUNDBOARD_SB_200           = 1,
  LIQ_SOUNDBOARD_SB_PRO           = 2,
  LIQ_SOUNDBOARD_SB_16            = 3,
  LIQ_SOUNDBOARD_THUNDERBRD       = 4,
  LIQ_SOUNDBOARD_PAS              = 5,
  LIQ_SOUNDBOARD_PAS_PLUS         = 6,
  LIQ_SOUNDBOARD_PAS_16           = 7,
  LIQ_SOUNDBOARD_WINDOWS_WAVE     = 8,
  LIQ_SOUNDBOARD_OS2              = 9,
  LIQ_SOUNDBOARD_DAC              = 10,
  LIQ_SOUNDBOARD_GUS              = 11,
  LIQ_SOUNDBOARD_ARIA             = 12,
  LIQ_SOUNDBOARD_ADLIB_GOLD       = 13,
  LIQ_SOUNDBOARD_WINDOWS_SND      = 14,
  LIQ_SOUNDBOARD_SOUND_GALAXY     = 15,
  LIQ_SOUNDBOARD_SB_AWE32         = 16,
  LIQ_SOUNDBOARD_SOUND_GALAXY_16  = 17,
  LIQ_SOUNDBOARD_AUDIO_WAVE_GRN   = 18,
  LIQ_SOUNDBOARD_GUS_MAX          = 19,
  LIQ_SOUNDBOARD_SOUND_GALAXY_PRO = 20,
  LIQ_SOUNDBOARD_TURTLE_BEACH     = 21,
  LIQ_SOUNDBOARD_AWE64            = 22,
  LIQ_SOUNDBOARD_AWE64_GOLD       = 23,
  LIQ_SOUNDBOARD_INTERWAVE        = 24,
  NUM_LIQ_SOUNDBOARDS,
  LIQ_SOUNDBOARD_UNKNOWN          = 255
};

static const int MAX_SOUNDBOARD_STRING = 24;
static const char LIQ_soundboard_strings[NUM_LIQ_SOUNDBOARDS][MAX_SOUNDBOARD_STRING + 1] =
{
  "Sound Blaster 1.50",
  "Sound Blaster 2.00",
  "Sound Blaster Pro",
  "Sound Blaster 16",
  "ThunderBrd",
  "Pro AudioSpectrum",
  "Pro AudioSpectrum Plus",
  "Pro AudioSpectrum 16",
  "Windows Wave",
  "OS/2 driver",
  "DAC",
  "Gravis UltraSound",
  "Aria",
  "AdLib Gold",
  "Windows Sound System",
  "Aztech Sound Galaxy",
  "Sound Blaster AWE32",
  "Aztech Sound Galaxy 16",
  "Audio Wave Grn",
  "Gravis UltraSound MAX",
  "Aztech Sound Galaxy Pro",
  "Turtle Beach",
  "Sound Blaster AWE64",
  "Sound Blaster AWE64 Gold",
  "Interwave",
};

static const char *LIQ_soundboard_string(uint8_t sound_board)
{
  if(sound_board < NUM_LIQ_SOUNDBOARDS)
    return LIQ_soundboard_strings[sound_board];

  if(sound_board != LIQ_SOUNDBOARD_UNKNOWN)
    return "<unknown> (bad ID)";

  return "<unknown>";
}

enum LIQ_header_flags
{
  LIQ_CUT_ON_LIMIT      = (1 << 0),
  LIQ_ST3_COMPATIBILITY = (1 << 1),
};

enum LIQ_echo_flags
{
  LIQ_ECHO_STEREO_FLIP = (1 << 0),
};


struct LIQ_echo_channel_setup
{
  /*  0 */ uint16_t mix_setup;
  /*  2 */ uint16_t echo_amount;
  /*  4 */
};

struct LIQ_echo_pool
{
  /*  0 */ uint32_t delay_ms;
  /*  4 */ uint32_t decay_left; /* 0-64 */
  /*  8 */ uint32_t decay_right; /* 0-64 */
  /* 12 */ uint32_t flags; /* LIQ_echo_flags */
  /* 16 */ uint32_t reserved;
  /* 20 */
};

struct LIQ_header
{
  /*  0 */ uint8_t  magic[14]; /* Liquid Module: */
  /* 14 */ char     name[30 + 1];
  /* 44 */ char     author[20 + 1];
  /* 64 */ uint8_t  eof; /* 0x1a */
  /* 65 */ char     tracker_name[20 + 1];
  /* 85 */ uint16_t format_version;
  /* 87 */ uint16_t initial_speed;
  /* 89 */ uint16_t initial_bpm;
  /* 91 */ uint16_t lowest_note; /* "Amiga Period*4" */
  /* 93 */ uint16_t highest_note;
  /* 95 */ uint16_t num_channels;
  /* 97 */ uint32_t flags; /* LIQ_header_flags */
  /*101 */ uint16_t num_patterns;
  /*103 */ uint16_t num_instruments;
  /*105 */ uint16_t num_orders;
  /*107 */ uint16_t header_bytes;
  /*109 */ std::vector<uint8_t> initial_pan; /* x num_channels */
  /*    */ std::vector<uint8_t> initial_volume; /* x num_channels */
  /*    */ std::vector<uint8_t> orders; /* x num_orders */

  /* Format version 1.01+ */
  /*  x */ uint8_t  echo_magic[4]; /* POOL */
  /*    */ std::vector<LIQ_echo_channel_setup> channel_setup;
  /*    */ uint32_t num_pools;
  /*    */ std::vector<LIQ_echo_pool> pools;

  /* Format version 1.02+ */
  /*    */ uint16_t amplification; /* "0-1000d" */
};

/* Note- the -1=none fields get shifted up by 1 since the formatter expects
 * 0 for none. No LIQ I've found contains the -1 code for effects, as the
 * pattern packer never seems to emit it. */
struct LIQ_event
{
  uint8_t note;       /* 0-107 C-1 thru B-9 (?), -1=none, -2=note off */
  uint8_t instrument; /* -1=none */
  uint8_t volume;     /* -1=none */
  uint8_t effect;     /* 65-90=A-Z, -1=none */
  uint8_t param;

  unsigned octave() const
  {
    return note && note != (0xfe + 1) ? (note - 1) / 12 : 0;
  }

  uint8_t fix_effect(uint8_t fx)
  {
    return fx != 0xff ? fx - '@' : 0;
  }

  unsigned load(const std::vector<uint8_t> data, size_t pos)
  {
    if(data.size() - pos < 5)
      return 0;

    note          = data[pos + 0] + 1;
    instrument    = data[pos + 1] + 1;
    volume        = data[pos + 2] + 1;
    effect        = fix_effect(data[pos + 3]);
    param         = data[pos + 4];
    return 5;
  }

  unsigned unpack(const std::vector<uint8_t> data, size_t pos, uint8_t mask)
  {
    static const uint8_t counts[32] =
    {
      0, 1, 1, 2, 1, 2, 2, 3,
      1, 2, 2, 3, 2, 3, 3, 4,
      1, 2, 2, 3, 2, 3, 3, 4,
      2, 3, 3, 4, 3, 4, 4, 5
    };
    unsigned num = counts[mask & 31];
    if(data.size() - pos < num)
      return 0;

    if(mask & 1)
      note        = data[pos++] + 1;
    if(mask & 2)
      instrument  = data[pos++] + 1;
    if(mask & 4)
      volume      = data[pos++] + 1;
    if(mask & 8)
      effect      = fix_effect(data[pos++]);
    if(mask & 16)
      param       = data[pos++];
    return num;
  }
};

struct LIQ_pattern
{
  /*  0 */ uint8_t  magic[4]; /* LP\0\0 */
  /*  4 */ uint8_t  name[30 + 1];
  /* 34 */ uint16_t num_rows;
  /* 36 */ uint32_t packed_bytes;
  /* 40 */ uint32_t reserved;
  /* 44 */

  unsigned num_channels;
  /* Note: events are stored in tracks rather than in rows. */
  std::vector<LIQ_event> events;

  modutil::error load(size_t num, size_t chn, std::vector<uint8_t> &data, FILE *fp,
   long file_length)
  {
    uint8_t buf[44];
    if(fread(magic, 1, 4, fp) < 4)
      return modutil::READ_ERROR;

    /* No pattern */
    if(!memcmp(magic, LIQ_NO_PATTERN_MAGIC, 4))
      return modutil::SUCCESS;

    if(fread(buf + 4, 1, 40, fp) < 40)
      return modutil::READ_ERROR;

    if(memcmp(magic, LIQ_PATTERN_MAGIC, 4))
    {
      format::warning("bad pattern %zu magic: %02x %02x %02x %02x",
        num, magic[0], magic[1], magic[2], magic[3]);
    }

    memcpy(buf + 4, name, 30);
    name[30] = '\0';

    num_rows      = mem_u16le(buf + 34);
    packed_bytes  = mem_u32le(buf + 36);
    reserved      = mem_u32le(buf + 40);
    num_channels  = chn;
    size_t num_events = num_rows * num_channels;
    events.resize(num_events);

    if(static_cast<int64_t>(packed_bytes) > file_length)
    {
      format::warning("bad pattern %zu packed length %" PRIu32, num, packed_bytes);
      return modutil::INVALID;
    }
    data.resize(packed_bytes);

    if(fread(data.data(), 1, packed_bytes, fp) < packed_bytes)
      return modutil::READ_ERROR;

    size_t row = 0;
    for(size_t pos = 0; pos < packed_bytes; )
    {
      uint8_t value = data[pos++];

      /* Stop pattern decoding */
      if(value == 0xc0)
        break;
      /* Stop track decoding */
      if(value == 0xa0)
      {
        chn = (row / num_rows) + 1;
        if(chn >= num_channels)
          break;
        row = chn * num_rows;
        continue;
      }
      /* Skip xx empty notes */
      if(value == 0xe0)
      {
        if(packed_bytes - pos < 1)
          return modutil::BAD_PACKING;

        row += data[pos++] + 1;
        continue;
      }
      /* Skip 1 empty note */
      if(value == 0x80)
      {
        row++;
        continue;
      }
      /* Skip xx empty tracks */
      if(value == 0xe1)
      {
        if(packed_bytes - pos < 1)
          return modutil::BAD_PACKING;

        chn = row / num_rows;
        chn += data[pos++] + 1;
        if(chn >= num_channels)
          break;
        row = chn * num_rows;
        continue;
      }

      LIQ_event tmp{};

      /* Packed event */
      if(value > 0xc0 && value < 0xe0)
      {
        unsigned num = tmp.unpack(data, pos, value);
        if(num == 0 || row >= num_events)
          return modutil::BAD_PACKING;

        events[row++] = tmp;
        pos += num;
      }
      else

      /* Multiple packed events */
      if(value > 0xa0 && value < 0xc0)
      {
        if(packed_bytes - pos < 1)
          return modutil::BAD_PACKING;

        unsigned count = data[pos++] + 1;
        while(count > 0)
        {
          unsigned num = tmp.unpack(data, pos, value);
          if(num == 0 || row >= num_events)
            return modutil::BAD_PACKING;

          events[row++] = tmp;
          pos += num;
          count--;
        }
      }
      else

      /* RLE event */
      if(value > 0x80 && value < 0xa0)
      {
        if(packed_bytes - pos < 1)
          return modutil::BAD_PACKING;

        unsigned count = data[pos++] + 1;
        unsigned num = tmp.unpack(data, pos, value);
        if(num == 0 || row + count > num_events)
          return modutil::BAD_PACKING;

        pos += num;
        while(count > 0)
        {
          events[row++] = tmp;
          count--;
        }
      }
      else /* Unpacked event */
      {
        unsigned num = tmp.load(data, pos - 1);
        if(num == 0 || row >= num_events)
          return modutil::BAD_PACKING;

        events[row++] = tmp;
        pos += num - 1;
      }
    }
    return modutil::SUCCESS;
  }
};

enum LIQ_sample_flags
{
  LIQ_16BIT   = (1 << 0),
  LIQ_STEREO  = (1 << 1),
  LIQ_SIGNED  = (1 << 2),
};

struct LIQ_instrument
{
  /*  0 */ uint8_t  magic[4]; /* LDSS */
  /*  4 */ uint16_t format_version;
  /*  6 */ char     name[30 + 1];
  /* 36 */ char     software_name[20 + 1];
  /* 56 */ char     author_name[20 + 1];
  /* 76 */ uint8_t  sound_board_id;
  /* 77 */ uint32_t length; /* bytes */
  /* 81 */ uint32_t loopstart; /* bytes */
  /* 85 */ uint32_t loopend; /* bytes; 0=disable */
  /* 89 */ uint32_t rate;
  /* 93 */ uint8_t  default_volume;
  /* 94 */ uint8_t  flags; /* LIQ_sample_flags */
  /* 95 */ uint8_t  default_pan; /* 0=left 32=mid 64=right 66=surround 255=disable */
  /* 96 */ uint8_t  midi_patch;
  /* 97 */ uint8_t  global_volume; /* TODO: described as "default", wtf? */
  /* 98 */ uint8_t  chord_type;
  /* 99 */ uint16_t length_bytes; /* "usually 90h" */
  /*101 */ uint16_t compression_type; /* 0=none */
  /*103 */ uint32_t crc32;
  /*107 */ uint8_t  midi_channel;
  /*108 */ int8_t   loop_type; /* -1 or 0=normal, 1=bidi */
  /*109 */ uint8_t  reserved[10];
  /*119 */ char     filename[25 + 1];
  /*144 */

  modutil::error load(size_t num, FILE *fp, long file_length)
  {
    uint8_t buf[144];

    if(fread(magic, 1, 4, fp) < 4)
      return modutil::READ_ERROR;

    /* Blank sample */
    if(!memcmp(magic, LIQ_NO_LDSS_MAGIC, 4))
      return modutil::SUCCESS;

    if(fread(buf + 4, 1, 140, fp) < 140)
      return modutil::READ_ERROR;

    if(memcmp(magic, LIQ_LDSS_MAGIC, 4))
    {
      format::warning("instrument %zu magic mismatch: %02x %02x %02x %02x",
        num, magic[0], magic[1], magic[2], magic[3]);
    }

    memcpy(name, buf + 6, 30);
    name[30] = '\0';
    memcpy(software_name, buf + 36, 20);
    software_name[20] = '\0';
    memcpy(author_name, buf + 56, 20);
    author_name[20] = '\0';

    format_version    = mem_u16le(buf + 4);
    sound_board_id    = buf[76];
    length            = mem_u32le(buf + 77);
    loopstart         = mem_u32le(buf + 81);
    loopend           = mem_u32le(buf + 85);
    rate              = mem_u32le(buf + 89);
    default_volume    = buf[93];
    flags             = buf[94];
    default_pan       = buf[95];
    midi_patch        = buf[96];
    global_volume     = buf[97];
    chord_type        = buf[98];
    length_bytes      = mem_u16le(buf + 99);
    compression_type  = mem_u16le(buf + 101);
    crc32             = mem_u32le(buf + 103);
    midi_channel      = buf[107];
    loop_type         = buf[108];

    memcpy(reserved, buf + 109, 10);
    memcpy(filename, buf + 119, 25);
    filename[25] = '\0';

    /* Skip sample data */
    long skip_bytes = static_cast<long>(length);
    if(skip_bytes != 0)
    {
      if(skip_bytes < 0 || fseek(fp, skip_bytes, SEEK_CUR) < 0)
        return modutil::SEEK_ERROR;
    }
    return modutil::SUCCESS;
  }
};

struct LIQ_data
{
  LIQ_header                  header;
  std::vector<LIQ_pattern>    patterns;
  std::vector<LIQ_instrument> instruments;

  bool uses[NUM_FEATURES];
};

class LIQ_loader : public modutil::loader
{
public:
  LIQ_loader(): modutil::loader("LIQ", "liqnew", "Liquid Tracker") {}

  virtual modutil::error load(FILE *fp, long file_length) const override
  {
    LIQ_data m{};
    LIQ_header &h = m.header;
    std::vector<uint8_t> patbuf;
    uint8_t buffer[109];
    size_t base_header_size;
    size_t header_remaining;
    size_t num_orders_to_load;
    size_t num_channels_to_load;
    size_t i;

    if(fread(h.magic, 1, 14, fp) < 14)
      return modutil::FORMAT_ERROR;
    if(memcmp(h.magic, LIQ_MAGIC, 14))
      return modutil::FORMAT_ERROR;

    total_liq++;

    /* Header */
    if(fread(buffer + 14, 1, 109 - 14, fp) < (109 - 14))
      return modutil::READ_ERROR;

    memcpy(h.name, buffer + 14, 30);
    h.name[30] = '\0';
    memcpy(h.author, buffer + 44, 20);
    h.author[20] = '\0';
    h.eof = buffer[64];
    memcpy(h.tracker_name, buffer + 65, 20);
    h.tracker_name[20] = '\0';

    h.format_version  = mem_u16le(buffer + 85);
    h.initial_speed   = mem_u16le(buffer + 87);
    h.initial_bpm     = mem_u16le(buffer + 89);
    h.lowest_note     = mem_u16le(buffer + 91);
    h.highest_note    = mem_u16le(buffer + 93);
    h.num_channels    = mem_u16le(buffer + 95);
    h.flags           = mem_u32le(buffer + 97);
    h.num_patterns    = mem_u16le(buffer + 101);
    h.num_instruments = mem_u16le(buffer + 103);

    if(h.format_version >= 0x100)
    {
      h.num_orders      = mem_u16le(buffer + 105);
      h.header_bytes    = mem_u16le(buffer + 107);

      num_orders_to_load = h.num_orders;
      num_channels_to_load = h.num_channels;
    }
    else
    {
      /* 256 orders stored always, scan for FFh like with NO for real end. */
      h.num_orders      = 0;
      h.header_bytes    = mem_u16le(buffer + 105);
      /* Skip 5 reserved bytes, 2 already read. */
      fseek(fp, 3, SEEK_CUR);

      num_channels_to_load = 64;
      num_orders_to_load = 256;
    }

    if(h.flags & LIQ_CUT_ON_LIMIT)
      m.uses[MODE_CUT_ON_LIMIT] = true;
    if(h.flags & LIQ_ST3_COMPATIBILITY)
      m.uses[MODE_S3M] = true;
    else
      m.uses[MODE_LIQ] = true;

    if(h.num_channels > MAX_CHANNELS)
    {
      format::warning("invalid channel count %u, stopping", h.num_channels);
      goto done;
    }
    if(h.num_patterns > MAX_PATTERNS)
    {
      format::warning("invalid pattern count %u, stopping", h.num_patterns);
      goto done;
    }
    if(h.num_instruments > MAX_INSTRUMENTS)
    {
      format::warning("invalid instrument count %u, stopping", h.num_instruments);
      goto done;
    }

    h.initial_pan.resize(num_channels_to_load);
    h.initial_volume.resize(num_channels_to_load);
    h.orders.resize(num_orders_to_load);

    if(fread(h.initial_pan.data(), 1, num_channels_to_load, fp) < num_channels_to_load)
    {
      format::warning("read error at initial pan table, stopping");
      goto done;
    }
    if(fread(h.initial_volume.data(), 1, num_channels_to_load, fp) < num_channels_to_load)
    {
      format::warning("read error at initial volume table, stopping");
      goto done;
    }
    if(fread(h.orders.data(), 1, num_orders_to_load, fp) < num_orders_to_load)
    {
      format::warning("read error at order table, stopping");
      goto done;
    }

    if(h.format_version < 0x100)
    {
      /* Scan real order count. */
      for(i = 0; i < 256; i++)
        if(h.orders[i] == 0xff)
          break;
      h.num_orders = i;
    }

    base_header_size = ftell(fp);
    if(base_header_size > h.header_bytes)
    {
      /* 0.00 has 0 in this field. */
      if(h.format_version > 0)
        format::warning("unreliable header bytes field: %u", h.header_bytes);
      header_remaining = 0;
    }
    else
      header_remaining = h.header_bytes - base_header_size;

    /* Header for format versions 1.01+ */
    if(header_remaining > 0 && header_remaining < 4)
    {
      format::warning("header data too short to fit echo data");
      header_remaining = 0;
    }
    if(h.format_version >= 0x101 && header_remaining >= 4)
    {
      if(fread(h.echo_magic, 1, 4, fp) < 4)
      {
        format::warning("read error in echo data, stopping");
        goto done;
      }
      header_remaining -= 4;

      if(!memcmp(h.echo_magic, LIQ_ECHO_MAGIC, 4) &&
       header_remaining >= 4u + (4u * h.num_channels))
      {
        h.channel_setup.resize(h.num_channels);
        for(i = 0; i < h.num_channels; i++)
        {
          if(fread(buffer, 1, 4, fp) < 4)
          {
            format::warning("read error in echo data, stopping");
            goto done;
          }
          h.channel_setup[i].mix_setup = mem_u16le(buffer + 0);
          h.channel_setup[i].echo_amount = mem_u16le(buffer + 2);
        }
        header_remaining -= 4 * h.num_channels;

        if(fread(buffer, 1, 4, fp) < 4)
        {
          format::warning("read error in echo data, stopping");
          goto done;
        }
        header_remaining -= 4;
        h.num_pools = mem_u32le(buffer);

        if(h.num_pools > 0 && header_remaining >= 20ull * h.num_pools)
        {
          h.pools.resize(h.num_pools);
          for(i = 0; i < h.num_pools; i++)
          {
            if(fread(buffer, 1, 20, fp) < 20)
            {
              format::warning("read error in echo pools, stopping");
              goto done;
            }
            LIQ_echo_pool &p  = h.pools[i];
            p.delay_ms        = mem_u32le(buffer + 0);
            p.decay_left      = mem_u32le(buffer + 4);
            p.decay_right     = mem_u32le(buffer + 8);
            p.flags           = mem_u32le(buffer + 12);
            p.reserved        = mem_u32le(buffer + 16);
          }
          header_remaining -= 20ull * h.num_pools;
        }
        else
        {
          format::warning("header data too short to fit echo pools");
          header_remaining = 0;
        }
      }
      else
      {
        format::warning("header data too short to fit echo data");
        header_remaining = 0;
      }
    }

    /* Header for format versions 1.02+ */
    if(h.format_version >= 0x102 && header_remaining >= 2)
    {
      h.amplification = fget_u16le(fp);
      header_remaining -= 2;
    }

    if(header_remaining > 0)
    {
      format::warning("unloaded header bytes: %zu", header_remaining);
      if(fseek(fp, header_remaining, SEEK_CUR) < 0)
      {
        format::warning("error seeking to end of header, stopping");
        goto done;
      }
    }

    /* Patterns */
    /* !!!! = blank pattern, no structure or data follows */
    m.patterns.resize(h.num_patterns);
    for(i = 0; i < h.num_patterns; i++)
    {
      LIQ_pattern &p = m.patterns[i];
      modutil::error err = p.load(i, h.num_channels, patbuf, fp, file_length);
      if(err != modutil::SUCCESS)
      {
        format::warning("error loading pattern %zu: %s", i,
          modutil::strerror(err));
        if(err != modutil::BAD_PACKING)
          goto done;
      }
      for(const LIQ_event &event : p.events)
      {
        if(event.octave() >= 8)
          m.uses[NOTE_OCTAVE_8_9] = true;
      }
    }

    /* Instruments */
    /* ???? = blank instrument, no structure or data follows */
    m.instruments.resize(h.num_instruments);
    for(i = 0; i < h.num_instruments; i++)
    {
      LIQ_instrument &ins = m.instruments[i];
      modutil::error err = ins.load(i, fp, file_length);
      if(err != modutil::SUCCESS)
      {
        format::warning("error loading instrument %zu: %s", i,
          modutil::strerror(err));
        goto done;
      }
      if(ins.length)
      {
        if(ins.flags & LIQ_16BIT)
          m.uses[SAMPLE_16BIT] = true;
        if(ins.flags & LIQ_STEREO)
          m.uses[SAMPLE_STEREO] = true;

        if(ins.flags & LIQ_SIGNED)
          m.uses[SAMPLE_SIGNED] = true;
        else
          m.uses[SAMPLE_UNSIGNED] = true;
      }
    }

done:
    /* Print information */
    format::line("Name",      "%s", h.name);
    format::line("Author",    "%s", h.author);
    format::line("Type",      "Liquid Tracker %d.%02x",
      h.format_version >> 8, h.format_version & 0xff);
    format::line("Tracker",   "%s", h.tracker_name);
    format::line("Channels",  "%d", h.num_channels);
    format::line("Patterns",  "%d", h.num_patterns);
    format::line("Orders",    "%d", h.num_orders);
    format::line("Instr.",    "%d", h.num_instruments);
    format::line("Speed",     "%d", h.initial_speed);
    format::line("BPM",       "%d", h.initial_bpm);
    format::line("NoteRng.",  "%d to %d", h.lowest_note, h.highest_note);
    if(h.format_version >= 0x102)
      format::line("Ampl.",   "%d", h.amplification);
    format::uses(m.uses, FEATURE_STR);

    if(Config.dump_samples && m.instruments.size())
    {
      format::line();

      static const char *s_labels[] =
      {
        "Name", "Length", "LoopStart", "LoopEnd",
        "Rate", "Vol", "Pan", "GVol", "Flg", "Loop"
      };
      static const char *d_labels[] =
      {
        "Filename", "Author", "Software", "Sound Board", "CRC-32", "Ver."
      };

      namespace table = format::table;
      table::table<
        table::string<30>,
        table::spacer,
        table::number<10>,
        table::number<10>,
        table::number<10>,
        table::spacer,
        table::number<10>,
        table::number<3>,
        table::number<3>,
        table::number<4>,
        table::number<3>,
        table::number<4>> s_table;

      table::table<
        table::string<25>,
        table::string<20>,
        table::string<20>,
        table::string<MAX_SOUNDBOARD_STRING>,
        table::number<8, table::HEX | table::ZEROS | table::RIGHT>,
        table::string<4>> d_table;

      s_table.header("Samples", s_labels);

      for(unsigned int i = 0; i < h.num_instruments; i++)
      {
        LIQ_instrument &ins = m.instruments[i];
        s_table.row(i, ins.name, {}, ins.length, ins.loopstart, ins.loopend, {},
          ins.rate, ins.default_volume, ins.default_pan, ins.global_volume,
          ins.flags, ins.loop_type);
      }

      if(Config.dump_samples_extra)
      {
        char tmp[32];
        format::line();
        d_table.header("Samples", d_labels);
        for(i = 0; i < h.num_instruments; i++)
        {
          LIQ_instrument &ins = m.instruments[i];
          snprintf(tmp, sizeof(tmp), "%d.%02x",
            ins.format_version >> 8, ins.format_version & 0xff);

          d_table.row(i, ins.filename, ins.author_name, ins.software_name,
            LIQ_soundboard_string(ins.sound_board_id), ins.crc32, tmp);
        }
      }
    }

    if(Config.dump_patterns)
    {
      format::line();

      if(!Config.quiet)
      {
        // hack
        O_("Panning :");
        for(i = 0; i < num_channels_to_load; i++)
          fprintf(stderr, " %02x", h.initial_pan[i]);
        fprintf(stderr, "\n");
        O_("Volume  :");
        for(i = 0; i < num_channels_to_load; i++)
          fprintf(stderr, " %02x", h.initial_volume[i]);
        fprintf(stderr, "\n");
      }
      format::line();
      format::orders("Orders", h.orders.data(), h.orders.size());

      if(!Config.dump_pattern_rows)
        format::line();

      for(i = 0; i < h.num_patterns; i++)
      {
        if(!m.patterns.size())
          break;

        LIQ_pattern &p = m.patterns[i];

        using EVENT = format::event<format::note, format::sample, format::volume, format::effectIT>;
        format::pattern<EVENT> pattern(i, p.num_channels, p.num_rows, p.packed_bytes);

        if(!Config.dump_pattern_rows)
        {
          pattern.summary();
          continue;
        }

        for(size_t row = 0; row < p.num_rows; row++)
        {
          for(size_t track = 0; track < p.num_channels; track++)
          {
            LIQ_event &current = p.events[track * p.num_rows + row];
            format::note      a{ current.note };
            format::sample    b{ current.instrument };
            format::volume    c{ current.volume };
            format::effectIT  d{ current.effect, current.param };
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
    if(!total_liq)
      return;

    format::report("Total Liquid (LIQ)", total_liq);
  }
};

static const LIQ_loader loader;
