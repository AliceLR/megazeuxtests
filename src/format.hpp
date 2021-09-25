/**
 * Copyright (C) 2021 Lachesis <petrifiedrowan@gmail.com>
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

#ifndef MODUTIL_FORMAT_HPP
#define MODUTIL_FORMAT_HPP

#include <stdarg.h>
#include <stdio.h>
#include <vector>
#include <tuple>

#include "attribute.hpp"
#include "common.hpp"

namespace format
{
#define O_(...) do { \
  fprintf(stderr, ": " __VA_ARGS__); \
  fflush(stderr); \
} while(0)

  /**
   * Common line printing functions.
   */

  static inline void spaces(int count)
  {
    fprintf(stderr, "%*s", count, "");
  }

  static inline void dashes(int count)
  {
    while(count > 0)
    {
      int n = count > 32 ? 32 : count;
      fprintf(stderr, "%*.*s", n, n, "--------------------------------");
      count -= n;
    }
  }

  static inline void endline()
  {
    fprintf(stderr, "\n");
    fflush(stderr); // MinGW buffers stderr...
  }

  static inline void line(const char *label = "")
  {
    O_("%-8.8s:", label);
    endline();
  }

  ATTRIBUTE_PRINTF(2, 3)
  static inline void line(const char *label, const char *fmt, ...)
  {
    O_("%-8.8s: ", label);
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    endline();
  }

  ATTRIBUTE_PRINTF(1, 2)
  static inline void warning(const char *fmt, ...)
  {
    O_("%-8.8s: ", "Warning");
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    endline();
  }

  ATTRIBUTE_PRINTF(1, 2)
  static inline void error(const char *fmt, ...)
  {
    O_("%-8.8s: ", "Error");
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    endline();
  }

  template<int N>
  static inline void uses(const bool (&uses)[N], const char * const (&desc)[N])
  {
    O_("%-8.8s:", "Uses");
    for(int i = 0; i < N; i++)
      if(uses[i])
        fprintf(stderr, " %s", desc[i]);
    endline();
  }

  template <typename T>
  static inline void orders(const char *label, const T *orders, size_t count)
  {
    O_("%-8.8s:", label);
    for(size_t i = 0; i < count; i++)
      fprintf(stderr, " %02x", orders[i]);
    endline();
  }

  template <typename T>
  static inline void song(const char *song_label, const char *order_label, unsigned int song_num,
   const char *name, const T *orders, size_t count)
  {
    if(name)
      O_("%-4.4s %02x : '%s', %zu %s\n", song_label, song_num, name, count, order_label);
    else
      O_("%-4.4s %02x : %zu %s\n", song_label, song_num, count, order_label);

    O_("%-8.8s:", "");
    for(size_t i = 0; i < count; i++)
      fprintf(stderr, " %02x", orders[i]);
    endline();
  }

  static inline void report(const char *label, size_t count)
  {
    endline();
    O_("%-20.20s: %zu\n", label, count);
    O_("%-20.20s:\n", "--------------------");
  }

  static inline void reportline(const char *label = "")
  {
    O_("%-20.20s:", label);
    endline();
  }

  ATTRIBUTE_PRINTF(2, 3)
  static inline void reportline(const char *label, const char *fmt, ...)
  {
    O_("%-20.20s: ", label);
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    endline();
  }

  /**
   * Pattern printing classes.
   */

  struct value
  {
    uint8_t value;
    uint8_t enable = true;
    static constexpr int width() { return 3; }
    bool can_print() const { return enable && value != 0; }
    void print() const { if(can_print()) fprintf(stderr, " %02x", value); else spaces(width()); }
  };

  struct effect
  {
    uint8_t effect;
    uint8_t param;
    static constexpr int width() { return 4; }
    bool can_print() const { return effect > 0 || param > 0; }
    void print() const { if(can_print()) fprintf(stderr, " %1x%02x", effect, param); else spaces(width()); }
  };

  struct effectXM
  {
    uint8_t effect;
    uint8_t param;
    static constexpr int width() { return 4; }
    bool can_print() const { return effect > 0 || param > 0; }
    char effect_char() const { return (effect < 10) ? effect + '0' : effect - 10 + 'A'; }
    void print() const { if(can_print()) fprintf(stderr, " %c%02x", effect_char(), param); else spaces(width()); }
  };

  struct effectIT
  {
    uint8_t effect;
    uint8_t param;
    static constexpr int width() { return 4; }
    bool can_print() const { return effect > 0 || param > 0; }
    void print() const { if(can_print()) fprintf(stderr, " %c%02x", effect + '@', param); else spaces(width()); }
  };

  /* GDM, MED, Oktalyzer, etc. support >16 effects. */
  struct effectWide
  {
    uint8_t effect;
    uint8_t param;
    static constexpr int width() { return 5; }
    bool can_print() const { return effect > 0 || param > 0; }
    void print() const { if(can_print()) fprintf(stderr, " %2x%02x", effect, param); else spaces(width()); }
  };

  template<class... ELEMENTS>
  struct event
  {
    std::tuple<ELEMENTS...> data;

    event() {}
    event(ELEMENTS... values): data(values...) {}

    static inline constexpr int count()
    {
      return sizeof...(ELEMENTS);
    }

    template<unsigned int I>
    static int print_width(bool (&print_element)[sizeof...(ELEMENTS)])
    {
      return 0;
    }

    template<unsigned int I, class T, class... REST>
    static int print_width(bool (&print_element)[sizeof...(ELEMENTS)])
    {
      if(I < sizeof...(ELEMENTS))
      {
        int w = print_element[I] ? T::width() : 0;
        return w + print_width<I + 1, REST...>(print_element);
      }
      return 0;
    }

    static inline int print_width(bool (&print_element)[sizeof...(ELEMENTS)])
    {
      return print_width<0, ELEMENTS...>(print_element);
    }

    template<unsigned int I>
    void get_print_elements(bool (&print_element)[sizeof...(ELEMENTS)]) const {}

    template<unsigned int I, class T, class... REST>
    void get_print_elements(bool (&print_element)[sizeof...(ELEMENTS)]) const
    {
      get_print_elements<I + 1, REST...>(print_element);
      if(!print_element[I])
      {
        print_element[I] |= std::get<I>(data).can_print();
        if(I + 1 < sizeof...(ELEMENTS))
          print_element[I] |= print_element[I + 1];
      }
    }

    void get_print_elements(bool (&print_element)[sizeof...(ELEMENTS)]) const
    {
      get_print_elements<0, ELEMENTS...>(print_element);
    }

    template<unsigned int I>
    bool print(const bool (&print_element)[sizeof...(ELEMENTS)]) const
    {
      return false;
    }

    template<unsigned int I, class T, class... REST>
    bool print(const bool (&print_element)[sizeof...(ELEMENTS)]) const
    {
      bool ret = false;
      if(print_element[I])
      {
        std::get<I>(data).print();
        ret = true;
      }
      print<I + 1, REST...>(print_element);
      return ret;
    }

    bool print(const bool (&print_element)[sizeof...(ELEMENTS)]) const
    {
      return print<0, ELEMENTS...>(print_element);
    }
  };

  template<class EVENT, size_t MAX_COLUMNS=256>
  struct pattern
  {
    const char *name = nullptr;
    const char *short_label = "Pat.";
    const char *long_label = "Pattern";
    unsigned int pattern_number;
    size_t rows;
    size_t columns;
    size_t size_in_bytes = 0;
    size_t current_column = 0;
    std::vector<EVENT> events;
    bool print_elements[MAX_COLUMNS][EVENT::count()]{};
    int widths[MAX_COLUMNS];

    pattern(unsigned int p, size_t c = 4, size_t r = 64, size_t s = 0):
     pattern_number(p), rows(r), columns(MIN(c, MAX_COLUMNS)), size_in_bytes(s) {}

    pattern(const char *n, unsigned int p, size_t c = 4, size_t r = 64, size_t s = 0):
     name(n), pattern_number(p), rows(r), columns(MIN(c, MAX_COLUMNS)), size_in_bytes(s) {}

    void insert(EVENT &&ev)
    {
      if(!events.size())
        events.reserve(rows * columns);

      ev.get_print_elements(print_elements[current_column]);
      events.push_back(std::move(ev));

      current_column++;
      if(current_column >= columns)
        current_column = 0;
    }

    void skip()
    {
      insert(EVENT{});
    }

    void labels(const char *s, const char *l)
    {
      short_label = s;
      long_label = l;
    }

    void summary(bool blank = false)
    {
      O_("%4.4s %02x :", short_label, pattern_number);
      if(name)
        fprintf(stderr, " '%s'", name);

      fprintf(stderr, " %zu columns, %zu rows", columns, rows);

      if(size_in_bytes)
        fprintf(stderr, " (%zu bytes)", size_in_bytes);

      if(blank)
        fprintf(stderr, "; %s is blank.\n", long_label);
      else
        fprintf(stderr, "\n");
    }

    void tracks(const int *column_tracks)
    {
      O_("%-8.8s:", "");
      for(size_t i = 0; i < columns; i++)
        fprintf(stderr, " %02x ", column_tracks[i]);
      format::endline();
    }

    void print(const char **column_labels = nullptr, const int *column_tracks = nullptr)
    {
      // Determine which columns to print...
      bool print_any = false;
      for(size_t i = 0; i < columns; i++)
      {
        widths[i] = EVENT::print_width(print_elements[i]);
        print_any |= widths[i] > 0;
      }

      format::endline();

      if(!print_any)
      {
        summary(true);
        if(column_tracks)
          tracks(column_tracks);
        return;
      }

      summary();
      if(column_tracks)
        tracks(column_tracks);
      O_("\n");

      O_("%-8.8s:", "");
      for(unsigned int track = 0; track < columns; track++)
      {
        if(!widths[track])
          continue;

        if(column_labels && column_labels[track])
        {
          fprintf(stderr, " %*.*s :", widths[track] - 1, widths[track] - 1, column_labels[track]);
        }
        else

        if(column_tracks)
        {
          char tmp[8];
          snprintf(tmp, sizeof(tmp), "T%02x", column_tracks[track]);
          fprintf(stderr, " %-*s:", widths[track], tmp);
        }
        else
          fprintf(stderr, " %02x%*s:", track, widths[track] - 2, "");
      }
      format::endline();

      O_("%-8.8s:", "--------");
      for(unsigned int track = 0; track < columns; track++)
      {
        if(!widths[track])
          continue;

        format::dashes(widths[track] + 1);
        fprintf(stderr, ":");
      }
      format::endline();

      const EVENT *ev = events.data();
      for(unsigned int row = 0; row < rows; row++)
      {
        char rowstr[8];
        sprintf(rowstr, "%02x", row);
        O_("%6.6s  :", rowstr);
        for(unsigned int track = 0; track < columns; track++, ev++)
        {
          if(!widths[track])
            continue;

          ev->print(print_elements[track]);
          fprintf(stderr, " :");
        }
        format::endline();
      }
    }
  };

} /* namespace format */

#endif /* MODUTIL_FORMAT_HPP */
