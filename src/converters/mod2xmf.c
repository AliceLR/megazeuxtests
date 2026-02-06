/* mod2xmf - convert modules to Imperium Galactica's XMF module format.
 *
 * Copyright (C) 2023-2026 Lachesis <petrifiedrowan@gmail.com>
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

#include <ctype.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define FORMAT_NONE -1
#define FORMAT_MOD 0
#define FORMAT_ULT 1

#define MOD_HEADER_LEN 1084
#define MOD_SAMPLE_LEN 30
#define MOD_ORDER      952
#define MOD_ORDERS     128
#define MOD_SAMPLES    31
#define MOD_MAX_CHANNELS 99
#define MOD_ROWS       64

#define ULT_V1_0          1
#define ULT_V1_4          2
#define ULT_V1_5          3
#define ULT_V1_6          4
#define ULT_SAMPLE_LEN_10 64
#define ULT_SAMPLE_LEN_16 66
#define ULT_MAX_CHANNELS  32 /* Format allows up to 256, tracker limits to 32 */
#define ULT_ROWS          64

#define XMF_ROWS 64
#define XMF_SAMPLES 256
#define XMF_ORDERS 256

#define GUS_SAMPLE_16_BIT (1 << 2)
#define GUS_SAMPLE_LOOP   (1 << 3)
#define GUS_SAMPLE_BIDIR  (1 << 4)
#define GUS_SAMPLE_VALID  (GUS_SAMPLE_16_BIT | GUS_SAMPLE_LOOP | GUS_SAMPLE_BIDIR)

#define MAX(a,b) ((a) > (b) ? (a) : (b))
#define MAGIC(a,b,c,d) ((a) | ((b) << 8) | ((c) << 16) | ((uint32_t)(d) << 24UL))

static uint16_t memget16be(const uint8_t *buf)
{
  return (buf[0] << 8) | buf[1];
}

static uint16_t memget16le(const uint8_t *buf)
{
  return buf[0] | (buf[1] << 8);
}

static uint32_t memget32le(const uint8_t *buf)
{
  return buf[0] | (buf[1] << 8) | (buf[2] << 16) | ((uint32_t)buf[3] << 24UL);
}

static void memput16le(uint16_t val, uint8_t *buf)
{
  buf[0] = (val >> 0) & 0xff;
  buf[1] = (val >> 8) & 0xff;
}

static void memput24le(uint32_t val, uint8_t *buf)
{
  buf[0] = (val >>  0) & 0xff;
  buf[1] = (val >>  8) & 0xff;
  buf[2] = (val >> 16) & 0xff;
}

static int convert_mod(FILE *out, const uint8_t *in, size_t in_len, int chn)
{
  uint8_t buf[MOD_MAX_CHANNELS * MOD_ROWS * 6];
  const uint8_t *samples = in + 20;
  const uint8_t *patterns = in + MOD_HEADER_LEN;
  const uint8_t *sdata;
  uint8_t *pos;
  size_t samples_total = 0;
  int num_patterns;
  int num_orders;
  int i;
  int j;
  int k;

  memset(buf, 0, 16 * 256);

  pos = buf;
  for(i = 0; i < MOD_SAMPLES; i++)
  {
    uint32_t len = memget16be(samples + 22) << 1;
    uint32_t loopstart = memget16be(samples + 26) << 1;
    uint32_t loopend = loopstart + (memget16be(samples + 28) << 1);
    uint8_t finetune = samples[24];
    uint8_t volume = samples[25];
    uint16_t rate = 8363;

    if(len == 0)
      continue;

    if(loopend > len)
      loopend = len;

    if(loopend == 2 || loopstart > loopend)
      loopend = loopstart = 0;

    // FIXME: finetune to rate
    (void)finetune;

    memput24le(loopstart,           pos + 0);
    memput24le(loopend,             pos + 3);
    memput24le(samples_total,       pos + 6);
    memput24le(samples_total + len, pos + 9);
    memput16le(rate,                pos + 14);

    pos[12] = (volume * 0xff) >> 6;
    pos[13] = loopend ? GUS_SAMPLE_LOOP : 0x00;

    samples_total += len;
    samples += MOD_SAMPLE_LEN;
    pos += 16;
  }

  if(in_len < samples_total)
    return 1;

  fputc(0x03, out);
  fwrite(buf, 16, 256, out);

  /* Sequence */
  num_orders = in[MOD_ORDER - 2];
  //restart = in[MOD_ORDER - 1];

  memset(buf, 0xff, XMF_ORDERS);
  num_patterns = 0;
  for(i = 0; i < MOD_ORDERS; i++)
  {
    if(i < num_orders)
      buf[i] = in[MOD_ORDER + i];

    num_patterns = MAX(num_patterns, in[MOD_ORDER + i] + 1);
  }
  buf[num_orders] = 0xff;
  buf[XMF_ORDERS] = chn - 1;
  buf[XMF_ORDERS + 1] = num_patterns - 1;

  if(in_len - samples_total <
   (size_t)(MOD_HEADER_LEN + num_patterns * chn * MOD_ROWS * 4))
    return 1;

  fwrite(buf, 1, XMF_ORDERS + 2, out);

  /* Panning table */
  for(i = 0; i < chn; i++)
    buf[i] = ((i + 1) & 0x02) ? 0x0b : 0x03;

  fwrite(buf, 1, chn, out);

  /* Convert patterns */
  for(i = 0; i < num_patterns; i++)
  {
    pos = buf;
    for(j = 0; j < MOD_ROWS; j++)
    {
      for(k = 0; k < chn; k++)
      {
        int note   = ((patterns[0] & 0x0f) << 8) | patterns[1];
        int inst   = (patterns[0] & 0xf0) | (patterns[2] >> 4);
        int effect = patterns[2] & 0x0f;
        int param  = patterns[3];

        /* stolen from libxmp */
        if(note)
          pos[0] = round(12.0 * log(13696.0 / note) / M_LN2) - 36 + 1;
        else
          pos[0] = 0;

        /* GUS 4-bit panning is effect 10xx. */
        if(effect == 0x0e && (param & 0xf0) == 0x80)
        {
          effect = 0x10;
          param = param & 0x0f;
        }
        /* Approximate S3M panning with effect 10xx. */
        if(effect == 0x08)
        {
          if(param <= 0x80)
          {
            effect = 0x10;
            param = ((param * 0xf) + 0x40) / 0x80;
          }
          else
            effect = param = 0;
        }

        /* Pattern jump is broken and jumps to the order *after* the param. */
        if(effect == 0x0b)
          param = param ? param - 1 : 0;

        if(effect == 0x0c)
          param = (param * 0xff) >> 6;

        pos[1] = inst;
        pos[2] = effect;
        pos[3] = 0;
        pos[4] = 0;
        pos[5] = param;
        patterns += 4;
        pos += 6;
      }
    }
    fwrite(buf, chn, 6 * XMF_ROWS, out);
  }

  /* Copy samples */
  sdata = in + MOD_HEADER_LEN + num_patterns * chn * MOD_ROWS * 4;
  fwrite(sdata, 1, samples_total, out);
  return 0;
}


static void ult_convert_fx(uint8_t *fx, uint8_t *param)
{
  /* XMF implements MOD commands aside from ULT balance (0x10) and
   * ULT retrigger (0x11). The non-standard ULT commands need to be
   * filtered out/remapped.
   */
  switch(*fx)
  {
  case 0x5:             /* Special commands */
  case 0x6:             /* Unused */
  case 0x8:             /* Unused */
    *fx = *param = 0;
    break;

  case 0xb:             /* Balance */
    *fx = 0x10;
    break;

  case 0x0e:
    switch(*param >> 4)
    {
    case 0x0:           /* Vibrato strength */
    case 0x3:           /* Unused */
    case 0x4:           /* Unused */
    case 0x5:           /* Unused */
    case 0x6:           /* Unused */
    case 0x7:           /* Unused */
    case 0xe:           /* Unused */
    case 0xf:           /* Unused */
      *fx = *param = 0;
      break;

    case 0x8:           /* Delay track */
      *param = 0xe0 | (*param & 0x0f);
      break;

    case 0x9:           /* Retrigger (XMF E9x only retriggers once?) */
      *fx = 0x11;
      *param &= 0x0f;
      break;
    }
  }
}

static void ult_convert_event(uint8_t *dest, const uint8_t *src)
{
  dest[0] = src[0];         /* Note */
  dest[1] = src[1];         /* Instrument */
  dest[2] = src[2] >> 4;    /* Effect (hi) */
  dest[3] = src[2] & 0x0f;  /* Effect (lo) */
  dest[4] = src[3];         /* Param (lo) */
  dest[5] = src[4];         /* Param (hi) */

  ult_convert_fx(dest + 2, dest + 5);
  ult_convert_fx(dest + 3, dest + 4);
}

static int convert_ult(FILE *out, const uint8_t *in, size_t in_len)
{
  const uint8_t *eof = in + in_len;
  const uint8_t *sequence;
  const uint8_t *patterns;
  const uint8_t *sdata;
  const uint8_t *sample;
  uint8_t *buf;
  uint8_t *pos;
  size_t samples_total = 0;
  size_t pattern_size;
  size_t pitch;

  unsigned version = in[14] - '0';
  unsigned text_length = in[47] * 32;
  unsigned num_samples;
  uint16_t num_channels;
  uint16_t num_patterns;
  size_t i;
  size_t j;
  size_t k;

  buf = (uint8_t *)calloc(16, XMF_SAMPLES);
  if(!buf)
    return 2;

  if(text_length > in_len || 49 >= in_len - text_length)
    return 2;

  num_samples = *(in + 48 + text_length);
  sample = in + 49 + text_length;
  pos = buf;

  for(i = 0; i < num_samples && (sample < eof - 66); i++)
  {
    uint32_t loop_start = memget32le(sample + 44);
    uint32_t loop_end   = memget32le(sample + 48);
    uint32_t size_start = memget32le(sample + 52);
    uint32_t size_end   = memget32le(sample + 56);
    uint8_t default_vol = sample[60];
    uint8_t bidi        = sample[61];
    uint16_t c2speed    = 8363;
    uint16_t finetune;
    size_t length = (size_end > size_start) ? size_end - size_start : 0;

    if(version >= ULT_V1_6)
    {
      c2speed = memget16le(sample + 62);
      finetune = memget16le(sample + 64);
      sample += ULT_SAMPLE_LEN_16;
    }
    else
    {
      finetune = memget16le(sample + 62);
      sample += ULT_SAMPLE_LEN_10;
    }

    memput24le(loop_start, pos + 0);
    memput24le(loop_end, pos + 3);
    memput24le(samples_total, pos + 6);
    memput24le(samples_total + length, pos + 9);
    pos[12] = default_vol;
    pos[13] = bidi & GUS_SAMPLE_VALID;
    memput16le(c2speed, pos + 14);

    /* XMF likely does not support UT-style "finetune" in any capacity. */
    (void)finetune;

    samples_total += length;
    pos += 16;
  }
  sequence = sample;

  /* Imperium Galactica uses 3, others use 4; possibly directly
   * copied (minus '0') from the Ultra Tracker magic string? */
  fputc(0x03, out);
  fwrite(buf, 16, 256, out);
  free(buf);

  /* Sequence is identical to Ultra Tracker. */
  if(sequence > eof || 258 > eof - sequence)
    return 2;

  num_channels = sequence[256] + 1;
  num_patterns = sequence[257] + 1;
  if(num_channels > ULT_MAX_CHANNELS || 258 + num_channels > eof - sequence)
    return 2;

  fwrite(sequence, 1, 258 + num_channels, out);
  patterns = sequence + 258 + num_channels;
  pattern_size = num_channels * ULT_ROWS * 6;
  pitch = num_channels * 6;

  /* Patterns - ULT stores patterns track-major, uses RLE compression;
   *            XMF are uncompressed and stored pattern-major (like MOD). */
  buf = (uint8_t *)calloc(num_patterns, pattern_size);
  if(!buf)
    return 2;

  for(i = 0; i < num_channels; i++)
  {
    for(j = 0; j < num_patterns; j++)
    {
      pos = buf + j * pattern_size + i * 6;

      for(k = 0; k < ULT_ROWS; )
      {
        if(patterns > eof - 5)
          return 2;

        if(patterns[0] == 0xfc) /* RLE */
        {
          uint8_t event[6];
          int count = patterns[1];

          if(patterns > eof - 7)
            return 2;

          ult_convert_event(event, patterns + 2);
          patterns += 7;
          do
          {
            memcpy(pos, event, 6);
            pos += pitch;
            count--;
            k++;
          }
          while(count > 0 && k < ULT_ROWS);
        }
        else
        {
          ult_convert_event(pos, patterns);
          patterns += 5;
          pos += pitch;
          k++;
        }
      }
    }
  }
  fwrite(buf, num_patterns, pattern_size, out);
  free(buf);

  sdata = patterns;
  if(sdata > eof || samples_total > (size_t)(eof - sdata))
    return 2;

  /* Copy sample data direct. */
  fwrite(sdata, 1, samples_total, out);
  return 0;
}

#ifdef LIBFUZZER_FRONTEND
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
  FILE *fp = fmemopen((uint8_t *)data, size, "rb");
  if(fp)
  {
    // TODO
    fclose(fp);
  }
  return 0;
}

#define main _main
static __attribute__((unused))
#endif

int main(int argc, char *argv[])
{
  struct stat st;
  size_t in_len;
  uint8_t *in;
  int format = FORMAT_NONE;
  int chn = 0;
  int ret;
  FILE *in_f;
  FILE *out;

  fprintf(stderr,
    "mod2xmf - convert MOD and ULT to Imperium Galactica XMF\n"
    "Copyright (C) 2023-2026 Lachesis\n"
    "\n"
    "NOTICE: This utility is intended for replayer research for Imperium\n"
    "Galactica ONLY. This utility makes NO ATTEMPT to ensure accurate\n"
    "conversion, and in fact intentionally avoids it in some cases\n"
    "for convenience. Any MOD/ULT provided to this utility should have been\n"
    "crafted WITH THE EXPRESS PURPOSE of being interpreted by Imperium Galactica\n"
    "and the output file should be well-tested with Imperium Galactica before\n"
    "distributing. To replayer authors: if you attempt to detect files made\n"
    "with this tool, they should be played as if they are original Imperium\n"
    "Galactica modules, not as their source formats.\n"
    "\n"
    "DO NOT USE THIS UTILITY FOR STUPID CRAP!\n"
    "\n"
  );
  if(argc != 3)
  {
    fprintf(stderr, "Usage: mod2xmf [infile] [outfile]\n"
                    "Writes 03h XMF conversion of [infile] to [outfile].\n");
    return 1;
  }

  if(stat(argv[1], &st) < 0)
    return 1;

  in_len = st.st_size;

  in_f = fopen(argv[1], "rb");
  if(!in_f)
    return 1;

  in = (uint8_t *)malloc(in_len);
  if(!in || fread(in, 1, in_len, in_f) < in_len)
  {
    fclose(in_f);
    return 1;
  }
  fclose(in_f);

  if(format == FORMAT_NONE && in_len >= 66)
  {
    if(!memcmp(in, "MAS_UTrack_V00", 14))
      format = FORMAT_ULT;
  }

  if(format == FORMAT_NONE && in_len >= MOD_HEADER_LEN)
  {
    /* Ignoring Digital Tracker, FEST, WOW for now. */
    uint32_t magic = memget32le(in + 1080);
    switch(magic)
    {
    case MAGIC('M','.','K','.'):
    case MAGIC('M','!','K','!'):
    case MAGIC('M','&','K','!'):
    case MAGIC('4','C','H','N'):
    case MAGIC('F','L','T','4'):
      chn = 4;
      break;

    case MAGIC('C','D','6','1'):
      chn = 6;
      break;

    case MAGIC('C','D','8','1'):
      chn = 8;
      break;

    default:
      if(magic >> 8 == MAGIC(0,'C','H','N') >> 8 ||
         magic >> 8 == MAGIC(0,'T','D','Z') >> 8)
      {
        chn = (magic & 0xff) - '0';
        if(chn < 0 || chn > 9)
          return 1;
      }
      else

      if(magic >> 16 == MAGIC(0, 0, 'C','H') >> 16 ||
         magic >> 16 == MAGIC(0, 0, 'C','N') >> 16)
      {
        if(!isdigit(magic & 0xff) || !isdigit((magic >> 8) & 0xff))
          return 1;

        chn = ((magic & 0xff) - '0') * 10 + (((magic >> 8) & 0xff) - '0');
      }
      else
        return 1;
    }
    if(chn > 0)
      format = FORMAT_MOD;
  }

  if(format == FORMAT_NONE)
    return 1;

  out = fopen(argv[2], "wb");
  if(!out)
  {
    free(in);
    return 1;
  }

  switch(format)
  {
  case FORMAT_MOD:
    ret = convert_mod(out, in, in_len, chn);
    break;
  case FORMAT_ULT:
    ret = convert_ult(out, in, in_len);
    break;
  default:
    ret = 1;
    break;
  }

  fclose(out);
  free(in);

  return ret;
}
