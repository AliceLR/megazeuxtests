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

#include "error.hpp"

namespace modutil
{
const char *strerror(modutil::error err)
{
  switch(err)
  {
    case SUCCESS:           return "no error";
    case FORMAT_ERROR:      return "wrong format, try next loader";
    case ALLOC_ERROR:       return "alloc error";
    case READ_ERROR:        return "read error";
    case SEEK_ERROR:        return "seek error";
    case INVALID:           return "invalid module";
    case NOT_IMPLEMENTED:   return "feature not implemented";
    case BAD_VERSION:       return "unrecognized format version";
    case BAD_PACKING:       return "invalid or corrupted packing";

    /* IFF */
    case IFF_CONFIG_ERROR:  return "invalid IFF configuration";
    case IFF_CONTAINER_ERROR: return "child IFF hunks exceed size of parent hunk";
    case IFF_NO_HANDLER:    return "invalid IFF ID";

    /* MOD / WOW / etc. */
    case MOD_INVALID_ORDER_COUNT: return "invalid order count";
    case MOD_IGNORE_ST26:   return "ignoring ST 2.6 .MOD";
    case MOD_IGNORE_IT10:   return "ignoring IceTracker .MOD";
    case MOD_IGNORE_MAGIC:  return "ignoring unsupported .MOD variant";

    /* IT */
    case IT_INVALID_SAMPLE: return "IT sample magic mismatch";
    case IT_INVALID_INSTRUMENT: return "IT instrument magic mismatch";
    case IT_INVALID_ORDER_COUNT: return "invalid order count >256";
    case IT_INVALID_PATTERN_COUNT: return "invalid pattern count >256";

    /* GDM */
    case GDM_TOO_MANY_EFFECTS: return "note has more effects (>4) than allowed";

    /* AMF/DSMI */
    case AMF_BAD_CHANNELS:  return "AMF has too many channels";
    case AMF_BAD_TRACKS:    return "AMF has too many tracks";

    /* DSIK (DSM) */
    case DSIK_OLD_FORMAT:   return "old format DSMs not supported";

    /* OctaMED */
    case MED_TOO_MANY_BLOCKS: return "only <=256 blocks supported";
    case MED_TOO_MANY_INSTR: return "only <=63 instruments supported";

    /* STM */
    case STM_INVALID_ORDERS: return "invalid order count >256";
    case STM_INVALID_PATTERNS: return "invalid pattern count >=64";
  }
  return "unknown error";
}
} /* namespace modutil */
