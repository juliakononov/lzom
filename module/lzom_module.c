// SPDX-License-Identifier: GPL-2.0-only

#include <linux/blk_types.h>
#include <linux/blkdev.h>
#include <linux/bvec.h>
#include <linux/highmem.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/vmalloc.h>

#include "lzom_module.h"

#include "lzom_extend.h"
#include "lzom_stats.h"

int lzorle1x_1_compress(const unsigned char *src, size_t src_len,
			unsigned char *dst, size_t *dst_len, void *wrkmem);
int lzo1x_decompress_safe(const unsigned char *src, size_t src_len,
			  unsigned char *dst, size_t *dst_len);

#define LZOM_NAME "lzom_module"
#define LZOM_INIT_MINOR 0
#define POOL_SIZE 512

#define LZOM_LOG(fmt, ...) \
	pr_info("%s[inf] " fmt "\n", LZOM_NAME, ##__VA_ARGS__)

#define LZOM_ERRLOG(fmt, ...) \
	pr_err("%s[err] " fmt "\n", LZOM_NAME, ##__VA_ARGS__)

static struct lzom_module_g lzom = { .free_minor = LZOM_INIT_MINOR };

static bool lzom_is_exist(void)
{
	return lzom.dev_path;
}

/* ----------------- submit bio -----------------*/
static unsigned short lzom_bio_size_to_pages(size_t size)
{
	size_t pages_needed = DIV_ROUND_UP(size, PAGE_SIZE);

	return (unsigned short)min_t(size_t, pages_needed, BIO_MAX_VECS);
}

static void lzom_buffer_free(struct lzom_buffer *buf)
{
	if (buf) {
		kfree(buf->data);
		kfree(buf);
	}
}

static struct lzom_buffer *lzom_buffer_alloc(size_t size)
{
	struct lzom_buffer *buf;

	buf = kzalloc(sizeof(*buf), GFP_NOIO);
	if (!buf) {
		LZOM_ERRLOG("failed to allocate buffer structure");
		return NULL;
	}

	buf->data = kzalloc(size, GFP_NOIO);
	if (!buf->data) {
		LZOM_ERRLOG("failed to allocate buffer data");
		kfree(buf);
		return NULL;
	}

	buf->buf_sz = size;
	return buf;
}

int lzom_copy_from_bio_to_buf(struct bio *bio, struct lzom_buffer *buf)
{
	struct bio_vec bv;
	struct bvec_iter iter;
	char *ptr = buf->data;

	if (!buf || !buf->data)
		return -EINVAL;

	if (buf->buf_sz < bio->bi_iter.bi_size)
		return -EINVAL;

	bio_for_each_segment (bv, bio, iter) {
		memcpy_from_bvec(ptr, &bv);
		ptr += bv.bv_len;
	}

	buf->data_sz = bio->bi_iter.bi_size;

	return 0;
}

static int lzom_add_buf_to_bio(struct lzom_buffer *buf, u32 part_to_use,
			       struct bio *bio)
{
	char *data = buf->data;
	unsigned int len = part_to_use;
	unsigned int pageoff, pagelen;
	int ret;

	if (buf->buf_sz < part_to_use) {
		return -EINVAL;
	}

	pageoff = offset_in_page(data);

	while (len > 0) {
		pagelen = min_t(unsigned int, len, PAGE_SIZE - pageoff);

		ret = bio_add_page(bio, virt_to_page(data), pagelen, pageoff);
		if (ret != pagelen) {
			LZOM_ERRLOG("bio_add_page failed");
			return -EAGAIN;
		}

		len -= pagelen;
		data += pagelen;
		pageoff = 0;
	}

	return 0;
}

static void lzom_req_free(struct lzom_req *lreq)
{
	if (!lreq)
		return;

	lzom_buffer_free(lreq->buffer);
	kfree(lreq);
}

static struct lzom_req *lzom_req_alloc(void)
{
	struct lzom_req *lreq;

	lreq = kzalloc(sizeof(*lreq), GFP_NOIO);
	if (!lreq) {
		LZOM_ERRLOG("failed to allocate request");
		return NULL;
	}

	return lreq;
}

static void lzom_write_req_endio(struct bio *bio)
{
	struct lzom_req *lreq = bio->bi_private;
	struct bio *original_bio = lreq->original_bio;

	bio_endio(original_bio);

	bio_put(bio);
	lzom_req_free(lreq);
}

static void lzom_read_req_endio(struct bio *bio)
{
	struct lzom_req *lreq = bio->bi_private;
	struct bio *original_bio = lreq->original_bio;

	bio_endio(original_bio);

	bio_put(bio);
	lzom_req_free(lreq);
}

static void lzom_init_bvec_array(struct bio_vec *bvec, size_t vec_cnt,
				 char *data_ptr, size_t data_len)
{
	size_t i;
	size_t remaining = data_len;
	char *cur_ptr = data_ptr;

	for (i = 0; i < vec_cnt && remaining > 0; i++) {
		size_t page_offset = offset_in_page(cur_ptr);
		size_t chunk_len =
			min_t(size_t, remaining, PAGE_SIZE - page_offset);

		bvec[i].bv_page = virt_to_page(cur_ptr);
		bvec[i].bv_offset = page_offset;
		bvec[i].bv_len = chunk_len;

		cur_ptr += chunk_len;
		remaining -= chunk_len;
	}
}

struct lzom_rt_stats {
	u64 copy_ns, comp_ns, decomp_ns, comp_bytes, mem;
};

/*
 * Each of these compresses `bio`'s data with one backend, then immediately
 * decompresses the result into the already-allocated flat buffer `out`
 * (bsize bytes). Returns 0 and fills `st` on success. The caller writes
 * `out` (the verified, decompressed -- i.e. original -- bytes) through to
 * the underlying device; nothing compressed ever reaches storage here, this
 * is a benchmarking proxy, not a real compressed block device.
 */

/* lzom_compress()/lzom_decompress_safe() straight across bio's own pages. */
static int lzom_rt_lzom(struct bio *bio, void *wrk, unsigned char *out,
			size_t bsize, struct lzom_rt_stats *st)
{
	size_t cap = lzo_worst_compress(bsize);
	size_t n = DIV_ROUND_UP(cap, PAGE_SIZE), done = 0, i;
	size_t out_vec_cnt = DIV_ROUND_UP(bsize, PAGE_SIZE) + 1;
	struct lzom_sg_buf src, dst, decomp_sg;
	struct bio_vec *dst_bv, *out_bv = NULL;
	u64 t0;
	int ret, lzo_ret;

	dst_bv = kvcalloc(n, sizeof(*dst_bv), GFP_NOIO);
	if (!dst_bv)
		return -ENOMEM;

	for (; done < n; done++) {
		struct page *page = alloc_page(GFP_NOIO);

		if (!page) {
			ret = -ENOMEM;
			goto out;
		}
		dst_bv[done] = (struct bio_vec){ .bv_page = page,
						 .bv_len = PAGE_SIZE };
	}

	src = lzom_sg_buf_create(bio->bi_iter, bio->bi_io_vec);
	dst = lzom_sg_buf_create((struct bvec_iter){ .bi_size = cap }, dst_bv);

	t0 = ktime_get_ns();
	lzo_ret = lzom_compress(&src, &dst, wrk);
	st->comp_ns = ktime_get_ns() - t0;
	if (lzo_ret != LZOM_E_OK) {
		ret = -EIO;
		goto out;
	}
	st->comp_bytes = dst.iter.bi_size;
	st->mem = cap + LZO1X_1_MEM_COMPRESS;

	out_bv = kvcalloc(out_vec_cnt, sizeof(*out_bv), GFP_NOIO);
	if (!out_bv) {
		ret = -ENOMEM;
		goto out;
	}
	lzom_init_bvec_array(out_bv, out_vec_cnt, out, bsize);
	decomp_sg = lzom_sg_buf_create((struct bvec_iter){ .bi_size = bsize },
				       out_bv);

	t0 = ktime_get_ns();
	lzo_ret = lzom_decompress_safe(&dst, &decomp_sg);
	st->decomp_ns = ktime_get_ns() - t0;
	ret = lzo_ret == LZOM_E_OK ? 0 : -EIO;
out:
	kvfree(out_bv);
	for (i = 0; i < done; i++)
		__free_page(dst_bv[i].bv_page);
	kvfree(dst_bv);
	return ret;
}

/* Copy the whole bio into one flat buffer, then the kernel's compressor. */
static int lzom_rt_cont(struct bio *bio, void *wrk, unsigned char *out,
			size_t bsize, struct lzom_rt_stats *st)
{
	size_t cap = lzo_worst_compress(bsize), out_len = 0, decomp_len = bsize;
	unsigned char *lin, *dst, *p;
	struct bio_vec bv;
	struct bvec_iter iter;
	u64 t0;
	int ret = -ENOMEM;

	lin = kvmalloc(bsize, GFP_NOIO);
	dst = kvmalloc(cap, GFP_NOIO);
	if (!lin || !dst)
		goto out;

	t0 = ktime_get_ns();
	p = lin;
	bio_for_each_segment (bv, bio, iter) {
		memcpy_from_bvec(p, &bv);
		p += bv.bv_len;
	}
	st->copy_ns = ktime_get_ns() - t0;

	t0 = ktime_get_ns();
	ret = lzorle1x_1_compress(lin, bsize, dst, &out_len, wrk);
	st->comp_ns = ktime_get_ns() - t0;
	if (ret) {
		ret = -EIO;
		goto out;
	}
	st->comp_bytes = out_len;
	st->mem = bsize + cap + LZO1X_1_MEM_COMPRESS;

	t0 = ktime_get_ns();
	ret = lzo1x_decompress_safe(dst, out_len, out, &decomp_len);
	st->decomp_ns = ktime_get_ns() - t0;
	ret = ret ? -EIO : 0;
out:
	kvfree(lin);
	kvfree(dst);
	return ret;
}

/* Kernel compressor on every segment on its own: no copy, no shared history. */
static int lzom_rt_vect(struct bio *bio, void *wrk, unsigned char *out,
			size_t bsize, struct lzom_rt_stats *st)
{
	unsigned int nsegs = bio_segments(bio), i = 0;
	unsigned char **dst;
	size_t *dst_len;
	struct bio_vec bv;
	struct bvec_iter iter;
	u64 t0;
	int ret = 0;

	dst = kcalloc(nsegs, sizeof(*dst), GFP_NOIO);
	dst_len = kcalloc(nsegs, sizeof(*dst_len), GFP_NOIO);
	if (!dst || !dst_len) {
		ret = -ENOMEM;
		goto out;
	}

	bio_for_each_segment (bv, bio, iter) {
		size_t cap = lzo_worst_compress(bv.bv_len);

		dst[i] = kmalloc(cap, GFP_NOIO);
		if (!dst[i]) {
			ret = -ENOMEM;
			goto out;
		}
		st->mem += cap;
		i++;
	}
	st->mem += LZO1X_1_MEM_COMPRESS;

	i = 0;
	t0 = ktime_get_ns();
	bio_for_each_segment (bv, bio, iter) {
		unsigned char *p = bvec_kmap_local(&bv);
		size_t out_len = 0;

		ret = lzorle1x_1_compress(p, bv.bv_len, dst[i], &out_len, wrk);
		kunmap_local(p);
		dst_len[i] = out_len;
		st->comp_bytes += out_len;
		i++;
		if (ret)
			break;
	}
	st->comp_ns = ktime_get_ns() - t0;
	if (ret) {
		ret = -EIO;
		goto out;
	}

	i = 0;
	t0 = ktime_get_ns();
	bio_for_each_segment (bv, bio, iter) {
		size_t seg_len = bv.bv_len;

		ret = lzo1x_decompress_safe(dst[i], dst_len[i], out, &seg_len);
		out += seg_len;
		i++;
		if (ret)
			break;
	}
	st->decomp_ns = ktime_get_ns() - t0;
	ret = ret ? -EIO : 0;
out:
	if (dst)
		for (i = 0; i < nsegs; i++)
			kfree(dst[i]);
	kfree(dst);
	kfree(dst_len);
	return ret;
}

static blk_status_t lzom_write_req_submit(struct lzom_req *lreq,
					  struct bio *original_bio,
					  struct lzom_dev *ldev)
{
	struct block_device *bdev = ldev->under_dev.bdev;
	struct bio *new_bio = NULL;
	struct lzom_buffer *decomp;
	struct lzom_rt_stats st = {};
	size_t bsize = original_bio->bi_iter.bi_size;
	u64 t0 = ktime_get_ns();
	void *wrk;
	int ret;

	wrk = kzalloc(LZO1X_1_MEM_COMPRESS, GFP_NOIO);
	if (!wrk)
		return BLK_STS_RESOURCE;

	decomp = lzom_buffer_alloc(bsize);
	if (!decomp) {
		kfree(wrk);
		return BLK_STS_RESOURCE;
	}

	switch (lzom_comp_type) {
	case LZOM_COMP_CONT:
		ret = lzom_rt_cont(original_bio, wrk, decomp->data, bsize, &st);
		break;
	case LZOM_COMP_VECT:
		ret = lzom_rt_vect(original_bio, wrk, decomp->data, bsize, &st);
		break;
	default:
		ret = lzom_rt_lzom(original_bio, wrk, decomp->data, bsize, &st);
	}
	kfree(wrk);

	if (ret) {
		LZOM_ERRLOG("round-trip failed: %d", ret);
		lzom_buffer_free(decomp);
		return ret == -ENOMEM ? BLK_STS_RESOURCE : BLK_STS_IOERR;
	}
	decomp->data_sz = bsize;

	new_bio = bio_alloc(bdev, lzom_bio_size_to_pages(bsize),
			    original_bio->bi_opf, GFP_NOIO);
	if (!new_bio) {
		LZOM_ERRLOG("failed to alloc new bio");
		lzom_buffer_free(decomp);
		return BLK_STS_RESOURCE;
	}

	lreq->buffer = decomp;
	ret = lzom_add_buf_to_bio(lreq->buffer, bsize, new_bio);
	if (ret != 0) {
		LZOM_ERRLOG("failed to add buffer to bio");
		bio_put(new_bio);
		lzom_buffer_free(decomp);
		lreq->buffer = NULL;
		return BLK_STS_IOERR;
	}

	new_bio->bi_end_io = lzom_write_req_endio;
	new_bio->bi_private = lreq;
	new_bio->bi_iter.bi_sector = original_bio->bi_iter.bi_sector;

	lreq->original_bio = original_bio;
	lreq->new_bio = new_bio;

	LZOM_STAT_ADD(w_reqs, 1);
	LZOM_STAT_ADD(w_bytes, bsize);
	LZOM_STAT_ADD(w_comp_bytes, st.comp_bytes);
	LZOM_STAT_ADD(w_vecs, bio_segments(original_bio));
	LZOM_STAT_ADD(w_copy_ns, st.copy_ns);
	LZOM_STAT_ADD(w_comp_ns, st.comp_ns);
	LZOM_STAT_ADD(w_decomp_ns, st.decomp_ns);
	LZOM_STAT_ADD(w_mem, st.mem);

	submit_bio_noacct(new_bio);
	LZOM_STAT_ADD(w_total_ns, ktime_get_ns() - t0);
	return BLK_STS_OK;
}

static blk_status_t lzom_passthrough_submit(struct lzom_req *lreq,
					    struct bio *original_bio,
					    struct lzom_dev *ldev)
{
	struct block_device *bdev = ldev->under_dev.bdev;
	struct bio_set *bset = ldev->under_dev.bset;
	struct bio *new_bio;

	new_bio = bio_alloc_clone(bdev, original_bio, GFP_NOIO, bset);
	if (!new_bio) {
		LZOM_ERRLOG("failed to clone original bio");
		return BLK_STS_RESOURCE;
	}

	new_bio->bi_end_io = lzom_read_req_endio;
	new_bio->bi_private = lreq;
	new_bio->bi_iter.bi_sector = original_bio->bi_iter.bi_sector;

	lreq->original_bio = original_bio;
	lreq->new_bio = new_bio;

	submit_bio_noacct(new_bio);

	return BLK_STS_OK;
}

static void lzom_submit_bio(struct bio *original_bio)
{
	struct lzom_dev *ldev = original_bio->bi_bdev->bd_disk->private_data;
	struct lzom_req *lreq;
	enum req_op op_type = bio_op(original_bio);
	u64 t0;

	if (!ldev || !ldev->under_dev.bdev) {
		LZOM_ERRLOG("invalid device context");
		goto submit_bio_with_err;
	}

	lreq = lzom_req_alloc();
	if (!lreq) {
		goto submit_bio_with_err;
	}

	switch (op_type) {
	case REQ_OP_WRITE:
		if (lzom_write_req_submit(lreq, original_bio, ldev) ==
		    BLK_STS_OK)
			return;
		goto submit_bio_with_err_free_req;

	case REQ_OP_READ:
		t0 = ktime_get_ns();
		if (lzom_passthrough_submit(lreq, original_bio, ldev) ==
		    BLK_STS_OK) {
			LZOM_STAT_ADD(r_reqs, 1);
			LZOM_STAT_ADD(r_bytes, original_bio->bi_iter.bi_size);
			LZOM_STAT_ADD(r_total_ns, ktime_get_ns() - t0);
			return;
		}
		goto submit_bio_with_err_free_req;

	default:
		LZOM_ERRLOG("unsupported request operation");
		goto submit_bio_with_err;
	}

submit_bio_with_err:
	bio_io_error(original_bio);
submit_bio_with_err_free_req:
	lzom_req_free(lreq);
	bio_io_error(original_bio);
}

static const struct block_device_operations lzom_fops = {
	.owner = THIS_MODULE,
	.submit_bio = lzom_submit_bio,
};

static int lzom_new_minor_get(void)
{
	return lzom.free_minor++;
}

static void lzom_dev_unregister(struct lzom_dev *ldev)
{
	del_gendisk(ldev->disk);
	put_disk(ldev->disk);
	ldev->disk = NULL;
	LZOM_LOG("disk unregistered successfully");
}

static int lzom_dev_register(int major, int minor, struct lzom_dev *ldev)
{
	struct gendisk *disk = ldev->disk;

	if (!disk) {
		LZOM_ERRLOG("attempt to register NULL disk");
		return -EINVAL;
	}

	disk->major = major;
	disk->first_minor = minor;
	disk->minors = 1;
	disk->fops = &lzom_fops;
	disk->private_data = ldev;

	disk->flags |= GENHD_FL_NO_PART;

	set_capacity(disk, get_capacity(ldev->under_dev.bdev->bd_disk));

	snprintf(disk->disk_name, DISK_NAME_LEN, "lzom%d", disk->first_minor);

	return add_disk(ldev->disk);
}

static void lzom_dev_deinit(struct lzom_dev *ldev)
{
	if (ldev->disk)
		put_disk(ldev->disk);

	if (ldev->under_dev.bdev_fl)
		bdev_fput(ldev->under_dev.bdev_fl);

	if (ldev->under_dev.bset) {
		bioset_exit(ldev->under_dev.bset);
		kfree(ldev->under_dev.bset);
	}

	LZOM_LOG("device deinitialized");
}

static int lzom_dev_init(const char *path, struct lzom_dev *ldev)
{
	struct file *fbdev;
	struct block_device *bdev;
	struct bio_set *bset;

	memset(ldev, 0, sizeof(*ldev));

	fbdev = bdev_file_open_by_path(path, BLK_OPEN_READ | BLK_OPEN_WRITE,
				       &ldev->under_dev, NULL);
	if (IS_ERR(fbdev)) {
		LZOM_ERRLOG("failed to open device path '%s'", path);
		return PTR_ERR(fbdev);
	}

	bdev = file_bdev(fbdev);

	ldev->under_dev.bdev = bdev;
	ldev->under_dev.bdev_fl = fbdev;

	bset = kzalloc(sizeof(*bset), GFP_KERNEL);
	if (!bset) {
		LZOM_ERRLOG("failed to allocate memory for bioset");
		goto err;
	}

	bioset_init(bset, POOL_SIZE, 0, BIOSET_NEED_BVECS);
	ldev->under_dev.bset = bset;
	LZOM_LOG("bioset initialized");

	ldev->disk = blk_alloc_disk(NULL, NUMA_NO_NODE);
	if (!ldev->disk) {
		LZOM_ERRLOG("failed to allocate disk");
		goto err;
	}

	LZOM_LOG("device initialized");
	return 0;

err:
	lzom_dev_deinit(ldev);
	return -ENOMEM;
}

static void lzom_path_remove(void)
{
	if (!lzom_is_exist()) {
		LZOM_ERRLOG("no device path to remove");
		return;
	}

	kfree(lzom.dev_path);
	lzom.dev_path = NULL;
	LZOM_LOG("device path removed");
}

static int lzom_path_set(const char *arg, struct lzom_module_g *lzom)
{
	size_t len = strlen(arg) + 1;

	BUG_ON(lzom->dev_path);

	lzom->dev_path = kzalloc(sizeof(char) * len, GFP_KERNEL);
	if (!lzom->dev_path) {
		LZOM_ERRLOG(
			"failed to allocate memory for device path: %lu bytes",
			len);
		return -ENOMEM;
	}

	strcpy(lzom->dev_path, arg);
	LZOM_LOG("device path set: %s", lzom->dev_path);
	return 0;
}

static void lzom_blk_destroy(void)
{
	if (!lzom_is_exist()) {
		LZOM_ERRLOG("no device for unmapping");
		return;
	}

	lzom_dev_unregister(&lzom.dev);
	lzom_dev_deinit(&lzom.dev);

	LZOM_LOG("device unmapped");
}

static int lzom_blk_create(const char *arg, const struct kernel_param *kp)
{
	int ret = 0;

	if (lzom_is_exist()) {
		LZOM_ERRLOG("device already exists");
		return -EACCES;
	}

	ret = lzom_path_set(arg, &lzom);
	if (ret)
		goto err;

	ret = lzom_dev_init(lzom.dev_path, &lzom.dev);
	if (ret)
		goto err;

	LZOM_LOG("device initialized");

	ret = lzom_dev_register(lzom.major, lzom_new_minor_get(), &lzom.dev);
	if (ret)
		goto err;

	LZOM_LOG("device mapped: %s", arg);
	return 0;

err:
	LZOM_ERRLOG("device mapping failed: error %d", ret);
	lzom_dev_deinit(&lzom.dev);
	lzom_path_remove();

	return ret;
}

static int lzom_blk_get(char *buf, const struct kernel_param *kp)
{
	size_t len;

	if (!lzom.dev_path)
		return -EINVAL;

	len = strlen(lzom.dev_path);
	strcpy(buf, lzom.dev_path);

	return len;
}

static const struct kernel_param_ops lzom_blk = {
	.set = lzom_blk_create,
	.get = lzom_blk_get,
};

MODULE_PARM_DESC(path, "Path to the module");
module_param_cb(path, &lzom_blk, NULL, S_IRUGO | S_IWUSR);

static int __init lzom_init(void)
{
	lzom.major = register_blkdev(0, LZOM_NAME);
	if (lzom.major < 0) {
		LZOM_ERRLOG("module NOT loaded");
		return -EIO;
	}

	LZOM_LOG("module loaded");
	return 0;
}

static void __exit lzom_exit(void)
{
	unregister_blkdev(lzom.major, LZOM_NAME);
	lzom_blk_destroy();

	LZOM_LOG("module unloaded");
}

module_init(lzom_init);
module_exit(lzom_exit);

MODULE_AUTHOR("Kononova Julia");
MODULE_LICENSE("GPL");
