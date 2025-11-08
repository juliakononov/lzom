#ifndef LZOM_SG_HELPERS_H
#define LZOM_SG_HELPERS_H

#include <linux/bvec.h>
#include "lzo_extend.h"

static inline int sg_write_bytes(struct lzom_sg_buf *buf,
                                 const unsigned char *data,
                                 size_t len)
{
    if (len > buf->iter.bi_size)
        return -EINVAL;

    while (len)
    {
        struct bio_vec bv;
        size_t to_write;

        bv = bvec_iter_bvec(buf->bvec, buf->iter);
        to_write = min_t(size_t, bv.bv_len, len);

        memcpy_to_bvec(&bv, (const char *)data);

        bvec_iter_advance(buf->bvec, &buf->iter, to_write);
        data += to_write;
        len -= to_write;
    }

    return 0;
}

static inline int sg_read_bytes(struct lzom_sg_buf *buf,
                                unsigned char *data,
                                size_t len)
{
    if (len > buf->iter.bi_size)
        return -EINVAL;

    while (len)
    {
        struct bio_vec bv;
        size_t to_read;

        bv = bvec_iter_bvec(buf->bvec, buf->iter);
        to_read = min_t(size_t, bv.bv_len, len);

        memcpy_from_bvec((char *)data, &bv);

        bvec_iter_advance(buf->bvec, &buf->iter, to_read);
        data += to_read;
        len -= to_read;
    }

    return 0;
}

static inline int sg_skip_bytes(struct lzom_sg_buf *buf, size_t len)
{
    if (len > buf->iter.bi_size)
        return -EINVAL;

    while (len)
    {
        struct bio_vec bv = bvec_iter_bvec(buf->bvec, buf->iter);
        size_t to_skip = min_t(size_t, bv.bv_len, len);

        bvec_iter_advance(buf->bvec, &buf->iter, to_skip);
        len -= to_skip;
    }

    return 0;
}

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

static inline void lzom_sg_copy4(struct lzom_sg_buf *dst, struct lzom_sg_buf *src)
{
    lzom_sg_write4(dst, lzom_sg_read4(src));
}

static inline void lzom_sg_copy8(struct lzom_sg_buf *dst, struct lzom_sg_buf *src)
{
    lzom_sg_write8(dst, lzom_sg_read8(src));
}

static inline void lzom_sg_copy(struct lzom_sg_buf *dst, struct lzom_sg_buf *src, unsigned char *tmp, size_t len)
{
    if (sg_read_bytes(src, tmp, len) < 0)
        return LZO_E_INPUT_OVERRUN;

    if (sg_write_bytes(dst, tmp, len) < 0)
        return LZO_E_OUTPUT_OVERRUN;
}
#endif /* LZOM_SG_HELPERS_H */