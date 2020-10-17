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

#include "common.hpp"

static const char USAGE[] =
  "wowutil, a utility to examine .MOD or .WOW files to\n"
  "determine basic size information about them. The intention\n"
  "is to be able to objectively tell if a M.K. .MOD file is\n"
  "actually a stealth .WOW file (or vice versa).\n\n"
  "Usage:\n"
  "  wowutil [.MOD and/or .WOW files...]\n\n"
  "A list of filenames to check can also be provided via stdin:\n"
  "  ls -1 *.mod | wowutil -\n";

enum MOD_type
{
  MOD_PROTRACKER,       // M.K.
  MOD_PROTRACKER_EXT,   // M!K!
  MOD_NOISETRACKER_EXT, // M&K!
  MOD_FASTTRACKER_XCHN, // 2CHN, 6CHN, 8CHN, etc.
  MOD_FASTTRACKER_XXCH, // 10CH, 16CH, 32CH, etc.
  MOD_OCTALYSER_CD61,   // CD61
  MOD_OCTALYSER_CD81,   // CD81
  MOD_OKTA,             // OKTA (Oktalyzer?)
  MOD_OCTA,             // OCTA (OctaMED?)
  MOD_STARTREKKER_EXO4, // EXO4
  MOD_STARTREKKER_FLT4, // FLT4
  MOD_STARTREKKER_FLT8, // FLT8
  MOD_HMN,              // His Master's Noise FEST
  MOD_LARD,             // Signature found in judgement_day_gvine.mod. It's a normal 4-channel MOD.
  MOD_NSMS,             // Signature found in kingdomofpleasure.mod. It's a normal 4-channel MOD.
  WOW,                  // Mod's Grave M.K.
  MOD_SOUNDTRACKER,     // ST 15-instrument .MOD, no signature.
  MOD_SOUNDTRACKER_26,  // Soundtracker 2.6 MTN\0
  MOD_ICETRACKER_IT10,  // Icetracker 1.x IT10
  MOD_UNKNOWN,          // ?
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
  { "M!K!", "ProTracker",  4  },
  { "M&K!", "NoiseTracker",4  },
  { "xCHN", "FastTracker", 0  },
  { "xxCH", "FastTracker", 0  },
  { "CD61", "Octalyser",   6  },
  { "CD81", "Octalyser",   8  },
  { "OKTA", "Oktalyzer?",  8  },
  { "OCTA", "OctaMED?",    8  },
  { "EXO4", "StarTrekker", 4  },
  { "FLT4", "StarTrekker", 4  },
  { "FLT8", "StarTrekker", 8  },
  { "FEST", "HMN",         4  },
  { "LARD", "Unknown 4ch", 4  },
  { "NSMS", "Unknown 4ch", 4  },
  { "M.K.", "Mod's Grave", 8  },
  { "",     "SoundTracker",4  },
  { "",     "ST 2.6",      -1 },
  { "",     "IceTracker",  -1 },
  { "",     "unknown",     -1 },
};

static int total_files;
static int total_files_nonzero_diff;
static int total_files_fp_diff;
static int type_count[NUM_MOD_TYPES];

static constexpr uint32_t pattern_size(uint32_t num_channels)
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
  MOD_IGNORE_ST,
  MOD_IGNORE_ST26,
  MOD_IGNORE_IT10,
  MOD_IGNORE_MAGIC,
};

static const char *MOD_strerror(int err)
{
  switch(err)
  {
    case MOD_SUCCESS: return "no error";
    case MOD_SEEK_ERROR: return "seek error";
    case MOD_READ_ERROR: return "read error";
    case MOD_INVALID_MAGIC: return "file is not 31-inst .MOD";
    case MOD_INVALID_ORDER_COUNT: return "invalid order count";
    case MOD_IGNORE_ST: return "ignoring ST 15-inst .MOD";
    case MOD_IGNORE_ST26: return "ignoring ST 2.6 .MOD";
    case MOD_IGNORE_IT10: return "ignoring IceTracker .MOD";
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
  uint8_t restart_byte;
  uint8_t orders[128];
  unsigned char magic[4];
};

struct ST_header
{
  char name[20];
  struct MOD_sample samples[15];
  uint8_t num_orders;
  uint8_t song_speed;
  uint8_t orders[128];
};

struct MOD_sample_data
{
  char name[23];
  uint32_t real_length;
};

struct MOD_data
{
  char name[21];
  char name_clean[21];
  unsigned char magic[4];
  const char *type_source;
  enum MOD_type type;
  int type_channels;
  int pattern_count;
  ssize_t real_length;
  ssize_t expected_length;
  ssize_t samples_length;
  struct MOD_sample_data samples[31];

  struct MOD_header file_data;
};

bool mod_strip_name(char *dest, size_t dest_len)
{
  size_t start = 0;
  size_t end = strlen(dest);

  if(end > dest_len)
    return false;

  // Strip non-ASCII chars and whitespace from the start.
  for(; start < end; start++)
    if(dest[start] >= 0x21 && dest[start] <= 0x7E)
      break;

  // Strip non-ASCII chars and whitespace from the end.
  for(; start < end; end--)
    if(dest[end - 1] >= 0x21 && dest[end - 1] < 0x7E)
      break;

  // Move the buffer to the start of the string, stripping non-ASCII
  // chars and combining spaces.
  size_t i = 0;
  size_t j = start;
  while(i < dest_len - 1 && j < end)
  {
    if(dest[j] == ' ')
    {
      while(dest[j] == ' ')
        j++;
      dest[i++] = ' ';
    }
    else

    if(dest[j] >= 0x21 && dest[j] <= 0x7E)
      dest[i++] = dest[j++];

    else
      j++;
  }
  dest[i] = '\0';
  return true;
}

bool is_ST_mod(ST_header *h)
{
  // Try to filter out ST mods based on sample data bounding.
  for(int i = 0; i < 15; i++)
  {
    uint16_t sample_length = h->samples[i].length;
    fix_u16be(sample_length);

    if(h->samples[i].finetune || h->samples[i].volume > 64 ||
     sample_length > 32768)
      return false;
  }
  // Make sure the order count and pattern numbers aren't nonsense.
  if(!h->num_orders || h->num_orders > 128)
    return false;

  for(int i = 0; i < 128; i++)
    if(h->orders[i] >= 0x80)
      return false;

  return true;
}

int mod_read(struct MOD_data &d, FILE *fp)
{
  struct MOD_header &h = d.file_data;
  bool maybe_wow = true;
  ssize_t samples_length = 0;
  ssize_t running_length;
  int i;

  memset(&d, 0, sizeof(struct MOD_data));
  total_files++;

  if(fseek(fp, 0, SEEK_END))
    return MOD_SEEK_ERROR;

  d.real_length = ftell(fp);
  errno = 0;
  rewind(fp);
  if(errno)
    return MOD_SEEK_ERROR;

  if(!fread(&(h), sizeof(struct MOD_header), 1, fp))
    return MOD_READ_ERROR;

  memcpy(d.name, h.name, arraysize(h.name));
  d.name[arraysize(d.name) - 1] = '\0';
  memcpy(d.name_clean, d.name, arraysize(d.name));
  memcpy(d.magic, h.magic, 4);
  if(!mod_strip_name(d.name_clean, arraysize(d.name_clean)))
    d.name_clean[0] = '\0';

  // Determine initial guess for what the mod type is.
  for(i = 0; i < MOD_UNKNOWN; i++)
  {
    if(TYPES[i].magic[0] && !memcmp(h.magic, TYPES[i].magic, 4))
      break;
  }

  // Check for FastTracker xCHN and xxCH magic formats.
  if(isdigit(h.magic[0]) && !memcmp(h.magic + 1, "CHN", 3))
  {
    d.type = MOD_FASTTRACKER_XCHN;
    d.type_source = TYPES[d.type].source;
    d.type_channels = h.magic[0] - '0';
  }
  else

  if(h.magic[0] >= '1' && h.magic[0] <= '3' && isdigit(h.magic[1]) && h.magic[2] == 'C' && h.magic[3] == 'H')
  {
    d.type = MOD_FASTTRACKER_XXCH;
    d.type_source = TYPES[d.type].source;
    d.type_channels = (h.magic[0] - '0') * 10 + (h.magic[1] - '0');
  }
  else

  if(i < MOD_UNKNOWN)
  {
    d.type = static_cast<MOD_type>(i);
    d.type_source = TYPES[i].source;
    d.type_channels = TYPES[i].channels;
  }
  else
  {
    if(is_ST_mod(reinterpret_cast<ST_header *>(&h)))
    {
      type_count[MOD_SOUNDTRACKER]++;
      return MOD_IGNORE_ST;
    }

    // No? Maybe an ST 2.6 mod...
    if(fseek(fp, 1464, SEEK_SET))
      return MOD_SEEK_ERROR;
    uint8_t tmp[4];
    if(!fread(tmp, 4, 1, fp))
      return MOD_READ_ERROR;
    if(!memcmp(tmp, "MTN\x00", 4))
    {
      type_count[MOD_SOUNDTRACKER_26]++;
      return MOD_IGNORE_ST26;
    }
    if(!memcmp(tmp, "IT10", 4))
    {
      type_count[MOD_ICETRACKER_IT10]++;
      return MOD_IGNORE_IT10;
    }

    O_("unknown/invalid magic %2x %2x %2x %2x\n", h.magic[0], h.magic[1], h.magic[2], h.magic[3]);
    type_count[MOD_UNKNOWN]++;
    return MOD_INVALID_MAGIC;
  }

  if(d.type_channels <= 0 || d.type_channels > 32)
  {
    O_("unsupported .MOD variant: %s %4.4s.\n", d.type_source, d.magic);
    type_count[d.type]++;
    return MOD_IGNORE_MAGIC;
  }

  if(!h.num_orders || h.num_orders > 128)
  {
    O_("valid magic %4.4s but invalid order count %u\n", h.magic, h.num_orders);
    type_count[MOD_UNKNOWN]++;
    return MOD_INVALID_ORDER_COUNT;
  }

  running_length = sizeof(struct MOD_header);

  // Get sample info.
  for(i = 0; i < 31; i++)
  {
    struct MOD_sample &hs = h.samples[i];
    struct MOD_sample_data &s = d.samples[i];

    fix_u16be(hs.length);
    fix_u16be(hs.repeat_start);
    fix_u16be(hs.repeat_length);

    memcpy(s.name, hs.name, arraysize(hs.name));
    s.name[arraysize(s.name) - 1] = '\0';
    s.real_length = hs.length * 2;
    samples_length += s.real_length;
    running_length += s.real_length;

    /**
     * .669s don't have sample volume or finetune, so every .WOW has
     * 0x00 and 0x40 for these bytes when the sample exists.
     */
    if(hs.length && (hs.finetune != 0x00 || hs.volume != 0x40))
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
  d.pattern_count = max_pattern + 1;

  // Calculate expected length.
  d.expected_length = running_length + d.pattern_count * pattern_size(d.type_channels);
  d.samples_length = samples_length;

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
  if(d.type == MOD_PROTRACKER && h.restart_byte == 0x00 && maybe_wow)
  {
    ssize_t wow_length = running_length + d.pattern_count * pattern_size(8);
    if((d.real_length & ~1) == wow_length)
    {
      d.type = WOW;
      d.type_channels = TYPES[WOW].channels;
      d.type_source = TYPES[WOW].source;
      d.expected_length = wow_length;
    }
  }

  ssize_t difference = d.real_length - d.expected_length;

  /**
   * Check for .MODs with lengths that would be a potential false positive for
   * .WOW detection.
   */
  ssize_t threshold = d.pattern_count * pattern_size(4);
  bool fp_diff = (difference > 0) && ((difference & ~1) == threshold);

  if(fp_diff)
    total_files_fp_diff++;
  if(difference)
    total_files_nonzero_diff++;

  O_("Name      : %s\n", d.name_clean);
  O_("Type      : %s %4.4s (%d ch.)\n", d.type_source, d.magic, d.type_channels);
  O_("Orders    : %u (0x%02x)\n", h.num_orders, h.restart_byte);
  O_("Patterns  : %d\n", d.pattern_count);
  O_("Sample sz.: %zd\n", d.samples_length);
  O_("File size : %zd\n", d.real_length);
  O_("Expected  : %zd\n", d.expected_length);
  O_("Difference: %zd%s%s\n\n",
    difference,
    difference ? " (!=0)" : "",
    fp_diff ? " (!!!)" : ""
  );
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
    O_("File '%s'.\n", filename);
    int err = mod_read(d, fp);
    if(err)
      O_("Error: %s\n\n", MOD_strerror(err));

    fclose(fp);
  }
  else
    O_("Failed to open '%s'.\n", filename);
}


int main(int argc, char *argv[])
{
  bool has_read_stdin = false;
  int i;

  if(!argv || argc < 2)
  {
    fprintf(stderr, "%s", USAGE);
    return 0;
  }

  for(i = 1; i < argc; i++)
  {
    if(!strcmp(argv[i], "-"))
    {
      if(!has_read_stdin)
      {
        char namebuffer[1024];
        while(fgets_safe(namebuffer, stdin))
          check_mod(namebuffer);
        has_read_stdin = true;
      }
      continue;
    }
    check_mod(argv[i]);
  }

  O_("%-18s : %d\n", "Total files", total_files);
  if(total_files_nonzero_diff)
    O_("%-18s : %d\n", "Nonzero difference", total_files_nonzero_diff);
  if(total_files_fp_diff)
    O_("%-18s : %d\n", "False positive?", total_files_fp_diff);
  O_("\n");

  for(int i = 0; i < NUM_MOD_TYPES; i++)
    if(type_count[i])
      O_("%-13s %4.4s : %d\n", TYPES[i].source, TYPES[i].magic, type_count[i]);

  return 0;
}
