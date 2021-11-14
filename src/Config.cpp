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

#include "Config.hpp"
#include "modutil.hpp"

#include <ctype.h>
#include <stdlib.h>

ConfigInfo Config;

const char ConfigInfo::COMMON_FLAGS[] =
  "Common flags:\n"
  "  -q        Suppress text output. Overrides dumping flags.\n"
  "  -f=fmt... Filter by format loader extension and/or tag (see supported formats).\n"
  "            'fmt' can be a comma separated list or -f can be specified multiple\n"
  "            times to allow multiple formats.\n"
  "  -a[=N]    Enable/disable all dump vars at a given level (if not provided, N=1).\n"
  "  -d[=N]    Dump description. N=1 (optional) enables, N=0 disables (default).\n"
  "  -s[=N]    Dump sample info. N=1 (optional) enables, N=0 disables (default).\n"
  "  -p[=N]    Dump patterns. N=1 (optional) enables, N=0 disables (default).\n"
  "            N=2 additionally dumps the entire pattern as raw data.\n"
  "  -H=...    Highlight data in pattern dump. Highlight string is in the format\n"
  "            'C:#[,...]' where C indicates the column type to highlight and\n"
  "            # indicates the value to highlight (decimal). Valid column types:\n"
  "            n=note, s or i=instrument, v=volume, e or x=effect, p=param.\n"
  "            If e/x and p are combined, only lines with both will highlight.\n"
  "  -         Read filenames from stdin. Useful when there are too many files\n"
  "            for argv. Place after any other options if applicable.\n\n";

static char next_char(const char **str)
{
  while(isspace(**str))
    (*str)++;

  return *((*str)++);
}

static bool parse_int(char opt, const char *str, long *ret)
{
  if(str[0])
  {
    char *end;
    long val = strtol(str, &end, 10);
    if(!end || !end[0])
    {
      *ret = val;
      return true;
    }
  }
  format::error("invalid value for option -%c", opt);
  return false;
}

static bool parse_highlight(const char *str)
{
  // n=note, s/i=instrument, v=volume, e/x=effect p=param
  while(true)
  {
    int type = 0;
    char c = next_char(&str);
    if(!c)
      return false;

    switch(tolower(c))
    {
      case 'n':
        type = Highlight::NOTE;
        break;
      case 's':
      case 'i':
        type = Highlight::INSTRUMENT;
        break;
      case 'v':
        type = Highlight::VOLUME;
        break;
      case 'e':
      case 'x':
        type = Highlight::EFFECT;
        break;
      case 'p':
        type = Highlight::PARAMETER;
        break;
      default:
        return false;
    }

    c = next_char(&str);
    if(c != ':')
      return false;

    int idx = 0;
    bool has_digit = false;
    while((c = next_char(&str)))
    {
      if(!isdigit(c))
        break;

      idx = idx * 10 + (c - '0');
      has_digit = true;
    }
    if(idx > 255 || !has_digit)
      return false;

    Config.highlight_mask |= type;
    Config.highlight[idx] |= type;
    if(c == '\0')
      return true;

    if(c != ',')
      return false;
  }
}

void ConfigInfo::set_dump_descriptions(int level)
{
  dump_descriptions = (level >= 1);
  quiet = quiet && !dump_descriptions;
}

void ConfigInfo::set_dump_samples(int level)
{
  dump_samples = (level >= 1);
  quiet = quiet && !dump_samples;
}

void ConfigInfo::set_dump_patterns(int level)
{
  dump_patterns = (level >= 1);
  dump_pattern_rows = (level >= 2);
  quiet = quiet && !dump_patterns;
}

bool ConfigInfo::init(int *_argc, char **argv, bool (*handler)(const char *, void *), void *priv)
{
  int argc = *_argc;
  int new_argc = 1;
  long value;

  for(int i = 1; i < argc; i++)
  {
    char *arg = argv[i];
    if(arg[0] == '-' && arg[1])
    {
      if(handler)
      {
        if(handler(arg, priv))
          continue;
      }

      switch(arg[1])
      {
        /* Highlight pattern dump. */
        case 'H':
          if(arg[2] == '=')
          {
            if(parse_highlight(arg + 3))
              continue;
          }
          else

          if(i + 1 < argc)
          {
            if(parse_highlight(argv[i + 1]))
            {
              i++;
              continue;
            }
          }
          format::error("invalid config for -H");
          return false;

        /* Dump all. */
        case 'a':
          value = 1;
          if(arg[2] == '=' && !parse_int(arg[1], arg + 3, &value))
            return false;

          set_dump_descriptions(value);
          set_dump_patterns(value);
          set_dump_samples(value);
          continue;

        /* Dump description text. */
        case 'd':
          value = 1;
          if(arg[2] == '=' && !parse_int(arg[1], arg + 3, &value))
            return false;

          set_dump_descriptions(value);
          continue;

        /* Dump pattern/order info. */
        case 'p':
          value = 1;
          if(arg[2] == '=' && !parse_int(arg[1], arg + 3, &value))
            return false;

          set_dump_patterns(value);
          continue;

        /* Dump sample/instrument info. */
        case 's':
          value = 1;
          if(arg[2] == '=' && !parse_int(arg[1], arg + 3, &value))
            return false;

          set_dump_samples(value);
          continue;

        /* Suppress text output. */
        case 'q':
          set_dump_descriptions(0);
          set_dump_patterns(0);
          set_dump_samples(0);
          quiet = true;
          continue;

        /* Filter by format. */
        case 'f':
        {
          if(arg[2] != '=')
            break;

          char *current = arg + 3;
          while(num_format_filters < MAX_FORMAT_FILTERS)
          {
            char *delim = strpbrk(current, ",");
            format_filter[num_format_filters++] = current;
            if(!delim)
              break;

            *(delim++) = '\0';
            current = delim;
          }
          continue;
        }
      }
      format::error("unknown option '%s'!", argv[i]);
      return false;
    }
    argv[new_argc++] = argv[i];
  }

  *_argc = new_argc;
  return true;
}
