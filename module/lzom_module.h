#ifndef LZOM_MODULE
#define LZOM_MODULE

struct underlying_dev {
	struct block_device *bdev;
	struct file *bdev_fl;
	struct bio_set *bset;
};

struct lzom_dev {
	struct gendisk *disk;
	struct underlying_dev under_dev;
};

struct lzom_module_g {
	int major;
	int free_minor;
	char *dev_path;
	struct lzom_dev dev;
};

struct lzom_req {
	struct bio *original_bio;
	struct bio *new_bio;
	struct lzom_buffer *buffer;
};

struct lzom_buffer {
	u32 data_sz;
	u32 buf_sz;
	char *data;
};

int lzom_copy_from_bio_to_buf(struct bio *bio, struct lzom_buffer *buf);

#endif // LZOM_MODULE
