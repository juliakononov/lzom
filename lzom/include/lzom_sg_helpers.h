#ifndef LZOM_SG_HELPERS_H
#define LZOM_SG_HELPERS_H

#include <linux/bvec.h>

#include "lzom_extend.h"

int sg_write_bytes(struct lzom_sg_buf *buf, const unsigned char *data,
		   size_t len);
int sg_read_bytes(struct lzom_sg_buf *buf, unsigned char *data, size_t len);
void sg_skip_bytes(struct lzom_sg_buf *buf, size_t len);

int lzom_sg_move_back(struct lzom_sg_buf *buf, struct bvec_iter *iter,
		      size_t offset);
unsigned char lzom_sg_read_back(struct lzom_sg_buf *buf, size_t offset);
int lzom_sg_write_back(struct lzom_sg_buf *buf, unsigned char value,
		       size_t offset);

/* ========== read with offset ========== */

unsigned char lzom_sg_read1_at(struct lzom_sg_buf *buf, struct bvec_iter start,
			       size_t offset);
u16 lzom_sg_read2_at(struct lzom_sg_buf *buf, struct bvec_iter start,
		     size_t offset);
u32 lzom_sg_read4_at(struct lzom_sg_buf *buf, struct bvec_iter start,
		     size_t offset);
u64 lzom_sg_read8_at(struct lzom_sg_buf *buf, struct bvec_iter start,
		     size_t offset);

/* ========== simple inline wrappers ========== */

static inline void lzom_sg_read_fast(struct lzom_sg_buf *buf, void *data,
				     size_t n)
{
	struct bio_vec bv = bvec_iter_bvec(buf->bvec, buf->iter);

	if (likely(bv.bv_len >= n)) {
		memcpy_from_page((char *)data, bv.bv_page, bv.bv_offset, n);
		bvec_iter_advance(buf->bvec, &buf->iter, n);
		return;
	}

	sg_read_bytes(buf, data, n);
}

static inline void lzom_sg_write_fast(struct lzom_sg_buf *buf, const void *data,
				      size_t n)
{
	struct bio_vec bv = bvec_iter_bvec(buf->bvec, buf->iter);

	if (likely(bv.bv_len >= n)) {
		memcpy_to_page(bv.bv_page, bv.bv_offset, (const char *)data, n);
		bvec_iter_advance(buf->bvec, &buf->iter, n);
		return;
	}

	sg_write_bytes(buf, data, n);
}

static inline unsigned char lzom_sg_read1(struct lzom_sg_buf *buf)
{
	unsigned char data;

	lzom_sg_read_fast(buf, &data, sizeof(data));
	return data;
}

static inline void lzom_sg_write1(struct lzom_sg_buf *buf, unsigned char data)
{
	lzom_sg_write_fast(buf, &data, sizeof(data));
}

static inline u16 lzom_sg_read2(struct lzom_sg_buf *buf)
{
	u16 data;

	lzom_sg_read_fast(buf, &data, sizeof(data));
	return data;
}

static inline void lzom_sg_write2(struct lzom_sg_buf *buf, u16 data)
{
	lzom_sg_write_fast(buf, &data, sizeof(data));
}

static inline u32 lzom_sg_read4(struct lzom_sg_buf *buf)
{
	u32 data;

	lzom_sg_read_fast(buf, &data, sizeof(data));
	return data;
}

static inline void lzom_sg_write4(struct lzom_sg_buf *buf, u32 data)
{
	lzom_sg_write_fast(buf, &data, sizeof(data));
}

static inline u64 lzom_sg_read8(struct lzom_sg_buf *buf)
{
	u64 data;

	lzom_sg_read_fast(buf, &data, sizeof(data));
	return data;
}

static inline void lzom_sg_write8(struct lzom_sg_buf *buf, u64 data)
{
	lzom_sg_write_fast(buf, &data, sizeof(data));
}

static inline void lzom_sg_copy1(struct lzom_sg_buf *dst,
				 struct lzom_sg_buf *src)
{
	lzom_sg_write1(dst, lzom_sg_read1(src));
}

static inline void lzom_sg_copy4(struct lzom_sg_buf *dst,
				 struct lzom_sg_buf *src)
{
	lzom_sg_write4(dst, lzom_sg_read4(src));
}

static inline void lzom_sg_copy8(struct lzom_sg_buf *dst,
				 struct lzom_sg_buf *src)
{
	lzom_sg_write8(dst, lzom_sg_read8(src));
}

static inline int lzom_sg_copy(struct lzom_sg_buf *dst, struct lzom_sg_buf *src,
			       unsigned char *tmp, size_t len)
{
	if (sg_read_bytes(src, tmp, len) < 0)
		return LZO_E_INPUT_OVERRUN;

	if (sg_write_bytes(dst, tmp, len) < 0)
		return LZO_E_OUTPUT_OVERRUN;

	return 0;
}

/* ======== decompression support (LZO1X match/zero-run handling) ======== */

int sg_write_zeros(struct lzom_sg_buf *buf, size_t len);

int lzom_sg_count_zero_run(struct lzom_sg_buf *in, size_t max_count,
			   size_t *count);

int lzom_sg_match_copy(struct lzom_sg_buf *out, size_t distance, size_t t);

void lzom_sg_finish(struct lzom_sg_buf *in, struct bvec_iter in_start,
		    struct lzom_sg_buf *out, struct bvec_iter out_start,
		    size_t out_cap);

#endif /* LZOM_SG_HELPERS_H */
