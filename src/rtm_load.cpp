/**
 * Copyright (C) 2025 Lachesis <petrifiedrowan@gmail.com>
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

/* Real Tracker 2 RTM Loader. */

#include "error.hpp"
#include "modutil.hpp"

static int total_rtm = 0;


static constexpr size_t MAX_CHANNELS = 32;
static constexpr size_t MAX_ROWS = 999;

enum RTM_features
{
  FT_LINEAR_TABLES,
  FT_AMIGA_TABLES,
  FT_TRACK_NAMES,
  FT_FX_ARPEGGIO,
  FT_FX_PORTAMENTO_UP,
  FT_FX_PORTAMENTO_DOWN,
  FT_FX_TONE_PORTAMENTO,
  FT_FX_VIBRATO,
  FT_FX_TONE_PORTAMENTO_VOLSLIDE,
  FT_FX_VIBRATO_VOLSLIDE,
  FT_FX_TREMOLO,
  FT_FX_PAN,
  FT_FX_OFFSET,
  FT_FX_VOLSLIDE,
  FT_FX_JUMP,
  FT_FX_VOLUME,
  FT_FX_BREAK,
  FT_EX_0,
  FT_EX_FINE_PORTAMENTO_UP,
  FT_EX_FINE_PORTAMENTO_DOWN,
  FT_EX_GLISSANDO,
  FT_EX_VIBRATO_CONTROL,
  FT_EX_FINETUNE,
  FT_EX_LOOP,
  FT_EX_TREMOLO_CONTROL,
  FT_EX_PAN,
  FT_EX_RETRIG,
  FT_EX_FINE_VOLSLIDE_UP,
  FT_EX_FINE_VOLSLIDE_DOWN,
  FT_EX_NOTE_CUT,
  FT_EX_NOTE_DELAY,
  FT_EX_PATTERN_DELAY,
  FT_EX_F,
  FT_FX_TEMPO,
  FT_FX_GLOBAL_VOLUME,
  FT_FX_GLOBAL_VOLSLIDE,
  FT_FX_I,
  FT_FX_J,
  FT_FX_NOTE_CUT,
  FT_FX_ENVELOPE_POSITION,
  FT_FX_MIDI_CONTROLLER,
  FT_FX_N,
  FT_FX_O,
  FT_FX_PAN_SLIDE,
  FT_FX_Q,
  FT_FX_MULTI_RETRIG,
  FT_EX_HIGH_OFFSET,
  FT_EX_SXY,
  FT_FX_TREMOR,
  FT_FX_U,
  FT_FX_MIDI_CONTROLLER_VALUE,
  FT_FX_W,
  FT_FX_EXTRA_FINE_PORTAMENTO,
  FT_FX_Y,
  FT_FX_Z,
  FT_FX_S3M_VOLSLIDE,
  FT_FX_S3M_PORTAMENTO_UP,
  FT_FX_S3M_PORTAMENTO_DOWN,
  FT_FX_S3M_VIBRATO_VOLSLIDE,
  FT_FX_S3M_SPEED,
  FT_FX_OVER_40,
  NUM_FEATURES
};

static constexpr const char *FEATURE_STR[NUM_FEATURES] =
{
  "M:Linear",
  "M:Amiga",
  "M:TrackNames",
  "E:0xyArpeggio",
  "E:1xxPortaUp",
  "E:2xxPortaDn",
  "E:3xxToneporta",
  "E:4xyVibrato",
  "E:5xyPortaVol",
  "E:6xyVibratoVol",
  "E:7xyTremolo",
  "E:8xxPan",
  "E:9xxOffset",
  "E:AxyVolslide",
  "E:BxxJump",
  "E:CxxVolume",
  "E:DxxBreak",
  "E:E0x",
  "E:E1xFinePortaUp",
  "E:E2xFinePortaDn",
  "E:E3xGlissando",
  "E:E4xVibratoCtrl",
  "E:E5xFinetune",
  "E:E6xLoop",
  "E:E7xTremoloCtrl",
  "E:E8xPan",
  "E:E9xRetrig",
  "E:EAxFineVolUp",
  "E:EBxFineVolDn",
  "E:ECxNoteCut",
  "E:EDxNoteDelay",
  "E:EExPatternDelay",
  "E:EFx",
  "E:FxxTempo",
  "E:GxxGVolume",
  "E:HxxGVolslide",
  "E:Ixx",
  "E:Jxx",
  "E:KxxNoteCut",
  "E:LxxEnvPos",
  "E:MxxMIDICtrl",
  "E:Nxx",
  "E:Oxx",
  "E:PxxPanslide",
  "E:Qxx",
  "E:RxyMultiRetrig",
  "E:SAxHiOffset",
  "E:Sxy",
  "E:TxyTremor",
  "E:Uxx",
  "E:VxxMIDICtrlVal",
  "E:Wxx",
  "E:XxyExFinePorta",
  "E:Yxx",
  "E:Zxx",
  "E:dxyS3MVolslide",
  "E:fxxS3MPortaUp",
  "E:exxS3MPortaDn",
  "E:kxyS3MVibratoVol",
  "E:axxS3MSpeed",
  "E:>40",
};


static void RTM_effect_usage(bool (&uses)[NUM_FEATURES],
  uint8_t effect, uint8_t param) noexcept
{
  static constexpr const RTM_features fx[41] =
  {
    FT_FX_ARPEGGIO,
    FT_FX_PORTAMENTO_UP,
    FT_FX_PORTAMENTO_DOWN,
    FT_FX_TONE_PORTAMENTO,
    FT_FX_VIBRATO,
    FT_FX_TONE_PORTAMENTO_VOLSLIDE,
    FT_FX_S3M_VIBRATO_VOLSLIDE,
    FT_FX_TREMOLO,
    FT_FX_PAN,
    FT_FX_OFFSET,
    FT_FX_VOLSLIDE,
    FT_FX_JUMP,
    FT_FX_VOLUME,
    FT_FX_BREAK,
    NUM_FEATURES, /* Extended */
    FT_FX_TEMPO,
    FT_FX_GLOBAL_VOLUME,
    FT_FX_GLOBAL_VOLSLIDE,
    FT_FX_I,
    FT_FX_J,
    FT_FX_NOTE_CUT,
    FT_FX_ENVELOPE_POSITION,
    FT_FX_MIDI_CONTROLLER,
    FT_FX_N,
    FT_FX_O,
    FT_FX_PAN_SLIDE,
    FT_FX_Q,
    FT_FX_MULTI_RETRIG,
    NUM_FEATURES, /* Extended (IT) */
    FT_FX_TREMOR,
    FT_FX_U,
    FT_FX_MIDI_CONTROLLER_VALUE,
    FT_FX_W,
    FT_FX_EXTRA_FINE_PORTAMENTO,
    FT_FX_Y,
    FT_FX_Z,
    FT_FX_S3M_VOLSLIDE,
    FT_FX_S3M_PORTAMENTO_UP,
    FT_FX_S3M_PORTAMENTO_DOWN,
    FT_FX_S3M_VIBRATO_VOLSLIDE,
    FT_FX_S3M_SPEED
  };
  static constexpr const RTM_features ex[16] =
  {
    FT_EX_0,
    FT_EX_FINE_PORTAMENTO_UP,
    FT_EX_FINE_PORTAMENTO_DOWN,
    FT_EX_GLISSANDO,
    FT_EX_VIBRATO_CONTROL,
    FT_EX_FINETUNE,
    FT_EX_LOOP,
    FT_EX_TREMOLO_CONTROL,
    FT_EX_PAN,
    FT_EX_RETRIG,
    FT_EX_FINE_VOLSLIDE_UP,
    FT_EX_FINE_VOLSLIDE_DOWN,
    FT_EX_NOTE_CUT,
    FT_EX_NOTE_DELAY,
    FT_EX_PATTERN_DELAY,
    FT_EX_F,
  };
  if(effect > arraysize(fx))
  {
    uses[FT_FX_OVER_40] = true;
    return;
  }

  RTM_features which = fx[effect];
  switch(effect)
  {
  case 0x00: /* Arpeggio */
    if(param)
      uses[which] = true;
    break;

  case 0x0e: /* Extended */
    uses[ex[param >> 4]] = true;
    break;

  case 0x1c: /* Extended (IT) */
    if((param >> 4) == 0x0a)
      uses[FT_EX_HIGH_OFFSET] = true;
    else
      uses[FT_EX_SXY] = true;
    break;

  default:
    uses[which] = true;
    break;
  }
}


struct RTM_object_header
{
  static const size_t size = 42;

  /*  0 */ char id[4];
  /*  4 */ char rc;               /* 0x20 */
  /*  5 */ char name[32];
  /* 37 */ char eof;              /* 0x1a */
  /* 38 */ uint16_t version;
  /* 40 */ uint16_t header_size;
  /* 42 */

  modutil::error load(const char *expected_id, size_t minimum_size, vio &vf) noexcept
  {
    uint8_t buf[42];

    if(vf.read_buffer(buf) < sizeof(buf))
      return modutil::READ_ERROR;

    memcpy(id, buf + 0, 4);
    memcpy(name, buf + 5, 32);
    rc          = buf[4];
    eof         = buf[37];
    version     = mem_u16le(buf + 38);
    header_size = mem_u16le(buf + 40);

    if(memcmp(id, expected_id, 4))
      return modutil::INVALID;
    if(header_size < minimum_size)
      return modutil::BAD_VERSION;

    return modutil::SUCCESS;
  }
};


struct RTM_header
{
  enum
  {
    LINEAR_TABLE        = (1 << 0),
    TRACK_NAMES_PRESENT = (1 << 1),
  };

  RTM_object_header obj;
  /*   0 */ char      tracker[20];
  /*  20 */ char      author[32];
  /*  52 */ uint16_t  flags;
  /*  54 */ uint8_t   num_channels;
  /*  55 */ uint8_t   num_instruments;
  /*  56 */ uint16_t  num_orders;
  /*  58 */ uint16_t  num_patterns;
  /*  60 */ uint8_t   initial_speed;
  /*  61 */ uint8_t   initial_tempo;
  /*  62 */ int8_t    initial_panning[32];
  /*  94 */ uint32_t  extra_data_length;
  /*  98 */ char      original_name[32];
  /* 130 */

  std::vector<uint16_t> orders;
  char track_names[MAX_CHANNELS][16];

  modutil::error load(vio &vf)
  {
    uint8_t buf[130]{};
    size_t read_wanted;
    size_t ext_read;
    modutil::error ret;

    ret = obj.load("RTMM", 98, vf);
    if(ret == modutil::INVALID)
      return modutil::FORMAT_ERROR;

    read_wanted = MIN((size_t)obj.header_size, sizeof(buf));
    if(ret || vf.read(buf, read_wanted) < read_wanted)
    {
      format::error("read error in RTM header");
      return ret;
    }

    memcpy(tracker,         buf + 0,  20);
    memcpy(author,          buf + 20, 32);
    memcpy(initial_panning, buf + 62, 32);
    memcpy(original_name,   buf + 98, 32);

    flags             = mem_u16le(buf + 52);
    num_channels      = buf[54];
    num_instruments   = buf[55];
    num_orders        = mem_u16le(buf + 56);
    num_patterns      = mem_u16le(buf + 58);
    initial_speed     = buf[60];
    initial_tempo     = buf[61];
    extra_data_length = mem_u32le(buf + 94);

    if(num_channels > MAX_CHANNELS)
    {
      format::error("invalid channel count %u", num_channels);
      return modutil::INVALID;
    }

    if(obj.header_size > 130)
    {
      if(vf.seek(obj.header_size - 130, SEEK_CUR) < 0)
      {
        format::error("seek error in RTM header");
        return modutil::SEEK_ERROR;
      }
    }

    orders.resize(num_orders);
    for(size_t i = 0; i < num_orders; )
    {
      if(vf.eof())
        break;

      size_t n = MIN(num_orders - i, (size_t)64);
      size_t num_in = vf.read(buf, n * 2);
      if(num_in < n * 2)
      {
        format::warning("read error in order list");
        memset(buf + num_in, 0, n * 2 - num_in);
      }
      for(size_t j = 0; j < n; j++)
        orders[i++] = mem_u16le(buf + j * 2);
    }
    ext_read = num_orders * 2;

    if(flags & TRACK_NAMES_PRESENT)
    {
      for(size_t i = 0; i < num_channels; i++)
      {
        if(vf.eof())
          break;

        size_t num_in = vf.read_buffer(track_names[i]);
        if(num_in < sizeof(track_names[i]))
        {
          format::warning("read error in track names %zu", i);
          memset(track_names[i] + num_in, 0, sizeof(track_names[i]) - num_in);
          break;
        }
      }
      ext_read += num_channels * 16;
    }

    if(extra_data_length != ext_read)
      format::warning("extra data length mismatch! expected %" PRIu32 ", got %zu",
        extra_data_length, ext_read);

    return modutil::SUCCESS;
  }
};


struct RTM_event
{
  enum
  {
    NEXT_ROW    = 0,
    TRACK       = (1 << 0),
    NOTE        = (1 << 1),
    INSTRUMENT  = (1 << 2),
    COMMAND_1   = (1 << 3),
    PARAM_1     = (1 << 4),
    COMMAND_2   = (1 << 5),
    PARAM_2     = (1 << 6),
  };

  /* Only the note uses -1 for empty for some reason;
   * instruments/samples are numbered normally. */
  uint8_t note = 0xff;
  uint8_t instrument;
  uint8_t command_1;
  uint8_t param_1;
  uint8_t command_2;
  uint8_t param_2;

  void usage(bool (&uses)[NUM_FEATURES]) const noexcept
  {
    RTM_effect_usage(uses, command_1, param_1);
    RTM_effect_usage(uses, command_2, param_2);
  }
};

struct RTM_pattern
{
  RTM_object_header obj;
  /*   0 */ uint16_t  flags; /* "always 1" */
  /*   2 */ uint8_t   num_channels;
  /*   3 */ uint16_t  num_rows;
  /*   5 */ uint32_t  data_size;
  /*   9 */

  std::vector<RTM_event> events;

  modutil::error load(size_t i, std::vector<uint8_t> &patbuf, vio &vf)
  {
    modutil::error ret;
    uint8_t buf[9];

    ret = obj.load("RTND", 9, vf);
    if(ret)
    {
      format::warning("error loading pattern %zu object header", i);
      return ret;
    }

    if(vf.read_buffer(buf) < sizeof(buf))
    {
      format::warning("read error in pattern %zu", i);
      return modutil::READ_ERROR;
    }

    flags         = mem_u16le(buf + 0);
    num_channels  = buf[2];
    num_rows      = mem_u16le(buf + 3);
    data_size     = mem_u32le(buf + 5);

    size_t bound = MIN((size_t)num_rows, MAX_ROWS) *
                   MIN((size_t)num_channels, MAX_CHANNELS) * 8;
    if(num_rows > MAX_ROWS || num_channels > MAX_CHANNELS || data_size > bound)
    {
      format::warning("invalid pattern %zu data: r:%d c:%d ds:%" PRIu32,
        i, num_rows, num_channels, data_size);

      /* Attempt to skip to next pattern. */
      vf.seek(data_size, SEEK_CUR);
      return modutil::INVALID;
    }
    patbuf.resize(data_size);

    size_t num_in = vf.read(patbuf.data(), data_size);
    if(num_in < data_size)
    {
      /* Recover broken pattern by zeroing missing portion. */
      format::warning("read error in pattern %zu", i);
      memset(patbuf.data() + num_in, 0, data_size - num_in);
    }

    events.resize((size_t)num_channels * num_rows);
    RTM_event dummy;
    uint8_t *pos = patbuf.data();
    uint8_t *end = pos + data_size;
    unsigned row = 0;
    unsigned chn = 0;
    while(pos < end && row < num_rows)
    {
      uint8_t v = *(pos++);
      if(v == RTM_event::NEXT_ROW)
      {
        chn = 0;
        row++;
        continue;
      }
      if((v & RTM_event::TRACK) && pos < end)
        chn = *(pos++);

      RTM_event &dest = (chn < num_channels) ? events[row * num_channels + chn] : dummy;
      if((v & RTM_event::NOTE) && pos < end)
        dest.note = *(pos++);
      if((v & RTM_event::INSTRUMENT) && pos < end)
        dest.instrument = *(pos++);
      if((v & RTM_event::COMMAND_1) && pos < end)
        dest.command_1 = *(pos++);
      if((v & RTM_event::PARAM_1) && pos < end)
        dest.param_1 = *(pos++);
      if((v & RTM_event::COMMAND_2) && pos < end)
        dest.command_2 = *(pos++);
      if((v & RTM_event::PARAM_2) && pos < end)
        dest.param_2 = *(pos++);

      chn++;
    }

    return modutil::SUCCESS;
  }
};


struct RTM_sample
{
  RTM_object_header obj;
  /*   0 */ uint16_t  flags;
  /*   2 */ uint8_t   global_volume;
  /*   3 */ uint8_t   default_volume;
  /*   4 */ uint32_t  length_bytes;
  /*   8 */ uint8_t   loop_mode;
  /*   9 */ uint8_t   unused[3];
  /*  12 */ uint32_t  loop_start_bytes;
  /*  16 */ uint32_t  loop_end_bytes;
  /*  20 */ uint32_t  base_frequency;
  /*  24 */ uint8_t   base_note;
  /*  25 */ int8_t    default_panning;
  /*  26 */

  modutil::error load(size_t ins_num, size_t sample_num, vio &vf)
  {
    modutil::error ret;
    uint8_t buf[26];

    ret = obj.load("RTSM", 0, vf);
    if(ret)
    {
      format::warning("error loading instrument %zu sample %zu object header",
        ins_num, sample_num);
      return ret;
    }

    size_t num_to_read = MIN((size_t)obj.header_size, sizeof(buf));
    size_t num_in = vf.read(buf, num_to_read);
    if(num_in < num_to_read)
      format::warning("read error in instrument %zu sample %zu", ins_num, sample_num);
    if(num_in < sizeof(buf))
      memset(buf + num_in, 0, sizeof(buf) - num_in);

    memcpy(unused, buf + 9, 3);

    flags             = mem_u16le(buf + 0);
    global_volume     = buf[2];
    default_volume    = buf[3];
    length_bytes      = mem_u32le(buf + 4);
    loop_mode         = buf[8];
    loop_start_bytes  = mem_u32le(buf + 12);
    loop_end_bytes    = mem_u32le(buf + 16);
    base_frequency    = mem_u32le(buf + 20);
    base_note         = buf[24];
    default_panning   = static_cast<int8_t>(buf[25]);

    if(vf.seek(length_bytes, SEEK_CUR) < 0)
    {
      format::warning("seek error in instrument %zu sample %zu", ins_num, sample_num);
      return modutil::SEEK_ERROR;
    }
    return modutil::SUCCESS;
  }
};

struct RTM_point
{
  /*  0 */ int32_t x;
  /*  4 */ int32_t y;
  /*  8 */

  template<size_t pos>
  constexpr void load(const uint8_t (&buf)[341])
  {
    x = mem_s32le(buf + pos + 0);
    y = mem_s32le(buf + pos + 4);
  }
};

struct RTM_envelope
{
  enum
  {
    ENVELOPE_ENABLED,
    SUSTAIN_ENABLED,
    LOOP_ENABLED
  };

  /* Embedded into RTM_instrument. */
  /*   0 */ uint8_t   num_points;
  /*   1 */ RTM_point points[12];
  /*  97 */ uint8_t   sustain_point;
  /*  98 */ uint8_t   loop_start;
  /*  99 */ uint8_t   loop_end;
  /* 100 */ uint16_t  flags;
  /* 102 */

  template<size_t pos>
  constexpr void load(const uint8_t (&buf)[341])
  {
    num_points    = buf[pos + 0];
    sustain_point = buf[pos + 97];
    loop_start    = buf[pos + 98];
    loop_end      = buf[pos + 99];
    flags         = mem_u16le(buf + pos + 100);

    points[0].load<pos + 1>(buf);
    points[1].load<pos + 9>(buf);
    points[2].load<pos + 17>(buf);
    points[3].load<pos + 25>(buf);
    points[4].load<pos + 33>(buf);
    points[5].load<pos + 41>(buf);
    points[6].load<pos + 49>(buf);
    points[7].load<pos + 57>(buf);
    points[8].load<pos + 65>(buf);
    points[9].load<pos + 73>(buf);
    points[10].load<pos + 81>(buf);
    points[11].load<pos + 89>(buf);
  }
};

struct RTM_instrument
{
  enum
  {
    DEFAULT_PAN_ENABLED,
    MUTE_SAMPLES,
  };

  RTM_object_header obj;
  /*   0 */ uint8_t       num_samples;
  /*   1 */ uint16_t      flags;
  /*   3 */ uint8_t       keymap[120];
  /* 123 */ RTM_envelope  volume_envelope;
  /* 225 */ RTM_envelope  panning_envelope;
  /* 327 */ int8_t        vibrato_type;
  /* 328 */ int8_t        vibrato_sweep;
  /* 329 */ int8_t        vibrato_depth;
  /* 330 */ int8_t        vibrato_rate;
  /* 331 */ uint16_t      fade_out;
  /* 333 */ uint8_t       midi_port;
  /* 334 */ uint8_t       midi_channel;
  /* 335 */ uint8_t       midi_program;
  /* 336 */ uint8_t       midi_enable;
  /* 337 */ int8_t        midi_transpose;
  /* 338 */ uint8_t       midi_bend_range;
  /* 339 */ uint8_t       midi_base_volume;
  /* 340 */ int8_t        midi_use_velocity;
  /* 341 */

  std::vector<RTM_sample>     samples;

  modutil::error load(size_t i, vio &vf)
  {
    modutil::error ret;
    uint8_t buf[341];

    ret = obj.load("RTIN", 0, vf);
    if(ret)
    {
      format::warning("error loading instrument %zu object header", i);
      return ret;
    }

    size_t num_to_read = MIN((size_t)obj.header_size, sizeof(buf));
    size_t num_in = vf.read(buf, num_to_read);
    if(num_in < num_to_read)
      format::warning("read error in instrument %zu header", i);
    if(num_in < sizeof(buf))
      memset(buf + num_in, 0, sizeof(buf) - num_in);

    memcpy(keymap, buf + 3, 120);
    volume_envelope.load<123>(buf);
    panning_envelope.load<225>(buf);

    num_samples       = buf[0];
    flags             = mem_u16le(buf + 1);
    vibrato_type      = static_cast<int8_t>(buf[327]);
    vibrato_sweep     = static_cast<int8_t>(buf[328]);
    vibrato_depth     = static_cast<int8_t>(buf[329]);
    vibrato_rate      = static_cast<int8_t>(buf[330]);
    fade_out          = mem_u16le(buf + 331);
    midi_port         = buf[333];
    midi_channel      = buf[334];
    midi_program      = buf[335];
    midi_enable       = buf[336];
    midi_transpose    = static_cast<int8_t>(buf[337]);
    midi_bend_range   = buf[338];
    midi_base_volume  = buf[339];
    midi_use_velocity = static_cast<int8_t>(buf[340]);

    samples.resize(num_samples);

    for(size_t j = 0; j < num_samples; j++)
    {
      ret = samples[j].load(i, j, vf);
      if(ret)
        return ret;
    }
    return modutil::SUCCESS;
  }
};


struct RTM_data
{
  RTM_header                  header;
  std::vector<RTM_pattern>    patterns;
  std::vector<RTM_instrument> instruments;

  size_t num_samples;
  bool uses[NUM_FEATURES];
};


class RTM_loader : public modutil::loader
{
public:
  RTM_loader(): modutil::loader("RTM", "rtm", "Real Tracker") {}

  modutil::error load(modutil::data state) const override
  {
    vio &vf = state.reader;

    RTM_data m{};
    RTM_header &h = m.header;
    modutil::error ret;

    ret = h.load(vf);
    if(ret != modutil::FORMAT_ERROR)
      total_rtm++;
    if(ret)
      return ret;

    if(h.flags & RTM_header::LINEAR_TABLE)
      m.uses[FT_LINEAR_TABLES] = true;
    else
      m.uses[FT_AMIGA_TABLES] = true;

    if(h.flags & RTM_header::TRACK_NAMES_PRESENT)
      m.uses[FT_TRACK_NAMES] = true;

    /* Format doc explicitly states to seek to this position to continue. */
    int64_t offset = RTM_object_header::size + h.obj.header_size + h.extra_data_length;
    if(vf.seek(offset, SEEK_SET) < 0)
    {
      format::error("seek error seeking to end of header data");
      return modutil::SEEK_ERROR;
    }

    /* Patterns */
    {
      std::vector<uint8_t> patbuf;

      m.patterns.resize(h.num_patterns);
      for(size_t i = 0; i < h.num_patterns; i++)
      {
        if(vf.eof())
          break;

        RTM_pattern &p = m.patterns[i];
        ret = p.load(i, patbuf, vf);
        if(ret)
          break;

        for(const RTM_event &ev : p.events)
          ev.usage(m.uses);
      }
    }

    /* Instruments */
    m.instruments.resize(h.num_instruments);
    for(size_t i = 0; i < h.num_instruments; i++)
    {
      if(vf.eof())
        break;

      RTM_instrument &ins = m.instruments[i];
      ret = ins.load(i, vf);
      if(ret)
        break;

      m.num_samples += ins.num_samples;
    }

    /* Print information. */

    format::line("Name",    "%-32.32s", h.obj.name);
    format::line("Author",  "%-32.32s", h.author);
    format::line("Tracker", "%-20.20s", h.tracker);
    format::line("Type",    "RTMM %d.%02x",
                            h.obj.version >> 8, h.obj.version & 0xff);
    format::line("Tracks",  "%u", h.num_channels);
    format::line("Instr",   "%u", h.num_instruments);
    format::line("Samples", "%zu", m.num_samples);
    format::line("Patterns","%u", h.num_patterns);
    format::line("Orders",  "%u", h.num_orders);
    format::line("Tempo",   "%u", h.initial_tempo);
    format::line("Speed",   "%u", h.initial_speed);
    format::line("RTMMSize","%" PRIu32, h.obj.header_size);
    if(h.extra_data_length > 0)
      format::line("ExtSize","%" PRIu32, h.extra_data_length);

    format::uses(m.uses, FEATURE_STR);

    if(Config.dump_samples)
    {
      namespace table = format::table;

      // TODO print envelopes.
      static constexpr const char *i_labels[] =
      {
        "Name", "Ver", "HSize",
        "#Sm", "Flg", "#VPt", "#PPt", "Fade",
        "VTp", "VSw", "VDe", "VRt"
      };

      static constexpr const char *m_labels[] =
      {
        "On?", "Port", "Chn", "Prg", "Trs", "Bnd", "Vol", "Vel"
      };

      static constexpr const char *s_labels[] =
      {
        "Name", "Ver", "HSize", "Ins",
        "Length", "LoopStart", "LoopEnd",
        "Flg", "L", "GVo", "Vol", "Pan", "Freq", "Note"
      };

      table::table<
        table::string<32>,
        table::number<3, table::HEX>,
        table::number<5>,
        table::spacer,
        table::number<3>,
        table::number<4, table::RIGHT|table::HEX|table::ZEROS>,
        table::number<3>,
        table::number<3>,
        table::number<5>,
        table::spacer,
        table::number<4>,
        table::number<4>,
        table::number<4>,
        table::number<4>> i_table;

      table::table<
        table::number<3>,
        table::number<4>,
        table::number<3>,
        table::number<3>,
        table::number<3>,
        table::number<3>,
        table::number<3>,
        table::number<4>> m_table;

      table::table<
        table::string<32>,
        table::number<3, table::HEX>,
        table::number<5>,
        table::number<4, table::RIGHT|table::HEX>,
        table::spacer,
        table::number<10>,
        table::number<10>,
        table::number<10>,
        table::spacer,
        table::number<4, table::RIGHT|table::HEX|table::ZEROS>,
        table::number<2, table::RIGHT|table::HEX|table::ZEROS>,
        table::number<3>,
        table::number<3>,
        table::number<4>,
        table::number<10>,
        table::number<4>> s_table;

      if(h.num_instruments)
      {
        format::line();
        i_table.header("Instr.", i_labels);

        size_t i = 1;
        for(const RTM_instrument &ins : m.instruments)
        {
          i_table.row(i, ins.obj.name, ins.obj.version, ins.obj.header_size, {},
            ins.num_samples, ins.flags, ins.volume_envelope.num_points,
            ins.panning_envelope.num_points, ins.fade_out, {},
            ins.vibrato_type, ins.vibrato_sweep, ins.vibrato_depth, ins.vibrato_rate);
          i++;
        }
      }

      if(Config.dump_samples_extra)
      {
        format::line();
        m_table.header("Ins.MIDI", m_labels);

        size_t i = 1;
        for(const RTM_instrument &ins : m.instruments)
        {
          m_table.row(i, ins.midi_enable, ins.midi_port, ins.midi_channel,
            ins.midi_program, ins.midi_transpose, ins.midi_bend_range,
            ins.midi_base_volume, ins.midi_use_velocity);
          i++;
        }
      }

      if(m.num_samples)
      {
        format::line();
        s_table.header("Samples", s_labels);

        size_t i = 1;
        size_t smp = 1;
        for(const RTM_instrument &ins : m.instruments)
        {
          for(const RTM_sample &s : ins.samples)
          {
            s_table.row(smp, s.obj.name, s.obj.version, s.obj.header_size, i, {},
              s.length_bytes, s.loop_start_bytes, s.loop_end_bytes, {},
              s.flags, s.loop_mode, s.global_volume, s.default_volume,
              s.default_panning, s.base_frequency, s.base_note);
            smp++;
          }
          i++;
        }
      }
    }

    if(Config.dump_patterns)
    {
      format::line();
      format::orders("Orders", h.orders.data(), h.num_orders);

      if(!Config.dump_pattern_rows)
        format::line();

      /* Module may include track names. */
      const char *column_labels[MAX_CHANNELS]{};
      bool has_labels = false;

      if(h.flags & RTM_header::TRACK_NAMES_PRESENT)
      {
        for(size_t i = 0; i < h.num_channels; i++)
          column_labels[i] = h.track_names[i];

        has_labels = true;
      }

      /* RTM is XM with some special-case values after Zxx. */
      struct effectRTM
      {
        uint8_t effect;
        uint8_t param;
        static constexpr int width() { return 4; }
        bool can_print() const { return effect > 0 || param > 0; }
        char effect_char() const { return (effect < 10) ? effect + '0' :
                                          (effect < 36) ? effect - 10 + 'A' :
                                          (effect == 36) ? 'd' :
                                          (effect == 37) ? 'f' :
                                          (effect == 38) ? 'e' :
                                          (effect == 39) ? 'k' :
                                          (effect == 40) ? 'a' : '?'; }
        void print() const
        {
          if(can_print())
          {
            fprintf(stderr, HIGHLIGHT_FX("%c%02x", effect, param),
              effect_char(), param);
          }
          else
            format::spaces(width());
        }
      };

      for(size_t i = 0; i < h.num_patterns; i++)
      {
        RTM_pattern &p = m.patterns[i];

        using EVENT = format::event<format::note<255>, format::sample<>,
                                    effectRTM, effectRTM>;
        format::pattern<EVENT> pattern(p.obj.name,
          i, h.num_channels, p.num_rows, p.data_size);

        if(!Config.dump_pattern_rows)
        {
          pattern.summary();
          continue;
        }

        for(const RTM_event &ev : p.events)
        {
          format::note<255> a{ ev.note };
          format::sample<>  b{ ev.instrument };
          /* documentation lies about the order;
           * bits 3/4 display on the right, 5/6 display on the left */
          effectRTM         c{ ev.command_2, ev.param_2 };
          effectRTM         d{ ev.command_1, ev.param_1 };

          pattern.insert(EVENT(a, b, c, d));
        }
        pattern.print(has_labels ? column_labels : nullptr);
      }
    }

    return modutil::SUCCESS;
  }

  void report() const override
  {
    if(!total_rtm)
      return;

    format::report("Total Real Tracker", total_rtm);
  }
};

static const RTM_loader loader;
