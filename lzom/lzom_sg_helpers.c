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
	while (offset > 0) {
		if (iter->bi_bvec_done >= offset) {
			iter->bi_bvec_done -= offset;
			return 0;
		}

		offset -= iter->bi_bvec_done;

		if (iter->bi_idx == 0)
			return -EINVAL;

		iter->bi_idx--;
		iter->bi_bvec_done = buf->bvec[iter->bi_idx].bv_len;
	}
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
