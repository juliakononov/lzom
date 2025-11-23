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
u32 lzom_sg_read4_at(struct lzom_sg_buf *buf, struct bvec_iter start,
		     size_t offset);
u64 lzom_sg_read8_at(struct lzom_sg_buf *buf, struct bvec_iter start,
		     size_t offset);

/* ========== simple inline wrappers ========== */

static inline unsigned char lzom_sg_read1(struct lzom_sg_buf *buf)
{
	unsigned char data;
	sg_read_bytes(buf, &data, sizeof(data));
	return data;
}

static inline void lzom_sg_write1(struct lzom_sg_buf *buf, unsigned char data)
{
	sg_write_bytes(buf, &data, sizeof(data));
}

static inline u32 lzom_sg_read4(struct lzom_sg_buf *buf)
{
	u32 data;
	sg_read_bytes(buf, (unsigned char *)&data, sizeof(data));
	return data;
}

static inline void lzom_sg_write4(struct lzom_sg_buf *buf, u32 data)
{
	sg_write_bytes(buf, (const unsigned char *)&data, sizeof(data));
}

static inline u64 lzom_sg_read8(struct lzom_sg_buf *buf)
{
	u64 data;
	sg_read_bytes(buf, (unsigned char *)&data, sizeof(data));
	return data;
}

static inline void lzom_sg_write8(struct lzom_sg_buf *buf, u64 data)
{
	sg_write_bytes(buf, (const unsigned char *)&data, sizeof(data));
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

#endif /* LZOM_SG_HELPERS_H */
