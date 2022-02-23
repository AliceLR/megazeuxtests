/**
 * Copyright (C) 2021 Lachesis <petrifiedrowan@gmail.com>
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

#ifndef MZXTEST_ATTRIBUTE_HPP
#define MZXTEST_ATTRIBUTE_HPP

#ifdef __has_attribute
#define HAS_ATTRIBUTE(x) __has_attribute(x)
#else
#define HAS_ATTRIBUTE(x) 0
#endif

/**
 * Catch-all for GCC printf instrumentation. GCC will emit warnings
 * if this attribute is not used on printf-like functions.
 */
#if (defined(__GNUC__) && !defined(__clang__)) || HAS_ATTRIBUTE(format)
#if __GNUC__ >= 5 || (__GNUC__ == 4 &&  __GNUC_MINOR__ >= 4)

// Note: param numbers are 1-indexed for normal functions and
// 2-indexed for member functions (including constructors).
#define ATTRIBUTE_PRINTF(string_index, first_to_check) \
 __attribute__((format(gnu_printf, string_index, first_to_check)))

#else

#define ATTRIBUTE_PRINTF(string_index, first_to_check) \
 __attribute__((format(printf, string_index, first_to_check)))

#endif
#endif

#ifndef ATTRIBUTE_PRINTF
#define ATTRIBUTE_PRINTF(string_index, first_to_check)
#endif

#endif /* MZXTEST_ATTRIBUTE_HPP */
