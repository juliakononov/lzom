#ifndef LZOM_STATS_H
#define LZOM_STATS_H

#include <linux/atomic.h>
#include <linux/ktime.h>
#include <linux/moduleparam.h>
#include <linux/string.h>

#define LZOM_STAT_LIST(S) \
	S(w_reqs)       /* how many write reqs submitted */ \
	S(w_bytes)      /* size of uncompressed data */     \
	S(w_comp_bytes) /* size of compressed data */       \
	S(w_vecs)       /* number of segments */            \
	S(w_copy_ns)    /* time for data copying */         \
	S(w_comp_ns)    /* time for compression */          \
	S(w_decomp_ns)  /* time for decompression */        \
	S(w_total_ns)   /* total elapsed time */            \
	S(w_mem)        /* memory usage in bytes */         \
	S(r_reqs)       /* how many read reqs submitted */  \
	S(r_bytes)      /* size of data read */             \
	S(r_total_ns)   /* total elapsed time */

#define LZOM_STAT_DEF(n) static atomic64_t lzom_stat_##n = ATOMIC64_INIT(0);
LZOM_STAT_LIST(LZOM_STAT_DEF)
#undef LZOM_STAT_DEF

#define LZOM_STAT_ADD(n, v) atomic64_add((v), &lzom_stat_##n)

static int lzom_stat_get(char *buf, const struct kernel_param *kp)
{
	return sysfs_emit(buf, "%lld\n", atomic64_read(kp->arg));
}

static int lzom_stat_set(const char *val, const struct kernel_param *kp)
{
	return -EPERM;
}

static const struct kernel_param_ops lzom_stat_ops = {
	.get = lzom_stat_get,
	.set = lzom_stat_set,
};

#define LZOM_STAT_PARAM(n) \
	module_param_cb(stats_##n, &lzom_stat_ops, &lzom_stat_##n, 0444);
LZOM_STAT_LIST(LZOM_STAT_PARAM)
#undef LZOM_STAT_PARAM

static int lzom_stats_reset_set(const char *val, const struct kernel_param *kp)
{
#define LZOM_STAT_RESET(n) atomic64_set(&lzom_stat_##n, 0);
	LZOM_STAT_LIST(LZOM_STAT_RESET)
#undef LZOM_STAT_RESET
	return 0;
}

static const struct kernel_param_ops lzom_stats_reset_ops = {
	.set = lzom_stats_reset_set,
};
module_param_cb(stats_reset, &lzom_stats_reset_ops, NULL, 0200);

enum lzom_comp_type { LZOM_COMP_LZOM, LZOM_COMP_CONT, LZOM_COMP_VECT };
static const char *const lzom_comp_names[] = { "lzom", "cont", "vect" };
static int lzom_comp_type;

static int lzom_comp_type_set(const char *val, const struct kernel_param *kp)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(lzom_comp_names); i++) {
		if (sysfs_streq(val, lzom_comp_names[i])) {
			lzom_comp_type = i;
			return 0;
		}
	}
	return -EINVAL;
}

static int lzom_comp_type_get(char *buf, const struct kernel_param *kp)
{
	return sysfs_emit(buf, "%s\n", lzom_comp_names[lzom_comp_type]);
}

static const struct kernel_param_ops lzom_comp_type_ops = {
	.set = lzom_comp_type_set,
	.get = lzom_comp_type_get,
};
MODULE_PARM_DESC(comp_type, "Compressor used on write: lzom|cont|vect");
module_param_cb(comp_type, &lzom_comp_type_ops, NULL, 0644);

#endif /* LZOM_STATS_H */
