#if 0
clang++ \
 -O3 -g -Wall -Wextra -pedantic \
 -I/megazeux/contrib/rad/ -I/c/megazeux-git/contrib/rad/ \
 "$@" radtest.cpp -oradtest
exit 0
#endif

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

#include <chrono>
#include <iostream>
#include <memory>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/stat.h>

#include "opal.cpp"
#include "player20.cpp"
#include "validate20.cpp"

#define OPL_RATE 49716

#define USAGE "Usage: radtest filename.rad [duration in samples=1000000] [rate]"

static void rad_player_callback(void *arg, uint16_t reg, uint8_t data)
{
  Opal *adlib = reinterpret_cast<Opal *>(arg);
  adlib->Port(reg, data);
}

int main(int argc, char *argv[])
{
  size_t duration = 1024*1024;
  size_t sample_rate = OPL_RATE;

  if(argc < 2 || argc > 4 || !argv || !argv[1])
  {
    std::cerr << USAGE << std::endl;
    return 0;
  }

  if(argc > 2)
  {
    duration = strtoul(argv[2], nullptr, 10);
    if(!duration)
    {
      std::cerr << "Error: invalid duration." << std::endl;
      return -1;
    }
  }

  if(argc > 3)
  {
    sample_rate = strtoul(argv[3], nullptr, 10);
    if(!sample_rate || sample_rate < 1024)
    {
      std::cerr << "Error: invalid sample rate." << std::endl;
      return -1;
    }
  }

  FILE *f = fopen(argv[1], "rb");

  struct stat st;
  if(!f)
  {
    std::cerr << "Error: failed to open file." << std::endl;
    return -1;
  }

  if(fstat(fileno(f), &st))
  {
    std::cerr << "Error: failed to fstat file." << std::endl;
    fclose(f);
    return -1;
  }

  size_t bufsize = st.st_size;
  std::unique_ptr<uint8_t[]> data(new uint8_t[bufsize + 1]{});
  if(!fread(data.get(), bufsize, 1, f))
  {
    std::cerr << "Error: error reading data." << std::endl;
    fclose(f);
    return -1;
  }
  fclose(f);

  // NOTE: buffer should be allocated bufsize + 1, see above.
  const char *err = RADValidate(data.get(), bufsize);
  if(err)
  {
    std::cerr << "Error: " << err << std::endl;
    return -1;
  }

  Opal adlib(sample_rate);
  RADPlayer player;

  player.Init(data.get(), rad_player_callback, &adlib);
  uint32_t update_hz = player.GetHertz();
  size_t timer = 0;
  size_t timer_max = sample_rate / update_hz;

  auto time_start = std::chrono::steady_clock::now();

  for(size_t i = 0; i < duration; i++)
  {
    int16_t left;
    int16_t right;
    adlib.Sample(&left, &right);

    timer++;
    if(timer >= timer_max)
    {
      player.Update();
      timer = 0;
//      std::cerr << "Order " << player.GetTunePos() << " line " << player.GetTuneLine() << std::endl;
    }
  }

  auto time_end = std::chrono::steady_clock::now();
  auto time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(time_end - time_start);
  std::cerr << "Time (ms): " << time_ms.count() << std::endl;

  return 0;
}
