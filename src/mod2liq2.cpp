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

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define ERROR(...) do { \
  fprintf(stderr, "ERROR: " __VA_ARGS__); \
  fprintf(stderr, "\n"); \
  fflush(stderr); \
} while(0)

enum mod_effect
{
  MOD_ARPEGGIO,
  MOD_PORTA_UP,
  MOD_PORTA_DN,
  MOD_TONEPORTA,
  MOD_VIBRATO,
  MOD_TONEPORTA_VOLSLIDE,
  MOD_VIBRATO_VOLSLIDE,
  MOD_TREMOLO,
  MOD_PAN_CONTROL,
  MOD_OFFSET,
  MOD_VOLUME_SLIDE,
  MOD_JUMP,
  MOD_VOLUME,
  MOD_BREAK,
  MOD_EXTENDED,
  MOD_SPEED_BPM
};

enum mod_extended
{
  MOD_E0_FILTER,
  MOD_E1_FINE_PORTA_UP,
  MOD_E2_FINE_PORTA_DN,
  MOD_E3_GLISSANDO,
  MOD_E4_VIBRATO_WAVEFORM,
  MOD_E5_SET_FINETUNE,
  MOD_E6_PATTERN_LOOP,
  MOD_E7_TREMOLO_WAVEFORM,
  MOD_E8_PAN_CONTROL,
  MOD_E9_RETRIGGER,
  MOD_EA_FINE_VOLUME_UP,
  MOD_EB_FINE_VOLUME_DN,
  MOD_EC_NOTE_CUT,
  MOD_ED_NOTE_DELAY,
  MOD_EE_PATTERN_DELAY,
  MOD_EF_INVERT_LOOP
};

enum no_effect
{
  NO_SPEED_BPM,
  NO_VIBRATO,
  NO_CUT,
  NO_PORTA_DN,
  NO_PORTA_UP,
  NO_GLOBAL_VOLUME, /* citation needed */
  NO_ARPEGGIO,
  NO_PAN_CONTROL,
  NO_MISC_1,
  NO_JUMP,
  NO_TREMOLO,
  NO_VOLUME_SLIDE,
  NO_MISC_2,
  NO_NOTEPORTA,
  NO_OFFSET,
  NO_NO_EFFECT
};

enum no_misc1
{
  NO_I0_VIBRATO_VOLSLIDE_UP,
  NO_I1_VIBRATO_VOLSLIDE_DN,
  NO_I2_NOTEPORTA_VOLSLIDE_UP,
  NO_I3_NOTEPORTA_VOLSLIDE_DN,
  NO_I4_TREMOLO_VOLSLIDE_UP,
  NO_I5_TREMOLO_VOLSLIDE_DN,
};

enum no_misc2
{
  NO_M0_FINE_PORTA_UP,
  NO_M1_FINE_PORTA_DN,
  NO_M2_FINE_VOLSLIDE_UP,
  NO_M3_FINE_VOLSLIDE_DN,
  NO_M4_VIBRATO_WAVEFORM,
  NO_M5_TREMOLO_WAVEFORM,
  NO_M6_RETRIGGER,
  NO_M7_NOTE_CUT,
  NO_M8_NOTE_DELAY,
  NO_M9_UNUSED,
  NO_MA_UNUSED,
  NO_MB_PATTERN_LOOP,
  NO_MC_PATTERN_DELAY,
  NO_MD_UNUSED,
  NO_ME_UNUSED,
  NO_MF_UNUSED
};

#define EXTENDED(ex,param) ((((ex) & 0x0f) << 4) | ((param) & 0x0f))

struct mod_instrument
{
  uint8_t name[22];
  uint16_t length_half;
  uint8_t finetune;
  uint8_t volume;
  uint16_t loopstart_half;
  uint16_t looplength_half;
};

struct mod_header
{
  uint8_t name[20];
  struct mod_instrument ins[31];
  uint8_t length;
  uint8_t restart;
  uint8_t order[128];
  uint8_t magic[4];

  /* Derived */
  uint8_t num_patterns;
  uint8_t num_channels;
  size_t  sample_bytes_total;
};

struct no_instrument
{
  uint8_t nlen;
  uint8_t name[30];
  uint8_t volume;
  uint16_t c2_freq;
  uint32_t length;
  uint32_t loopstart;
  uint32_t loopend;
};

struct no_header
{
  uint8_t magic[4]; /* "NO\x00\x00" */
  uint8_t nlen;
  uint8_t name[29];
  uint8_t num_patterns;
  uint8_t unknown_ff;
  uint8_t num_channels;
  uint8_t unknown[6];
  uint8_t order[256];
  struct no_instrument ins[63];
};

static uint16_t read_u16be(const uint8_t *data)
{
  return (data[0] << 8) | data[1];
}

static void write_u16le(uint8_t *data, uint16_t value)
{
  data[0] = value & 0xff;
  data[1] = value >> 8;
}

static void write_u32le(uint8_t *data, uint32_t value)
{
  data[0] = value & 0xff;
  data[1] = value >> 8;
  data[2] = value >> 16;
  data[3] = value >> 24;
}

template<int N>
static size_t mod_strlen(const uint8_t (&buf)[N])
{
  size_t len;
  for(len = 0; len < N; len++)
    if(buf[len] == '\0')
      break;
  for(; len > 0; len--)
    if(buf[len] != ' ')
      break;
  return len;
}

static bool load_mod_header(struct mod_header &mod, FILE *in)
{
  uint8_t buf[1084];
  int tmp;
  int i;

  if(fread(buf, 1, sizeof(buf), in) < sizeof(buf))
  {
    ERROR("read error on input");
    return false;
  }
  mod.num_patterns = 0;
  mod.num_channels = 0;
  mod.sample_bytes_total = 0;

  memcpy(mod.magic, buf + 1080, 4);
  tmp = (mod.magic[0] - '0') * 10 + (mod.magic[1] - '0');

  if(!memcmp(mod.magic, "M.K.", 4) || !memcmp(mod.magic, "M!K!", 4) ||
     !memcmp(mod.magic, "M&K!", 4))
  {
    mod.num_channels = 4;
  }
  else

  if(!memcmp(mod.magic + 1, "CHN", 3) && mod.magic[0] >= '1' && mod.magic[0] <= '9')
  {
    mod.num_channels = mod.magic[0] - '0';
  }
  else

  if(!memcmp(mod.magic + 2, "CH", 2) && tmp >= 10 && tmp <= 99)
  {
    mod.num_channels = tmp;
  }
  else

  if(!memcmp(mod.magic, "TDZ", 3) && mod.magic[3] >= '1' && mod.magic[3] <= '3')
  {
    mod.num_channels = mod.magic[3] - '0';
  }

  if(mod.num_channels == 0)
  {
    ERROR("unsupported MOD variant or not a MOD");
    return false;
  }

  memcpy(mod.name, buf, 20);
  mod.length = buf[950];
  mod.restart = buf[951];
  memcpy(mod.order, buf + 952, 128);

  /* Parse samples */
  const uint8_t *pos = buf + 20;
  for(i = 0; i < 31; i++)
  {
    struct mod_instrument &ins = mod.ins[i];
    memcpy(ins.name, pos, 22);
    ins.length_half = read_u16be(pos + 22);
    ins.finetune = pos[24];
    ins.volume = pos[25];
    ins.loopstart_half = read_u16be(pos + 26);
    ins.looplength_half = read_u16be(pos + 28);
    pos += 30;

    mod.sample_bytes_total += ins.length_half * 2;
  }

  /* Get pattern count */
  for(i = 0; i < 128; i++)
  {
    if(mod.order[i] < 0x80 && mod.order[i] >= mod.num_patterns)
      mod.num_patterns = mod.order[i] + 1;
  }
  return true;
}

static void convert_mod_instrument(struct no_instrument &no_ins, const struct mod_instrument &mod_ins)
{
  int fine = (int8_t)((mod_ins.finetune) << 4) >> 4;

  no_ins.nlen = mod_strlen(mod_ins.name);
  if(no_ins.nlen)
    memcpy(no_ins.name, mod_ins.name, no_ins.nlen);

  no_ins.volume = mod_ins.volume;
  /* Note: not clear how accurate this is, as later versions have a finetune effect. */
  //no_ins.c2_freq = 8363;
  no_ins.c2_freq = (int)(8363.0f * powf(2.0f, fine / (8.0f * 12.0f)));
  no_ins.length = mod_ins.length_half << 1;
  if(mod_ins.looplength_half > 1)
  {
    no_ins.loopstart = mod_ins.loopstart_half << 1;
    no_ins.loopend = (mod_ins.looplength_half << 1) + no_ins.loopstart;
  }
  else
    no_ins.loopstart = no_ins.loopend = 0;
}

static void default_no_instrument(struct no_instrument &no_ins)
{
  no_ins.nlen = 0;
  no_ins.volume = 64;
  no_ins.c2_freq = 8363;
  no_ins.length = 0;
  no_ins.loopstart = 0;
  no_ins.loopend = 0;
}

static bool convert_mod_header(struct no_header &no, const struct mod_header &mod)
{
  size_t i;

  if(mod.num_channels > 16)
  {
    ERROR("Liquid Module NO supports 16 channels maximum; input has %d", mod.num_channels);
    return false;
  }
  memset(&no, 0, sizeof(struct no_header));

  memcpy(no.magic, "NO\0", 4);
  no.nlen = mod_strlen(mod.name);
  if(no.nlen)
    memcpy(no.name, mod.name, no.nlen);
  no.num_patterns = mod.num_patterns;
  no.unknown_ff = 0xff;
  no.num_channels = mod.num_channels;
  memcpy(no.order, mod.order, mod.length);
  memset(no.order + mod.length, 0xff, 256 - mod.length);

  /* Instruments */
  for(i = 0; i < 31; i++)
    convert_mod_instrument(no.ins[i], mod.ins[i]);
  for(; i < 63; i++)
    default_no_instrument(no.ins[i]);

  return true;
}

static bool write_no_header(const struct no_header &no, FILE *out)
{
  uint8_t buf[0xC7D];
  memcpy(buf, no.magic, 4);
  buf[4] = no.nlen;
  memcpy(buf + 5, no.name, 29);
  buf[34] = no.num_patterns;
  buf[35] = no.unknown_ff;
  buf[36] = no.num_channels;
  memcpy(buf + 37, no.unknown, 6);
  memcpy(buf + 43, no.order, 256);

  uint8_t *pos = buf + 0x12B;
  for(int i = 0; i < 63; i++)
  {
    const struct no_instrument &ins = no.ins[i];
    pos[0] = ins.nlen;
    memcpy(pos + 1, ins.name, 30);
    pos[31] = ins.volume;
    write_u16le(pos + 32, ins.c2_freq);
    write_u32le(pos + 34, ins.length);
    write_u32le(pos + 38, ins.loopstart);
    write_u32le(pos + 42, ins.loopend);
    pos += 46;
  }
  if(pos - buf != 0xC7D)
  {
    ERROR("internal error");
    return false;
  }

  if(fwrite(buf, 1, sizeof(buf), out) < sizeof(buf))
  {
    ERROR("write error in header");
    return false;
  }
  return true;
}

static void convert_mod_event(uint8_t *event)
{
  int period;
  int ins;
  int effect;
  int param;
  int note = -1;
  int volume = -1;

  period = ((event[0] & 0x0f) << 8) | event[1];
  ins = (event[0] & 0xf0) | ((event[2] & 0xf0) >> 4);
  effect = event[2] & 0x0f;
  param = event[3];

  /* Convert note */
  if(period)
    note = (int)roundf(12.0f * logf(13696.0f / period) / M_LN2) - 36;

  /* Convert ins */
  ins--;

  /* Convert effect */
  switch(effect)
  {
  case MOD_ARPEGGIO:
    if(param)
      effect = NO_ARPEGGIO;
    else
      effect = param = -1;
    break;
  case MOD_PORTA_UP:
    effect = NO_PORTA_UP;
    break;
  case MOD_PORTA_DN:
    effect = NO_PORTA_DN;
    break;
  case MOD_TONEPORTA:
    effect = NO_NOTEPORTA;
    break;
  case MOD_VIBRATO:
    effect = NO_VIBRATO;
    break;
  case MOD_TONEPORTA_VOLSLIDE:
    effect = NO_MISC_1;
    if(param & 0x0f)
      param = EXTENDED(NO_I3_NOTEPORTA_VOLSLIDE_DN, param);
    else
      param = EXTENDED(NO_I2_NOTEPORTA_VOLSLIDE_UP, param >> 4);
    break;
  case MOD_VIBRATO_VOLSLIDE:
    effect = NO_MISC_1;
    if(param & 0x0f)
      param = EXTENDED(NO_I1_VIBRATO_VOLSLIDE_DN, param);
    else
      param = EXTENDED(NO_I0_VIBRATO_VOLSLIDE_UP, param >> 4);
    break;
  case MOD_TREMOLO:
    effect = NO_TREMOLO;
    break;
  case MOD_PAN_CONTROL:
    effect = NO_PAN_CONTROL;
    param = param * 64 / 255;
    param = (param / 10 * 16) + (param % 10);
    break;
  case MOD_OFFSET:
    effect = NO_OFFSET;
    break;
  case MOD_VOLUME_SLIDE:
    effect = NO_VOLUME_SLIDE;
    break;
  case MOD_JUMP:
    effect = NO_JUMP;
    break;
  case MOD_VOLUME:
    volume = param;
    effect = -1;
    param = -1;
    break;
  case MOD_BREAK:
    effect = NO_CUT;
    break;
  case MOD_EXTENDED:
    effect = NO_MISC_2;
    switch(param >> 4)
    {
    case MOD_E0_FILTER:
      /* conversion not based in reality, for testing only */
      effect = NO_MISC_1;
      param = EXTENDED(NO_I5_TREMOLO_VOLSLIDE_DN, param);
      break;
    case MOD_E1_FINE_PORTA_UP:
      param = EXTENDED(NO_M0_FINE_PORTA_UP, param);
      break;
    case MOD_E2_FINE_PORTA_DN:
      param = EXTENDED(NO_M1_FINE_PORTA_DN, param);
      break;
    case MOD_E3_GLISSANDO:
      effect = param = -1;
      break;
    case MOD_E4_VIBRATO_WAVEFORM:
      param = EXTENDED(NO_M4_VIBRATO_WAVEFORM, param);
      break;
    case MOD_E5_SET_FINETUNE:
      effect = param = -1;
      break;
    case MOD_E6_PATTERN_LOOP:
      param = EXTENDED(NO_MB_PATTERN_LOOP, param);
      break;
    case MOD_E7_TREMOLO_WAVEFORM:
      param = EXTENDED(NO_M5_TREMOLO_WAVEFORM, param);
      break;
    case MOD_E8_PAN_CONTROL:
      effect = NO_PAN_CONTROL;
      param = (param & 0x0f) << 2;
      param = (param / 10 * 16) + (param % 10);
      break;
    case MOD_E9_RETRIGGER:
      param = EXTENDED(NO_M6_RETRIGGER, param);
      break;
    case MOD_EA_FINE_VOLUME_UP:
      param = EXTENDED(NO_M2_FINE_VOLSLIDE_UP, param);
      break;
    case MOD_EB_FINE_VOLUME_DN:
      param = EXTENDED(NO_M3_FINE_VOLSLIDE_DN, param);
      break;
    case MOD_EC_NOTE_CUT:
      param = EXTENDED(NO_M7_NOTE_CUT, param);
      break;
    case MOD_ED_NOTE_DELAY:
      param = EXTENDED(NO_M8_NOTE_DELAY, param);
      break;
    case MOD_EE_PATTERN_DELAY:
      param = EXTENDED(NO_MC_PATTERN_DELAY, param);
      break;
    case MOD_EF_INVERT_LOOP:
      /* conversion not based in reality, for testing only */
      effect = NO_MISC_1;
      param = EXTENDED(NO_I4_TREMOLO_VOLSLIDE_UP, param);
      break;
    };
    break;
  case MOD_SPEED_BPM:
    effect = NO_SPEED_BPM;
    break;
  }

  /* Repack */
  uint32_t new_event =
    (note & 0x3f) |
    ((ins & 0x7f) << 6) |
    ((volume & 0x7f) << 13) |
    ((effect & 0x0f) << 20) |
    ((param & 0xff) << 24);

  write_u32le(event, new_event);
}

static bool convert_mod_pattern(uint8_t *patbuf, size_t patsz,
 const struct mod_header &mod)
{
  for(size_t i = 0; i < patsz; i += 4)
    convert_mod_event(patbuf + i);

  return true;
}

int main(int argc, char *argv[])
{
  static uint8_t patbuf[64 * 64 * 4];
  size_t pattern_bytes;
  struct mod_header mod;
  struct no_header no;

  if(argc < 2)
  {
    fprintf(stderr, "Usage: mod2liq2 file.mod [...]\n"
                    "Writes NO conversion of [name].mod to [name].liq.\n");
    return 1;
  }

  for(int i = 1; i < argc; i++)
  {
    FILE *in;
    FILE *out;
    char *extpos;
    char *path;
    size_t len;

    fprintf(stderr, "  %s... ", argv[i]);
    fflush(stderr);

    in = fopen(argv[i], "rb");
    if(!in)
    {
      ERROR("failed to fopen '%s'", argv[i]);
      continue;
    }
    if(!load_mod_header(mod, in) || !convert_mod_header(no, mod))
    {
      ERROR("failed to convert '%s'", argv[i]);
      goto err_close;
    }

    len = strlen(argv[i]);
    extpos = strrchr(argv[i], '.');
    if(len + 5 > sizeof(patbuf))
    {
      ERROR("path too long, skipping '%s'", argv[i]);
      goto err_close;
    }
    path = reinterpret_cast<char *>(patbuf);
    if(extpos && !strcasecmp(extpos, ".mod"))
      snprintf(path, sizeof(patbuf), "%.*s.liq", (int)(extpos - argv[i]), argv[i]);
    else
      snprintf(path, sizeof(patbuf), "%s.liq", argv[i]);

    out = fopen(path, "wb");
    if(!out)
    {
      ERROR("failed to fopen '%s' output file '%s'", argv[i], path);
      goto err_close;
    }
    if(!write_no_header(no, out))
    {
      ERROR("failed to convert '%s'", argv[i]);
      goto err_close2;
    }

    // Convert and copy patterns
    pattern_bytes = (size_t)mod.num_channels * 64 * 4;

    for(size_t j = 0; j < mod.num_patterns; j++)
    {
      if(fread(patbuf, 1, pattern_bytes, in) < pattern_bytes ||
         !convert_mod_pattern(patbuf, pattern_bytes, mod) ||
         fwrite(patbuf, 1, pattern_bytes, out) < pattern_bytes)
      {
        ERROR("failed to convert '%s' pattern %zu", argv[i], j);
        goto err_close2;
      }
    }

    // Copy samples
    for(size_t k = 0; k < mod.sample_bytes_total;)
    {
      size_t sz = sizeof(patbuf) < mod.sample_bytes_total - k ?
                  sizeof(patbuf) : mod.sample_bytes_total - k;

      k += sz;
      if(fread(patbuf, 1, sz, in) < sz)
      {
        ERROR("read error in '%s' sample data", argv[i]);
        goto err_close2;
      }
      /* Convert signed -> unsigned */
      for(size_t n = 0; n < sz; n++)
        patbuf[n] ^= 0x80;

      if(fwrite(patbuf, 1, sz, out) < sz)
      {
        ERROR("write error in '%s' sample data", argv[i]);
        goto err_close2;
      }
    }
    fprintf(stderr, "OK\n");
    fflush(stderr);

err_close2:
    fclose(out);
err_close:
    fclose(in);
  }
  return 0;
}
