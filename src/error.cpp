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
    case GDM_BAD_CHANNEL:   return "invalid GDM channel index";
    case GDM_BAD_PATTERN:   return "invalid GDM pattern data";
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
