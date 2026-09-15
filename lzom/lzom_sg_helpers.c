// SPDX-License-Identifier: GPL-2.0-only

#include <linux/bvec.h>
#include <linux/kernel.h>

#include "include/lzom_sg_helpers.h"

int sg_write_bytes(struct lzom_sg_buf *buf, const unsigned char *data,
		   size_t len)
{
	if (len > buf->iter.bi_size)
		return -EINVAL;

	while (len) {
		struct bio_vec bv;
		size_t to_write;

		bv = bvec_iter_bvec(buf->bvec, buf->iter);
		to_write = min_t(size_t, bv.bv_len, len);

		memcpy_to_page(bv.bv_page, bv.bv_offset, (const char *)data,
			       to_write);

		bvec_iter_advance(buf->bvec, &buf->iter, to_write);
		data += to_write;
		len -= to_write;
	}

	return 0;
}

int sg_read_bytes(struct lzom_sg_buf *buf, unsigned char *data, size_t len)
{
	if (len > buf->iter.bi_size)
		return -EINVAL;

	while (len) {
		struct bio_vec bv;
		size_t to_read;

		bv = bvec_iter_bvec(buf->bvec, buf->iter);
		to_read = min_t(size_t, bv.bv_len, len);

		memcpy_from_page((char *)data, bv.bv_page, bv.bv_offset,
				 to_read);

		bvec_iter_advance(buf->bvec, &buf->iter, to_read);
		data += to_read;
		len -= to_read;
	}

	return 0;
}

void sg_skip_bytes(struct lzom_sg_buf *buf, size_t len)
{
	bool res;

	if (len > buf->iter.bi_size) {
		pr_err("Len = %lu > bi_size = %u", len, buf->iter.bi_size);
		BUG();
	}

	res = bvec_iter_advance(buf->bvec, &buf->iter, len);
	BUG_ON(!res);
}

unsigned char lzom_sg_read1_at(struct lzom_sg_buf *buf, struct bvec_iter start,
			       size_t offset)
{
	struct bvec_iter saved = buf->iter;
	unsigned char value;

	buf->iter = start;
	sg_skip_bytes(buf, offset);
	value = lzom_sg_read1(buf);
	buf->iter = saved;

	return value;
}

u16 lzom_sg_read2_at(struct lzom_sg_buf *buf, struct bvec_iter start,
		     size_t offset)
{
	struct bvec_iter saved = buf->iter;
	u16 value;

	buf->iter = start;
	sg_skip_bytes(buf, offset);
	value = lzom_sg_read2(buf);
	buf->iter = saved;

	return value;
}

u32 lzom_sg_read4_at(struct lzom_sg_buf *buf, struct bvec_iter start,
		     size_t offset)
{
	struct bvec_iter saved = buf->iter;
	u32 value;

	buf->iter = start;
	sg_skip_bytes(buf, offset);
	value = lzom_sg_read4(buf);
	buf->iter = saved;

	return value;
}

u64 lzom_sg_read8_at(struct lzom_sg_buf *buf, struct bvec_iter start,
		     size_t offset)
{
	struct bvec_iter saved = buf->iter;
	u64 value;

	buf->iter = start;
	sg_skip_bytes(buf, offset);
	value = lzom_sg_read8(buf);
	buf->iter = saved;

	return value;
}

int lzom_sg_move_back(struct lzom_sg_buf *buf, struct bvec_iter *iter,
		      size_t offset)
{
	size_t remaining = offset;

	while (remaining > 0) {
		if (iter->bi_bvec_done >= remaining) {
			iter->bi_bvec_done -= remaining;
			iter->bi_size += offset;
			return 0;
		}

		remaining -= iter->bi_bvec_done;

		if (iter->bi_idx == 0)
			return -EINVAL;

		iter->bi_idx--;
		iter->bi_bvec_done = buf->bvec[iter->bi_idx].bv_len;
	}
	iter->bi_size += offset;
	return 0;
}

unsigned char lzom_sg_read_back(struct lzom_sg_buf *buf, size_t offset)
{
	struct bvec_iter saved = buf->iter;
	struct bvec_iter tmp = buf->iter;
	unsigned char value;

	if (lzom_sg_move_back(buf, &tmp, offset) < 0)
		return -EINVAL;

	buf->iter = tmp;
	value = lzom_sg_read1(buf);
	buf->iter = saved;

	return value;
}

int lzom_sg_write_back(struct lzom_sg_buf *buf, unsigned char value,
		       size_t offset)
{
	struct bvec_iter saved = buf->iter;
	struct bvec_iter tmp = buf->iter;

	if (lzom_sg_move_back(buf, &tmp, offset) < 0)
		return -EINVAL;

	buf->iter = tmp;
	lzom_sg_write1(buf, value);
	buf->iter = saved;

	return 0;
}

int sg_write_zeros(struct lzom_sg_buf *buf, size_t len)
{
	static const unsigned char zeros[32];

	while (len) {
		size_t chunk = min_t(size_t, len, sizeof(zeros));

		if (sg_write_bytes(buf, zeros, chunk) < 0)
			return -EINVAL;

		len -= chunk;
	}

	return 0;
}

int lzom_sg_count_zero_run(struct lzom_sg_buf *in, size_t max_count,
			   size_t *count)
{
	size_t n = 0;

	for (;;) {
		unsigned char byte;

		if (unlikely(in->iter.bi_size < 1))
			return LZO_E_INPUT_OVERRUN;

		byte = lzom_sg_read1(in);
		if (byte != 0) {
			if (unlikely(lzom_sg_move_back(in, &in->iter, 1) < 0))
				return LZO_E_ERROR;
			break;
		}

		if (unlikely(++n > max_count))
			return LZO_E_ERROR;
	}

	*count = n;
	return LZO_E_OK;
}

int lzom_sg_match_copy(struct lzom_sg_buf *out, size_t distance, size_t t)
{
	struct bvec_iter src_iter = out->iter;
	struct lzom_sg_buf src;

	if (unlikely(lzom_sg_move_back(out, &src_iter, distance) < 0))
		return LZO_E_LOOKBEHIND_OVERRUN;

	if (unlikely(t > out->iter.bi_size))
		return LZO_E_OUTPUT_OVERRUN;

	src = lzom_sg_buf_create(src_iter, out->bvec);

	if (distance >= 8) {
		while (t >= 8) {
			lzom_sg_copy8(out, &src);
			t -= 8;
		}
	}
	while (t > 0) {
		lzom_sg_copy1(out, &src);
		t--;
	}

	return LZO_E_OK;
}

void lzom_sg_finish(struct lzom_sg_buf *in, struct bvec_iter in_start,
		    struct lzom_sg_buf *out, struct bvec_iter out_start,
		    size_t out_cap)
{
	size_t out_len = out_cap - out->iter.bi_size;

	out->iter = out_start;
	out->iter.bi_size = out_len;
	in->iter = in_start;
}
