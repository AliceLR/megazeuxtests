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

#define ice_load_STREAMSIZE		    JOIN(ice_load,STREAMSIZE)
#define ice_read_table_STREAMSIZE	    JOIN(ice_read_table,STREAMSIZE)
#define ice_read_bits_STREAMSIZE	    JOIN(ice_read_bits,STREAMSIZE)
#define ice_read_literal_length_STREAMSIZE  JOIN(ice_read_literal_length,STREAMSIZE)
#define ice_read_window_length_STREAMSIZE   JOIN(ice_read_window_length,STREAMSIZE)
#define ice_read_window_distance_STREAMSIZE JOIN(ice_read_window_distance,STREAMSIZE)
#define ice_unpack_fn_STREAMSIZE	    JOIN(ice_unpack_fn,STREAMSIZE)

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
	if (ice->bits_left <= 0 && ice_load_STREAMSIZE(ice) < 0) {
		return -1;
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
		if (ice_load_STREAMSIZE(ice) < 0) {
			return -1;
		}
		code |= ice->bits >> (32 - table_bits + num);

		e = table[code];
		used = e.bits_used - num;
	}

	/* Consume used bits directly off the buffer. */
	debug("      <- %d (bits: %03x)", e.value, code >> (table_bits - e.bits_used));
	ice->bits <<= used;
	ice->bits_left -= used;
	return e.value;
}
#endif /* ICE_TABLE_DECODING */

static inline int ice_read_bits_STREAMSIZE(struct ice_state *ice, int num)
{
	/* NOTE: there are interleaved uncompressed bytes in the input so
	 * this unfortunately can't be optimized very much. This can't really
	 * be implemented the way it was "supposed" to since the table
	 * decoder needs the terminator bits removed.
	 */
	int ret = 0;
	int n;

#if 0
	while (num) {
		int bit (ice->bits >> 31u);
		ice->bits <<= 1;
		if (!ice->bits) {
			if (ice_load_STREAMSIZE(ice) < 0)
				return -1;

			bit = (ice->bits >> 31u);
			ice->bits = (ice->bits << 1) | 1;
		}
		ret = (ret << 1) | bit;
		num--;
	}
#endif

	while (num > 0) {
		if (ice->bits_left <= 0) {
			if (ice_load_STREAMSIZE(ice) < 0)
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

static inline int ice_read_literal_length_STREAMSIZE(struct ice_state *ice)
{
	int length;
#ifdef ICE_TABLE_DECODING
	length = ice_read_table_STREAMSIZE(ice, literal_table, 9);
	if (length == VALUE_SPECIAL) {
		length = 15 + ice_read_bits_STREAMSIZE(ice, 8);
		if (length == 270) {
			length = 270 + ice_read_bits_STREAMSIZE(ice, 15);
		}
	}
#else
	length = ice_read_bits_STREAMSIZE(ice, 1);
	if (length ==   1) length = ice_read_bits_STREAMSIZE(ice,  1) +   1;
	if (length ==   2) length = ice_read_bits_STREAMSIZE(ice,  2) +   2;
	if (length ==   5) length = ice_read_bits_STREAMSIZE(ice,  2) +   5;
	if (length ==   8) length = ice_read_bits_STREAMSIZE(ice,  3) +   8;
	if (length ==  15) length = ice_read_bits_STREAMSIZE(ice,  8) +  15;
	if (length == 270) length = ice_read_bits_STREAMSIZE(ice, 15) + 270;
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

static inline int ice_unpack_fn_STREAMSIZE(struct ice_state *ice,
	ice_uint8 * ICE_RESTRICT dest, size_t dest_len)
{
	size_t dest_offset = dest_len;
	size_t window_offset;
	int length;
	int dist;

	while (dest_offset > 0) {
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
		if (length < 0) {
			return -1;
		}
		dist = ice_read_window_distance_STREAMSIZE(ice, length);
		if (dist < 0) {
			return -1;
		}

		if (STREAMSIZE == 32)	dist = dist + length - 1;
		else if (dist > 1) 	dist = dist + length - 2;

		debug("  (%zu remaining) window copy of %u, dist %d",
			dest_offset, length, dist);
		if ((size_t)length > dest_offset) {
			debug("  ERROR: copy would write past start of file");
			return -1;
		}

		window_offset = dist + dest_offset;
		if (length && window_offset > dest_len) {
			size_t zero_len = window_offset - dest_len;
			zero_len = ICE_MIN(zero_len, (size_t)length);
			memset(dest + dest_offset - zero_len, 0, zero_len);
			window_offset -= zero_len;
			dest_offset -= zero_len;
			length -= zero_len;
		}
		for (; length > 0; length--) {
			dest[--dest_offset] = dest[--window_offset];
		}
	}
	if (ice->version >= VERSION_21X &&
	    ice_read_bits_STREAMSIZE(ice, 1) == 1) {
		debug("  bitplane filter not supported yet :-(");
	}
	return 0;
}

#undef ice_load_STREAMSIZE
#undef ice_read_table_STREAMSIZE
#undef ice_read_bits_STREAMSIZE
#undef ice_read_literal_length_STREAMSIZE
#undef ice_read_window_length_STREAMSIZE
#undef ice_read_window_distance_STREAMSIZE
#undef ice_unpack_fn_STREAMSIZE
