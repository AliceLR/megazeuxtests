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

static const char USAGE[] =
  "wowutil, a utility to examine .MOD or .WOW files to\n"
  "determine basic size information about them. The intention\n"
  "is to be able to objectively tell if a M.K. .MOD file is\n"
  "actually a stealth .WOW file (or vice versa).\n\n"
  "Usage: wowutil [.MOD and/or .WOW files...]\n";

#define O_(...) do { \
  fprintf(stderr, "WOW: " __VA_ARGS__); \
  fflush(stderr); \
} while(0)

template<class T, int N>
constexpr int arraysize(T (&arr)[N])
{
  return N;
}

constexpr bool is_big_endian()
{
  const uint32_t t = 0x12345678;
  return *reinterpret_cast<const uint8_t *>(&t) == 0x12;
}

static void fix_u16(uint16_t &value)
{
  if(!is_big_endian())
    value = __builtin_bswap16(value);
}

enum MOD_type
{
  MOD_PROTRACKER,       // M.K.
  MOD_FASTTRACKER_6CHN, // 6CHN
  MOD_FASTTRACKER_8CHN, // 8CHN
  MOD_FASTTRACKER_10CH, // 10CH
  MOD_FASTTRACKER_12CH, // 12CH
  MOD_FASTTRACKER_14CH, // 14CH
  MOD_FASTTRACKER_16CH, // 16CH
  MOD_STARTREKKER_EXO4, // EXO4
  MOD_STARTREKKER_FLT4, // FLT4
  MOD_STARTREKKER_FLT8, // FLT8
  WOW,                  // Mod's Grave M.K.
  MOD_UNKNOWN,          // Probably an ST 15-instrument MOD or ST 2.6 or something.
  NUM_MOD_TYPES
};

struct MOD_type_info
{
  const char magic[5];
  const char * const source;
  int channels; // -1=ignore this type ;-(
};

static const struct MOD_type_info TYPES[NUM_MOD_TYPES] =
{
  { "M.K.", "ProTracker",  4  },
  { "6CHN", "FastTracker", 6  },
  { "8CHN", "FastTracker", 8  },
  { "10CH", "FastTracker", 10 },
  { "12CH", "FastTracker", 12 },
  { "14CH", "FastTracker", 14 },
  { "16CH", "FastTracker", 16 },
  { "EXO4", "StarTrekker", -1 },
  { "FLT4", "StarTrekker", -1 },
  { "FLT8", "StarTrekker", -1 },
  { "M.K.", "Mod's Grave", 8  },
  { "",     "unknown",     -1 },
};

static int total_files;
static int type_count[NUM_MOD_TYPES];

static uint32_t pattern_size(uint32_t num_channels)
{
  return num_channels * 4 * 64;
}

enum MOD_error
{
  MOD_SUCCESS,
  MOD_SEEK_ERROR,
  MOD_READ_ERROR,
  MOD_INVALID_MAGIC,
  MOD_INVALID_ORDER_COUNT,
  MOD_IGNORE_MAGIC,
};

static const char *MOD_strerror(int err)
{
  switch(err)
  {
    case MOD_SUCCESS: return "no error";
    case MOD_SEEK_ERROR: return "seek error";
    case MOD_READ_ERROR: return "read error";
    case MOD_INVALID_MAGIC: return "file is not a 31-channel .MOD";
    case MOD_INVALID_ORDER_COUNT: return "invalid order count";
    case MOD_IGNORE_MAGIC: return "ignoring unsupported .MOD variant";
  }
  return "unknown error";
}


/**
 * Structs for raw data as provided in the files.
 * uint16_ts need to be byteswapped on little-endian machines.
 */
struct MOD_sample
{
  char name[22];          // NOTE: null-padded, but not necessarily null-terminated.
  uint16_t length;        // Half the actual length.
  uint8_t finetune;
  uint8_t volume;
  uint16_t repeat_start;  // Half the actual repeat start.
  uint16_t repeat_length; // Half the actual repeat length.
};

struct MOD_header
{
  // NOTE: space-padded, not null-terminated.
  char name[20];
  struct MOD_sample samples[31];
  uint8_t num_orders;
  uint8_t reserved;
  uint8_t orders[128];
  char magic[4];
};

struct MOD_sample_data
{
  char name[23];
  uint32_t real_length;
};

struct MOD_data
{
  char name[21];
  char magic[4];
  const char *type_source;
  enum MOD_type type;
  int type_channels;
  int pattern_count;
  ssize_t real_length;
  ssize_t expected_length;
  struct MOD_sample_data samples[31];

  struct MOD_header file_data;
};

int mod_read(struct MOD_data &d, FILE *fp)
{
  struct MOD_header &h = d.file_data;
  ssize_t running_length;
  int i;

  memset(&d, 0, sizeof(struct MOD_data));
  total_files++;

  if(fseek(fp, 0, SEEK_END))
    return MOD_SEEK_ERROR;

  d.real_length = ftell(fp);
  rewind(fp);
  if(errno)
    return MOD_SEEK_ERROR;

  if(!fread(&(h), sizeof(struct MOD_header), 1, fp))
    return MOD_READ_ERROR;

  // Determine initial guess for what the mod type is.
  for(i = 0; i < MOD_UNKNOWN; i++)
  {
    if(!memcmp(h.magic, TYPES[i].magic, 4))
      break;
  }
  if(i >= MOD_UNKNOWN)
  {
    type_count[MOD_UNKNOWN]++;
    return MOD_INVALID_MAGIC;
  }

  if(!h.num_orders || h.num_orders > 128)
  {
    O_("valid magic %4.4s but invalid order count %u\n", h.magic, h.num_orders);
    type_count[MOD_UNKNOWN]++;
    return MOD_INVALID_ORDER_COUNT;
  }

  memcpy(d.name, h.name, arraysize(h.name));
  d.name[arraysize(d.name) - 1] = '\0';

  d.type = static_cast<MOD_type>(i);
  memcpy(d.magic, h.magic, 4);
  d.type_source = TYPES[i].source;
  d.type_channels = TYPES[i].channels;
  if(d.type_channels < 0)
  {
    O_("unsupported .MOD variant: %s %4.4s.\n", d.type_source, d.magic);
    type_count[d.type]++;
    return MOD_IGNORE_MAGIC;
  }

  running_length = sizeof(struct MOD_header);

  // Get sample info.
  for(i = 0; i < 31; i++)
  {
    struct MOD_sample &hs = h.samples[i];
    struct MOD_sample_data &s = d.samples[i];

    fix_u16(hs.length);
    fix_u16(hs.repeat_start);
    fix_u16(hs.repeat_length);

    memcpy(s.name, hs.name, arraysize(hs.name));
    s.name[arraysize(s.name) - 1] = '\0';
    s.real_length = hs.length * 2;
    running_length += s.real_length;
  }

  // Determine pattern count.
  uint8_t max_pattern = 0;
  for(i = 0; i < h.num_orders; i++)
  {
    if(h.orders[i] > max_pattern)
      max_pattern = h.orders[i];
  }
  d.pattern_count = max_pattern + 1;

  // Calculate expected length.
  d.expected_length = running_length + d.pattern_count * pattern_size(d.type_channels);

  // Calculate expected length of a Mod's Grave .WOW to see if a M.K. file
  // is actually a stealth .WOW.
  if(d.type == MOD_PROTRACKER)
  {
    ssize_t wow_length = running_length + d.pattern_count * pattern_size(8);
    if(d.real_length >= wow_length)
    {
      d.type = WOW;
      d.type_channels = TYPES[WOW].channels;
      d.type_source = TYPES[WOW].source;
      d.expected_length = wow_length;
    }
  }

  O_("Name        : %s\n", d.name);
  O_("Type        : %s %4.4s (%d channels)\n", d.type_source, d.magic, d.type_channels);
  O_("Orders      : %u\n", h.num_orders);
  O_("Patterns    : %d\n", d.pattern_count);
  O_("File length : %zd\n", d.real_length);
  O_("Expected    : %zd\n", d.expected_length);
  O_("Difference  : %zd\n", d.real_length - d.expected_length);
  type_count[d.type]++;

/*
  for(i = 0; i < 31; i++)
    O_("Sample %2d   : %6u : %s\n", i, d.samples[i].real_length, d.samples[i].name);
*/
  return MOD_SUCCESS;
}

void check_mod(const char *filename)
{
  struct MOD_data d;

  FILE *fp = fopen(filename, "rb");
  if(fp)
  {
    O_("Checking '%s'.\n", filename);
    int err = mod_read(d, fp);
    if(!err)
      O_("Successfully read file.\n\n");
    else
      O_("Failed to read file: %s\n\n", MOD_strerror(err));

    fclose(fp);
  }
  else
    O_("Failed to open '%s'.\n", filename);
}


int main(int argc, char *argv[])
{
  int i;

  if(!argv || argc < 2)
  {
    fprintf(stderr, "%s", USAGE);
    return 0;
  }

  for(i = 1; i < argc; i++)
    check_mod(argv[i]);

  O_("%-16s : %d\n", "Total files", total_files);

  for(int i = 0; i < NUM_MOD_TYPES; i++)
    if(type_count[i])
      O_("%-11s %4.4s : %d\n", TYPES[i].source, TYPES[i].magic, type_count[i]);

  return 0;
}
