/* Extended Module Player
 * Copyright (C) 2024 Alice Rowan <petrifiedrowan@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

/* Depacker for Pack-Ice Ice!/ICE! packed files.
 * Due to the strange reverse output nature of this format
 * it has to be depacked in memory all at once.
 *
 * Implementation largely based on this post by nocash:
 * https://eab.abime.net/showpost.php?p=1617809&postcount=7
 */

#include "ice_unpack.h"

#if 1
#define ICE_DEBUG
#endif

#if 1
#define ICE_ENABLE_ICE1
#endif

#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Size of input buffer for filesystem reads. */
#define ICE_BUFFER_SIZE		4096

/* loader.h */
#ifndef MAGIC4
#define MAGIC4(a,b,c,d) \
	(((ice_uint32)(a)<<24)|((ice_uint32)(b)<<16)|((ice_uint32)(c)<<8)|(d))
#endif

#define ICE_OLD_MAGIC		MAGIC4('I','c','e','!')
#define ICE_NEW_MAGIC		MAGIC4('I','C','E','!')
#define CJ_MAGIC		MAGIC4('-','C','J','-')
#define MICK_MAGIC		MAGIC4('M','I','C','K')
#define SHE_MAGIC		MAGIC4('S','H','E','!')
#define TMM_MAGIC		MAGIC4('T','M','M','!')
#define TSM_MAGIC		MAGIC4('T','S','M','!')

#define VERSION_113		113
#define VERSION_21X		210
#define VERSION_21X_OR_220	215
#define VERSION_220		220
#define VERSION_23X		230

struct ice_state
{
	void *in;
	long in_size;
	ice_read_fn read_fn;
	ice_seek_fn seek_fn;
	ice_uint32 compressed_size;
	ice_uint32 uncompressed_size;
	int version;
	int eof;
	int bits_left;
	ice_uint32 bits;
	ice_uint8 buffer[ICE_BUFFER_SIZE];
	unsigned buffer_pos;
	unsigned next_length;
	long next_seek;
};

#if (defined(__GNUC__) || defined(__clang__)) && !defined(ICE_DEBUG)
#define debug(...)
#else
ICE_ATTRIB_PRINTF(1,2)
static inline void debug(const char *fmt, ...)
{
#ifdef ICE_DEBUG
	va_list args;
	va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	fprintf(stderr, "\n");
	fflush(stderr);
	va_end(args);
#endif
}
#endif

static ice_uint32 mem_u32(ice_uint8 *buf)
{
	return (buf[0] << 24UL) | (buf[1] << 16UL) | (buf[2] << 8UL) | buf[3];
}

static int ice_check_compressed_size(struct ice_state *ice)
{
	debug("ice_check_compressed_size");
	if (ice->in_size < 12 || ice->compressed_size < 4 ||
	    ice->compressed_size > (size_t)ice->in_size) {
		debug("  bad compressed_size %u", (unsigned)ice->compressed_size);
		return -1;
	}
	return 0;
}

static int ice_check_uncompressed_size(struct ice_state *ice, size_t dest_len)
{
	debug("ice_check_uncompressed_size");
	if (ice->uncompressed_size == 0 ||
	    ice->uncompressed_size > (size_t)INT_MAX) {
		debug("  bad uncompressed_size %u", (unsigned)ice->uncompressed_size);
		return -1;
	}
	if (ice->uncompressed_size > dest_len) {
		debug("  uncompressed_size %u exceeds provided buffer size %u\n",
			(unsigned)ice->uncompressed_size, (unsigned)dest_len);
		return -1;
	}
	return 0;
}

static int ice_fill_buffer(struct ice_state *ice)
{
	debug("ice_fill_buffer");
	if (ice->seek_fn(ice->in, ice->next_seek, SEEK_SET) < 0) {
		debug("  failed to seek to %ld", ice->next_seek);
		ice->eof = 1;
		return -1;
	}
	if (ice->read_fn(ice->buffer, ice->next_length, ice->in) < ice->next_length) {
		debug("  failed to read %u", ice->next_length);
		ice->eof = 1;
		return -1;
	}
	ice->buffer_pos = ice->next_length;
	ice->next_seek -= ICE_BUFFER_SIZE;
	ice->next_length = ICE_BUFFER_SIZE;
	return 0;
}

static int ice_peek_start(struct ice_state *ice, ice_uint8 buf[4])
{
	debug("ice_peek_start");
	if (ice->seek_fn(ice->in, (long)ice->compressed_size - 4, SEEK_SET) < 0) {
		debug("  failed seek to %ld", (long)ice->compressed_size - 4);
		return -1;
	}
	if (ice->read_fn(buf, 4, ice->in) < 4) {
		debug("  failed read");
		return -1;
	}
	debug("  = %02x%02x%02x%02x", buf[0], buf[1], buf[2], buf[3]);
	return 0;
}

static int ice_init_buffer(struct ice_state *ice)
{
	size_t len = ice->compressed_size;
	ice_uint8 tmp[4];
	ice->eof = 0;
	ice->bits = 0;
	ice->bits_left = 0;

	debug("ice_init_buffer");

	ice->next_length = len % ICE_BUFFER_SIZE;
	if (ice->next_length == 0) {
		ice->next_length = ICE_BUFFER_SIZE;
	}

	ice->next_seek = len - ice->next_length;
	/*
	if (ice->version >= VERSION_21X) {
		ice->next_seek += 12;
	}
	*/

	/* Attempt version filtering for ambiguous Ice! files: */
	if (ice->version == VERSION_21X_OR_220) {
		if (ice_peek_start(ice, tmp) < 0) {
			return -1;
		}
		if (~tmp[3] & 0x80) {
			/* 8-bit streams require a bit set here. */
			ice->version = VERSION_21X;
		} else if (~tmp[0] & 0x80) {
			/* 32-bit streams require a bit set here. */
			ice->version = VERSION_220;
		}
	}

	return ice_fill_buffer(ice);
}

static int ice_read_byte(struct ice_state *ice)
{
	if (ice->buffer_pos == 0) {
		if (ice_fill_buffer(ice) < 0) {
			return -1;
		}
	}
	return ice->buffer[--ice->buffer_pos];
}

/* The original Pack-Ice bitstream is implemented roughly as follows:
 *
 * readbit():
 *   bits += bits;                 // add, output is in carry flag
 *   if(!bits)                     // the last bit is a terminating flag
 *     bits = read_char();
 *     bits += bits + carryflag;   // add-with-carry, output is in carry flag
 *
 * readbits(N):
 *   for 0 until N:
 *     readbit();
 *     out = (out << 1) + carryflag;
 *
 * Initially, the Pack-Ice unpacker preloads a byte (or 4 bytes) but
 * does not preload a terminating bit, which means the lowest set bit of
 * the preloaded byte(s) will be used as a terminating bit instead.
 * This function readjusts the initial bit count to reflect this.
 */
static int ice_preload_adjust(struct ice_state *ice)
{
	unsigned tmp = ice->bits >> (unsigned)(32 - ice->bits_left);

	debug("ice_preload_adjust of %02x (bits left: %d)", tmp, ice->bits_left);

	if (!tmp) {
		return -1;
	}
	while (~tmp & 1) {
		tmp >>= 1;
		ice->bits_left--;
	}
	/* Last valid bit is also discarded. */
	tmp >>= 1;
	ice->bits_left--;

	tmp <<= 32 - ice->bits_left;
	ice->bits = tmp;
	debug("  adjusted to %02x (bits left: %d)", tmp, ice->bits_left);
	return 0;
}

static int ice_load8(struct ice_state *ice)
{
	int val = ice_read_byte(ice);
	if (val < 0) {
		return -1;
	}
	ice->bits = (unsigned)val << 24u;
	ice->bits_left = 8;
	return 0;
}

static int ice_read_bits8(struct ice_state *ice, int num)
{
	/* NOTE: there are interleaved uncompressed bytes in the input so
	 * this unfortunately can't be optimized very much. */
	int ret = 0;
	int n;

#if 0
	if (num == 1) {
		ret = (ice->bits >> 31u);
		ice->bits <<= 1;
		ice->bits_left--;

		if (!ice->bits_left) {
			if (ice_load8(ice) < 0)
				return -1;

			ret = (ice->bits >> 31u);
			ice->bits <<= 1;
			ice->bits_left--;
		}

	} else
#endif
	while (num > 0) {
		if (ice->bits_left <= 0) {
			if (ice_load8(ice) < 0)
				return -1;
		}
		n = ICE_MIN(num, ice->bits_left);
		ret |= ice->bits >> (32 - num);
		num -= n;
		ice->bits <<= n;
		ice->bits_left -= n;
	}
	debug("      <- %03x", ret);
	return ret;
}

static int ice_load32(struct ice_state *ice)
{
	int a, b, c, d;
	if ((a = ice_read_byte(ice)) < 0) return -1;
	if ((b = ice_read_byte(ice)) < 0) return -1;
	if ((c = ice_read_byte(ice)) < 0) return -1;
	if ((d = ice_read_byte(ice)) < 0) return -1;

	ice->bits = ((unsigned)a << 24u) | (b << 16) | (c << 8) | d;
	ice->bits_left = 32;
	return 0;
}

static int ice_read_bits32(struct ice_state *ice, int num)
{
	int ret = 0;
	int n;

#if 0
	if (num == 1) {
		ret = (ice->bits >> 31u);
		ice->bits <<= 1;
		ice->bits_left--;

		if (!ice->bits_left) {
			if (ice_load32(ice) < 0)
				return -1;

			ret = (ice->bits >> 31u);
			ice->bits <<= 1;
			ice->bits_left--;
		}

	} else
#endif
	while (num > 0) {
		if (ice->bits_left <= 0) {
			if (ice_load32(ice) < 0)
				return -1;
		}
		n = ICE_MIN(num, ice->bits_left);
		ret |= ice->bits >> (32 - num);
		num -= n;
		ice->bits <<= n;
		ice->bits_left -= n;
	}
	debug("      <- %03x", ret);
	return ret;
}

#define ice_output_byte(b) do { \
	ice_uint8 byte = (b); \
	dest[--dest_offset] = byte; \
} while(0)

#define ice_window_copy(off, len) do { \
	size_t copy_offset = (off) + dest_offset;			\
	size_t copy_length = (len);					\
	debug("  (%zu remaining) window copy of %zu, dist %d",		\
		dest_offset, copy_length, (off));			\
	if (copy_length > dest_offset)					\
		return -1;						\
	if (copy_length && copy_offset > dest_len) {			\
		size_t zero_len = copy_offset - dest_len;		\
		zero_len = ICE_MIN(zero_len, copy_length);		\
		memset(dest + dest_offset - zero_len, 0, zero_len);	\
		copy_length -= zero_len;				\
		dest_offset -= zero_len;				\
	}								\
	for (; copy_length > 0; copy_length--) {			\
		ice_output_byte(dest[--copy_offset]);			\
	}								\
} while(0)

#define ice_unpack_routine(read_fn, streamsize) do { \
	size_t dest_offset = dest_len;					\
	int length;							\
	int dist;							\
	int val;							\
	while (dest_offset > 0) {					\
		val = read_fn(ice, 1);					\
		if (val ==   1) val = read_fn(ice,  1) +   1;		\
		if (val ==   2) val = read_fn(ice,  2) +   2;		\
		if (val ==   5) val = read_fn(ice,  2) +   5;		\
		if (val ==   8) val = read_fn(ice,  3) +   8;		\
		if (val ==  15) val = read_fn(ice,  8) +  15;		\
		if (val == 270) val = read_fn(ice, 15) + 270;		\
		if (ice->eof) return -1;				\
		debug("  (%zu remaining) copy of %d", dest_offset, val);\
		if ((size_t)val > dest_offset) {			\
			return -1;					\
		}							\
		for (; val > 0; val--) {				\
			int b = ice_read_byte(ice);			\
			if (b < 0) return -1;				\
			ice_output_byte(b);				\
		}							\
		if (dest_offset == 0) break;				\
									\
		if (read_fn(ice, 1) == 0) {				\
			length = 2;					\
		} else if (read_fn(ice, 1) == 0) {			\
			length= 3;					\
		} else if (read_fn(ice, 1) == 0) {			\
			length = 4 + read_fn(ice, 1);			\
		} else if (read_fn(ice, 1) == 0) {			\
			length = 6 + read_fn(ice, 2);			\
		} else {						\
			length = 10 + read_fn(ice, 10);			\
		}							\
		if (ice->eof) return -1;				\
		debug("    length=%d", length);				\
									\
		if (length == 2) {					\
			if (read_fn(ice, 1) == 0)			\
				dist = 1 + read_fn(ice, 6);		\
			else 						\
				dist = 65 + read_fn(ice, 9);		\
		} else {						\
			if (read_fn(ice, 1) == 0)			\
				dist = 33 + read_fn(ice, 8);		\
			else if (read_fn(ice, 1) == 0)			\
				dist = 1 + read_fn(ice, 5);		\
			else 						\
				dist = 289 + read_fn(ice, 12);		\
		}							\
		if (ice->eof) return -1;				\
		debug("    dist=%d", dist);				\
									\
		if (streamsize == 32)	dist = dist + length - 1;	\
		else if (dist > 1) 	dist = dist + length - 2;	\
		ice_window_copy(dist, length); 				\
	}								\
	if (ice->version >= VERSION_21X && read_fn(ice, 1) == 1) {	\
		debug("  bitplane filter not supported yet :-(");	\
	} \
} while(0)

static int ice_unpack8(struct ice_state * ICE_RESTRICT ice,
	ice_uint8 * ICE_RESTRICT dest, size_t dest_len)
{
	debug("ice_unpack8");
	if (ice_init_buffer(ice) < 0) {
		return -1;
	}
	if (ice_load8(ice) < 0 || ice_preload_adjust(ice) < 0) {
		return -1;
	}
	ice_unpack_routine(ice_read_bits8, 8);
	return 0;
}

static int ice_unpack32(struct ice_state * ICE_RESTRICT ice,
	ice_uint8 * ICE_RESTRICT dest, size_t dest_len)
{
	debug("ice_unpack32");
	if (ice_init_buffer(ice) < 0) {
		return -1;
	}
	if (ice_load32(ice) < 0 || ice_preload_adjust(ice) < 0) {
		return -1;
	}
	ice_unpack_routine(ice_read_bits32, 32);
	return 0;
}

static int ice_unpack(struct ice_state * ICE_RESTRICT ice,
	ice_uint8 * ICE_RESTRICT dest, size_t dest_len)
{
	debug("ice_unpack");
	if (ice_check_compressed_size(ice) < 0 ||
	    ice_check_uncompressed_size(ice, dest_len) < 0) {
		return -1;
	}

	if (ice->version >= VERSION_21X_OR_220) {
		if (ice_unpack8(ice, dest, dest_len) == 0) {
			return 0;
		}
	}
	if (ice->version <= VERSION_21X_OR_220) {
		if (ice_unpack32(ice, dest, dest_len) == 0) {
			return 0;
		}
	}

	// FIXME:
	return 0;
/*
	return -1;
*/
}


long ice1_test(const void *end_of_file, size_t sz)
{
	ice_uint8 *data = (ice_uint8 *)end_of_file;
	ice_uint32 uncompressed_size;
	ice_uint32 magic;

	if (sz < 8) {
		return -1;
	}
	magic = mem_u32(data + sz - 4);
	uncompressed_size = mem_u32(data + sz - 8);

	if (magic == ICE_OLD_MAGIC) {
		return (long)uncompressed_size;
	}
	return -1;
}

int ice1_unpack(void * ICE_RESTRICT dest, size_t dest_len,
	ice_read_fn read_fn, ice_seek_fn seek_fn, void *priv, size_t in_len)
{
	struct ice_state ice;
	ice_uint8 buf[8];
	int ret;

	memset(&ice, 0, sizeof(ice));

	if (seek_fn(priv, -8, SEEK_END) < 0) {
		return -1;
	}
	if (read_fn(buf, 8, priv) < 8) {
		return -1;
	}
	if (ice1_test(buf, 8) < 0) {
		return -1;
	}

	ice.in = priv;
	ice.in_size = in_len;
	ice.read_fn = read_fn;
	ice.seek_fn = seek_fn;
	ice.compressed_size = in_len - 8;
	ice.uncompressed_size = mem_u32(buf + 0);
	ice.version = VERSION_113;

	ret = ice_unpack(&ice, (ice_uint8 *)dest, dest_len);
	return ret;
}

long ice2_test(const void *start_of_file, size_t sz)
{
	ice_uint8 *data = (ice_uint8 *)start_of_file;
	ice_uint32 uncompressed_size;
	ice_uint32 magic;

	if (sz < 12) {
		return -1;
	}
	magic = mem_u32(data + 0);
	uncompressed_size = mem_u32(data + 8);

	switch (magic) {
	case ICE_OLD_MAGIC:
	case ICE_NEW_MAGIC:
	case CJ_MAGIC:
	case MICK_MAGIC:
	case SHE_MAGIC:
	case TMM_MAGIC:
	case TSM_MAGIC:
		return (long)uncompressed_size;
	}
	return -1;
}

int ice2_unpack(void * ICE_RESTRICT dest, size_t dest_len,
	ice_read_fn read_fn, ice_seek_fn seek_fn, void *priv, size_t in_len)
{
	struct ice_state ice;
	ice_uint8 buf[12];
	int ret;

	memset(&ice, 0, sizeof(ice));

	if (seek_fn(priv, 0, SEEK_SET) < 0) {
		return -1;
	}
	if (read_fn(buf, 12, priv) < 12) {
		return -1;
	}
	if (ice2_test(buf, 12) < (long)dest_len) {
		return -1;
	}

	ice.in = priv;
	ice.in_size = in_len;
	ice.read_fn = read_fn;
	ice.seek_fn = seek_fn;
	ice.compressed_size = mem_u32(buf + 4);
	ice.uncompressed_size = mem_u32(buf + 8);

	switch (mem_u32(buf + 0)) {
	case ICE_OLD_MAGIC:
		/* Ice! may use a 32-bit or an 8-bit buffer. */
		ice.version = VERSION_21X_OR_220;
		break;
	case ICE_NEW_MAGIC:
		/* ICE! always uses an 8-bit buffer. */
		ice.version = VERSION_23X;
		break;
	default:
		/* Most hacked magics used older versions (apparently). */
		ice.version = VERSION_21X;
		break;
	}

	ret = ice_unpack(&ice, (ice_uint8 *)dest, dest_len);
	return ret;
}

