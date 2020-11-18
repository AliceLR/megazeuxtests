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

#ifndef MZXTEST_CONFIG_HPP
#define MZXTEST_CONFIG_HPP

struct ConfigInfo final
{
  static const char COMMON_FLAGS[];

  bool dump_samples = false;
  bool dump_patterns = false;
  bool dump_pattern_rows = false;

  /**
   * Read configuration options out of argv.
   * This will remove all valid options from argv aside from '-',
   * which signifies stdin should be used as an input. If an invalid
   * option is encountered, this function will print an error and
   * return false.
   */
  bool init(int *argc, char **argv);
};

extern ConfigInfo Config;

#endif /* MZXTEST_CONFIG_HPP */
