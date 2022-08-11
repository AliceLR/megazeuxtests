/**
 * Copyright (C) 2020 Lachesis <petrifiedrowan@gmail.com>
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

#ifndef MZXTEST_CONFIG_HPP
#define MZXTEST_CONFIG_HPP

#include <stdint.h>

namespace Highlight
{
  enum
  {
    NOTE       = (1<<0),
    INSTRUMENT = (1<<1),
    VOLUME     = (1<<2),
    EFFECT     = (1<<3),
    PARAMETER  = (1<<4),
  };
}

struct ConfigInfo final
{
  static const char COMMON_FLAGS[];
  static constexpr int MAX_FORMAT_FILTERS = 32;

  bool quiet = false;
  bool trace = false;
  bool dump_descriptions = false;
  bool dump_samples = false;
  bool dump_patterns = false;
  bool dump_pattern_rows = false;
  uint8_t highlight_mask = 0;
  uint8_t highlight[256];

  int num_format_filters = 0;
  const char *format_filter[MAX_FORMAT_FILTERS];

  /**
   * Read configuration options out of argv.
   * This will remove all valid options from argv aside from '-',
   * which signifies stdin should be used as an input. If an invalid
   * option is encountered, this function will print an error and
   * return false.
   */
  bool init(int *argc, char **argv, bool (*handler)(const char *, void *), void *priv);

  bool init(int *argc, char **argv)
  {
    return init(argc, argv, nullptr, nullptr);
  }

private:
  void set_dump_descriptions(int level);
  void set_dump_samples(int level);
  void set_dump_patterns(int level);
};

extern ConfigInfo Config;

#endif /* MZXTEST_CONFIG_HPP */
