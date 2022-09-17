/**
 * Copyright (C) 2021-2022 Lachesis <petrifiedrowan@gmail.com>
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

#include <assert.h>
#include <ctype.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <memory>
#include <vector>

#include <fcntl.h>

#define ERROR(...) do{ fprintf(stderr, __VA_ARGS__); fflush(stderr); exit(-1); }while(0)

#ifdef DEBUG
#undef DEBUG
#define DEBUG(...) do{ fprintf(stderr, __VA_ARGS__); fflush(stderr); }while(0)
#else
#define DEBUG(...)
#endif

static constexpr int NUM_SAMPLES = 63;
static constexpr int MAX_ORDERS = 4096;
static constexpr int MAX_PATTERNS = 4096;
static constexpr size_t MAX_COMMENTS = 256; // FIXME: is there an actual line limit?

struct sample
{
  bool present;
  uint8_t  type;
  unsigned length;
  unsigned loop_start;
  unsigned loop_length;
  unsigned volume;
  int finetune;
  char name[32];
  char filename[256];
  bool input_is_16bit;
};

struct pattern
{
  uint32_t events[64];

  static uint32_t pack(int line, unsigned note, unsigned sample, unsigned effect, unsigned param)
  {
    if(note >= 37)
      ERROR("line %d: invalid note %u\n", line, note);
    if(sample >= 64)
      ERROR("line %d: invalid sample %u\n", line, sample);
    if(effect >= 64)
      ERROR("line %d: invalid effect %u\n", line, effect);
    if(param >= 4096)
      ERROR("line %d: invalid param %u\n", line, param);

    return note | (sample << 6) | (effect << 14) | (param << 20);
  }
};

struct comment
{
  char text[64];
};

struct dsym
{
  char name[256];
  unsigned version;
  unsigned num_channels;
  unsigned num_orders;
  unsigned num_patterns;
  unsigned orders[4096][8];
  pattern patterns[MAX_PATTERNS];
  sample samples[NUM_SAMPLES];
  comment comments[MAX_COMMENTS];
  size_t comment_length;
  size_t num_comment_lines;

  // Loader state.
  bool current_is_comment;
  int current_pattern;
  int pos_in_pattern;
  int line;
};

static void fput16(int value, FILE *fp)
{
  fputc(value & 0xff, fp);
  fputc(value >> 8, fp);
}

static void fput24(int value, FILE *fp)
{
  fputc(value & 0xff, fp);
  fputc((value & 0x00ff00) >> 8, fp);
  fputc((value & 0xff0000) >> 16, fp);
}

static void fput32(uint32_t value, FILE *fp)
{
  fputc(value & 0xff, fp);
  fputc((value & 0x0000ff00UL) >> 8, fp);
  fputc((value & 0x00ff0000UL) >> 16, fp);
  fputc((value & 0xff000000UL) >> 24, fp);
}

/* Sigma-delta 8-bit sample compression. */
struct bitstream
{
  std::vector<uint8_t> &out;
  uint64_t buf = 0;
  unsigned pos = 0;

  bitstream(std::vector<uint8_t> &o): out(o) {}

  void flush()
  {
    out.push_back((buf & 0x000000ffUL) >> 0);
    out.push_back((buf & 0x0000ff00UL) >> 8);
    out.push_back((buf & 0x00ff0000UL) >> 16);
    out.push_back((buf & 0xff000000UL) >> 24);
    buf >>= 32;
    pos -= 32;
  }

  void write(uint64_t value, unsigned width)
  {
    buf |= value << pos;
    pos += width;

    if(pos >= 32)
      flush();
  }
};

static void sigma_delta_compress(std::vector<uint8_t> &out,
 const std::vector<uint8_t> &in, unsigned max_runlength)
{
  if(in.size() < 1)
    return;

  bitstream stream(out);
  unsigned width = 8;
  int8_t delta_min = -127;
  int8_t delta_max = 127;
  unsigned runlength = 0;
  uint8_t prev = in[0];

  out.push_back(max_runlength);
  stream.write(in[0], 8);

  for(size_t i = 1; i < in.size(); i++)
  {
    int8_t delta = static_cast<int8_t>(in[i] - prev);
    prev = in[i];

    if(delta == -128)
    {
      /* Pretend this edge case didn't happen... */
      prev++;
      delta = -127;
    }

    while(delta < delta_min || delta > delta_max)
    {
      /* Expand width. */
      assert(width < 8);
      stream.write(0, width);

      width++;
      delta_max = (1 << (width - 1)) - 1;
      delta_min = -delta_max;
      runlength = 0;
      //DEBUG("  %u: expand width to %d\n", i, width);
    }

    /* Output delta. */
    uint8_t code;
    if(delta <= 0)
    {
      code = 0x01 | (static_cast<uint8_t>(-delta) << 1);
      stream.write(code, width);
    }
    else
    {
      code = static_cast<uint8_t>(delta) << 1;
      stream.write(code, width);
    }
    //DEBUG("  %u: write %02x\n", i, code);

    /* Reset the run length for large values. */
    if(code >> (width - 1))
    {
      runlength = 0;
      continue;
    }

    /* Otherwise, increment the run length. */
    if(++runlength >= max_runlength)
    {
      runlength = 0;

      /* Shrink width. */
      if(width > 1)
      {
        width--;
        delta_max = (1 << (width - 1)) - 1;
        delta_min = -delta_max;
        //DEBUG("  %u: shrink width to %d\n", i, width);
      }
    }
  }

  /* Output any remaining bytes + padding. */
  stream.flush();
}

static void sigma_delta_compress(std::vector<uint8_t> &out,
 const std::vector<uint8_t> &in)
{
  /* Brute force run lengths to find the smallest encoding. */
  unsigned max = 255;
  if(in.size() < max)
    max = in.size();

  for(unsigned i = 1; i < max; i++)
  {
    std::vector<uint8_t> tmp;
    tmp.reserve(in.size());

    sigma_delta_compress(tmp, in, i);

    if(tmp.size() < out.size() || out.size() == 0)
    {
      DEBUG("  using max_runlength=%u, output size=%zu\n", i, tmp.size());
      out = std::move(tmp);
    }
  }
}

/* Template parsing. */

static void read_events(dsym &m, pattern &p, const char *pos)
{
  size_t i;
  for(i = m.pos_in_pattern; i < 64; i++)
  {
    int note = 0;
    int sample = 0;
    int effect = 0;
    int param = 0;
    if(sscanf(pos, " %i %i %i %i |", &note, &sample, &effect, &param) > 0)
      p.events[i] = pattern::pack(m.line, note, sample, effect, param);

    pos = strchr(pos, '|');
    if(!pos)
    {
      i++;
      break;
    }
    pos++;
  }
  m.pos_in_pattern = i;
}

static void read_comment_line(dsym &m, const char *pos)
{
  if(m.num_comment_lines >= MAX_COMMENTS)
    ERROR("line %d: exceeded maximum comment lines\n", m.line);

  comment &c = m.comments[m.num_comment_lines++];
  snprintf(c.text, sizeof(c.text), "%s", pos);

  size_t len = strlen(c.text);
  while(len > 0 && isspace(c.text[len - 1]))
    c.text[--len] = '\0';

  m.comment_length += len + 1; /* +1 for newline */
  DEBUG("comment: %s\n", c.text);
}

static char *get_data_pos(dsym &m, char *line)
{
  char *pos = strstr(line, "::");
  if(!pos)
    ERROR("line %d: error parsing line\n", m.line);
  pos += 2;

  /* Skip zero or one space character(s). */
  if(*pos && isspace(*pos))
    pos++;

  return pos;
}

static bool read_line(dsym &m, FILE *in)
{
  char linebuf[2048];
  char str[256];
  char *pos;
  int num;

  if(!fgets(linebuf, sizeof(linebuf), in))
    return false;

  m.line++;
  if(linebuf[0] == '#')
    return true;

  size_t len = strlen(linebuf);
  while(len && (linebuf[len - 1] == '\n' || linebuf[len - 1] == '\r'))
    linebuf[--len] = '\0';

  if(sscanf(linebuf, "%255s %i ::", str, &num) == 2 && strcmp(str, "::"))
  {
    if(num < 0)
      ERROR("line %d: invalid negative target %d\n", m.line, num);

    m.current_pattern = -1;
    m.current_is_comment = false;

    pos = get_data_pos(m, linebuf);

    if(!strcasecmp(str, "ORDER"))
    {
      DEBUG("order %d\n", num);
      if(num < 0 || num >= MAX_ORDERS)
        ERROR("line %d: invalid order %u\n", m.line, num);

      if(m.num_channels == 0)
        ERROR("line %d: ORDER must follow VOICES\n", m.line);

      if(m.num_orders < (unsigned)num + 1)
        m.num_orders = num + 1;

      unsigned order = num;

      int num_read = sscanf(pos, " %i %i %i %i %i %i %i %i",
        (int *)m.orders[order],     (int *)m.orders[order] + 1,
        (int *)m.orders[order] + 2, (int *)m.orders[order] + 3,
        (int *)m.orders[order] + 4, (int *)m.orders[order] + 5,
        (int *)m.orders[order] + 6, (int *)m.orders[order] + 7);

      if(num_read < 0 || (unsigned)num_read < m.num_channels)
        ERROR("line %d: error reading order %u: %d\n", m.line, order, num_read);

      for(size_t i = 0; i < m.num_channels; i++)
      {
        if(m.orders[order][i] > MAX_PATTERNS) // Allow =MAX_PATTERNS for blank.
          ERROR("line %d: voice %zu in order %u is invalid pattern %u\n", m.line, i + 1, order, num);
      }
    }
    else

    if(!strcasecmp(str, "PATTERN"))
    {
      DEBUG("pattern %d\n", num);
      if(num < 0 || num >= MAX_PATTERNS)
        ERROR("line %d: invalid pattern %d\n", m.line, num);

      if(m.num_patterns < (unsigned)num + 1)
        m.num_patterns = num + 1;

      m.current_pattern = num;
      m.pos_in_pattern = 0;

      pattern &p = m.patterns[num];
      read_events(m, p, pos);
    }
    else

    if(!strcasecmp(str, "SAMPLE") || !strcasecmp(str, "SAMP16") ||
     !strcasecmp(str, "SIGMA8") || !strcasecmp(str, "SIGM16") ||
     !strcasecmp(str, "SIGLN8") || !strcasecmp(str, "SIGL16"))
    {
      DEBUG("sample %d\n", num);
      if(num < 1 || num > 63)
        ERROR("line %d: invalid sample %d\n", m.line, num);

      sample &s = m.samples[num - 1];

      s.type = 2;
      if(!strcasecmp(str, "SAMP16")) s.type = 3, s.input_is_16bit = true;
      if(!strcasecmp(str, "SIGMA8")) s.type = 4;
      if(!strcasecmp(str, "SIGM16")) s.type = 4, s.input_is_16bit = true;
      if(!strcasecmp(str, "SIGLN8")) s.type = 5;
      if(!strcasecmp(str, "SIGL16")) s.type = 5, s.input_is_16bit = true;

      s.present = true;
      if(sscanf(pos, " %i %i %i %i %d %255s %31s",
         (int *)&s.length, (int *)&s.loop_start, (int *)&s.loop_length,
         (int *)&s.volume, &s.finetune, s.filename, s.name) < 6)
        ERROR("line %d: error reading sample %u\n", m.line, num);

      DEBUG("  type:%u 16bit:%d len:%u loopstart:%u looplen:%u vol:%u finetune:%d filename:'%s' name:'%s'\n",
       s.type, s.input_is_16bit, s.length, s.loop_start, s.loop_length, s.volume, s.finetune, s.filename, s.name);

      if(s.volume > 64)
        ERROR("line %d: invalid volume for sample %u: %u\n", m.line, num, s.volume);
      if(s.finetune < -8 || s.finetune > 7)
        ERROR("line %d: invalid finetune for sample %u: %d\n", m.line, num, s.finetune);
      /*
      if(strlen(s.name) > 63)
        ERROR("line %d: name for sample %u exceeds 63 bytes\n", m.line, num);
      */
    }
    else
      ERROR("line %d: unknown field '%s'\n", m.line, str);
  }
  else

  if(sscanf(linebuf, "%255s ::", str) == 1 && strcmp(str, "::"))
  {
    m.current_pattern = -1;
    m.current_is_comment = false;

    pos = get_data_pos(m, linebuf);

    if(!strcasecmp(str, "NAME"))
    {
      if(sscanf(pos, " %255[^\n]s", m.name) < 1)
        ERROR("line %d: error reading module name\n", m.line);

      DEBUG("name %s\n", m.name);
    }
    else

    if(!strcasecmp(str, "VOICES"))
    {
      if(sscanf(pos, " %i", &num) < 1)
        ERROR("line %d: malformed VOICES line\n", m.line);
      if(num < 1 || num > 8)
        ERROR("line %d: invalid VOICES %d\n", m.line, num);
      if(m.num_channels != 0)
        ERROR("line %d: duplicate VOICES\n", m.line);

      m.num_channels = num;
      DEBUG("voices %d\n", num);
    }
    else

    if(!strcasecmp(str, "COMMENT") || !strcasecmp(str, "DESC"))
    {
      m.current_is_comment = true;
      read_comment_line(m, pos);
    }
    else
      ERROR("line %d: unknown field '%s'\n", m.line, str);
  }
  else

  if(sscanf(linebuf, " %255s", str) == 1 && !strcmp(str, "::"))
  {
    // Continue previous line.
    pos = get_data_pos(m, linebuf);

    if(m.current_pattern >= 0)
    {
      DEBUG("  continuing pattern %d from row %d\n", m.current_pattern, m.pos_in_pattern);
      pattern &p = m.patterns[m.current_pattern];
      read_events(m, p, pos);
    }
    else

    if(m.current_is_comment)
    {
      read_comment_line(m, pos);
    }
    else
      ERROR("line %d: invalid extension line\n", m.line);
  }

  return true;
}

int main()
{
  static dsym m{};
  unsigned i;

#ifdef _WIN32
  _setmode(_fileno(stdout), _O_BINARY);
#endif

  /* Header */
  if(fscanf(stdin, "BASSTRAK v%u\n", &m.version) < 1)
    ERROR("not a Digital Symphony template.\n");

  if(m.version > 1)
    ERROR("invalid Digital Symphony version (valid values are 0 and 1)\n");

  while(read_line(m, stdin));
  fclose(stdin);

  DEBUG("writing module\n");

  /* Write */
  fputs("\x02\x01\x13\x13\x14\x12\x01\x0b", stdout);
  fputc(m.version, stdout);
  fputc(m.num_channels, stdout);
  fput16(m.num_orders, stdout);
  fput16(m.num_patterns, stdout);
  fput24(m.comment_length, stdout);

  for(i = 0; i < NUM_SAMPLES; i++)
  {
    sample &s = m.samples[i];

    int flags = s.present ? 0 : 0x80;
    int len = strlen(s.name);

    fputc(flags | len, stdout);
    if(s.present)
      fput24(s.length >> 1, stdout);
  }

  fputc(strlen(m.name), stdout);
  fputs(m.name, stdout);
  fputs("\xff\xff\xff\xff\xff\xff\xff\xff", stdout); /* Effects allowed table. */

  if(m.num_orders > 0)
  {
    DEBUG("writing %u orders\n", m.num_orders);
    fputc(0, stdout); // Packing method.
    for(i = 0; i < m.num_orders; i++)
      for(size_t j = 0; j < m.num_channels; j++)
        fput16(m.orders[i][j], stdout);
  }

  if(m.num_patterns > 0)
  {
    DEBUG("writing %u patterns\n", m.num_patterns);
    for(i = 0; i < m.num_patterns;)
    {
      DEBUG("  block of %u\n", (m.num_patterns - i) < 2000 ? (m.num_patterns - i) : 2000);
      fputc(0, stdout); // Packing method.
      for(size_t j = 0; i < m.num_patterns && j < 2000; i++, j++)
      {
        pattern &p = m.patterns[i];
        for(size_t r = 0; r < 64; r++)
          fput32(p.events[r], stdout);
      }
    }
  }

  for(i = 0; i < NUM_SAMPLES; i++)
  {
    sample &s = m.samples[i];
    fputs(s.name, stdout);
    if(!s.present)
      continue;

    DEBUG("writing sample %u\n", i);

    fput24(s.loop_start >> 1, stdout);
    fput24(s.loop_length >> 1, stdout);
    fputc(s.volume, stdout);
    fputc((signed char)s.finetune, stdout);

    if(!s.length)
      continue;

    fputc(s.type, stdout); // Sample packing type.

    size_t buf_size = s.length;
    if(s.input_is_16bit)
      buf_size *= 2;

    std::vector<uint8_t> buf(buf_size);

    FILE *fp = fopen(s.filename, "rb");
    if(fp)
    {
      if(!fread(buf.data(), buf_size, 1, fp))
        ERROR("read error for sample file '%s'\n", s.filename);

      fclose(fp);
    }
    else
      ERROR("failed to open sample file '%s'\n", s.filename);

    switch(s.type)
    {
      case 0: // signed uncompressed 8-bit log
      case 2: // signed uncompressed 8-bit
      case 3: // signed uncompressed 16-bit
        fwrite(buf.data(), buf_size, 1, stdout);
        break;

      case 4: // unsigned sigma-delta 8-bit
      {
        DEBUG("  linear sigma-delta\n");
        if(s.input_is_16bit)
        {
          DEBUG("  truncating 16-bit to 8-bit\n");
          for(size_t j = 0; j < s.length; j++)
            buf[j] = buf[j * 2 + 1];

          buf.resize(s.length);
        }
        /* Convert signed to unsigned. */
        for(size_t j = 0; j < s.length; j++)
          buf[j] += 128;

        std::vector<uint8_t> compressed;
        sigma_delta_compress(compressed, buf);

        fwrite(compressed.data(), compressed.size(), 1, stdout);
        break;
      }

      case 5: // unsigned sigma-delta 8-bit log
      {
        /* Convert to 8-bit mu-law variant.
         * This doesn't match mu-law or the Archimedes log sample format,
         * and seems to be designed specifically to have a continuous int
         * representation for better sigma-delta compression.
         *
         * 0x00..0x7f -> 32767 .. 0
         * 0x80..0xff ->     0 .. -32768 */
        static constexpr double logbase = 5.5451774444796; /* log(1.0 + 255.0) */
        static constexpr double int8base = INT8_MAX + 1.0;
        static constexpr double int16base = INT16_MAX + 1.0;

        DEBUG("  logarithmic sigma-delta\n");

        if(s.input_is_16bit)
        {
          DEBUG("  converting 16-bit linear to 8-bit logarithmic\n");
          for(size_t j = 0; j < s.length; j++)
          {
            int16_t v          = buf[j * 2] | (buf[j * 2 + 1] << 8);
            double amp_norm    = ((v >= 0) ? v : -v) / int16base;
            double amp_norm_ln = log(1.0 + 255.0 * amp_norm) / logbase;
            unsigned amp_out   = static_cast<unsigned>(amp_norm_ln * 127.0);
            buf[j] = (v > 0) ? (amp_out ^ 0x7f) : (0x80 | amp_out);
          }
          buf.resize(s.length);
        }
        else
        {
          DEBUG("  converting 8-bit linear to 8-bit logarithmic\n");
          for(size_t j = 0; j < s.length; j++)
          {
            int8_t v           = buf[j];
            double amp_norm    = ((v >= 0) ? v : -v) / int8base;
            double amp_norm_ln = log(1.0 + 255.0 * amp_norm) / logbase;
            unsigned amp_out   = static_cast<unsigned>(amp_norm_ln * 127.0);
            buf[j] = (v > 0) ? (amp_out ^ 0x7f) : (0x80 | amp_out);
          }
        }

        std::vector<uint8_t> compressed;
        sigma_delta_compress(compressed, buf);

        fwrite(compressed.data(), compressed.size(), 1, stdout);
        break;
      }

      case 1: // signed LZW 8-bit
      default:
        ERROR("unsupported sample type %u!", s.type);
    }
  }

  if(m.comment_length > 0)
  {
    DEBUG("writing comment, length %zu\n", m.comment_length);

    fputc(0, stdout); // Packing method.

    for(i = 0; i < m.num_comment_lines; i++)
      fprintf(stdout, "%s\n", m.comments[i].text);

    for(i = m.comment_length & 3; i & 3; i++)
      fputc(0, stdout);
  }

  return 0;
}
