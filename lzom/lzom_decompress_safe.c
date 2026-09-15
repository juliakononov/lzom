// SPDX-License-Identifier: GPL-2.0-only
/*
 *  LZO1X Decompressor from LZO
 *
 *  Copyright (C) 1996-2012 Markus F.X.J. Oberhumer <markus@oberhumer.com>
 *
 *  The full LZO package can be found at:
 *  http://www.oberhumer.com/opensource/lzo/
 *
 *  Changed for Linux kernel use by:
 *  Nitin Gupta <nitingupta910@gmail.com>
 *  Richard Purdie <rpurdie@openedhand.com>
 *
 *  Modified by:
 *  julickononov <julickkria@gmail.com>
 */

#ifndef STATIC
#include <linux/kernel.h>
#include <linux/module.h>
#endif
#include <linux/unaligned.h>

#include "include/lzom_extend.h"
#include "include/lzom_sg_helpers.h"
#include "include/lzomdefs.h"

#define HAVE_IP(x) (in->iter.bi_size >= (size_t)(x))
#define HAVE_OP(x) (out->iter.bi_size >= (size_t)(x))
#define NEED_IP(x)                 \
	if (unlikely(!HAVE_IP(x))) \
	goto input_overrun
#define NEED_OP(x)                 \
	if (unlikely(!HAVE_OP(x))) \
	goto output_overrun

/* This MAX_255_COUNT is the maximum number of times we can add 255 to a base
 * count without overflowing an integer. The multiply will overflow when
 * multiplying 255 by more than MAXINT/255. The sum will overflow earlier
 * depending on the base count. Since the base count is taken from a u8
 * and a few bits, it is safe to assume that it will always be lower than
 * or equal to 2*255, thus we can always prevent any overflow by accepting
 * two less 255 steps. See Documentation/staging/lzo.rst for more information.
 */
#define MAX_255_COUNT ((((size_t)~0) / 255) - 2)

int lzom_decompress_safe(struct lzom_sg_buf *in, struct lzom_sg_buf *out)
{
	struct bvec_iter in_iter = in->iter;
	struct bvec_iter out_iter = out->iter;
	size_t out_cap = out->iter.bi_size;

	size_t t, next;
	size_t state = 0;
	size_t distance;
	unsigned char bitstream_version;
	int ret;

	if (unlikely(!HAVE_IP(3)))
		goto input_overrun;

	if (likely(HAVE_IP(5)) &&
	    likely(lzom_sg_read1_at(in, in->iter, 0) == 17)) {
		bitstream_version = lzom_sg_read1_at(in, in->iter, 1);
		sg_skip_bytes(in, 2);
	} else {
		bitstream_version = 0;
	}

	if (lzom_sg_read1_at(in, in->iter, 0) > 17) {
		t = lzom_sg_read1(in) - 17;
		if (t < 4) {
			next = t;
			goto match_next;
		}
		goto copy_literal_run;
	}

	for (;;) {
		t = lzom_sg_read1(in);
		if (t < 16) {
			if (likely(state == 0)) {
				if (unlikely(t == 0)) {
					size_t zrun;

					ret = lzom_sg_count_zero_run(
						in, MAX_255_COUNT, &zrun);
					if (unlikely(ret ==
						     LZO_E_INPUT_OVERRUN))
						goto input_overrun;
					if (unlikely(ret != LZO_E_OK))
						return ret;

					t += ((zrun << 8) - zrun) + 15 +
					     lzom_sg_read1(in);
				}
				t += 3;
			copy_literal_run:
				NEED_OP(t);
				NEED_IP(t + 3);
				while (t >= 8) {
					LZOM_COPY8(out, in);
					t -= 8;
				}
				if (t > 0) {
					unsigned char tmp[8];

					lzom_sg_copy(out, in, tmp, t);
				}
				state = 4;
				continue;
			} else if (state != 4) {
				next = t & 3;
				distance = 1 + (t >> 2) +
					   ((size_t)lzom_sg_read1(in) << 2);
				ret = lzom_sg_match_copy(out, distance, 2);
				if (unlikely(ret == LZO_E_LOOKBEHIND_OVERRUN))
					goto lookbehind_overrun;
				if (unlikely(ret == LZO_E_OUTPUT_OVERRUN))
					goto output_overrun;
				goto match_next;
			} else {
				next = t & 3;
				distance = (1 + M2_MAX_OFFSET) + (t >> 2) +
					   ((size_t)lzom_sg_read1(in) << 2);
				t = 3;
			}
		} else if (t >= 64) {
			next = t & 3;
			distance = 1 + ((t >> 2) & 7) +
				   ((size_t)lzom_sg_read1(in) << 3);
			t = (t >> 5) - 1 + (3 - 1);
		} else if (t >= 32) {
			t = (t & 31) + (3 - 1);
			if (unlikely(t == 2)) {
				size_t zrun;

				ret = lzom_sg_count_zero_run(in, MAX_255_COUNT,
							     &zrun);
				if (unlikely(ret == LZO_E_INPUT_OVERRUN))
					goto input_overrun;
				if (unlikely(ret != LZO_E_OK))
					return ret;

				t += ((zrun << 8) - zrun) + 31 +
				     lzom_sg_read1(in);
			}
			{
				u16 v = le16_to_cpu(lzom_sg_read2(in));

				distance = 1 + (v >> 2);
				next = v & 3;
			}
		} else {
			NEED_IP(2);
			{
				u16 v = le16_to_cpu(
					lzom_sg_read2_at(in, in->iter, 0));

				if (((v & 0xfffc) == 0xfffc) &&
				    ((t & 0xf8) == 0x18) &&
				    likely(bitstream_version)) {
					NEED_IP(3);
					t &= 7;
					t |= (size_t)lzom_sg_read1_at(
						     in, in->iter, 2)
					     << 3;
					t += MIN_ZERO_RUN_LENGTH;
					NEED_OP(t);
					sg_write_zeros(out, t);
					next = v & 3;
					sg_skip_bytes(in, 3);
					goto match_next;
				}

				distance = (t & 8) << 11;
				t = (t & 7) + (3 - 1);
				if (unlikely(t == 2)) {
					size_t zrun;

					ret = lzom_sg_count_zero_run(
						in, MAX_255_COUNT, &zrun);
					if (unlikely(ret ==
						     LZO_E_INPUT_OVERRUN))
						goto input_overrun;
					if (unlikely(ret != LZO_E_OK))
						return ret;

					t += ((zrun << 8) - zrun) + 7 +
					     lzom_sg_read1(in);
					NEED_IP(2);
					v = le16_to_cpu(lzom_sg_read2_at(
						in, in->iter, 0));
				}
				sg_skip_bytes(in, 2);
				distance += v >> 2;
				next = v & 3;
				if (distance == 0)
					goto eof_found;
				distance += 0x4000;
			}
		}

		ret = lzom_sg_match_copy(out, distance, t);
		if (unlikely(ret == LZO_E_LOOKBEHIND_OVERRUN))
			goto lookbehind_overrun;
		if (unlikely(ret == LZO_E_OUTPUT_OVERRUN))
			goto output_overrun;
	match_next:
		state = next;
		t = next;
		NEED_IP(t + 3);
		NEED_OP(t);
		while (t > 0) {
			lzom_sg_copy1(out, in);
			t--;
		}
	}

eof_found: {
	size_t in_remaining = in->iter.bi_size;

	lzom_sg_finish(in, in_iter, out, out_iter, out_cap);

	if (t != 3)
		return LZO_E_ERROR;

	return in_remaining == 0 ? LZO_E_OK : LZO_E_INPUT_NOT_CONSUMED;
}

input_overrun:
	lzom_sg_finish(in, in_iter, out, out_iter, out_cap);
	return LZO_E_INPUT_OVERRUN;

output_overrun:
	lzom_sg_finish(in, in_iter, out, out_iter, out_cap);
	return LZO_E_OUTPUT_OVERRUN;

lookbehind_overrun:
	lzom_sg_finish(in, in_iter, out, out_iter, out_cap);
	return LZO_E_LOOKBEHIND_OVERRUN;
}
#ifndef STATIC
// EXPORT_SYMBOL_GPL(lzom_decompress_safe);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("LZO1X Decompressor");

#endif
