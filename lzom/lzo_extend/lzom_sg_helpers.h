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

static inline void sg_skip_bytes(struct lzom_sg_buf *buf, size_t len)
{
    BUG_ON(len > buf->iter.bi_size);

    bool res = bvec_iter_advance(buf->bvec, &buf->iter, len);
    BUG_ON(!res);
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

static inline int lzom_sg_copy(struct lzom_sg_buf *dst, struct lzom_sg_buf *src, unsigned char *tmp, size_t len)
{
    if (sg_read_bytes(src, tmp, len) < 0)
        return LZO_E_INPUT_OVERRUN;

    if (sg_write_bytes(dst, tmp, len) < 0)
        return LZO_E_OUTPUT_OVERRUN;
    
    return 0;
}

static inline unsigned char lzom_sg_read1_at(struct lzom_sg_buf *buf,
                                             struct bvec_iter start,
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

static inline u32 lzom_sg_read4_at(struct lzom_sg_buf *buf,
                                   struct bvec_iter start,
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

static inline u32 lzom_sg_read8_at(struct lzom_sg_buf *buf,
                                   struct bvec_iter start,
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

static inline int lzom_sg_move_back(struct lzom_sg_buf *buf,
                                    struct bvec_iter *iter,
                                    size_t offset)
{
    while (offset > 0)
    {
        if (iter->bi_bvec_done >= offset)
        {
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

static inline unsigned char lzom_sg_read_back(struct lzom_sg_buf *buf, size_t offset)
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

static inline int lzom_sg_write_back(struct lzom_sg_buf *buf, unsigned char value, size_t offset)
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
#endif /* LZOM_SG_HELPERS_H */