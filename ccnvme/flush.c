#include <linux/bitops.h>
#include <linux/blk_types.h>
#include <linux/blkdev.h>
#include <linux/compiler.h>
#include <linux/completion.h>
#include <linux/fs.h>
#include <linux/types.h>

#include "horae.h"

extern struct olayer_info *olayer;
extern unsigned int nr_streams;

#ifdef USE_CMB
int stream_cmb_needs_flush(struct stream *stream, int device_id) {  // ordering constraints of pcie
    return (stream->nr_devices > 1) && (stream->cmb_device_id != device_id);
}
#endif

static int device_flush_supported(struct block_device *bdev) {
    struct request_queue *q = bdev_get_queue(bdev);
    return test_bit(QUEUE_FLAG_WC, &q->queue_flags);
}

static void bio_end_flush_async(struct bio *bio) {
    struct completion *completion = bio->bi_private;
    complete(completion);
    bio_put(bio);
}

int blkdev_issue_flush_async(struct block_device *bdev, gfp_t gfp_mask,
                             struct completion *completion) {
    struct request_queue *q;
    struct bio *bio;
    int ret = 0;

    if (bdev->bd_disk == NULL) return -ENXIO;

    q = bdev_get_queue(bdev);
    if (!q) return -ENXIO;

    /*
     * some block devices may not have their queue correctly set up here
     * (e.g. loop device without a backing file) and so issuing a flush
     * here will panic. Ensure there is a request function before issuing
     * the flush.
     */
    if (!q->make_request_fn) return -ENXIO;

    bio = bio_alloc(gfp_mask, 0);
    bio_set_dev(bio, bdev);
    bio->bi_private = completion;
    bio->bi_end_io = bio_end_flush_async;
    bio->bi_opf = REQ_OP_WRITE | REQ_PREFLUSH;
    submit_bio(bio);

    return ret;
}

int stream_blkdev_issue_flush_noop(uint sid) {
    uint tail_old;
    struct stream *str;

    if (sid >= nr_streams) {
        pr_warn("stream id out of range\n");
        sid = 0;
    }
    str = &olayer->stream[sid];
    if (horae_count(str->head, str->tail, str->nr_ometa) == 0) {
        return 0;
    }

    down_read(&str->cp_rwsem);
    tail_old = READ_ONCE(str->tail);

    WRITE_ONCE(str->head, tail_old);
    up_read(&str->cp_rwsem);

    return 0;
}
EXPORT_SYMBOL(stream_blkdev_issue_flush_noop);

extern struct cmb_info ci;

int stream_blkdev_issue_flush(struct block_device **bdevs, uint nr_devices, gfp_t gfp_mask,
                              sector_t *error_sector, uint sid) {
    uint tail_old;
    int ret;
    int i, j = 0;
    struct block_device *bdev;
    struct stream *str;
    struct completion *complete;

    if (sid >= nr_streams) {
        pr_warn("stream id out of range\n");
        sid = 0;
    }
    str = &olayer->stream[sid];

    olayer_debug(3, "phase 1.\n");

    WARN_ON(nr_devices == 0);
    if (nr_devices == 0 || horae_count(str->head, str->tail, str->nr_ometa) == 0) {
        olayer_debug(3, "phase 1-1.\n");
        return 0;
    }

    complete = kmalloc(sizeof(struct completion) * nr_devices, GFP_KERNEL);

    down_read(&str->cp_rwsem);
    tail_old = READ_ONCE(str->tail);

#if 0  // sync flushing
    for (i = 0; i < nr_devices; i++) {
        bdev = bdevs[i];
        blkdev_issue_flush(bdev, gfp_mask, NULL);
        // submit Eager Commit Write here
    }
#endif

#if 1  // async flushing
    if (nr_devices == 1) {
        blkdev_issue_flush(bdevs[0], gfp_mask, NULL);
    } else {
        for (i = 0; i < nr_devices; i++) {
            bdev = bdevs[i];
            if (device_flush_supported(bdev)) {
                init_completion(&complete[j]);
                blkdev_issue_flush_async(bdev, gfp_mask, &complete[j]);
                j++;
            } else {
                blkdev_issue_flush(bdev, gfp_mask, NULL);
            }
        }
        olayer_debug(3, "phase 2.\n");
        if (j > 0)
            for (i = 0; i < j; i++) {
                wait_for_completion_io(&complete[i]);
            }
        olayer_debug(3, "phase 3.\n");
    }
#endif

    WRITE_ONCE(str->flushed, tail_old);
    cmb_flush(&ci, str, 0, sizeof(str->flushed));
    up_read(&str->cp_rwsem);

    kfree(complete);
    return ret;
}
EXPORT_SYMBOL(stream_blkdev_issue_flush);