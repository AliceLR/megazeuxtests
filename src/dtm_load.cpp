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
#include "IFF.hpp"

#include <vector>

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

static size_t num_dtm = 0;


enum DTM_feature
{
  FT_CHUNK_VERS,
  FT_CHUNK_SV19,
  FT_CHUNK_IENV,
  FT_PATTERN_MOD,
  FT_PATTERN_V204,
  FT_PATTERN_V206,
  FT_PATTERN_UNKNOWN,
  FT_MODE_OLD_STEREO,
  FT_MODE_PANORAMIC_STEREO,
  FT_MODE_UNKNOWN_STEREO,
  FT_SAMPLE_8_BIT,
  FT_SAMPLE_16_BIT,
  FT_SAMPLE_UNKNOWN_BITS,
  FT_SAMPLE_STEREO,
  FT_ROWS_96,
  FT_FX_ARPEGGIO,
  FT_FX_PORTA_UP,
  FT_FX_PORTA_DN,
  FT_FX_TONE_PORTAMENTO,
  FT_FX_VIBRATO,
  FT_FX_TONE_PORTAMENTO_VOLSLIDE,
  FT_FX_VIBRATO_VOLSLIDE,
  FT_FX_TREMOLO,
  FT_FX_8,
  FT_FX_SET_SAMPLE_OFFSET,
  FT_FX_VOLSLIDE,
  FT_FX_PATTERN_JUMP,
  FT_FX_VOLUME,
  FT_FX_PATTERN_BREAK,
  FT_FX_EXTENDED,
  FT_FX_SPEED,
  FT_FX_GT_10,
  FT_FX_EX_0,
  FT_FX_FINE_PORTA_UP,
  FT_FX_FINE_PORTA_DN,
  FT_FX_GLISSANDO_CONTROL,
  FT_FX_SET_VIBRATO_WAVEFORM,
  FT_FX_SET_FINETUNE,
  FT_FX_PATTERN_LOOP,
  FT_FX_EX_7,
  FT_FX_EX_8,
  FT_FX_EX_9,
  FT_FX_FINE_VOLSLIDE_UP,
  FT_FX_FINE_VOLSLIDE_DN,
  FT_FX_NOTE_CUT,
  FT_FX_NOTE_DELAY,
  FT_FX_PATTERN_DELAY,
  FT_FX_EX_F,
  NUM_FEATURES
};

static constexpr const char *FEATURE_STR[NUM_FEATURES] =
{
  "C:VERS",
  "C:SV19",
  "C:IENV",
  "P:MOD",
  "P:2.04",
  "P:2.06",
  "P:???",
  "M:OldStereo",
  "M:Panoramic",
  "M:???",
  "S:8",
  "S:16",
  "S:??",
  "S:Stereo",
  "Rows>96",
  "E:Arp",
  "E:PortaUp",
  "E:PortaDn",
  "E:Toneporta",
  "E:Vibrato",
  "E:TPVolside",
  "E:VibVolslide",
  "E:Tremolo",
  "E:8",
  "E:Offset",
  "E:Volslide",
  "E:Jump",
  "E:Volume",
  "E:Break",
  "E:Ex",
  "E:Speed",
  "E:>=10",
  "E:E0x",
  "E:FPortaUp",
  "E:FPortaDn",
  "E:Glissando",
  "E:VibWF",
  "E:Finetune",
  "E:Loop",
  "E:E7x",
  "E:E8x",
  "E:E9x",
  "E:FVolslideUp",
  "E:FVolslideDn",
  "E:NoteCut",
  "E:NoteDelay",
  "E:PatternDelay",
  "E:EFx",
};

static constexpr unsigned MAX_CHANNELS = 32;
static constexpr unsigned MAX_ROWS = 512;
static constexpr unsigned MAX_INSTRUMENTS = 64;
static constexpr unsigned MAX_PATTERNS = 128;
static constexpr unsigned MAX_SEQUENCE = 128;

class format_version
{
public:
  uint32_t value;
  constexpr format_version(uint32_t v) noexcept: value(v) {}

  constexpr format_version(char a, char b, char c, char d) noexcept:
   format_version(((unsigned)a << 24u) | ((unsigned)b << 16u) |
                  ((unsigned)c <<  8u) | ((unsigned)d)) {}

  constexpr format_version(const char (&a)[5]) noexcept:
   format_version(a[0], a[1], a[2], a[3]) {}

  constexpr bool operator=(format_version v) const noexcept
  {
     return value == v.value;
  }
  constexpr bool operator=(uint32_t v) const noexcept
  {
    return value == v;
  }
  constexpr operator uint32_t() const noexcept
  {
    return value;
  }
};

static constexpr format_version format_mod(0);
static constexpr format_version format_v204("2.04");
static constexpr format_version format_v206("2.06");


static constexpr DTM_feature effect_features[16] =
{
  FT_FX_ARPEGGIO,
  FT_FX_PORTA_UP,
  FT_FX_PORTA_DN,
  FT_FX_TONE_PORTAMENTO,
  FT_FX_VIBRATO,
  FT_FX_TONE_PORTAMENTO_VOLSLIDE,
  FT_FX_VIBRATO_VOLSLIDE,
  FT_FX_TREMOLO,
  FT_FX_8,
  FT_FX_SET_SAMPLE_OFFSET,
  FT_FX_VOLSLIDE,
  FT_FX_PATTERN_JUMP,
  FT_FX_VOLUME,
  FT_FX_PATTERN_BREAK,
  FT_FX_EXTENDED,
  FT_FX_SPEED,
};

static constexpr DTM_feature extended_features[16] =
{
  FT_FX_EX_0,
  FT_FX_FINE_PORTA_UP,
  FT_FX_FINE_PORTA_DN,
  FT_FX_GLISSANDO_CONTROL,
  FT_FX_SET_VIBRATO_WAVEFORM,
  FT_FX_SET_FINETUNE,
  FT_FX_PATTERN_LOOP,
  FT_FX_EX_7,
  FT_FX_EX_8,
  FT_FX_EX_9,
  FT_FX_FINE_VOLSLIDE_UP,
  FT_FX_FINE_VOLSLIDE_DN,
  FT_FX_NOTE_CUT,
  FT_FX_NOTE_DELAY,
  FT_FX_PATTERN_DELAY,
  FT_FX_EX_F,
};

class DTM_event
{
public:
  static constexpr size_t size_mod = 4;
  static constexpr size_t size_v204 = 4;
  static constexpr size_t size_v206 = 6;

  /* MOD:  Amiga period */
  /* 2.04: upper: octave, lower: note */
  /* 2.06: FIXME */
  uint16_t note;
  uint8_t  instrument; /* 0-63 */
  uint8_t  volume; /* 0-63? */
  uint8_t  effect; /* 0-15 */
  uint8_t  param; /* 0-255 */

  static constexpr size_t size(uint32_t format) noexcept
  {
    switch(format)
    {
      case format_mod:
        return size_mod;
      case format_v204:
        return size_v204;
      case format_v206:
        return size_v206;
    }
    return 0;
  }

  constexpr size_t unpack_mod(const uint8_t *data, size_t data_len) noexcept
  {
    if(data_len < size_mod)
      return 0;

    note = ((data[0] & 0x0F) << 8) | (data[1]);
    volume = 0;
    instrument = (data[0] & 0xF0) | (data[2] >> 4);
    effect = (data[2] & 0x0F);
    param = data[3];
    return size_mod;
  }

  constexpr size_t unpack_v204(const uint8_t *data, size_t data_len) noexcept
  {
    if(data_len < size_v204)
      return 0;

    note = data[0];
    volume = data[1] >> 2;
    instrument = ((data[2] & 0xF0) >> 4) | ((data[1] & 0x03) << 4);
    effect = (data[2] & 0x0F);
    param = data[3];
    return size_v204;
  }

  constexpr size_t unpack_v206(const uint8_t *data, size_t data_len) noexcept
  {
    if(data_len < size_v206)
      return 0;

    return 0; // FIXME!
  }

  constexpr size_t unpack(const uint8_t *data, size_t data_len, uint32_t format) noexcept
  {
    switch(format)
    {
      case format_mod:
        return unpack_mod(data, data_len);
      case format_v204:
        return unpack_v204(data, data_len);
      case format_v206:
        return unpack_v206(data, data_len);
    }
    return 0;
  }

  constexpr void check_features(bool (&uses)[NUM_FEATURES]) const noexcept
  {
    if(effect >= 0x10)
    {
      uses[FT_FX_GT_10] = true;
      return;
    }

    if(effect == 0 && param == 0)
      return;

    if(effect == 0x0E)
      uses[extended_features[param >> 4]] = true;

    uses[effect_features[effect]] = true;
  }
};

class DTM_pattern
{
public:
  static constexpr size_t max_name_length = 128;
  static constexpr size_t max_DAPT_length = 0;

  bool loaded_DAPT;
  /* PATN */
  char name[max_name_length + 1];
  char name_clean[max_name_length + 1];
  /* DAPT */
  uint32_t reserved;
  uint16_t length;
  uint16_t channels; // Copied from global data
  std::vector<DTM_event> events;

  DTM_pattern() noexcept: loaded_DAPT(false),
   name{}, name_clean{}, reserved{}, length(0), events{} {}

  void set_name(const char *data, size_t data_len) noexcept
  {
    data_len = MIN(data_len, sizeof(name) - 1);
    memcpy(name, data, data_len);
    name[data_len] = '\0';
    // FIXME: clean
  }

  void set_header(uint16_t chn, uint32_t res, uint16_t len)
  {
    reserved = res;
    length = len;
    channels = chn;
    events.resize((size_t)chn * len);
  }

  bool load(const uint8_t *data, size_t data_len, uint32_t format)
  {
    size_t ev = 0;
    for(size_t i = 0; i < length; i++)
    {
      for(size_t j = 0; j < channels; j++)
      {
        size_t used = events[ev++].unpack(data, data_len, format);
        if(!used)
          return false;

        data += used;
        data_len -= used;
      }
    }
    return true;
  }

  void check_features(bool (&uses)[NUM_FEATURES]) const
  {
    for(const DTM_event &ev : events)
      ev.check_features(uses);
  }
};

class DTM_channel
{
public:
  static constexpr size_t max_name_length = 31;

  bool loaded_TRKN;
  /* TRKN */
  char name[max_name_length + 1];
  char name_clean[max_name_length + 1];
  /* SV19 */
  int16_t initial_pan;

  DTM_channel(): name{}, name_clean{}, initial_pan{} {}

  void set_name(const char *data, size_t data_len) noexcept
  {
    data_len = MIN(data_len, sizeof(name) - 1);
    memcpy(name, data, data_len);
    name[data_len] = '\0';
    // FIXME: clean
  }
};

class DTM_instrument
{
public:
  static constexpr size_t INST_header_length = 2;
  static constexpr size_t INST_entry_length = 50;
  static constexpr size_t INST_max_length = INST_header_length + INST_entry_length * MAX_INSTRUMENTS;
  static constexpr size_t max_name_length = 22;

  bool loaded_DAIT;
  /* INST */
  uint32_t reserved;
  uint32_t length; // bytes?
  uint8_t  finetune;
  uint8_t  default_volume; // actually default?
  uint32_t loop_start;
  uint32_t loop_length;
  char     name[max_name_length + 1];
  char     name_clean[max_name_length + 1];
  uint8_t  sample_stereo;
  uint8_t  sample_bits; /* 8:8-bit, 16:16-bit, 0:deleted? */
  uint16_t midi_note; /* "Note" field used as a transpose in later versions. */
  uint16_t midi_unknown; /* MIDI patch/bank? what? */
  uint32_t frequency; /* C2? C4? C5? */
  /* SV19 */
  uint8_t  type; // 0=memory, 1=external file, 2=midi
  /* DAIT */
  void *sample_data;

  constexpr DTM_instrument() noexcept: loaded_DAIT(false),
   reserved{}, length{}, finetune{}, default_volume{64},
   loop_start{}, loop_length{}, name{}, name_clean{},
   sample_stereo{}, sample_bits{}, midi_note{}, midi_unknown{},
   frequency{}, type{}, sample_data{} {}

  modutil::error load(const uint8_t *data, size_t data_len) noexcept
  {
    if(data_len < INST_entry_length)
      return modutil::INVALID;

    reserved       = mem_u32be(data + 0);
    length         = mem_u32be(data + 4);
    finetune       = data[8];
    default_volume = data[9];
    loop_start     = mem_u32be(data + 10);
    loop_length    = mem_u32be(data + 14);
    sample_stereo  = data[40];
    sample_bits    = data[41];
    midi_note      = mem_u16be(data + 42);
    midi_unknown   = mem_u16be(data + 44);
    frequency      = mem_u32be(data + 46);

    memcpy(name, data + 18, max_name_length);
    name[max_name_length] = '\0';
    // FIXME: clean
    return modutil::SUCCESS;
  }

  constexpr bool is_default() const noexcept
  {
    return length == 0 && loop_start == 0 && loop_length == 0 && finetune == 0 &&
      default_volume == 64 && sample_stereo == 0 && sample_bits == 8 &&
      (frequency == 8363 || frequency == 8400);
  }
};

class DTM_comment
{
public:
  static constexpr size_t min_TEXT_length = 12;

  /* TEXT */
  uint16_t type; // 0=pattern, 1="free", 2=song
  uint32_t length;
  uint16_t tabulation; // ???
  uint16_t reserved;
  uint16_t odd_length; /* =$FFFF <=> length is odd" */
  // Note: padding byte for odd text is PREFIXED for some reason...
  std::vector<uint8_t> raw;
  std::vector<char>    clean;
  size_t calculated_length;
  size_t actual_length;

  DTM_comment(const uint8_t *data, size_t data_len):
   raw{}, clean{}
  {
    if(data_len < min_TEXT_length)
      throw -1;

    type       = mem_u16be(data + 0);
    length     = mem_u32be(data + 2);
    tabulation = mem_u16be(data + 6);
    reserved   = mem_u16be(data + 8);
    odd_length = mem_u16be(data + 10);
  }

  DTM_comment(DTM_comment &&src)
  {
    type = src.type;
    length = src.length;
    tabulation = src.tabulation;
    reserved = src.reserved;
    odd_length = src.odd_length;
    calculated_length = src.calculated_length;
    actual_length = src.actual_length;
    raw = std::move(src.raw);
    clean = std::move(src.clean);
  }

  modutil::error read_comment_text(FILE *fp, size_t max_read)
  {
    size_t skip_padding = (odd_length == 0xffff);
    calculated_length = max_read ? max_read - skip_padding : 0;

    if(calculated_length != length)
    {
      format::warning("TEXT claims %" PRIu32 " bytes, actually contains %zu",
       length, calculated_length);
    }

    try
    {
      raw.resize(calculated_length + 1);
    }
    catch(std::exception &e)
    {
      format::warning("std::vector::resize error in TEXT: %s", e.what());
      return modutil::ALLOC_ERROR;
    }

    if(skip_padding)
      fgetc(fp);

    actual_length = fread(raw.data(), 1, calculated_length, fp);
    raw[actual_length] = '\0';
    if(actual_length < calculated_length)
      format::warning("read error in TEXT body");

    // FIXME: clean

    return modutil::SUCCESS;
  }
};

class DTM_module
{
public:
  static constexpr size_t D_T_header_length = 14;
  static constexpr size_t min_SV19_length = 4 + 2 * MAX_CHANNELS;
  static constexpr size_t max_SV19_length = min_SV19_length + MAX_INSTRUMENTS;
  static constexpr size_t max_PATT_length = 8;
  static constexpr unsigned old_stereo = 0;
  static constexpr unsigned panoramic_stereo = 0xff;

  bool     loaded_D_T_;
  bool     loaded_VERS;
  bool     loaded_PATN;
  bool     loaded_TRKN;
  bool     loaded_SV19;
  bool     loaded_S_Q_;
  bool     loaded_PATT;
  bool     loaded_INST;
  size_t   orders_in_SQ;
  size_t   patterns_in_PATN;
  size_t   channels_in_TRKN;
  size_t   instruments_in_SV19;
  size_t   instruments_in_INST; // number of instruments actually found in data, regardless of claimed count
  /* VERS */
  uint32_t version;
  /* D.T. */
  uint16_t file_type; // ??
  uint8_t  stereo_mode; // 00h = old stereo, FFh = panoramic stereo
  uint8_t  global_sample_depth; // pre-2.04
  uint16_t reserved_dt;
  uint16_t initial_speed;
  uint16_t initial_bpm; // tracker BPM
  uint32_t global_sample_rate; // pre-2.04
  uint8_t  name[129];
  char     name_clean[129];
  /* SV19 */
  uint16_t ticks_per_beat;
  uint32_t initial_bpm_frac; // tracker BPM (fractional portion)
  /* S.Q. */
  uint16_t num_orders;
  uint16_t repeat_position;
  uint32_t reserved_sq;
  /* PATT */
  uint16_t num_channels;
  uint16_t num_patterns;
  uint32_t pattern_format_version; // ??
  /* INST */
  uint16_t num_instruments;

  bool     uses[NUM_FEATURES];

  uint8_t     sequence[MAX_SEQUENCE];
  DTM_channel channels[MAX_CHANNELS];
  DTM_pattern patterns[MAX_PATTERNS];

  std::vector<DTM_instrument> instruments;
  std::vector<DTM_comment>    comments;
};


class DdTd_handler
{
public:
  static constexpr IFFCode id = IFFCode("D.T.");

  static modutil::error parse(FILE *fp, size_t len, DTM_module &m)
  {
    if(m.loaded_D_T_)
    {
      format::error("duplicate D.T. chunk");
      return modutil::INVALID;
    }
    m.loaded_D_T_ = true;

    if(len < DTM_module::D_T_header_length || len > 128 + DTM_module::D_T_header_length)
    {
      format::error("invalid D.T. chunk length %zu", len);
      return modutil::INVALID;
    }

    uint8_t buf[128 + DTM_module::D_T_header_length];
    if(fread(buf, 1, len, fp) < len)
    {
      format::error("read error in D.T.");
      return modutil::INVALID;
    }

    m.file_type = mem_u16be(buf + 0);
    m.stereo_mode = buf[2];
    m.global_sample_depth = buf[3]; /* pre-2.04 only */
    m.reserved_dt = mem_u16be(buf + 4);
    m.initial_speed = mem_u16be(buf + 6);
    m.initial_bpm = mem_u16be(buf + 8);
    m.global_sample_rate = mem_u32be(buf + 10); /* pre-2.04 only */

    size_t name_len = len - DTM_module::D_T_header_length;
    memcpy(m.name, buf + DTM_module::D_T_header_length, name_len);
    m.name[name_len] = '\0';
    // FIXME: clean

    switch(m.stereo_mode)
    {
      case DTM_module::old_stereo:
        m.uses[FT_MODE_OLD_STEREO] = true;
        break;
      case DTM_module::panoramic_stereo:
        m.uses[FT_MODE_PANORAMIC_STEREO] = true;
        break;
      default:
        m.uses[FT_MODE_UNKNOWN_STEREO] = true;
        break;
    }

    return modutil::SUCCESS;
  }
};

class VERS_handler
{
public:
  static constexpr IFFCode id = IFFCode("VERS");

  static modutil::error parse(FILE *fp, size_t len, DTM_module &m)
  {
    if(m.loaded_VERS)
      format::warning("duplicate VERS chunk");
    m.loaded_VERS = true;
    m.uses[FT_CHUNK_VERS] = true;

    if(len < 4)
    {
      format::warning("skipping invalid VERS length %zu", len);
      return modutil::SUCCESS;
    }

    m.version = fget_u32be(fp);
    if(feof(fp))
    {
      format::error("read error in VERS");
      return modutil::READ_ERROR;
    }
    return modutil::SUCCESS;
  }
};

class SdQd_handler
{
public:
  static constexpr IFFCode id = IFFCode("S.Q.");

  static modutil::error parse(FILE *fp, size_t len, DTM_module &m)
  {
    if(m.loaded_S_Q_)
    {
      format::warning("ignoring duplicate S.Q.");
      return modutil::SUCCESS;
    }
    if(len < 8)
    {
      format::warning("ignoring S.Q. of invalid length %zu", len);
      return modutil::SUCCESS;
    }
    if(len > 8 + MAX_SEQUENCE)
    {
      format::warning("ignoring S.Q. orders beyond 128 (found %zu)", len - 8);
      len = 8 + MAX_SEQUENCE;
    }
    m.loaded_S_Q_ = true;

    uint8_t buf[8 + MAX_SEQUENCE];
    if(fread(buf, 1, len, fp) < len)
    {
      format::error("read error in S.Q.");
      return modutil::READ_ERROR;
    }
    m.num_orders      = mem_u16be(buf + 0);
    m.repeat_position = mem_u16be(buf + 2);
    m.reserved_sq     = mem_u32be(buf + 4);
    m.orders_in_SQ    = len - 8;
    memcpy(m.sequence, buf + 8, m.orders_in_SQ);

    if(m.orders_in_SQ < m.num_orders)
    {
      format::warning("read fewer orders from S.Q. (%zu) than were specified (%d)",
       m.orders_in_SQ, m.num_orders);
    }
    return modutil::SUCCESS;
  }
};

class PATN_handler
{
public:
  static constexpr IFFCode id = IFFCode("PATN");

  static modutil::error parse(FILE *fp, size_t len, DTM_module &m)
  {
    if(m.loaded_PATN)
    {
      format::warning("ignoring duplicate PATN");
      return modutil::SUCCESS;
    }
    m.loaded_PATN = true;

    char buf[DTM_pattern::max_name_length + 1];

    for(size_t i = 0; i < MAX_PATTERNS && len > 0; i++)
    {
      size_t in_len = fget_asciiz(buf, len, fp);
      len -= MIN(len, in_len + 1);

      if(in_len > DTM_pattern::max_name_length)
        format::warning("truncating pattern %zu name of length %zu", i, in_len);

      m.patterns[i].set_name(buf, in_len);
      m.patterns_in_PATN++;
    }

    if(len)
      format::warning("%zu extra bytes at the end of PATN", len);

    return modutil::SUCCESS;
  }
};

class TRKN_handler
{
public:
  static constexpr IFFCode id = IFFCode("TRKN");

  static modutil::error parse(FILE *fp, size_t len, DTM_module &m)
  {
    if(m.loaded_TRKN)
    {
      format::warning("ignoring duplicate TRKN");
      return modutil::SUCCESS;
    }
    m.loaded_TRKN = true;

    char buf[DTM_channel::max_name_length + 1];

    for(size_t i = 0; i < MAX_CHANNELS && len > 0; i++)
    {
      size_t in_len = fget_asciiz(buf, len, fp);
      len -= MIN(len, in_len + 1);

      if(in_len > DTM_channel::max_name_length)
        format::warning("truncating channel %zu name of length %zu", i, in_len);

      m.channels[i].set_name(buf, in_len);
      m.channels_in_TRKN++;
    }
    if(len)
      format::warning("%zu extra bytes at the end of TRKN", len);

    return modutil::SUCCESS;
  }
};

class SV19_handler
{
public:
  static constexpr IFFCode id = IFFCode("SV19");

  static modutil::error parse(FILE *fp, size_t len, DTM_module &m)
  {
    if(m.loaded_SV19)
      format::warning("duplicate SV19 chunk");
    m.loaded_SV19 = true;
    m.uses[FT_CHUNK_SV19] = true;

    if(len < DTM_module::min_SV19_length ||
     len > DTM_module::max_SV19_length)
    {
      format::warning("skipping invalid SV19 length %zu", len);
      return modutil::SUCCESS;
    }

    uint8_t buf[DTM_module::max_SV19_length];
    if(fread(buf, 1, len, fp) < len)
    {
      format::warning("read error in SV19, skipping");
      return modutil::SUCCESS;
    }
    m.ticks_per_beat   = mem_u16be(buf + 0);
    m.initial_bpm_frac = mem_u32be(buf + 2);

    /* 4 (32 * 2) - initial panning table */
    size_t i;
    for(i = 0; i < MAX_CHANNELS; i++)
      m.channels[i].initial_pan = mem_u16be(buf + 4 + i*2);

    /* 78 (instr * 1) - instrument type table */
    m.instruments_in_SV19 = len - DTM_module::min_SV19_length;
    if(m.instruments.size() < m.instruments_in_SV19)
      m.instruments.resize(m.instruments_in_SV19);

    for(i = 0; i < m.instruments_in_SV19; i++)
      m.instruments[i].type = buf[4 + 64 + i];

    return modutil::SUCCESS;
  }
};

class TEXT_handler
{
public:
  static constexpr IFFCode id = IFFCode("TEXT");

  static modutil::error parse(FILE *fp, size_t len, DTM_module &m)
  {
    if(len < DTM_comment::min_TEXT_length || (len & 1))
    {
      format::warning("ignoring invalid TEXT of length %zu", len);
      return modutil::SUCCESS;
    }

    uint8_t buf[DTM_comment::min_TEXT_length];
    if(fread(buf, 1, DTM_comment::min_TEXT_length, fp) < DTM_comment::min_TEXT_length)
    {
      format::warning("read error in TEXT, skipping");
      return modutil::SUCCESS;
    }

    DTM_comment cmt(buf, DTM_comment::min_TEXT_length);
    modutil::error ret = cmt.read_comment_text(fp, len - DTM_comment::min_TEXT_length);
    if(ret == modutil::SUCCESS)
      m.comments.push_back(std::move(cmt));

    return ret;
  }
};

class PATT_handler
{
public:
  static constexpr IFFCode id = IFFCode("PATT");

  static modutil::error parse(FILE *fp, size_t len, DTM_module &m)
  {
    if(m.loaded_PATT)
    {
      format::warning("ignoring duplicate PATT");
      return modutil::SUCCESS;
    }
    if(len != DTM_module::max_PATT_length)
    {
      format::warning("ignoring PATT of invalid length %zu", len);
      return modutil::SUCCESS;
    }
    m.loaded_PATT = true;

    uint8_t buf[DTM_module::max_PATT_length];
    if(fread(buf, 1, DTM_module::max_PATT_length, fp) < DTM_module::max_PATT_length)
    {
      format::warning("read error in PATT");
      return modutil::SUCCESS;
    }

    m.num_channels = mem_u16be(buf + 0);
    m.num_patterns = mem_u16be(buf + 2);
    m.pattern_format_version = mem_u32be(buf + 4);

    switch(m.pattern_format_version)
    {
      case 0:
        m.uses[FT_PATTERN_MOD] = true;
        break;
      case format_v204:
        m.uses[FT_PATTERN_V204] = true;
        break;
      case format_v206:
        m.uses[FT_PATTERN_V206] = true;
        break;
      default:
        m.uses[FT_PATTERN_UNKNOWN] = true;
        break;
    }

    if(m.num_channels > MAX_CHANNELS)
      format::warning("PATT claims invalid channel count %d", m.num_channels);
    if(m.num_patterns > MAX_PATTERNS)
      format::warning("PATT claims invalid pattern count %d", m.num_patterns);

    return modutil::SUCCESS;
  }
};

class INST_handler
{
public:
  static constexpr IFFCode id = IFFCode("INST");

  static modutil::error parse(FILE *fp, size_t len, DTM_module &m)
  {
    if(m.loaded_INST)
    {
      format::warning("ignoring duplicate INST");
      return modutil::SUCCESS;
    }
    if(len < DTM_instrument::INST_header_length)
    {
      format::warning("ignoring INST of invalid length %zu", len);
      return modutil::SUCCESS;
    }
    if(len > DTM_instrument::INST_max_length)
      format::warning("INST of length %zu longer than expected", len);

    if((len - DTM_instrument::INST_header_length) % DTM_instrument::INST_entry_length)
      format::warning("INST of length %zu contains incomplete instrument definition", len);

    m.loaded_INST = true;
    m.instruments_in_INST = (len - DTM_instrument::INST_header_length) / DTM_instrument::INST_entry_length;

    uint8_t buf[MAX(DTM_instrument::INST_header_length, DTM_instrument::INST_entry_length)];
    if(fread(buf, 1, DTM_instrument::INST_header_length, fp) < DTM_instrument::INST_header_length)
    {
      format::warning("read error in INST");
      return modutil::SUCCESS;
    }
    m.num_instruments = mem_u16be(buf + 0);

    if(m.num_instruments > MAX_INSTRUMENTS)
      format::warning("INST claims %d instruments, greater than maximum", m.num_instruments);
    if(m.instruments_in_INST > MAX_INSTRUMENTS)
      format::warning("INST contains %zu instruments, greater than maximum", m.instruments_in_INST);
    if(m.num_instruments != m.instruments_in_INST)
    {
      format::warning("INST claims %d instruments but contains %zu",
       m.num_instruments, m.instruments_in_INST);
    }
    size_t alloc_inst = MAX((size_t)m.num_instruments, m.instruments_in_INST);
    if(alloc_inst > m.instruments.size())
      m.instruments.resize(alloc_inst);

    for(size_t i = 0; i < m.instruments_in_INST; i++)
    {
      if(fread(buf, 1, DTM_instrument::INST_entry_length, fp) < DTM_instrument::INST_entry_length)
      {
        format::warning("read error in INST");
        break;
      }
      m.instruments[i].load(buf, DTM_instrument::INST_entry_length);

      if(m.instruments[i].sample_bits == 8)
        m.uses[FT_SAMPLE_8_BIT] = true;
      else
      if(m.instruments[i].sample_bits == 16)
        m.uses[FT_SAMPLE_16_BIT] = true;
      else
      if(m.instruments[i].sample_bits != 0)
        m.uses[FT_SAMPLE_UNKNOWN_BITS] = true;

      if(m.instruments[i].sample_stereo)
        m.uses[FT_SAMPLE_STEREO] = true;
    }
    return modutil::SUCCESS;
  }
};

class DAPT_handler
{
public:
  static constexpr IFFCode id = IFFCode("DAPT");

  static modutil::error parse(FILE *fp, size_t len, DTM_module &m)
  {
    if(len < 8)
    {
      format::warning("ignoring DAPT of invalid length %zu\n", len);
      return modutil::SUCCESS;
    }
    uint8_t buf[MAX_CHANNELS * MAX_ROWS];
    if(fread(buf, 1, 8, fp) < 8)
    {
      format::warning("read error in DAPT");
      return modutil::SUCCESS;
    }
    uint32_t reserved = mem_u32be(buf + 0);
    uint16_t num      = mem_u16be(buf + 4);
    uint16_t length   = mem_u16be(buf + 6);
    if(num >= MAX_PATTERNS)
    {
      format::warning("ignoring DAPT for invalid pattern number %d", num);
      return modutil::SUCCESS;
    }
    if(length > MAX_ROWS)
    {
      format::warning("ignoring DAPT %d with unsupported row count %d", num, length);
      return modutil::SUCCESS;
    }
    if(length > 96)
      m.uses[FT_ROWS_96] = true;

    DTM_pattern &pat = m.patterns[num];
    if(pat.loaded_DAPT)
    {
      format::warning("ignoring duplicate DAPT %d", num);
      return modutil::SUCCESS;
    }
    pat.set_header(m.num_channels, reserved, length);

    size_t event_size = DTM_event::size(m.pattern_format_version);
    if(!event_size)
    {
      format::warning("skipping DAPT %d for unknown pattern version %zu",
       num, (size_t)m.pattern_format_version);
      return modutil::SUCCESS;
    }

    len = MIN(len, event_size * m.num_channels * length);
    if(len > sizeof(buf))
    {
      format::warning("skipping DAPT %d of unsupported packed size %zu", num, len);
      return modutil::SUCCESS;
    }
    if(fread(buf, 1, len, fp) < len)
    {
      format::warning("read error in DAPT %d", num);
      return modutil::SUCCESS;
    }

    if(!pat.load(buf, len, m.pattern_format_version))
      format::warning("error unpacking DAPT %d", num);

    pat.check_features(m.uses);
    return modutil::SUCCESS;
  }
};

class DAIT_handler
{
public:
  static constexpr IFFCode id = IFFCode("DAIT");

  static modutil::error parse(FILE *fp, size_t len, DTM_module &m)
  {
    // FIXME: implement uniqueness check
    return modutil::SUCCESS;
  }
};


static const IFF<
  DTM_module,
  DdTd_handler,
  VERS_handler,
  SdQd_handler,
  PATN_handler,
  TRKN_handler,
  SV19_handler,
  TEXT_handler,
  PATT_handler,
  INST_handler,
  DAPT_handler,
  DAIT_handler> DTM_parser(Endian::BIG, IFFPadding::BYTE);


class DTM_loader: modutil::loader
{
public:
  DTM_loader(): modutil::loader("DTM", "dtm", "Digital Tracker") {}

  virtual modutil::error load(modutil::data state) const override
  {
    FILE *fp = state.reader.unwrap(); /* FIXME: */
    long file_length = state.reader.length(); /* FIXME: */

    DTM_module m{};
    uint8_t magic[4];
    if(fread(magic, 1, 4, fp) < 4)
      return modutil::FORMAT_ERROR;

    if(memcmp(magic, "D.T.", 4))
      return modutil::FORMAT_ERROR;

    // This isn't really IFF, the "magic" is a chunk.
    fseek(fp, 0, SEEK_SET);

    num_dtm++;

    auto parser = DTM_parser;
    modutil::error err = parser.parse_iff(fp, file_length, m);
    if(err)
      return err;

    // FIXME: warn missing data

    format::line("Name",     "%s", m.name);
    if(m.version)
      format::line("Version","%d.%d", (int)m.version / 10, (int)m.version % 10);
    else
      format::line("Version","%s", m.pattern_format_version == format_v204 ? "2.04" :
                                   m.global_sample_depth > 0 ? "2.03" : "2.015");
    format::line("Speed",    "%d", m.initial_speed);
    if(m.version >= 19)
      format::line("Tempo",  "%.02f", ((double)m.initial_bpm_frac / 4294967296.0) + m.initial_bpm);
    else
      format::line("Tempo",    "%d", m.initial_bpm);
    if(m.loaded_SV19 && m.ticks_per_beat && m.initial_bpm)
    {
      format::line("perBeat","%d", m.ticks_per_beat);
      format::line("BPM",    "%" PRIu32, m.initial_bpm);
    }
    format::line("Orders",   "%d (%d)", m.num_orders, m.repeat_position);
    format::line("Patterns", "%d", m.num_patterns);
    format::line("Channels", "%d", m.num_channels);
    format::line("Instr.",   "%d", m.num_instruments);
    if(m.global_sample_rate > 0 && m.global_sample_depth > 0)
      format::line("InsConf.","%d-bit %" PRIu32 "Hz", m.global_sample_depth, m.global_sample_rate);
    else
    if(m.global_sample_rate > 0)
      format::line("InsConf.","%" PRIu32 "Hz", m.global_sample_rate);
    format::uses(m.uses, FEATURE_STR);

    // FIXME: comments

    if(Config.dump_samples)
    {
      namespace table = format::table;

      static constexpr const char *labels[] =
      {
        "Name", "Length", "LoopStart", "LoopLen", "Fmt", "Ch", "Freq.", "Fine", "Vol", "Note"
      };

      table::table<
        table::string<22>,
        table::spacer,
        table::number<10>,
        table::number<10>,
        table::number<10>,
        table::spacer,
        table::number<3>,
        table::number<2>,
        table::number<5>,
        table::number<4>,
        table::number<4>,
        table::string<4, encode::strip, table::RIGHT>> s_table;

      format::line();
      s_table.header("Sample", labels);

      for(size_t i = 0; i < m.instruments.size(); i++)
      {
        DTM_instrument &ins = m.instruments[i];
        if(ins.is_default() && !Config.dump_samples_extra)
          continue;

        char tmp[4];
        auto notefunc = [&tmp](uint16_t midi_note)
        {
          static constexpr const char notes[12][3] =
          {
            "C-", "C#", "D-", "D#", "E-", "F-",
            "F#", "G-", "G#", "A-", "A#", "B-",
          };
          if(midi_note < 120)
            snprintf(tmp, sizeof(tmp), "%s%d", notes[midi_note % 12], midi_note / 12);
          else
            strcpy(tmp, "???");
          return tmp;
        };

        s_table.row(i + 1, ins.name, {},
          ins.length, ins.loop_start, ins.loop_length, {},
          ins.sample_bits, ins.sample_stereo ? 2 : 1,
          ins.frequency, ins.finetune, ins.default_volume, notefunc(ins.midi_note));
      }
    }

    // FIXME: channels
    if(Config.dump_patterns)
    {
      format::line();
      format::orders("Orders", m.sequence, m.num_orders);

      if(!Config.dump_pattern_rows)
        format::line();

      for(size_t i = 0; i < m.num_patterns; i++)
      {
        DTM_pattern &p = m.patterns[i];

        if(m.pattern_format_version == format_mod)
        {
          using EVENT = format::event<format::periodMOD, format::sample, format::effect>;
          format::pattern<EVENT> pattern(i, m.num_channels, p.length);

          if(!Config.dump_pattern_rows)
          {
            pattern.summary();
            continue;
          }

          for(const DTM_event &ev : p.events)
          {
            format::periodMOD a{ ev.note };
            format::sample    b{ ev.instrument };
            format::effect    c{ ev.effect, ev.param };

            pattern.insert(EVENT(a, b, c));
          }
          pattern.print();
        }
        else
        {
          using EVENT = format::event<format::note, format::sample, format::volume, format::effectXM>;
          format::pattern<EVENT> pattern(i, m.num_channels, p.length);

          if(!Config.dump_pattern_rows)
          {
            pattern.summary();
            continue;
          }

          for(const DTM_event &ev : p.events)
          {
            format::note     a{ static_cast<uint8_t>(ev.note) };
            format::sample   b{ ev.instrument };
            format::volume   c{ ev.volume };
            format::effectXM d{ ev.effect, ev.param };

            pattern.insert(EVENT(a, b, c, d));
          }
          pattern.print();
        }
      }
    }
    return modutil::SUCCESS;
  }

  virtual void report() const override
  {
    if(!num_dtm)
      return;

    format::report("Total DTM", num_dtm);
  }
};

static const DTM_loader loader;
