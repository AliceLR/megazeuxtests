#if 0
[ -n "$CXX" ] || { CXX="g++"; }
"$CXX" -O3 -g -Wall -Wextra -pedantic \
 -I/megazeux/contrib/rad/ -I/c/megazeux-git/contrib/rad/ \
 "$@" radtest.cpp -oradtest
exit 0
#endif

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

#define USAGE "Usage: radtest filename.rad [(duration/rate) in samples=128] [rate]"

class FastOpal : Opal
{
public:
  FastOpal() : Opal(OPL_RATE) {}

  /**
   * This skips Opal's built-in linear resampler entirely.
   */
  void Sample(int16_t *left, int16_t *right)
  {
    Output(*left, *right);
  }
};

typedef void (*rad_player_callback)(void *arg, uint16_t reg, uint8_t data);

static void opal_callback(void *arg, uint16_t reg, uint8_t data)
{
  Opal *adlib = reinterpret_cast<Opal *>(arg);
  adlib->Port(reg, data);
}

template<rad_player_callback CALLBACK, class OPLTYPE>
void test_opl(OPLTYPE &adlib, const uint8_t *data, size_t duration, size_t sample_rate)
{
  RADPlayer player;

  player.Init(data, CALLBACK, &adlib);
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
}

int main(int argc, char *argv[])
{
  size_t duration = 128;
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
    if(!sample_rate)
      sample_rate = OPL_RATE;
    if(sample_rate < 1024)
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

  duration *= sample_rate;

  std::cerr << "Using Opal:" << std::endl;

  Opal adlib(sample_rate);
  test_opl<opal_callback>(adlib, data.get(), duration, sample_rate);

  if(sample_rate == OPL_RATE)
  {
    std::cerr << std::endl << "Using FastOpal:" << std::endl;

    FastOpal adlib2;
    test_opl<opal_callback>(adlib2, data.get(), duration, OPL_RATE);
  }

  return 0;
}
