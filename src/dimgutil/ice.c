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

#if 1
#define ICE_DEBUG
#endif

#if 1
#define ICE_ENABLE_ICE1
#endif

#if 0
#define ICE_LIBXMP
#include "depacker.h"
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

/* LZ77 -> no useful bound, just use the library maximum limit. */
#define ICE_DEPACK_LIMIT	LIBXMP_DEPACK_LIMIT
#define ICE_RESTRICT		LIBXMP_RESTRICT
#define ICE_ATTRIB_PRINTF(x,y)	LIBXMP_ATTRIB_PRINTF(x,y)
#else
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef uint8_t uint8;
typedef uint32_t uint32;
typedef uint64_t uint64;
#define ICE_DEPACK_LIMIT	(1<<28)
#define ICE_RESTRICT		__restrict
#define ICE_ATTRIB_PRINTF(x,y)	__attribute__((__format__(gnu_printf,x,y)))
#define MIN(a,b) ((a) < (b) ? (a) : (b))

typedef struct { const uint8 *buffer; size_t len; size_t pos; } HIO_HANDLE;

static inline long hio_size(HIO_HANDLE *f)
{
	return f->len;
}

static inline size_t hio_read(void *dest, size_t len, size_t nmemb, HIO_HANDLE *f)
{
	if (len == 0) return 0;
	if ((f->len - f->pos) < len * nmemb) {
		nmemb = (f->len - f->pos) / len;
	}
	memcpy(dest, f->buffer + f->pos, len * nmemb);
	f->pos += len * nmemb;
	return nmemb;
}

static inline long hio_tell(HIO_HANDLE *f)
{
	return f->pos;
}

static inline int hio_seek(HIO_HANDLE *f, long pos, int whence)
{
	switch (whence) {
	case SEEK_SET:
		break;
	case SEEK_CUR:
		pos += f->pos;
		break;
	case SEEK_END:
		pos += f->len;
		break;
	default:
		return -1;
	}
	if (pos < 0 || (size_t)pos > f->len) return -1;
	f->pos = pos;
	return 0;
}

struct depacker {
	int (*test)(unsigned char *);
	int (*depack)(HIO_HANDLE *, void **, long *);
	int (*test_handle)(HIO_HANDLE *);
};
const struct depacker libxmp_depacker_ice1;
const struct depacker libxmp_depacker_ice2;
#endif

/* Size of input buffer for filesystem reads. */
#define ICE_BUFFER_SIZE		4096

/* loader.h */
#define MAGIC4(a,b,c,d) \
	(((uint32)(a)<<24)|((uint32)(b)<<16)|((uint32)(c)<<8)|(d))

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
	HIO_HANDLE *in;
	long in_size;
	uint32 compressed_size;
	uint32 uncompressed_size;
	int version;
	int eof;
	int bits_left;
	uint32 bits;
	uint8 buffer[ICE_BUFFER_SIZE];
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

static uint32 mem_u32(uint8 *buf)
{
	return (buf[0] << 24UL) | (buf[1] << 16UL) | (buf[2] << 8UL) | buf[3];
}

static int ice_check_sizes(struct ice_state *ice)
{
	debug("ice_check_sizes");
	if (ice->in_size < 12 || ice->compressed_size < 4 ||
	    ice->compressed_size > (size_t)ice->in_size) {
		debug("  bad compressed_size %u", (unsigned)ice->compressed_size);
		return -1;
	}
	if (ice->uncompressed_size == 0 ||
	    ice->uncompressed_size > ICE_DEPACK_LIMIT) {
		debug("  bad uncompressed_size %u", (unsigned)ice->uncompressed_size);
		return -1;
	}
	return 0;
}

static int ice_fill_buffer(struct ice_state *ice)
{
	debug("ice_fill_buffer");
	if (hio_seek(ice->in, ice->next_seek, SEEK_SET) < 0) {
		debug("  failed to seek to %ld", ice->next_seek);
		ice->eof = 1;
		return -1;
	}
	if (hio_read(ice->buffer, 1, ice->next_length, ice->in) < ice->next_length) {
		debug("  failed to read %u", ice->next_length);
		ice->eof = 1;
		return -1;
	}
	ice->buffer_pos = ice->next_length;
	ice->next_seek -= ICE_BUFFER_SIZE;
	ice->next_length = ICE_BUFFER_SIZE;
	return 0;
}

static int ice_peek_start(struct ice_state *ice, uint8 buf[4])
{
	debug("ice_peek_start");
	if (hio_seek(ice->in, (long)ice->compressed_size - 4, SEEK_SET) < 0) {
		debug("  failed seek to %ld", (long)ice->compressed_size - 4);
		return -1;
	}
	if (hio_read(buf, 1, 4, ice->in) < 4) {
		debug("  failed read");
		return -1;
	}
	debug("  = %02x%02x%02x%02x", buf[0], buf[1], buf[2], buf[3]);
	return 0;
}

static int ice_init_buffer(struct ice_state *ice)
{
	size_t len = ice->compressed_size;
	uint8 tmp[4];
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
		n = MIN(num, ice->bits_left);
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

	} else while (num > 0) {
		if (ice->bits_left <= 0) {
			if (ice_load32(ice) < 0)
				return -1;
		}
		n = MIN(num, ice->bits_left);
		ret |= ice->bits >> (32 - num);
		num -= n;
		ice->bits <<= n;
		ice->bits_left -= n;
	}
	debug("      <- %03x", ret);
	return ret;
}

#define ice_output_byte(b) do { \
	uint8 byte = (b); \
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
		zero_len = MIN(zero_len, copy_length);			\
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
	uint8 * ICE_RESTRICT dest, size_t dest_len)
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
	uint8 * ICE_RESTRICT dest, size_t dest_len)
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
	void **out, long *outlen)
{
	uint8 *outbuf;
	size_t sz;

	debug("ice_unpack");
	if (ice_check_sizes(ice) < 0) {
		return -1;
	}

	sz = ice->uncompressed_size;
	outbuf = (uint8 *) malloc(sz);
	if (!outbuf) {
		debug("  alloc error");
		return -1;
	}

	if (ice->version >= VERSION_21X_OR_220) {
		if (ice_unpack8(ice, outbuf, sz) == 0) {
			*out = outbuf;
			*outlen = sz;
			return 0;
		}
	}
	if (ice->version <= VERSION_21X_OR_220) {
		if (ice_unpack32(ice, outbuf, sz) == 0) {
			*out = outbuf;
			*outlen = sz;
			return 0;
		}
	}
	*out = outbuf;
	*outlen = sz;
	return 0;

/*
	free(outbuf);
	return -1;
*/
}

#ifdef ICE_ENABLE_ICE1
static int ice1_test(HIO_HANDLE *in)
{
	uint8 buf[4];

	if (hio_seek(in, -4, SEEK_END) < 0) {
		return -1;
	}
	if (hio_read(buf, 1, 4, in) < 4) {
		return -1;
	}
	return mem_u32(buf) == ICE_OLD_MAGIC;
}

static int ice1_decrunch(HIO_HANDLE *in, void **out, long *outlen)
{
	struct ice_state ice;
	uint8 buf[8];
	int ret;

	memset(&ice, 0, sizeof(ice));

	if (hio_seek(in, -8, SEEK_END) < 0) {
		return -1;
	}
	if (hio_read(buf, 1, 4, in) < 4) {
		return -1;
	}

	ice.in = in;
	ice.in_size = hio_size(in);
	ice.compressed_size = ice.in_size - 8;
	ice.uncompressed_size = mem_u32(buf + 0);
	ice.version = VERSION_113;

	ret = ice_unpack(&ice, out, outlen);
	return ret;
}

const struct depacker libxmp_depacker_ice1 =
{
	NULL,
	ice1_decrunch,
	ice1_test
};
#endif

static int ice2_test(unsigned char *data)
{
	uint32 magic = mem_u32(data);
	return	magic == ICE_OLD_MAGIC ||
		magic == ICE_NEW_MAGIC ||
		magic == CJ_MAGIC ||
		magic == MICK_MAGIC ||
		magic == SHE_MAGIC ||
		magic == TMM_MAGIC ||
		magic == TSM_MAGIC;
}

static int ice2_decrunch(HIO_HANDLE *in, void **out, long *outlen)
{
	struct ice_state ice;
	uint8 buf[12];
	int ret;

	memset(&ice, 0, sizeof(ice));

	if (hio_read(buf, 1, 12, in) < 12) {
		return -1;
	}

	ice.in = in;
	ice.in_size = hio_size(in);
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

	ret = ice_unpack(&ice, out, outlen);
	return ret;
}

const struct depacker libxmp_depacker_ice2 =
{
	ice2_test,
	ice2_decrunch,
	NULL
};

#ifndef ICE_LIBXMP
#ifdef _WIN32
#include <fcntl.h>
#endif

static const struct depacker * const d[] =
{
	&libxmp_depacker_ice1,
	&libxmp_depacker_ice2,
	NULL
};

static int depack(HIO_HANDLE *f, void **out, long *outlen)
{
	uint8 buf[1024];
	int i;
	if (hio_read(buf, 1, 1024, f) < 1024) {
		return -1;
	}
	for (i = 0; d[i]; i++) {
		if ((d[i]->test && d[i]->test(buf)) ||
		    (d[i]->test_handle && d[i]->test_handle(f))) {
			hio_seek(f, 0, SEEK_SET);
			return d[i]->depack(f, out, outlen);
		}
	}
	return -1;
}

#ifdef LIBFUZZER_FRONTEND
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	HIO_HANDLE hio = { data, 0, size };
	void *out = NULL;
	long out_length;
	depack(&hio, &out, &out_length);
	free(ret);
	return 0;
}

#define main _main
static __attribute__((unused))
#endif

int main(int argc, char *argv[])
{
	HIO_HANDLE hio;
	FILE *f;
	void *out;
	long out_length;
	void *data;
	unsigned long file_length;
	int ret;

	if(argc < 2)
		return -1;

#ifdef _WIN32
	/* Windows forces stdout to be text mode by default, fix it. */
	_setmode(_fileno(stdout), _O_BINARY);
#endif

	f = fopen(argv[1], "rb");
	if(!f)
		return -1;

	fseek(f, 0, SEEK_END);
	file_length = ftell(f);
	rewind(f);
	if ((data = malloc(file_length)) == NULL) {
		fclose(f);
		return -1;
	}
	if (fread(data, 1, file_length, f) < (size_t)file_length) {
		fclose(f);
		return -1;
	}
	fclose(f);

	hio.buffer = data;
	hio.pos = 0;
	hio.len = file_length;

	ret = depack(&hio, &out, &out_length);
	free(data);

	if (ret < 0)
		return ret;

	fwrite(out, out_length, 1, stdout);
	free(out);
	return 0;
}
#endif
