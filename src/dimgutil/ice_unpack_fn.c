/* Extended Module Player
 * Copyright (C) 2024-2025 Alice Rowan <petrifiedrowan@gmail.com>
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

#define ice_load_SZ			JOIN(ice_load,STREAMSIZE)
#define ice_read_table_SZ		JOIN(ice_read_table,STREAMSIZE)
#define ice_read_bits_SZ		JOIN(ice_read_bits,STREAMSIZE)
#define ice_read_literal_length_ext_SZ	JOIN(ice_read_literal_length_ext,STREAMSIZE)
#define ice_read_literal_length_SZ	JOIN(ice_read_literal_length,STREAMSIZE)
#define ice_read_window_length_SZ	JOIN(ice_read_window_length,STREAMSIZE)
#define ice_read_window_distance_SZ	JOIN(ice_read_window_distance,STREAMSIZE)
#define ice_at_stream_start_SZ		JOIN(ice_at_stream_start,STREAMSIZE)
#define ice_unpack_fn_SZ		JOIN(ice_unpack_fn,STREAMSIZE)

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
static ICE_INLINE int ice_read_table_SZ(
	struct ice_state * ICE_RESTRICT ice,
	ice_uint32 * ICE_RESTRICT bits, int * ICE_RESTRICT bits_left,
	const struct ice_table_entry *table, int table_bits)
{
	struct ice_table_entry e;
	int used;
	int code;
	int num;
	/* Need at least 1 bit in the buffer. */
	if (*bits_left <= 0) {
		ice_load_SZ(ice, bits, bits_left);
	}

	code = *bits >> (32 - table_bits);
	e = table[code];
	used = e.bits_used;

	if (*bits_left < used) {
		/* Treat the bits in the buffer as consumed and load more.
		 * Note: tables >9 bits would require a second load for
		 * 8-bit reads, but this implementation doesn't use any.
		 */
		num = *bits_left;
		ice_load_SZ(ice, bits, bits_left);
#ifdef ICE_ORIGINAL_BITSTREAM
		/* Mask off terminator bit */
		code &= (0xffffffffu << table_bits) >> num;
#endif
		code |= *bits >> ((32 - table_bits) + num);

		e = table[code];
		used = e.bits_used - num;
	}

	/* Consume used bits directly off the buffer. */
	debug("      <- %d (bits: %03x)", e.value, code >> (table_bits - e.bits_used));
	*bits <<= used;
	*bits_left -= e.bits_used;

#ifdef ICE_ORIGINAL_BITSTREAM
	/* Inject new terminator bit */
	*bits |= (1 << (31 - *bits_left));
#endif
	return e.value;
}
#endif /* ICE_TABLE_DECODING */

static ICE_INLINE int ice_read_bits_SZ(
	struct ice_state * ICE_RESTRICT ice,
	ice_uint32 * ICE_RESTRICT bits, int * ICE_RESTRICT bits_left, int num)
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
#ifdef ICE_TABLE_DECODING
	*bits_left -= num;
#endif
	while (num) {
		int bit = (*bits >> 31u);
		*bits <<= 1;
		if (!*bits) {
			ice_load_SZ(ice, bits, bits_left);

			bit = *bits >> 31u;
			*bits = (*bits << 1) | (1 << (32 - STREAMSIZE));
		}
		ret = (ret << 1) | bit;
		num--;
	}
#else /* !ICE_ORIGINAL_BITSTREAM */
	int left = num - *bits_left;
	ret = *bits >> (32 - num);

	*bits_left -= num;
	if (left <= 0) {
		*bits <<= num;
	} else {
#if STREAMSIZE == 8
		if (left > 8) {
			/* Can load two bytes safely in this case--due to the
			 * backwards stream order they're read little endian. */
			ice_load16le(ice, bits, bits_left);
		} else
#endif
		{
			ice_load_SZ(ice, bits, bits_left);
		}
		ret |= *bits >> (32 - left);
		*bits <<= left;
	}
#endif /* !ICE_ORIGINAL_BITSTREAM */
	debug("      <- %03x", ret);
	return ret;
}

/* Split off from the main function since 1.x does something else and it's
 * also the same with and without table decoding. */
static ICE_INLINE int ice_read_literal_length_ext_SZ(
	struct ice_state * ICE_RESTRICT ice,
	ice_uint32 * ICE_RESTRICT bits, int * ICE_RESTRICT bits_left)
{
	int length;
#if STREAMSIZE == 32
	if (ice->version == VERSION_113) {
		return ice_read_bits_SZ(ice, bits, bits_left, 10) + 15;
	}
#endif
	length = ice_read_bits_SZ(ice, bits, bits_left,  8) + 15;
	if (length == 270) {
		length = ice_read_bits_SZ(ice, bits, bits_left, 15) + 270;
	}
	return length;
}

static ICE_INLINE int ice_read_literal_length_SZ(
	struct ice_state * ICE_RESTRICT ice,
	ice_uint32 * ICE_RESTRICT bits, int * ICE_RESTRICT bits_left)
{
	int length;
#ifdef ICE_TABLE_DECODING
	length = ice_read_table_SZ(ice, bits, bits_left, literal_table, 9);
	if (length == VALUE_SPECIAL) {
		length = ice_read_literal_length_ext_SZ(ice, bits, bits_left);
	}
#else
	length = ice_read_bits_SZ(ice, bits, bits_left, 1);
	if (length ==  1) length = ice_read_bits_SZ(ice, bits, bits_left, 1) + 1;
	if (length ==  2) length = ice_read_bits_SZ(ice, bits, bits_left, 2) + 2;
	if (length ==  5) length = ice_read_bits_SZ(ice, bits, bits_left, 2) + 5;
	if (length ==  8) length = ice_read_bits_SZ(ice, bits, bits_left, 3) + 8;
	if (length == 15) length = ice_read_literal_length_ext_SZ(ice, bits, bits_left);
#endif
	if (ice->eof) {
		return -1;
	}
	return length;
}

static ICE_INLINE int ice_read_window_length_SZ(
	struct ice_state * ICE_RESTRICT ice,
	ice_uint32 * ICE_RESTRICT bits, int * ICE_RESTRICT bits_left)
{
	int length;
#ifdef ICE_TABLE_DECODING
	length = ice_read_table_SZ(ice, bits, bits_left, length_table, 6);
	if (length == VALUE_SPECIAL) {
		length = 10 + ice_read_bits_SZ(ice, bits, bits_left, 10);
	}
#else
	if (ice_read_bits_SZ(ice, bits, bits_left, 1) == 0) {
		length = 2;
	} else if (ice_read_bits_SZ(ice, bits, bits_left, 1) == 0) {
		length = 3;
	} else if (ice_read_bits_SZ(ice, bits, bits_left, 1) == 0) {
		length = 4 + ice_read_bits_SZ(ice, bits, bits_left, 1);
	} else if (ice_read_bits_SZ(ice, bits, bits_left, 1) == 0) {
		length = 6 + ice_read_bits_SZ(ice, bits, bits_left, 2);
	} else {
		length = 10 + ice_read_bits_SZ(ice, bits, bits_left, 10);
	}
#endif
	if (ice->eof) {
		return -1;
	}
	debug("    length=%d", length);
	return length;
}

static ICE_INLINE int ice_read_window_distance_SZ(
	struct ice_state * ICE_RESTRICT ice,
	ice_uint32 * ICE_RESTRICT bits, int * ICE_RESTRICT bits_left,
	int length)
{
	int dist;
	if (length == 2) {
		/*
		if (ice_read_bits_SZ(ice, bits, bits_left, 1) == 0)
			dist = 1 + ice_read_bits_SZ(ice, bits, bits_left, 6);
		else
			dist = 65 + ice_read_bits_SZ(ice, bits, bits_left, 9);
		*/
		dist = 1 + ice_read_bits_SZ(ice, bits, bits_left, 7);
		if (dist >= 65) {
			dist = ((dist - 65) << 3) + 65 +
				ice_read_bits_SZ(ice, bits, bits_left, 3);
		}
	} else {
#ifdef ICE_TABLE_DECODING
		dist = ice_read_table_SZ(ice, bits, bits_left, distance_table, 9);
		if (dist == VALUE_SPECIAL) {
			dist = 289 + ice_read_bits_SZ(ice, bits, bits_left, 12);
		}
#else
		if (ice_read_bits_SZ(ice, bits, bits_left, 1) == 0)
			dist = 33 + ice_read_bits_SZ(ice, bits, bits_left, 8);
		else if (ice_read_bits_SZ(ice, bits, bits_left, 1) == 0)
			dist = 1 + ice_read_bits_SZ(ice, bits, bits_left, 5);
		else
			dist = 289 + ice_read_bits_SZ(ice, bits, bits_left, 12);
#endif
	}
	if (ice->eof) {
		return -1;
	}
	debug("    dist=%d", dist);
	return dist;
}

static ICE_INLINE int ice_at_stream_start_SZ(
	struct ice_state * ICE_RESTRICT ice,
	ice_uint32 * ICE_RESTRICT bits, int * ICE_RESTRICT bits_left,
	size_t offset)
{
#if STREAMSIZE == 32
	size_t start = (ice->version == VERSION_113) ? 0 : 12;
#else
	size_t start = 12;
#endif
	/* Uses one or the other based on config; suppress both. */
	(void)bits;
	(void)bits_left;

#ifdef ICE_ORIGINAL_BITSTREAM
	return offset == start && *bits == 0x80000000u;
#else
	return offset == start && *bits_left <= 0;
#endif
}

static ICE_INLINE int ice_unpack_fn_SZ(
	struct ice_state * ICE_RESTRICT ice,
	ice_uint32 * ICE_RESTRICT bits, int * ICE_RESTRICT bits_left,
	ice_uint8 * ICE_RESTRICT dest, size_t dest_len)
{
	ice_uint8 *pos = dest + dest_len;
	ice_uint8 *window_pos;
	int length;
	int dist;

	/* Don't terminate here--streams ending with a window copy expect
	 * a final zero-length literal block. Ending here breaks the
	 * bitplane filter check. */
	while (1) {
		length = ice_read_literal_length_SZ(ice, bits, bits_left);
		if (length < 0) {
			return -1;
		}
		debug("  (%zu remaining) copy of %d", pos - dest, length);
		if (length > pos - dest) {
			debug("  ERROR: copy would write past start of file");
			return -1;
		}
		for (; length > 0; length--) {
			int b = ice_read_byte(ice);
			if (b < 0) {
				return -1;
			}
			*(--pos) = (ice_uint8)b; /* MSVC hallucinates C4244 */
		}
		if (pos == dest) {
			break;
		}

		length = ice_read_window_length_SZ(ice, bits, bits_left);
		if (length <= 0) {
			return -1;
		}
		dist = ice_read_window_distance_SZ(ice, bits, bits_left, length);
		if (dist <= 0) {
			return -1;
		}

		/* The distance value is relative to the last byte written,
		 * not the current position. The copied word never overlaps
		 * the area being written unless dist == 0 (RLE). */
		if (STREAMSIZE == 32)	dist = dist + length - 1;
		else if (dist > 1)	dist = dist + length - 2;

		debug("  (%zu remaining) window copy of %u, dist %d",
			pos - dest, length, dist);
		if (length > pos - dest) {
			debug("  ERROR: copy would write past start of file");
			return -1;
		}

		window_pos = dist + pos;
		if (window_pos > dest + dest_len) {
			/* Haven't found a valid Pack-Ice file that does this. */
			size_t zero_len = window_pos - dest - dest_len;
			zero_len = ICE_MIN(zero_len, (size_t)length);
			memset(pos - zero_len, 0, zero_len);
			window_pos -= zero_len;
			pos -= zero_len;
			length -= (int)zero_len; /* MSVC C4267 */
			debug("    (window copy is in suffix zone)");
		}
		for (; length > 0; length--) {
			*(--pos) = *(--window_pos);
		}
	}

	/* Bitplane filter (optional). */
	if (ice->version >= VERSION_21X &&
	    ice_read_bits_SZ(ice, bits, bits_left, 1) == 1) {
		debug("  bitplane filter used");
		length = 320 * 200 / 16;
		if (!ice_at_stream_start_SZ(ice, bits, bits_left, ice->buffer_pos) &&
		    ice_read_bits_SZ(ice, bits, bits_left, 1) == 1) {
			length = ice_read_bits_SZ(ice, bits, bits_left, 16) + 1;
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

	if (!ice_at_stream_start_SZ(ice, bits, bits_left, ice->buffer_pos)) {
		debug("  remaining data in stream: pos=%u bits=%08u left=%d",
			(unsigned)ice->buffer_pos, (unsigned)*bits, *bits_left);
	}
	return 0;
}

#undef ice_load_SZ
#undef ice_read_table_SZ
#undef ice_read_bits_SZ
#undef ice_read_literal_length_ext_SZ
#undef ice_read_literal_length_SZ
#undef ice_read_window_length_SZ
#undef ice_read_window_distance_SZ
#undef ice_at_stream_start_SZ
#undef ice_unpack_fn_SZ
