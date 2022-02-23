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

#ifndef MODUTIL_ERROR_HPP
#define MODUTIL_ERROR_HPP

namespace modutil
{
  enum error
  {
    SUCCESS,
    FORMAT_ERROR,
    ALLOC_ERROR,
    READ_ERROR,
    SEEK_ERROR,
    INVALID,
    NOT_IMPLEMENTED,
    BAD_VERSION,
    BAD_PACKING,

    /* IFF */
    IFF_CONFIG_ERROR,
    IFF_CONTAINER_ERROR,
    IFF_NO_HANDLER,

    /* MOD/WOW/etc */
    MOD_INVALID_ORDER_COUNT,
    MOD_IGNORE_ST26,
    MOD_IGNORE_IT10,
    MOD_IGNORE_MAGIC,

    /* IT */
    IT_INVALID_SAMPLE,
    IT_INVALID_INSTRUMENT,
    IT_INVALID_ORDER_COUNT,
    IT_INVALID_PATTERN_COUNT,

    /* GDM */
    GDM_TOO_MANY_EFFECTS,

    /* AMF/DSMI */
    AMF_BAD_CHANNELS,
    AMF_BAD_TRACKS,

    /* DSIK (DSM) */
    DSIK_OLD_FORMAT,

    /* MED and OctaMED */
    MED_TOO_MANY_BLOCKS,
    MED_TOO_MANY_INSTR,

    /* STM */
    STM_INVALID_ORDERS,
    STM_INVALID_PATTERNS,
  };

  const char *strerror(modutil::error err);
}

#endif /* MODUTIL_ERROR_HPP */
