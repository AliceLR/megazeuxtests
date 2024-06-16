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

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Enable debug output to stderr. */
#if 0
#define ICE_DEBUG
#endif

/* Enable table decoding, which is a ~30% performance increase over the
 * default bitstream and a ~10% performance increase over the original
 * bitstream. This performance increase should be higher, but the format
 * sabotages this kind of optimization by interleaving uncompressed bytes
 * into the stream. It's also possible I am just bad at optimizing this.
 * It should only be used with the default bitstream. */
#if 1
#define ICE_TABLE_DECODING
#endif

/* Enable the original bitstream, which is ~10% slower than table decoding.
 * This shouldn't be used at the same time as table decoding because it the
 * extra hacks needed slow things down further. */
#if 0
#define ICE_ORIGINAL_BITSTREAM
#endif

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

#ifdef ICE_TABLE_DECODING

#define ENTRIES_X2(v, b)	{ (v), (b) }, { (v), (b) }
#define ENTRIES_X4(v, b)	ENTRIES_X2((v),(b)), ENTRIES_X2((v),(b))
#define ENTRIES_X8(v, b)	ENTRIES_X4((v),(b)), ENTRIES_X4((v),(b))
#define ENTRIES_X16(v, b)	ENTRIES_X8((v),(b)), ENTRIES_X8((v),(b))
#define ENTRIES_X32(v, b)	ENTRIES_X16((v),(b)), ENTRIES_X16((v),(b))
#define ENTRIES_X64(v, b)	ENTRIES_X32((v),(b)), ENTRIES_X32((v),(b))
#define ENTRIES_X128(v, b)	ENTRIES_X64((v),(b)), ENTRIES_X64((v),(b))
#define ENTRIES_X256(v, b)	ENTRIES_X128((v),(b)), ENTRIES_X128((v),(b))

#define LINEAR_X2(v, b)		{ (v), (b) }, { (v)+1, (b) }
#define LINEAR_X4(v, b)		LINEAR_X2((v),(b)), LINEAR_X2((v)+2,(b))
#define LINEAR_X8(v, b)		LINEAR_X4((v),(b)), LINEAR_X4((v)+4,(b))
#define LINEAR_X16(v, b)	LINEAR_X8((v),(b)), LINEAR_X8((v)+8,(b))
#define LINEAR_X32(v, b)	LINEAR_X16((v),(b)), LINEAR_X16((v)+16,(b))
#define LINEAR_X64(v, b)	LINEAR_X32((v),(b)), LINEAR_X32((v)+32,(b))
#define LINEAR_X128(v, b)	LINEAR_X64((v),(b)), LINEAR_X64((v)+64,(b))
#define LINEAR_X256(v, b)	LINEAR_X128((v),(b)), LINEAR_X128((v)+128,(b))

#define LINEAR_X2_X4(v, b)	ENTRIES_X4((v),(b)), ENTRIES_X4((v)+1,(b))
#define LINEAR_X4_X4(v, b)	LINEAR_X2_X4((v),(b)), LINEAR_X2_X4((v)+2,(b))
#define LINEAR_X8_X4(v, b)	LINEAR_X4_X4((v),(b)), LINEAR_X4_X4((v)+4,(b))
#define LINEAR_X16_X4(v, b)	LINEAR_X8_X4((v),(b)), LINEAR_X8_X4((v)+8,(b))
#define LINEAR_X32_X4(v, b)	LINEAR_X16_X4((v),(b)), LINEAR_X16_X4((v)+16,(b))

#define VALUE_SPECIAL 65535

struct ice_table_entry
{
	ice_uint16 value;
	ice_uint16 bits_used;
};

static const struct ice_table_entry literal_table[512] =
{
	ENTRIES_X256(0, 1),		/* 0........ - length 0 */
	ENTRIES_X128(1, 2),		/* 10....... - length 1 */
	ENTRIES_X32(2, 4),		/* 11xx..... - length 2 + x */
	ENTRIES_X32(3, 4),
	ENTRIES_X32(4, 4),
	ENTRIES_X8(5, 6),		/* 1111xx... - length 5 + x */
	ENTRIES_X8(6, 6),
	ENTRIES_X8(7, 6),
	{  8, 9 },			/* 111111xxx - length 8 + x */
	{  9, 9 },
	{ 10, 9 },
	{ 11, 9 },
	{ 12, 9 },
	{ 13, 9 },
	{ 14, 9 },
	{ VALUE_SPECIAL, 9 }		/* 111111111 - (read 8) + 15,
						       (read 15 + 270) if 270 */
};

static const struct ice_table_entry length_table[64] =
{
	ENTRIES_X32(2, 1),		/* 0..... - length 2 */
	ENTRIES_X16(3, 2),		/* 10.... - length 3 */
	ENTRIES_X4(4, 4),		/* 1100.. - length 4 */
	ENTRIES_X4(5, 4),		/* 1101.. - length 5 */
	{ 6, 6 },			/* 1110xx - length 6 + x */
	{ 7, 6 },
	{ 8, 6 },
	{ 9, 6 },
	ENTRIES_X4(VALUE_SPECIAL, 4)	/* 1111.. - length 10 + (read 10) */
};

static const struct ice_table_entry distance_table[512] =
{
	LINEAR_X256(33, 9),		/* 0xxxxxxxx - distance 33 + x */
	LINEAR_X32_X4(1, 7),		/* 10xxxxx.. - distance 1 + x */
	ENTRIES_X128(VALUE_SPECIAL, 2)	/* 11....... - distance 289 + (read 12) */
};

#endif /* ICE_TABLE_DECODING */

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
	ice_uint8 buffer[ICE_BUFFER_SIZE + 4];
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

static inline ice_uint16 mem_u16le(ice_uint8 *buf)
{
	return buf[0] | (buf[1] << 8u);
}

static inline ice_uint32 mem_u32(ice_uint8 *buf)
{
	return (buf[0] << 24u) | (buf[1] << 16u) | (buf[2] << 8u) | buf[3];
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
	if (ice->uncompressed_size == 0) {
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
	/* Save up to 4 extra bytes to allow for peeking. */
	if (ice->buffer_pos > 4) {
		debug("  ice_fill_buffer with %u remaining?", ice->buffer_pos);
		return -1;
	}
	if (ice->buffer_pos > 0 && ice->buffer_pos <= 4) {
		memcpy(ice->buffer + ice->next_length, ice->buffer, 4);
	}

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
	ice->buffer_pos += ice->next_length;
	ice->next_seek -= ICE_BUFFER_SIZE;
	ice->next_length = ICE_BUFFER_SIZE;
	return 0;
}

static int ice_read_byte(struct ice_state *ice)
{
	if (ice->buffer_pos < 1) {
		if (ice_fill_buffer(ice) < 0)
			return -1;
		if (ice->buffer_pos < 1)
			return -1;
	}
	return ice->buffer[--ice->buffer_pos];
}

static inline int ice_read_u16le(struct ice_state *ice)
{
	if (ice->buffer_pos < 2) {
		if (ice_fill_buffer(ice) < 0)
			return -1;
		if (ice->buffer_pos < 2)
			return -1;
	}
	ice->buffer_pos -= 2;
	return mem_u16le(ice->buffer + ice->buffer_pos);
}

/* Note: 0 should never happen under normal circumstances,
 * so it's the error return value for this function. */
static ice_uint32 ice_peek_u32(struct ice_state *ice)
{
	if (ice->buffer_pos < 4) {
		if (ice_fill_buffer(ice) < 0)
			return 0;
		if (ice->buffer_pos < 4)
			return 0;
	}
	return mem_u32(ice->buffer + ice->buffer_pos - 4);
}

static int ice_init_buffer(struct ice_state *ice)
{
	size_t len = ice->compressed_size;
	ice->eof = 0;
	ice->bits = 0;
	ice->bits_left = 0;

	debug("ice_init_buffer");

	ice->next_length = len % ICE_BUFFER_SIZE;
	if (ice->next_length == 0) {
		ice->next_length = ICE_BUFFER_SIZE;
	}

	ice->next_seek = len - ice->next_length;

	if (ice_fill_buffer(ice) < 0) {
		return -1;
	}

	/* Attempt version filtering for ambiguous Ice! files: */
	if (ice->version == VERSION_21X_OR_220) {
		ice_uint32 peek = ice_peek_u32(ice);
		debug("  version is ambiguous 'Ice!', trying to determine");
		debug("  = %08x", peek);
		if (peek == 0) {
			debug("  failed to peek ahead 32 bits, must be 8bit");
			ice->version = VERSION_220;
		} else if (~peek & 0x80u) {
			/* 8-bit streams require a bit set here. */
			debug("  first bit (8bit) not set, must be 32bit");
			ice->version = VERSION_21X;
		} else if (~peek & 0x80000000u) {
			/* 32-bit streams require a bit set here. */
			debug("  first bit (32bit) not set, must be 8bit");
			ice->version = VERSION_220;
		}
	}
	return 0;
}

/* The original Pack-Ice bitstream is implemented roughly as follows:
 *
 * readbit():
 *   bits += bits;                 // add, output is in carry flag
 *   if(!bits)                     // the last bit is a terminating flag
 *     bits = load();
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

	if (~ice->bits & 0x80000000u) {
		debug("  first bit not set; stream is invalid at this size");
		return -1;
	}
	while (~tmp & 1) {
		tmp >>= 1;
		ice->bits_left--;
	}
	/* Last valid bit is also discarded. */
	tmp >>= 1;
	ice->bits_left--;

	if (ice->bits_left) {
		tmp <<= 32 - ice->bits_left;
	}
#ifndef ICE_ORIGINAL_BITSTREAM
	ice->bits = tmp;
#endif
	debug("  adjusted to %02x (bits left: %d)", ice->bits, ice->bits_left);
	return 0;
}

static int ice_load8(struct ice_state *ice)
{
	int val = ice_read_byte(ice);
	if (val < 0) {
		return -1;
	}
	ice->bits = (unsigned)val << 24u;
	ice->bits_left += 8;
	return 0;
}

static inline int ice_load16le(struct ice_state *ice)
{
	int val = ice_read_u16le(ice);
	if (val < 0) {
		return -1;
	}
	ice->bits = (unsigned)val << 16u;
	ice->bits_left += 16;
	return 0;
}

static int ice_load32(struct ice_state *ice)
{
	ice_uint32 bits = ice_peek_u32(ice);
	if (bits == 0) { /* EOF */
		return -1;
	}
	ice->buffer_pos -= 4;
	ice->bits = bits;
	ice->bits_left += 32;
	return 0;
}

/* ice_unpack_fn8 and its helper functions. */
#define STREAMSIZE 8
#include "ice_unpack_fn.c"
#undef STREAMSIZE

/* ice_unpack_fn32 and its helper functions. */
#define STREAMSIZE 32
#include "ice_unpack_fn.c"
#undef STREAMSIZE

static int ice_unpack8(struct ice_state * ICE_RESTRICT ice,
	ice_uint8 * ICE_RESTRICT dest, size_t dest_len)
{
	debug("ice_unpack8");
	if (ice_load8(ice) < 0 || ice_preload_adjust(ice) < 0) {
		return -1;
	}
	return ice_unpack_fn8(ice, dest, dest_len);
}

static int ice_unpack32(struct ice_state * ICE_RESTRICT ice,
	ice_uint8 * ICE_RESTRICT dest, size_t dest_len)
{
	debug("ice_unpack32");
	if (ice_load32(ice) < 0 || ice_preload_adjust(ice) < 0) {
		return -1;
	}
	return ice_unpack_fn32(ice, dest, dest_len);
}

static int ice_unpack(struct ice_state * ICE_RESTRICT ice,
	ice_uint8 * ICE_RESTRICT dest, size_t dest_len)
{
	debug("ice_unpack");
	if (ice_check_compressed_size(ice) < 0 ||
	    ice_check_uncompressed_size(ice, dest_len) < 0 ||
	    ice_init_buffer(ice) < 0) {
		return -1;
	}

	if (ice->version >= VERSION_21X_OR_220) {
		if (ice_unpack8(ice, dest, dest_len) == 0) {
			return 0;
		}
	}
	/* Ambiguous version: reset buffer to try again. */
	if (ice->version == VERSION_21X_OR_220 &&
	    ice_init_buffer(ice) < 0) {
		return -1;
	}
	if (ice->version <= VERSION_21X_OR_220) {
		if (ice_unpack32(ice, dest, dest_len) == 0) {
			return 0;
		}
	}
	return -1;
}


long ice1_unpack_test(const void *end_of_file, size_t sz)
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
	if (ice1_unpack_test(buf, 8) < 0) {
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

long ice2_unpack_test(const void *start_of_file, size_t sz)
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
	if (ice2_unpack_test(buf, 12) < (long)dest_len) {
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
