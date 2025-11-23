#ifndef _LZO_EXTEND_H
#define _LZO_EXTEND_H

#include <linux/bvec.h>
#include <linux/types.h>

#define LZO1X_1_MEM_COMPRESS (8192 * sizeof(unsigned short))
#define LZO1X_MEM_COMPRESS LZO1X_1_MEM_COMPRESS

#define lzo_worst_compress(x) ((x) + ((x) / 16) + 64 + 3 + 2)

#define LZO_E_OK 0
#define LZO_E_ERROR (-1)
#define LZO_E_OUT_OF_MEMORY (-2)
#define LZO_E_NOT_COMPRESSIBLE (-3)
#define LZO_E_INPUT_OVERRUN (-4)
#define LZO_E_OUTPUT_OVERRUN (-5)
#define LZO_E_LOOKBEHIND_OVERRUN (-6)
#define LZO_E_EOF_NOT_FOUND (-7)
#define LZO_E_INPUT_NOT_CONSUMED (-8)
#define LZO_E_NOT_YET_IMPLEMENTED (-9)
#define LZO_E_INVALID_ARGUMENT (-10)

#define LZOM_E_OK LZO_E_OK
#define LZOM_E_ERROR LZO_E_ERROR

struct lzom_sg_buf {
	struct bio_vec *bvec;
	struct bvec_iter iter;
};

#define lzom_sg_buf_pr_info(sg_buf, fmt, ...)                                  \
	pr_info("bvec=%p i.bi_size=%u i.bi_idx=%u i.done=%u " fmt,             \
		(sg_buf)->bvec, (sg_buf)->iter.bi_size, (sg_buf)->iter.bi_idx, \
		(sg_buf)->iter.bi_bvec_done, ##__VA_ARGS__)

#define bvec_iter_pr_info(iter, fmt, ...)                                  \
	pr_info("i.bi_size=%u i.bi_idx=%u i.done=%u " fmt, (iter).bi_size, \
		(iter).bi_idx, (iter).bi_bvec_done, ##__VA_ARGS__)

static inline struct lzom_sg_buf lzom_sg_buf_create(struct bvec_iter iter,
						    struct bio_vec *bvec)
{
	return (struct lzom_sg_buf){ .iter = iter, .bvec = bvec };
}

int lzom_compress(struct lzom_sg_buf *src, struct lzom_sg_buf *dst,
		  void *wrkmem);

int lzom_decompress_safe(const unsigned char *in, size_t in_len,
			 unsigned char *out, size_t *out_len);

#endif /* _LZO_EXTEND_H */
