/* mod2xmf - convert modules to Imperium Galactica's XMF module format.
 *
 * Copyright (C) 2023-2026 Lachesis <petrifiedrowan@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
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

#define MOD_HEADER_LEN 1084
#define MOD_SAMPLE_LEN 30
#define MOD_ORDER      952
#define MOD_ORDERS     128
#define MOD_SAMPLES    31
#define MOD_MAX_CHANNELS 99
#define MOD_ROWS       64

#define XMF_ROWS 64
#define XMF_SAMPLES 256
#define XMF_ORDERS 256

#define MAX(a,b) ((a) > (b) ? (a) : (b))
#define MAGIC(a,b,c,d) ((a) | ((b) << 8) | ((c) << 16) | ((uint32_t)(d) << 24UL))

static uint16_t memget16be(const uint8_t *buf)
{
  return (buf[0] << 8) | buf[1];
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
  uint8_t buf[MOD_MAX_CHANNELS * MOD_ROWS * 4];
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
    pos[13] = loopend ? 0x08 : 0x00;

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

  if(argc != 3)
    return 1;

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

  if(in_len >= MOD_HEADER_LEN)
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
  default:
    ret = 1;
    break;
  }

  fclose(out);
  free(in);

  return ret;
}
