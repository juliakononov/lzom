// SPDX-License-Identifier: GPL-2.0-only

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/blkdev.h>
#include <linux/bvec.h>
#include <linux/blk_types.h>
#include <linux/vmalloc.h>
#include <linux/highmem.h>
#include <linux/lzo.h>

#include "lzom.h"

#define LZOM_NAME "lzo_module"
#define LZOM_INIT_MINOR 0
#define POOL_SIZE 512

#define LZOM_LOG(fmt, ...) \
    pr_info("%s[inf] " fmt "\n", LZOM_NAME, ##__VA_ARGS__)

#define LZOM_ERRLOG(fmt, ...) \
    pr_err("%s[err] " fmt "\n", LZOM_NAME, ##__VA_ARGS__)

static struct lzom_module_g lzom = {.free_minor = LZOM_INIT_MINOR};

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
    if (buf)
    {
        kfree(buf->data);
        kfree(buf);
    }
}

static struct lzom_buffer *lzom_buffer_alloc(size_t size)
{
    struct lzom_buffer *buf;

    buf = kzalloc(sizeof(*buf), GFP_NOIO);
    if (!buf)
    {
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

static int lzom_copy_from_bio_to_buf(struct bio *bio, struct lzom_buffer *buf)
{
    struct bio_vec bv;
    struct bvec_iter iter;
    char *ptr = buf->data;

    if (!buf || !buf->data)
        return -EINVAL;

    if (buf->buf_sz < bio->bi_iter.bi_size)
        return -EINVAL;

    bio_for_each_segment(bv, bio, iter)
    {
        memcpy_from_bvec(ptr, &bv);
        ptr += bv.bv_len;
    }

    buf->data_sz = bio->bi_iter.bi_size;

    return 0;
}

static int lzom_add_buf_to_bio(struct lzom_buffer *buf, u32 part_to_use, struct bio *bio) 
{
    char *data = buf->data;
    unsigned int len = part_to_use;
    unsigned int pageoff, pagelen;
    int ret;

    if (buf->buf_sz < part_to_use) {
        //TODO: LZOM_ERRLOG("");
        return -EINVAL;
    }

    pageoff = offset_in_page(data);

    while (len > 0) {
        pagelen = min_t(unsigned int, len, PAGE_SIZE - pageoff);

        ret = bio_add_page(bio, virt_to_page(data), pagelen, pageoff);
        if (ret != pagelen)
        {
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

static blk_status_t lzom_write_req_submit(struct lzom_req *lreq, struct bio *original_bio, struct lzom_dev *ldev)
{
    struct block_device *bdev = ldev->under_dev.bdev;
    struct bio *new_bio;
    struct lzom_buffer *dst;
    struct lzom_buffer *src;
    struct lzom_buffer *decomp;
    int bsize = original_bio->bi_iter.bi_size;
    int ret, lzo_ret;
    size_t dst_len, decomp_len;
    void *wrkmem;

    src = lzom_buffer_alloc(bsize);
    if (!src)
    {
        LZOM_ERRLOG("failed to alloc src buffer");
        ret = BLK_STS_RESOURCE;
        goto err_out;
    }

    ret = lzom_copy_from_bio_to_buf(original_bio, src);
    if (ret)
    {
        LZOM_ERRLOG("copy from bio failed: %d", ret);
        ret = BLK_STS_IOERR;
        goto err_out;
    }

    dst = lzom_buffer_alloc(lzo1x_worst_compress(bsize));
    if (!dst)
    {
        LZOM_ERRLOG("failed to alloc dst buffer");
        return BLK_STS_RESOURCE;
    }

    wrkmem = kzalloc(LZO1X_1_MEM_COMPRESS, GFP_NOIO);
    if (!wrkmem)
    {
        LZOM_ERRLOG("failed to alloc wrkmem");
        ret = BLK_STS_RESOURCE;
        goto err_out;
    }

    dst_len = dst->buf_sz;
    lzo_ret = lzo1x_1_compress(
        (const unsigned char *)src->data,
        src->data_sz,
        (unsigned char *)dst->data,
        &dst_len,
        wrkmem);

    if (lzo_ret != LZO_E_OK)
    {
        LZOM_ERRLOG("lzo compress failed: %d", lzo_ret);
        ret = BLK_STS_IOERR;
        goto err_out;
    }
    dst->data_sz = dst_len;

    LZOM_LOG("Compressed %u bytes to %u bytes",
             src->data_sz, dst->data_sz);

    decomp = lzom_buffer_alloc(bsize);
    if (!decomp)
    {
        LZOM_ERRLOG("failed to alloc decomp buffer");
        ret = BLK_STS_RESOURCE;
        goto err_out;
    }

    decomp_len = decomp->buf_sz;
    lzo_ret = lzo1x_decompress_safe(
        (const unsigned char *)dst->data,
        dst->data_sz,
        (unsigned char *)decomp->data,
        &decomp_len);

    if (lzo_ret != LZO_E_OK)
    {
        LZOM_ERRLOG("lzo decompress failed: %d", lzo_ret);
        ret = BLK_STS_IOERR;
        goto err_out;
    }

    decomp->data_sz = decomp_len;

    if (decomp_len != src->data_sz)
    {
        LZOM_ERRLOG("decompressed size mismatch: expected %u, got %zu",
                    src->data_sz, decomp_len);
        ret = BLK_STS_IOERR;
        goto err_out;
    }

    if (memcmp(src->data, decomp->data, src->data_sz) != 0)
    {
        LZOM_ERRLOG("decompressed data mismatch");
        ret = BLK_STS_IOERR;
        goto err_out;
    }

    LZOM_LOG("Decompression verified successfully");

    lreq->buffer = decomp;

    lzom_buffer_free(src);
    src = NULL;
    lzom_buffer_free(dst);
    dst = NULL;
    kfree(wrkmem);
    wrkmem = NULL;

    new_bio = bio_alloc(bdev, lzom_bio_size_to_pages(bsize), original_bio->bi_opf, GFP_NOIO);
    if (!new_bio)
    {
        LZOM_ERRLOG("failed to alloc new bio");
        ret = BLK_STS_RESOURCE;
        goto err_out;
    }

    ret = lzom_add_buf_to_bio(lreq->buffer, bsize, new_bio);
    if (ret != 0)
    {
        LZOM_ERRLOG("failed to add buffer to bio");
        ret = BLK_STS_IOERR;
        goto err_out;
    }

    new_bio->bi_end_io = lzom_write_req_endio;
    new_bio->bi_private = lreq;
    new_bio->bi_iter.bi_sector = original_bio->bi_iter.bi_sector;

    lreq->original_bio = original_bio;
    lreq->new_bio = new_bio;

    submit_bio_noacct(new_bio);
    return BLK_STS_OK;

err_out:
    if (src)
        lzom_buffer_free(src);
    if (dst)
        lzom_buffer_free(dst);
    if (decomp)
        lzom_buffer_free(decomp);
    if (lreq->buffer)
    {
        lzom_buffer_free(lreq->buffer);
        lreq->buffer = NULL;
    }
    if (new_bio)
        bio_put(new_bio);
    if (wrkmem)
        kfree(wrkmem);
    return ret;
}

static blk_status_t lzom_read_req_submit(struct lzom_req *lreq, struct bio *original_bio, struct lzom_dev *ldev)
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
            if (lzom_write_req_submit(lreq, original_bio, ldev) == BLK_STS_OK)
                return;
            goto submit_bio_with_err_free_req;

        case REQ_OP_READ:
            if (lzom_read_req_submit(lreq, original_bio, ldev) == BLK_STS_OK)
                return;
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

    fbdev = bdev_file_open_by_path(
        path, BLK_OPEN_READ | BLK_OPEN_WRITE, &ldev->under_dev, NULL);
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
        LZOM_ERRLOG("failed to allocate memory for device path: %lu bytes", len);
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