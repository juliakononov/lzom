#ifndef _LZO_EXTEND_H
#define _LZO_EXTEND_H

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

int lzom_compress(const unsigned char *in, size_t in_len,
                  unsigned char *out, size_t *out_len,
                  void *wrkmem);

int lzom_decompress_safe(const unsigned char *in, size_t in_len,
                         unsigned char *out, size_t *out_len);

#endif /* _LZO_EXTEND_H */