// SPDX-License-Identifier: GPL-2.0-only
/*
 *  LZO1X Compressor from LZO
 *
 *  Copyright (C) 1996-2012 Markus F.X.J. Oberhumer <markus@oberhumer.com>
 *
 *  The full LZO package can be found at:
 *  http://www.oberhumer.com/opensource/lzo/
 *
 *  Changed for Linux kernel use by:
 *  Nitin Gupta <nitingupta910@gmail.com>
 *  Richard Purdie <rpurdie@openedhand.com>
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/unaligned.h>
#include "lzodefs.h"
#include "lzo_extend.h"
#include "lzom_sg_helpers.h"

#undef LZO_UNSAFE

#define TODO_IMPLEMENT

#ifndef LZO_SAFE
#define LZO_UNSAFE 1
#define LZO_SAFE(name) name
#define HAVE_OP(x) 1
#endif

#define NEED_OP(x)             \
    if (unlikely(!HAVE_OP(x))) \
    goto output_overrun

static noinline int
LZO_SAFE(lzo1x_1_do_compress)(struct lzom_sg_buf *in,
                              size_t in_len,
                              struct lzom_sg_buf *out,
                              size_t *tp, void *wrkmem,
                              signed char *state_offset,
                              const unsigned char bitstream_version)
{
    struct bvec_iter block_start = in->iter;
    struct bvec_iter ii_iter = in->iter;
    size_t ip_offset = 0;
    size_t ii_offset = 0;
    const size_t ip_end = in_len - 20;
    lzo_dict_t *const dict = (lzo_dict_t *)wrkmem;
    size_t ti = *tp;

    if (ti < 4)
    {
        size_t skip = 4 - ti;
        sg_skip_bytes(in, skip);
        ip_offset += skip;
    }

    for (;;)
    {
        size_t m_offset = 0;
        size_t t, m_len, m_off;
        u32 dv;
        u32 run_length = 0;

    literal:
        size_t advance = 1 + ((ip_offset - ii_offset) >> 5);
        sg_skip_bytes(in, advance);
        ip_offset += advance;
    next:
        if (unlikely(ip_offset >= ip_end))
            break;

        lzom_sg_buf_pr_info(in, "in:76\n");
        bvec_iter_pr_info(block_start, "block_start:76 ip_offset=%lu\n", ip_offset);

        dv = le32_to_cpu(lzom_sg_read4_at(in, block_start, ip_offset));
#ifndef TODO_IMPLEMENT
        if (dv == 0 && bitstream_version)
        {
            const unsigned char *ir = ip + 4;
            const unsigned char *limit = min(ip_end, ip + MAX_ZERO_RUN_LENGTH + 1);
#if defined(CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS) && \
    defined(LZO_FAST_64BIT_MEMORY_ACCESS)
            u64 dv64;

            for (; (ir + 32) <= limit; ir += 32)
            {
                dv64 = get_unaligned((u64 *)ir);
                dv64 |= get_unaligned((u64 *)ir + 1);
                dv64 |= get_unaligned((u64 *)ir + 2);
                dv64 |= get_unaligned((u64 *)ir + 3);
                if (dv64)
                    break;
            }
            for (; (ir + 8) <= limit; ir += 8)
            {
                dv64 = get_unaligned((u64 *)ir);
                if (dv64)
                {
#if defined(__LITTLE_ENDIAN)
                    ir += __builtin_ctzll(dv64) >> 3;
#elif defined(__BIG_ENDIAN)
                    ir += __builtin_clzll(dv64) >> 3;
#else
#error "missing endian definition"
#endif
                    break;
                }
            }
#else
            while ((ir < (const unsigned char *)
                             ALIGN((uintptr_t)ir, 4)) &&
                   (ir < limit) && (*ir == 0))
                ir++;
            if (IS_ALIGNED((uintptr_t)ir, 4))
            {
                for (; (ir + 4) <= limit; ir += 4)
                {
                    dv = *((u32 *)ir);
                    if (dv)
                    {
#if defined(__LITTLE_ENDIAN)
                        ir += __builtin_ctz(dv) >> 3;
#elif defined(__BIG_ENDIAN)
                        ir += __builtin_clz(dv) >> 3;
#else
#error "missing endian definition"
#endif
                        break;
                    }
                }
            }
#endif
            while (likely(ir < limit) && unlikely(*ir == 0))
                ir++;
            run_length = ir - ip;
            if (run_length > MAX_ZERO_RUN_LENGTH)
                run_length = MAX_ZERO_RUN_LENGTH;
        }
        else
        // {
#endif
        t = ((dv * 0x1824429d) >> (32 - D_BITS)) & D_MASK;
        m_offset = dict[t];
        dict[t] = (lzo_dict_t)ip_offset;

        u32 dv_match = le32_to_cpu(lzom_sg_read4_at(in, block_start, m_offset));
        if (unlikely(dv != dv_match))
            goto literal;
        // }  TODO_IMPLEMENT

        ii_offset -= ti;
        ti = 0;
        t = ip_offset - ii_offset;

        if (t != 0)
        {
            struct bvec_iter saved_ip = in->iter;
            struct bvec_iter saved_ii = ii_iter;
            in->iter = ii_iter;

            if (t <= 3)
            {
                unsigned char prev_byte = lzom_sg_read_back(out, -(*state_offset));
                prev_byte |= t;
                lzom_sg_write_back(out, prev_byte, -(*state_offset));

                unsigned char tmp[4];
                if (lzom_sg_copy(out, in, tmp, t) < 0)
                    goto output_overrun;
            }
            else if (t <= 16)
            {
                lzom_sg_write1(out, t - 3);

                size_t remaining = t;
                while (remaining >= 8)
                {
                    LZOM_COPY8(out, in);
                    remaining -= 8;
                }
                if (remaining > 0)
                {
                    unsigned char tmp[8];
                    if (lzom_sg_copy(out, in, tmp, remaining) < 0)
                        goto output_overrun;
                }
            }
            else
            {
                if (t <= 18)
                {
                    lzom_sg_write1(out, t - 3);
                }
                else
                {
                    size_t tt = t - 18;
                    lzom_sg_write1(out, 0);

                    while (unlikely(tt > 255))
                    {
                        tt -= 255;
                        lzom_sg_write1(out, 0);
                    }

                    lzom_sg_write1(out, (unsigned char)tt);
                }
                do
                {
                    LZOM_COPY8(out, in);
                    LZOM_COPY8(out, in);
                    t -= 16;
                } while (t >= 16);
                if (t > 0)
                {
                    unsigned char tmp[16];
                    if (lzom_sg_copy(out, in, tmp, t) < 0)
                        goto output_overrun;
                }
                saved_ii = in->iter;
            }
            ii_iter = saved_ii;
            in->iter = saved_ip;
        }

#ifndef TODO_IMPLEMENT
        if (unlikely(run_length))
        {
            ip += run_length;
            run_length -= MIN_ZERO_RUN_LENGTH;
            NEED_OP(4);
            put_unaligned_le32((run_length << 21) | 0xfffc18 | (run_length & 0x7), op);
            op += 4;
            run_length = 0;
            *state_offset = -3;
            goto finished_writing_instruction;
        }
#endif

        m_len = 4;
        {
#if defined(CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS) && defined(LZO_USE_CTZ64)
            u64 v = le64_to_cpu(lzom_sg_read8_at(in, block_start, ip_offset + m_len)) ^
                    le64_to_cpu(lzom_sg_read8_at(in, block_start, m_offset + m_len));

            if (unlikely(v == 0))
            {
                do
                {
                    m_len += 8;

                    v = le64_to_cpu(lzom_sg_read8_at(in, block_start, ip_offset + m_len)) ^
                        le64_to_cpu(lzom_sg_read8_at(in, block_start, m_offset + m_len));

                    if (unlikely(ip_offset + m_len >= ip_end))
                        goto m_len_done;
                } while (v == 0);
            }
#if defined(__LITTLE_ENDIAN)
            m_len += (unsigned)__builtin_ctzll(v) / 8;
#elif defined(__BIG_ENDIAN)
            m_len += (unsigned)__builtin_clzll(v) / 8;
#else
#error "missing endian definition"
#endif
#elif defined(CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS) && defined(LZO_USE_CTZ32)
            u32 v = le32_to_cpu(lzom_sg_read4_at(in, block_start, ip_offset + m_len)) ^
                    le32_to_cpu(lzom_sg_read4_at(in, block_start, m_offset + m_len));

            if (unlikely(v == 0))
            {
                do
                {
                    m_len += 4;

                    v = le32_to_cpu(lzom_sg_read4_at(in, block_start, ip_offset + m_len)) ^
                        le32_to_cpu(lzom_sg_read4_at(in, block_start, m_offset + m_len));

                    if (v != 0)
                        break;

                    m_len += 4;

                    v = le32_to_cpu(lzom_sg_read4_at(in, block_start, ip_offset + m_len)) ^
                        le32_to_cpu(lzom_sg_read4_at(in, block_start, m_offset + m_len));

                    if (unlikely(ip_offset + m_len >= ip_end))
                        goto m_len_done;
                } while (v == 0);
            }
#if defined(__LITTLE_ENDIAN)
            m_len += (unsigned)__builtin_ctz(v) / 8;
#elif defined(__BIG_ENDIAN)
            m_len += (unsigned)__builtin_clz(v) / 8;
#else
#error "missing endian definition"
#endif
#else
            if (unlikely(lzom_sg_read1_at(in, block_start, ip_offset + m_len) == 
                 lzom_sg_read1_at(in, block_start, m_offset + m_len)))
            {
                do
                {
                    m_len += 1;
                    if (lzom_sg_read1_at(in, block_start, ip_offset + m_len) !=
                        lzom_sg_read1_at(in, block_start, m_offset + m_len))
                        break;

                    m_len += 1;
                    if (lzom_sg_read1_at(in, block_start, ip_offset + m_len) !=
                        lzom_sg_read1_at(in, block_start, m_offset + m_len))
                        break;

                    m_len += 1;
                    if (lzom_sg_read1_at(in, block_start, ip_offset + m_len) !=
                        lzom_sg_read1_at(in, block_start, m_offset + m_len))
                        break;

                    m_len += 1;
                    if (lzom_sg_read1_at(in, block_start, ip_offset + m_len) !=
                        lzom_sg_read1_at(in, block_start, m_offset + m_len))
                        break;

                    m_len += 1;
                    if (lzom_sg_read1_at(in, block_start, ip_offset + m_len) !=
                        lzom_sg_read1_at(in, block_start, m_offset + m_len))
                        break;

                    m_len += 1;
                    if (lzom_sg_read1_at(in, block_start, ip_offset + m_len) !=
                        lzom_sg_read1_at(in, block_start, m_offset + m_len))
                        break;

                    m_len += 1;
                    if (lzom_sg_read1_at(in, block_start, ip_offset + m_len) !=
                        lzom_sg_read1_at(in, block_start, m_offset + m_len))
                        break;

                    m_len += 1;
                    if (unlikely(ip_offset + m_len >= ip_end))
                        goto m_len_done;
                } while (lzom_sg_read1_at(in, block_start, ip_offset + m_len) ==
                         lzom_sg_read1_at(in, block_start, m_offset + m_len));
            }
#endif
        }
    m_len_done:
        m_off = ip_offset - m_offset;
        ip_offset += m_len;

        in->iter = block_start;
        sg_skip_bytes(in, ip_offset);

        if (m_len <= M2_MAX_LEN && m_off <= M2_MAX_OFFSET)
        {
            m_off -= 1;
            lzom_sg_write1(out, (unsigned char)((m_len - 1) << 5) | ((m_off & 7) << 2));
            lzom_sg_write1(out, (unsigned char)(m_off >> 3));
        }
        else if (m_off <= M3_MAX_OFFSET)
        {
            m_off -= 1;
            if (m_len <= M3_MAX_LEN)
                lzom_sg_write1(out, (unsigned char)(M3_MARKER | (m_len - 2)));
            else
            {
                m_len -= M3_MAX_LEN;
                lzom_sg_write1(out, (unsigned char)(M3_MARKER | 0));

                while (unlikely(m_len > 255))
                {
                    m_len -= 255;
                    lzom_sg_write1(out, 0);
                }
                lzom_sg_write1(out, (unsigned char)m_len);
            }
            lzom_sg_write1(out, (unsigned char)(m_off << 2));
            lzom_sg_write1(out, (unsigned char)(m_off >> 6));
        }
        else
        {
            m_off -= 0x4000;
            if (m_len <= M4_MAX_LEN)
                lzom_sg_write1(out, (unsigned char)(M4_MARKER | ((m_off >> 11) & 8) | (m_len - 2)));
            else
            {
                if (unlikely(((m_off & 0x403f) == 0x403f) && (m_len >= 261) && (m_len <= 264)) && likely(bitstream_version))
                {
                    // Under lzo-rle, block copies
                    // for 261 <= length <= 264 and
                    // (distance & 0x80f3) == 0x80f3
                    // can result in ambiguous
                    // output. Adjust length
                    // to 260 to prevent ambiguity.
                    ip_offset -= m_len - 260;
                    m_len = 260;

                    in->iter = block_start;
                    sg_skip_bytes(in, ip_offset);
                }
                m_len -= M4_MAX_LEN;
                lzom_sg_write1(out, (unsigned char)(M4_MARKER | ((m_off >> 11) & 8)));

                while (unlikely(m_len > 255))
                {
                    m_len -= 255;
                    lzom_sg_write1(out, 0);
                }
                lzom_sg_write1(out, (unsigned char)m_len);
            }
            lzom_sg_write1(out, (unsigned char)(m_off << 2));
            lzom_sg_write1(out, (unsigned char)(m_off >> 6));
        }
        *state_offset = -2;
    finished_writing_instruction:
        ii_offset = ip_offset;
        ii_iter = in->iter;
        goto next;
    }
    *tp = in_len - (ii_offset - ti);

    return LZO_E_OK;

output_overrun:
    return LZO_E_OUTPUT_OVERRUN;
}

static int LZO_SAFE(lzogeneric1x_1_compress)(
    struct lzom_sg_buf *in,
    struct lzom_sg_buf *out,
    void *wrkmem, const unsigned char bitstream_version)
{
    size_t in_len = in->iter.bi_size;

    struct bvec_iter in_iter = in->iter;
    struct bvec_iter out_iter = out->iter;

    if (in_len == 0)
        return LZO_E_OK;

    size_t l = in_len;
    size_t t = 0;
    signed char state_offset = -2;
    unsigned int m4_max_offset;

    // LZO v0 will never write 17 as first byte (except for zero-length
    // input), so this is used to version the bitstream
    if (bitstream_version > 0)
    {
        unsigned char header[2] = {17, bitstream_version};
        if (sg_write_bytes(out, header, 2) < 0)
            return LZO_E_OUTPUT_OVERRUN;
        m4_max_offset = M4_MAX_OFFSET_V1;
    }
    else
    {
        m4_max_offset = M4_MAX_OFFSET_V0;
    } 

    while (l > 20)
    {
        size_t ll = min_t(size_t, l, m4_max_offset + 1);
        uintptr_t ll_end = (uintptr_t)(in_len - l) + ll;
        int err;

        if ((ll_end + ((t + ll) >> 5)) <= ll_end)
            break;

        BUILD_BUG_ON(D_SIZE * sizeof(lzo_dict_t) > LZO1X_1_MEM_COMPRESS);
        memset(wrkmem, 0, D_SIZE * sizeof(lzo_dict_t));

        err = LZO_SAFE(lzo1x_1_do_compress)(in, ll, out, &t, wrkmem,
                                            &state_offset, bitstream_version);

        if (err != LZO_E_OK)
            return err;

        l -= ll;
    }
    t += l;

    if (t > 0)
    {
        if (out_iter.bi_size == out->iter.bi_size && t <= 238)
        {
            lzom_sg_write1(out, 17 + t);
        }
        else if (t <= 3)
        {
            unsigned char prev_byte = lzom_sg_read_back(out, -state_offset);
            prev_byte |= t;
            lzom_sg_write_back(out, prev_byte, -state_offset);
        }
        else if (t <= 18)
        {
            lzom_sg_write1(out, t - 3);
        }
        else
        {
            size_t tt = t - 18;
            lzom_sg_write1(out, 0);

            while (tt > 255)
            {
                tt -= 255;
                lzom_sg_write1(out, 0);
            }

            lzom_sg_write1(out, (unsigned char)tt);
        }

        if (t >= 16)
            do
            {
                LZOM_COPY8(out, in);
                LZOM_COPY8(out, in);
                t -= 16;
            } while (t >= 16);
        if (t > 0)
        {
            unsigned char tmp[16];
            if (lzom_sg_copy(out, in, tmp, t) < 0)
                return LZO_E_OUTPUT_OVERRUN;
        }
    }

    unsigned char eof[3] = {M4_MARKER | 1, 0, 0};
    if (sg_write_bytes(out, eof, 3) < 0) {
        goto output_overrun;
    }

    size_t out_len = out_iter.bi_size - out->iter.bi_size;
    in->iter = in_iter;
    out->iter = out_iter;
    out->iter.bi_size = out_len;
    
    return LZO_E_OK;

output_overrun:
    return LZO_E_OUTPUT_OVERRUN;
}

int lzom_compress(struct lzom_sg_buf *src,
                  struct lzom_sg_buf *dst,
                  void *wrkmem)
{
    return LZO_SAFE(lzogeneric1x_1_compress)(
        src, dst, wrkmem, 0);
}

// int lzom_rle_compressstruct(struct lzom_sg_buf *src,
//                             struct lzom_sg_buf *dst,
//                             void *wrkmem)
// {
//     return LZO_SAFE(lzogeneric1x_1_compress)(
//         src, dst, wrkmem, LZO_VERSION);
// }

#ifndef LZO_UNSAFE
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("LZO1X-1 Compressor");
#endif
