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

/**
 * Utility to examine .GDM files for useful information.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.hpp"

static const char USAGE[] =
  "A utility to dump GDM metadata and patterns.\n"
  "This will print useful information, such as:\n\n"
  "* Sample information.\n"
  "* Pattern lengths.\n"
  "* Uses of GDM fine tone/volume slides.\n"
  "* Uses of GDM empty note values to retrigger instruments.\n"
  "* Pattern dumps (with the -d option).\n\n"
  "Usage:\n"
  "  gdmutil [options] [filenames...]\n\n"
  "Options:\n"
  "  -s[=N]    Dump sample info. N=1 (optional) enables, N=0 disables (default).\n"
  "  -p[=N]    Dump patterns. N=1 (optional) enables, N=0 disables (default).\n"
  "            N=2 additionally dumps the entire pattern as raw data.\n"
  "  -         Read filenames from stdin. Useful when there are too many files\n"
  "            for argv. Place after any other options if applicable.\n\n";

static bool dump_samples      = false;
static bool dump_patterns     = false;
static bool dump_pattern_rows = false;

enum GDM_err
{
  GDM_SUCCESS,
  GDM_ALLOC_ERROR,
  GDM_READ_ERROR,
  GDM_SEEK_ERROR,
  GDM_BAD_SIGNATURE,
  GDM_BAD_VERSION,
  GDM_BAD_CHANNEL,
  GDM_BAD_PATTERN,
  GDM_TOO_MANY_EFFECTS,
};

static const char *GDM_strerror(int err)
{
  switch(err)
  {
    case GDM_SUCCESS:          return "no error";
    case GDM_READ_ERROR:       return "read error";
    case GDM_SEEK_ERROR:       return "seek error";
    case GDM_BAD_SIGNATURE:    return "GDM signature mismatch";
    case GDM_BAD_VERSION:      return "GDM version invalid";
    case GDM_BAD_CHANNEL:      return "invalid GDM channel index";
    case GDM_BAD_PATTERN:      return "invalid GDM pattern data";
    case GDM_TOO_MANY_EFFECTS: return "note has more effects (>4) than allowed";
  }
  return "unknown error";
}

static const char MAGIC[] = "GDM\xFE";
static const char MAGIC_EOF[] = "\x0D\x0A\x1A";
static const char MAGIC_2[] = "GMFS";

static const char *TRACKERS[] =
{
  "2GDM"
};
static const int NUM_TRACKERS = arraysize(TRACKERS);

static const char *FORMATS[] =
{
  ".GDM",
  ".MOD",
  ".MTM",
  ".S3M",
  ".669",
  ".FAR",
  ".ULT",
  ".STM",
  ".MED"
};
static const int NUM_FORMATS = arraysize(FORMATS);

static constexpr const char *TRACKER(uint16_t tracker)
{
  return (tracker < NUM_TRACKERS) ? TRACKERS[tracker] : "unknown";
}

static constexpr const char *FORMAT(uint16_t format)
{
  return (format < NUM_FORMATS) ? FORMATS[format] : "unknown";
}

static constexpr uint8_t VER_MINOR(uint16_t version)
{
  return version >> 8;
}

static constexpr uint8_t VER_MAJOR(uint16_t version)
{
  return version & 0xFF;
}

enum GDM_sample_flags
{
  S_LOOP   = (1<<0),
  S_S16    = (1<<1),
  S_VOL    = (1<<2),
  S_PAN    = (1<<3),
  S_LZW    = (1<<4),
  S_STEREO = (1<<5),
};

enum GDM_effects
{
  E_NONE,
  E_PORTAMENTO_UP,
  E_PORTAMENTO_DOWN,
  E_PORTAMENTO_TONE,
  E_VIBRATO,
  E_VOLSLIDE_PORTAMENTO,
  E_VIBRATO_PORTAMENTO,
  E_TREMOLO,
  E_TREMOR,
  E_SAMPLE_OFFSET,
  E_VOLSLIDE,
  E_PATTERN_JUMP,
  E_VOLUME,
  E_PATTERN_BREAK,
  E_EXT,
  E_TEMPO
};

enum GDM_effects_ext
{
  EX_FILTER,
  EX_FINE_PORTAMENTO_UP,
  EX_FINE_PORTAMENTO_DOWN,
  EX_GLISSANDO,
  EX_VIBRATO_WAVEFORM,
  EX_C4_TUNING,
  EX_LOOP,
  EX_TREMOLO_WAVEFORM,
  EX_EXTRA_FINE_PORTAMENTO_UP,
  EX_EXTRA_FINE_PORTAMENTO_DOWN,
  EX_FINE_VOLSLIDE_UP,
  EX_FINE_VOLSLIDE_DOWN,
  EX_NOTE_CUT,
  EX_NOTE_DELAY,
  EX_EXTEND_ROW,
  EX_FUNKREPEAT
};

template<int N>
static const char *FLAG_STR(char (&buffer)[N], uint8_t flags)
{
  static_assert(N >= 7, "use a bigger buffer pls ;-(");
  buffer[0] = (flags & S_LOOP)   ? 'r' : ' ';
  buffer[1] = (flags & S_S16)    ? 'w' : ' ';
  buffer[2] = (flags & S_VOL)    ? 'v' : ' ';
  buffer[3] = (flags & S_PAN)    ? 'p' : ' ';
  buffer[4] = (flags & S_LZW)    ? 'x' : ' ';
  buffer[5] = (flags & S_STEREO) ? 's' : ' ';
  buffer[6] = '\0';
  return buffer;
}

/**
 * NOTE: sloppy header packing means this can't really be read
 * out of the file directly. Don't try it. Oh well...
 */
struct GDM_header
{
  char magic[4];
  char name[33]; // Null-terminator not stored? array size is 1 larger than field.
  char author[33]; // Ditto.
  char eof[3];
  char magic2[4];
  uint16_t gdm_version;
  uint16_t tracker_id;
  uint16_t tracker_version;
  uint8_t panning[32];
  uint8_t global_volume;
  uint8_t tempo;
  uint8_t bpm;
  uint16_t original_format;
  uint32_t order_offset;
  uint8_t num_orders;
  uint32_t pattern_offset;
  uint8_t num_patterns;
  uint32_t sample_offset;
  uint32_t sample_data_offset;
  uint8_t num_samples;
  uint32_t message_offset;
  uint32_t message_length;
  uint32_t scrolly_offset; // ??
  uint16_t scrolly_length;
  uint32_t graphic_offset; // ??
  uint16_t graphic_length;
};

/**
 * Same note above applies to these.
 */
struct GDM_sample
{
  char name[33]; // Null-terminator not stored? Array size is 1 larger than field.
  char filename[13]; // Ditto.
  uint8_t ems; // ignore.
  uint32_t length;
  uint32_t loopstart;
  uint32_t loopend;
  uint8_t flags;
  uint16_t c4rate;
  uint8_t default_volume;
  uint8_t default_panning;
};

struct GDM_note
{
  uint8_t note;
  uint8_t sample;
  struct
  {
    uint8_t effect;
    uint8_t param;
  } effects[4];
};

struct GDM_pattern
{
  // NOTE: this could theoretically be longer but this is probably the maximum.
  GDM_note rows[256][32];
  uint8_t max_track_effects[32];
  uint16_t raw_size;
  uint16_t num_rows;
};

struct GDM_data
{
  GDM_header header;
  GDM_sample samples[256];
  GDM_pattern *patterns[256];
  uint8_t orders[256];
  uint8_t buffer[65536];
  uint8_t num_channels;
};

void GDM_free(GDM_data &d)
{
  for(int i = 0; i < 256; i++)
    free(d.patterns[i]);
}

int GDM_read(GDM_data &d, FILE *fp)
{
  GDM_header &h = d.header;
  memset(&d, 0, sizeof(struct GDM_data));

  if(!fread(h.magic, 4, 1, fp))
    return GDM_READ_ERROR;
  if(memcmp(h.magic, MAGIC, 4))
    return GDM_BAD_SIGNATURE;

  if(!fread(h.name, 32, 1, fp) ||
     !fread(h.author, 32, 1, fp) ||
     !fread(h.eof, 3, 1, fp) ||
     !fread(h.magic2, 4, 1, fp))
    return GDM_READ_ERROR;
  if(memcmp(h.eof, MAGIC_EOF, 3) || memcmp(h.magic2, MAGIC_2, 4))
    return GDM_BAD_SIGNATURE;

  h.name[32] = '\0';
  h.author[32] = '\0';

  h.gdm_version         = fget_u16le(fp);
  h.tracker_id          = fget_u16le(fp);
  h.tracker_version     = fget_u16le(fp);

  if(!fread(h.panning, 32, 1, fp))
    return GDM_READ_ERROR;

  h.global_volume       = fgetc(fp);
  h.tempo               = fgetc(fp);
  h.bpm                 = fgetc(fp);
  h.original_format     = fget_u16le(fp);
  h.order_offset        = fget_u32le(fp);
  h.num_orders          = fgetc(fp) + 1;
  h.pattern_offset      = fget_u32le(fp);
  h.num_patterns        = fgetc(fp) + 1;
  h.sample_offset       = fget_u32le(fp);
  h.sample_data_offset  = fget_u32le(fp);
  h.num_samples         = fgetc(fp) + 1;
  h.message_offset      = fget_u32le(fp);
  h.message_length      = fget_u32le(fp);
  h.scrolly_offset      = fget_u32le(fp);
  h.scrolly_length      = fget_u16le(fp);
  h.graphic_offset      = fget_u32le(fp);
  h.graphic_length      = fget_u16le(fp);

  if(feof(fp))
    return GDM_READ_ERROR;

  // Get channel count by checking for 255 in the panning table.
  bool uses_chpan = false;
  bool uses_surround = false;
  for(int i = 0; i < 32; i++)
  {
    if(h.panning[i] != 255)
    {
      d.num_channels = i + 1;

      if(h.panning[i] == 16)
        uses_surround = true;
      if(h.panning[i] != 8)
        uses_chpan = true;
    }
  }

  O_("Name      : %s\n", h.name);
  O_("Type      : GDM %u.%u (%s/%s %u.%u)\n",
   VER_MAJOR(h.gdm_version), VER_MINOR(h.gdm_version), FORMAT(h.original_format),
   TRACKER(h.tracker_id), VER_MAJOR(h.tracker_version), VER_MINOR(h.tracker_version));
  O_("Orders    : %u\n", h.num_orders);
  O_("Patterns  : %u\n", h.num_patterns);
  O_("Tracks    : %u\n", d.num_channels);
  O_("Samples   : %u\n", h.num_samples);

  // Order list.
  if(fseek(fp, h.order_offset, SEEK_SET))
    return GDM_SEEK_ERROR;

  if(!fread(d.orders, h.num_orders, 1, fp))
    return GDM_READ_ERROR;

  // Samples.
  if(fseek(fp, h.sample_offset, SEEK_SET))
    return GDM_SEEK_ERROR;

  bool uses_svol = false;
  bool uses_nosvol = false;
  bool uses_span = false;
  bool uses_lzw = false;
  bool print_sample_header = true;
  for(size_t i = 0; i < h.num_samples; i++)
  {
    GDM_sample &s = d.samples[i];
    if(!fread(s.name, 32, 1, fp) || !fread(s.filename, 12, 1, fp))
      return GDM_READ_ERROR;

    s.name[32] = '\0';
    s.filename[12] = '\0';

    s.ems             = fgetc(fp); // Safe to ignore.
    s.length          = fget_u32le(fp);
    s.loopstart       = fget_u32le(fp);
    s.loopend         = fget_u32le(fp);
    s.flags           = fgetc(fp);
    s.c4rate          = fget_u16le(fp);
    s.default_volume  = fgetc(fp);
    s.default_panning = fgetc(fp);

    if(feof(fp))
      return GDM_READ_ERROR;

    if(dump_samples)
    {
      char tmp[16];
      if(print_sample_header)
      {
        O_("          : %-32s  %-12s  %-10s %-10s %-10s %-7s %-7s %-5s %-5s\n",
         "Name", "Filename", "Length", "L. Start", "L. End", "Flags", "C4Rate", "Vol.", "Pan.");
        print_sample_header = false;
      }
      O_("Sample %02x : %-32s  %-12s  %-10u %-10u %-10u %-7s %-7u %-5u %-5u\n",
       (unsigned int)i, s.name, s.filename, s.length, s.loopstart, s.loopend,
       FLAG_STR(tmp, s.flags), s.c4rate, s.default_volume, s.default_panning);
    }
    if((s.flags & S_VOL) && s.default_volume != 255)
      uses_svol = true;
    else
      uses_nosvol = true;
    if((s.flags & S_PAN) && s.default_panning != 255)
    {
      if(s.default_panning == 16)
        uses_surround = true;

      uses_span = true;
    }
    if(s.flags & S_LZW)
      uses_lzw = true;
  }

  // Patterns.
  if(fseek(fp, h.pattern_offset, SEEK_SET))
    return GDM_SEEK_ERROR;

  bool uses_nonote = false;
  bool uses_noinst = false;
  bool uses_finevol = false;
  bool uses_fineport = false;
  bool uses_fxch3 = false;
  bool uses_fxch4 = false;
  bool uses_over64 = false;
  for(size_t i = 0; i < h.num_patterns; i++)
  {
    GDM_pattern *p = (GDM_pattern *)calloc(1, sizeof(GDM_pattern));
    d.patterns[i] = p;
    if(!p)
      return GDM_ALLOC_ERROR;

    p->raw_size = fget_u16le(fp) - 2;

    size_t pos = 0;
    size_t row = 0;
    while(pos < p->raw_size && row < (size_t)arraysize(p->rows))
    {
      uint8_t t = fgetc(fp);
      pos++;

      if(t == 0)
      {
        row++;
        if(pos < p->raw_size)
          continue;

        break;
      }

      uint8_t track = (t & 0x1F);
      GDM_note &n = p->rows[row][track];

      if(t & 0x20)
      {
        uint8_t note = fgetc(fp);
        uint8_t inst = fgetc(fp);
        pos += 2;

        n.note = note;
        n.sample = inst;

        if(!note)
          uses_nonote = true;
        if(!inst)
          uses_noinst = true;
      }

      if(t & 0x40)
      {
        int j;
        for(j = 0; j < 3; j++)
        {
          uint8_t fx = fgetc(fp);
          uint8_t fx_param = fgetc(fp);
          pos += 2;

          uint8_t fx_effect = fx & 0x1F;
          uint8_t fx_channel = (fx >> 6) & 0x03;

          if(fx_channel + 1 > p->max_track_effects[track])
            p->max_track_effects[track] = fx_channel + 1;

          n.effects[fx_channel].effect = fx_effect;
          n.effects[fx_channel].param = fx_param;

          if(fx_channel == 0x02)
            uses_fxch3 = true;
          if(fx_channel == 0x03)
            uses_fxch4 = true;

          if(fx_effect == E_EXT)
          {
            uint8_t fx_ext = (fx_param >> 4) & 0x0F;
            if(fx_ext == EX_FINE_PORTAMENTO_UP ||
             fx_ext == EX_FINE_PORTAMENTO_DOWN ||
             fx_ext == EX_EXTRA_FINE_PORTAMENTO_UP ||
             fx_ext == EX_EXTRA_FINE_PORTAMENTO_DOWN)
              uses_fineport = true;

            if(fx_ext == EX_FINE_VOLSLIDE_UP || fx_ext == EX_FINE_VOLSLIDE_DOWN)
              uses_finevol = true;
          }

          if(!(fx & 0x20))
            break;
        }
        if(j >= 4)
          return GDM_TOO_MANY_EFFECTS;
      }
    }
    if(feof(fp))
      return GDM_READ_ERROR;

    if(pos != p->raw_size)
      return GDM_BAD_PATTERN;

    p->num_rows = row;
    if(row > 64)
      uses_over64 = true;
  }

  O_("Uses      :%s%s%s%s%s%s%s%s%s%s%s%s%s\n",
    uses_surround ? " Surround" : "",
    uses_chpan    ? " ChPan"    : "",
    uses_svol     ? " SVol"     : "",
    uses_nosvol   ? " NoSVol"   : "",
    uses_span     ? " SPan"     : "",
    uses_lzw      ? " LZW"      : "",
    uses_nonote   ? " NoNote"   : "",
    uses_noinst   ? " NoInst"   : "",
    uses_finevol  ? " FineVol"  : "",
    uses_fineport ? " FinePort" : "",
    uses_fxch3    ? " FXCh3"    : "",
    uses_fxch4    ? " FXCh4"    : "",
    uses_over64   ? " >64Rows"  : ""
  );

#define P_PRINT(x)   do{ if(x) fprintf(stderr, " %02x", x); else fprintf(stderr, "   "); }while(0)
#define E_PRINT(x,y) do{ if(x) fprintf(stderr, " %2x%02x", x, y); else fprintf(stderr, "     "); }while(0)

  if(dump_patterns)
  {
    O_("Panning   :");
    for(unsigned int k = 0; k < d.num_channels; k++)
    {
      if(h.panning[k] == 255)
        continue;
      fprintf(stderr, " %02x", h.panning[k]);
    }
    fprintf(stderr, "\n");

    O_("Order Tbl.:");
    for(unsigned int i = 0; i < h.num_orders; i++)
      fprintf(stderr, " %02x", d.orders[i]);
    fprintf(stderr, "\n");

    for(unsigned int i = 0; i < h.num_patterns; i++)
    {
      if(dump_pattern_rows)
        fprintf(stderr, "\n");

      GDM_pattern *p = d.patterns[i];
      O_("Pattern %02x: %u rows\n", i, p->num_rows);

      if(dump_pattern_rows)
      {
        O_("          :");
        for(unsigned int k = 0; k < d.num_channels; k++)
        {
          if(h.panning[k] == 255)
            continue;

          fprintf(stderr, " Ch.%02x", k);
          for(unsigned int x = 0; x < p->max_track_effects[k]; x++)
            fprintf(stderr, "     ");
          fprintf(stderr, ":");
        }
        fprintf(stderr, "\n");

        for(unsigned int j = 0; j < p->num_rows; j++)
        {
          O_("       %02x :", j);
          for(unsigned int k = 0; k < d.num_channels; k++)
          {
            if(h.panning[k] == 255)
              continue;

            GDM_note &n = p->rows[j][k];
            P_PRINT(n.note);
            P_PRINT(n.sample);
            for(size_t x = 0; x < p->max_track_effects[k]; x++)
              E_PRINT(n.effects[x].effect, n.effects[x].param);
            fprintf(stderr, ":");
          }
          fprintf(stderr, "\n");
        }
      }
    }
  }
  return GDM_SUCCESS;
}

void check_gdm(const char *filename)
{
  static GDM_data d;
  int ret;

  FILE *fp = fopen(filename, "rb");
  if(fp)
  {
    O_("File      : %s\n", filename);

    // Can't read entire header into a struct so add a buffer instead.
    setvbuf(fp, NULL, _IOFBF, 4096);

    ret = GDM_read(d, fp);
    if(ret)
      O_("Error     : %s\n\n", GDM_strerror(ret));
    else
      fprintf(stderr, "\n");

    fclose(fp);
    GDM_free(d);
  }
  else
    O_("Failed to open '%s'\n.", filename);
}


int main(int argc, char *argv[])
{
  bool read_stdin = false;

  if(!argv || argc < 2)
  {
    fprintf(stdout, "%s", USAGE);
    return 0;
  }

  for(int i = 1; i < argc; i++)
  {
    char *arg = argv[i];
    if(arg[0] == '-')
    {
      switch(arg[1])
      {
        case '\0':
          if(!read_stdin)
          {
            char buffer[1024];
            while(fgets_safe(buffer, stdin))
              check_gdm(buffer);

            read_stdin = true;
          }
          continue;

        case 'p':
          if(!arg[2] || !strcmp(arg + 2, "=1"))
          {
            dump_patterns = true;
            dump_pattern_rows = false;
            continue;
          }
          if(!strcmp(arg + 2, "=2"))
          {
            dump_patterns = true;
            dump_pattern_rows = true;
            continue;
          }
          if(!strcmp(arg + 2, "=0"))
          {
            dump_patterns = false;
            dump_pattern_rows = false;
            continue;
          }
          break;

        case 's':
          if(!arg[2] || !strcmp(arg + 2, "=1"))
          {
            dump_samples = true;
            continue;
          }
          if(!strcmp(arg + 2, "=0"))
          {
            dump_samples = false;
            continue;
          }
          break;
      }
    }
    check_gdm(arg);
  }

  return 0;
}
