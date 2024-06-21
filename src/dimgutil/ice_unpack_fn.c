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

/* Internal code for ice_unpack.c.
 * This file is included twice with different defines to generate the two
 * different depackers, which rely on very different functions to work.
 * The alternative was even more macros... */

#ifndef STREAMSIZE
#error Define STREAMSIZE!
#endif

#undef JOIN_2
#undef JOIN
#define JOIN_2(a, b) a##b
#define JOIN(a, b) JOIN_2(a, b)

#define ice_load_STREAMSIZE			JOIN(ice_load,STREAMSIZE)
#define ice_read_table_STREAMSIZE		JOIN(ice_read_table,STREAMSIZE)
#define ice_read_bits_STREAMSIZE		JOIN(ice_read_bits,STREAMSIZE)
#define ice_read_literal_length_ext_STREAMSIZE	JOIN(ice_read_literal_length_ext,STREAMSIZE)
#define ice_read_literal_length_STREAMSIZE	JOIN(ice_read_literal_length,STREAMSIZE)
#define ice_read_window_length_STREAMSIZE	JOIN(ice_read_window_length,STREAMSIZE)
#define ice_read_window_distance_STREAMSIZE	JOIN(ice_read_window_distance,STREAMSIZE)
#define ice_at_stream_start_STREAMSIZE		JOIN(ice_at_stream_start,STREAMSIZE)
#define ice_unpack_fn_STREAMSIZE		JOIN(ice_unpack_fn,STREAMSIZE)

/* If ICE_TABLE_DECODING is defined, use the optimization tables a la
 * DEFLATE to reduce the number of single bit reads. Due to the janky way
 * this format is implemented this can't be optimized to the same degree
 * as DEFLATE or Amiga LZX.
 *
 * Otherwise, read the packed data mostly the intended (slow) way.
 * The length-2 distances reads are somewhat optimized regardless.
 * Also see the table generator in ice_unpack.c.
 */

#ifdef ICE_TABLE_DECODING

/* Skip the read function and read values and bit counts off a table.
 */
static inline int ice_read_table_STREAMSIZE(struct ice_state *ice,
 const struct ice_table_entry *table, int table_bits)
{
	struct ice_table_entry e;
	int used;
	int code;
	int num;
	/* Need at least 1 bit in the buffer. */
	if (ice->bits_left <= 0) {
		ice_load_STREAMSIZE(ice);
	}

	code = ice->bits >> (32 - table_bits);
	e = table[code];
	used = e.bits_used;

	if (ice->bits_left < used) {
		/* Treat the bits in the buffer as consumed and load more.
		 * Note: tables >9 bits would require a second load for
		 * 8-bit reads, but this implementation doesn't use any.
		 */
		num = ice->bits_left;
		ice_load_STREAMSIZE(ice);
#ifdef ICE_ORIGINAL_BITSTREAM
		/* Mask off terminator bit */
		code &= (0xffffffffu << table_bits) >> num;
#endif
		code |= ice->bits >> ((32 - table_bits) + num);

		e = table[code];
		used = e.bits_used - num;
	}

	/* Consume used bits directly off the buffer. */
	debug("      <- %d (bits: %03x)", e.value, code >> (table_bits - e.bits_used));
	ice->bits <<= used;
	ice->bits_left -= e.bits_used;

#ifdef ICE_ORIGINAL_BITSTREAM
	/* Inject new terminator bit */
	ice->bits |= (1 << (31 - ice->bits_left));
#endif
	return e.value;
}
#endif /* ICE_TABLE_DECODING */

static inline int ice_read_bits_STREAMSIZE(struct ice_state *ice, int num)
{
	/* NOTE: there are interleaved uncompressed bytes in the input so
	 * this unfortunately can't be optimized very much.
	 */
	int ret = 0;

#ifdef ICE_ORIGINAL_BITSTREAM
	/* This decoder is surprisingly fast on platforms with carry flags,
	 * but it's not really compatible with the table decoding (faster)
	 * and the hacks that allow it to work negate the benefits.
	 */
	ice_uint32 bits = ice->bits;
#ifdef ICE_TABLE_DECODING
	ice->bits_left -= num;
#endif
	while (num) {
		int bit = (bits >> 31u);
		bits <<= 1;
		if (!bits) {
			ice_load_STREAMSIZE(ice);

			bit = (ice->bits >> 31u);
			bits = (ice->bits << 1) | (1 << (32 - STREAMSIZE));
		}
		ret = (ret << 1) | bit;
		num--;
	}
	ice->bits = bits;
#else /* !ICE_ORIGINAL_BITSTREAM */
	int left = num - ice->bits_left;
	ret = ice->bits >> (32 - num);

	ice->bits_left -= num;
	if (left <= 0) {
		ice->bits <<= num;
	} else {
#if STREAMSIZE == 8
		if (left > 8) {
			/* Can load two bytes safely in this case--due to the
			 * backwards stream order they're read little endian. */
			ice_load16le(ice);
		} else
#endif
		{
			ice_load_STREAMSIZE(ice);
		}
		ret |= ice->bits >> (32 - left);
		ice->bits <<= left;
	}
#endif /* !ICE_ORIGINAL_BITSTREAM */
	debug("      <- %03x", ret);
	return ret;
}

/* Split off from the main function since 1.x does something else and it's
 * also the same with and without table decoding. */
static inline int ice_read_literal_length_ext_STREAMSIZE(struct ice_state *ice)
{
	int length;
#if STREAMSIZE == 32
	if (ice->version == VERSION_113) {
		return ice_read_bits_STREAMSIZE(ice, 10) + 15;
	}
#endif
	length = ice_read_bits_STREAMSIZE(ice,  8) + 15;
	if (length == 270) {
		length = ice_read_bits_STREAMSIZE(ice, 15) + 270;
	}
	return length;
}

static inline int ice_read_literal_length_STREAMSIZE(struct ice_state *ice)
{
	int length;
#ifdef ICE_TABLE_DECODING
	length = ice_read_table_STREAMSIZE(ice, literal_table, 9);
	if (length == VALUE_SPECIAL) {
		length = ice_read_literal_length_ext_STREAMSIZE(ice);
	}
#else
	length = ice_read_bits_STREAMSIZE(ice, 1);
	if (length ==  1) length = ice_read_bits_STREAMSIZE(ice, 1) + 1;
	if (length ==  2) length = ice_read_bits_STREAMSIZE(ice, 2) + 2;
	if (length ==  5) length = ice_read_bits_STREAMSIZE(ice, 2) + 5;
	if (length ==  8) length = ice_read_bits_STREAMSIZE(ice, 3) + 8;
	if (length == 15) length = ice_read_literal_length_ext_STREAMSIZE(ice);
#endif
	if (ice->eof) {
		return -1;
	}
	return length;
}

static inline int ice_read_window_length_STREAMSIZE(struct ice_state *ice)
{
	int length;
#ifdef ICE_TABLE_DECODING
	length = ice_read_table_STREAMSIZE(ice, length_table, 6);
	if (length == VALUE_SPECIAL) {
		length = 10 + ice_read_bits_STREAMSIZE(ice, 10);
	}
#else
	if (ice_read_bits_STREAMSIZE(ice, 1) == 0) {
		length = 2;
	} else if (ice_read_bits_STREAMSIZE(ice, 1) == 0) {
		length = 3;
	} else if (ice_read_bits_STREAMSIZE(ice, 1) == 0) {
		length = 4 + ice_read_bits_STREAMSIZE(ice, 1);
	} else if (ice_read_bits_STREAMSIZE(ice, 1) == 0) {
		length = 6 + ice_read_bits_STREAMSIZE(ice, 2);
	} else {
		length = 10 + ice_read_bits_STREAMSIZE(ice, 10);
	}
#endif
	if (ice->eof) {
		return -1;
	}
	debug("    length=%d", length);
	return length;
}

static inline int ice_read_window_distance_STREAMSIZE(struct ice_state *ice, int length)
{
	int dist;
	if (length == 2) {
		/*
		if (ice_read_bits_STREAMSIZE(ice, 1) == 0)
			dist = 1 + ice_read_bits_STREAMSIZE(ice, 6);
		else
			dist = 65 + ice_read_bits_STREAMSIZE(ice, 9);
		*/
		dist = 1 + ice_read_bits_STREAMSIZE(ice, 7);
		if (dist >= 65) {
			dist = ((dist - 65) << 3) + 65 + ice_read_bits_STREAMSIZE(ice, 3);
		}
	} else {
#ifdef ICE_TABLE_DECODING
		dist = ice_read_table_STREAMSIZE(ice, distance_table, 9);
		if (dist == VALUE_SPECIAL) {
			dist = 289 + ice_read_bits_STREAMSIZE(ice, 12);
		}
#else
		if (ice_read_bits_STREAMSIZE(ice, 1) == 0)
			dist = 33 + ice_read_bits_STREAMSIZE(ice, 8);
		else if (ice_read_bits_STREAMSIZE(ice, 1) == 0)
			dist = 1 + ice_read_bits_STREAMSIZE(ice, 5);
		else
			dist = 289 + ice_read_bits_STREAMSIZE(ice, 12);
#endif
	}
	if (ice->eof) {
		return -1;
	}
	debug("    dist=%d", dist);
	return dist;
}

static inline int ice_at_stream_start_STREAMSIZE(struct ice_state *ice,
	size_t offset)
{
#if STREAMSIZE == 32
	size_t start = (ice->version == VERSION_113) ? 0 : 12;
#else
	size_t start = 12;
#endif

#ifdef ICE_ORIGINAL_BITSTREAM
	return offset == start && ice->bits == 0x80000000u;
#else
	return offset == start && ice->bits_left <= 0;
#endif
}

static inline int ice_unpack_fn_STREAMSIZE(struct ice_state *ice,
	ice_uint8 * ICE_RESTRICT dest, size_t dest_len)
{
	size_t dest_offset = dest_len;
	size_t window_offset;
	int length;
	int dist;

	/* Don't terminate here--streams ending with a window copy expect
	 * a final zero-length literal block. Ending here breaks the
	 * bitplane filter check. */
	while (1) {
		length = ice_read_literal_length_STREAMSIZE(ice);
		if (length < 0) {
			return -1;
		}
		debug("  (%zu remaining) copy of %d", dest_offset, length);
		if ((size_t)length > dest_offset) {
			debug("  ERROR: copy would write past start of file");
			return -1;
		}
		for (; length > 0; length--) {
			int b = ice_read_byte(ice);
			if (b < 0) {
				return -1;
			}
			dest[--dest_offset] = b;
		}
		if (dest_offset == 0) {
			break;
		}

		length = ice_read_window_length_STREAMSIZE(ice);
		if (length <= 0) {
			return -1;
		}
		dist = ice_read_window_distance_STREAMSIZE(ice, length);
		if (dist <= 0) {
			return -1;
		}

		/* The distance value is relative to the last byte written,
		 * not the current position. The copied word never overlaps
		 * the area being written unless dist == 0 (RLE). */
		if (STREAMSIZE == 32)	dist = dist + length - 1;
		else if (dist > 1)	dist = dist + length - 2;

		debug("  (%zu remaining) window copy of %u, dist %d",
			dest_offset, length, dist);
		if ((size_t)length > dest_offset) {
			debug("  ERROR: copy would write past start of file");
			return -1;
		}

		window_offset = dist + dest_offset;
		if (window_offset > dest_len) {
			/* Haven't found a valid Pack-Ice file that does this. */
			size_t zero_len = window_offset - dest_len;
			zero_len = ICE_MIN(zero_len, (size_t)length);
			memset(dest + dest_offset - zero_len, 0, zero_len);
			window_offset -= zero_len;
			dest_offset -= zero_len;
			length -= zero_len;
			debug("    (window copy is in suffix zone)");
		}
		for (; length > 0; length--) {
			dest[--dest_offset] = dest[--window_offset];
		}
	}

	/* Bitplane filter (optional). */
	if (ice->version >= VERSION_21X &&
	    ice_read_bits_STREAMSIZE(ice, 1) == 1) {
		debug("  bitplane filter used");
		length = 320 * 200 / 16;
		if (!ice_at_stream_start_STREAMSIZE(ice, ice->buffer_pos) &&
		    ice_read_bits_STREAMSIZE(ice, 1) == 1) {
			length = ice_read_bits_STREAMSIZE(ice, 16) + 1;
			if (ice->eof) {
				debug("  failed to read bitplane filter length");
				return -1;
			}
			debug("  bitplane filter of size %d\n", length);
		} else {
			debug("  bitplane filter of size 320 * 200 / 16");
		}

		if (ice_bitplane_filter(ice, dest, dest_len, length) < 0) {
			return -1;
		}
	}

	if (!ice_at_stream_start_STREAMSIZE(ice, ice->buffer_pos)) {
		debug("  remaining data in stream: pos=%u bits=%08u left=%d",
			(unsigned)ice->buffer_pos, (unsigned)ice->bits, ice->bits_left);
	}
	return 0;
}

#undef ice_load_STREAMSIZE
#undef ice_read_table_STREAMSIZE
#undef ice_read_bits_STREAMSIZE
#undef ice_read_literal_length_ext_STREAMSIZE
#undef ice_read_literal_length_STREAMSIZE
#undef ice_read_window_length_STREAMSIZE
#undef ice_read_window_distance_STREAMSIZE
#undef ice_at_stream_start_STREAMSIZE
#undef ice_unpack_fn_STREAMSIZE
