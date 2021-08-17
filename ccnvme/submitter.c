#include "horae.h"

extern struct olayer_info *olayer;

unsigned int nr_submitters = OLAYER_NR_SUBMITTERS;
module_param(nr_submitters, uint, 0444);
MODULE_PARM_DESC(nr_submitters, "Number of submitters. 0 means per-core streams. Default 0.");

int olayer_submit_bio(struct bio *bio) { return submit_bio(bio); }

struct hd_struct *__disk_get_part(struct gendisk *disk, int partno) {
    struct disk_part_tbl *ptbl = rcu_dereference(disk->part_tbl);

    if (unlikely(partno < 0 || partno >= ptbl->len)) return NULL;
    return rcu_dereference(ptbl->part[partno]);
}

void guard_bio_eod(int op, struct bio *bio) {
    sector_t maxsector;
    struct bio_vec *bvec = bio_last_bvec_all(bio);
    unsigned truncated_bytes;
    struct hd_struct *part;

    rcu_read_lock();
    part = __disk_get_part(bio->bi_disk, bio->bi_partno);
    if (part)
        maxsector = part_nr_sects_read(part);
    else
        maxsector = get_capacity(bio->bi_disk);
    rcu_read_unlock();

    if (!maxsector) return;

    /*
     * If the *whole* IO is past the end of the device,
     * let it through, and the IO layer will turn it into
     * an EIO.
     */
    if (unlikely(bio->bi_iter.bi_sector >= maxsector)) return;

    maxsector -= bio->bi_iter.bi_sector;
    if (likely((bio->bi_iter.bi_size >> 9) <= maxsector)) return;

    /* Uhhuh. We've got a bio that straddles the device size! */
    truncated_bytes = bio->bi_iter.bi_size - (maxsector << 9);

    /* Truncate the bio.. */
    bio->bi_iter.bi_size -= truncated_bytes;
    bvec->bv_len -= truncated_bytes;

    /* ..and clear the end of the buffer for reads */
    if (op == REQ_OP_READ) {
        zero_user(bvec->bv_page, bvec->bv_offset + bvec->bv_len, truncated_bytes);
    }
}

static void end_bio_bh_io_sync(struct bio *bio) {
    struct buffer_head *bh = bio->bi_private;

    if (unlikely(bio_flagged(bio, BIO_QUIET))) set_bit(BH_Quiet, &bh->b_state);

    bh->b_end_io(bh, !bio->bi_status);
    bio_put(bio);
}

struct bio *bh2bio(int op, int op_flags, struct buffer_head *bh) {
    struct bio *bio;
    BUG_ON(!buffer_locked(bh));
    BUG_ON(!buffer_mapped(bh));
    BUG_ON(!bh->b_end_io);
    BUG_ON(buffer_delay(bh));
    BUG_ON(buffer_unwritten(bh));
    /*
     * Only clear out a write error when rewriting
     */
    if (test_set_buffer_req(bh) && (op == REQ_OP_WRITE)) clear_buffer_write_io_error(bh);

    /*
     * from here on down, it's all bio -- do the initial mapping,
     * submit_bio -> generic_make_request may further map this bio around
     */
    bio = bio_alloc(GFP_NOIO, 1);

    bio->bi_iter.bi_sector = bh->b_blocknr * (bh->b_size >> 9);
    bio_set_dev(bio, bh->b_bdev);
    bio->bi_write_hint = 0;

    bio_add_page(bio, bh->b_page, bh->b_size, bh_offset(bh));
    BUG_ON(bio->bi_iter.bi_size != bh->b_size);

    bio->bi_end_io = end_bio_bh_io_sync;
    bio->bi_private = bh;

    /* Take care of bh's that straddle the end of the device */
    guard_bio_eod(op, bio);

    if (buffer_meta(bh)) op_flags |= REQ_META;
    if (buffer_prio(bh)) op_flags |= REQ_PRIO;
    bio_set_op_attrs(bio, op, op_flags);

    return bio;
}

int bio_delegater(void *data) {
    struct submitter *sub = (struct submitter *)(data);
    struct submitter_ring *ring = &sub->ring;
    struct bio *sqe;
    unsigned long timeout = 0;
    u64 head;

    while (!kthread_should_stop() && !sub->should_stop) {
        head = ring->head;
        do {
            rmb();
            if (head == ring->tail) break;
            sqe = ring->bio_sqes[head];
            olayer_submit_bio(sqe);
            head = (head + 1) % ring->len;

            timeout = jiffies + msecs_to_jiffies(DELEGATOR_IDEL_TIMEOUT);
        } while (1);
        ring->head = head;
        wmb();

        if (!time_after(jiffies, timeout)) {
            cond_resched();
            continue;
        }

        ring->flags |= HORAE_SUBMITTER_NEED_WAKEUP;
        /* make sure to read SQ tail after writing flags */
        smp_mb();
        set_current_state(TASK_INTERRUPTIBLE);
        schedule();
        ring->flags &= ~HORAE_SUBMITTER_NEED_WAKEUP;
    }

    while (!kthread_should_stop()) {
        set_current_state(TASK_INTERRUPTIBLE);
        schedule();
    }
    return 0;
}

int delegate_bio(struct stream *stream, struct bio *src_bio, uint submitter_id) {
    struct submitter *sub;
    struct submitter_ring *ring;
    u64 tail_old, tail_new, nr_entries = 1;

#ifdef OLAYER_ANALYSIS
    u64 s = ktime_get_ns(), e;
#endif

    if (submitter_id >= nr_submitters) {
        // pr_warn("submitter id out of range\n");
        submitter_id %= nr_submitters;
    }

    sub = &olayer->submitter[submitter_id];
    ring = &sub->ring;

    do {
        tail_old = READ_ONCE(ring->tail_watermark);
        if (horae_ring_space(ring->head, tail_old, ring->len) < nr_entries) {
            // do by myself
            stream->done_by_self_bio++;
            olayer_submit_bio(src_bio);
            return 0;
        }
        tail_new = tail_old + nr_entries;
        tail_new %= ring->len;
    } while (cmpxchg(&ring->tail_watermark, tail_old, tail_new) != tail_old);

    ring->bio_sqes[tail_old] = src_bio;

    while (cmpxchg(&ring->tail, tail_old, tail_new) != tail_old);

    if (ring->flags & HORAE_SUBMITTER_NEED_WAKEUP) {
        wake_up_process(sub->submitter_thread);
    }

    stream->delegated_bio++;

#ifdef OLAYER_ANALYSIS
    e = ktime_get_ns();
    olayer->t_delegate_bio += e - s;
#endif
    return 0;
}

#define kthread_run_on_node(threadfn, data, node_id, namefmt, ...)                   \
    ({                                                                               \
        struct task_struct *__k =                                                    \
            kthread_create_on_node(threadfn, data, node_id, namefmt, ##__VA_ARGS__); \
        if (!IS_ERR(__k)) wake_up_process(__k);                                      \
        __k;                                                                         \
    })

int init_submitter() {
    int i;
    struct submitter *sub;
    if (nr_submitters <= 0) {
        olayer->percore_submitter = 1;
        nr_submitters = num_possible_nodes();
    }

    olayer->submitter = kmalloc(nr_submitters * sizeof(struct submitter), GFP_KERNEL);

    for (i = 0; i < nr_submitters; i++) {
        sub = &olayer->submitter[i];
        sub->ring.head = sub->ring.tail = sub->ring.tail_watermark = 0;
        sub->ring.len = MAX_BIO_SQES;
        sub->should_stop = 0;
        sub->ring.bio_sqes = kmalloc(sizeof(struct bio *) * MAX_BIO_SQES, GFP_KERNEL);
        sub->id = i;
        sub->submitter_thread =
            kthread_run_on_node(bio_delegater, sub, numa_node_id(), "horae-sub%u", i);
    }

    olayer_debug(0, "setup %d submitters\n", nr_submitters);
    return 0;
}

int stop_submitter() {
    int i;
    struct submitter *sub;
    for (i = 0; i < nr_submitters; i++) {
        sub = &olayer->submitter[i];
        sub->should_stop = 1;
        kthread_stop(sub->submitter_thread);
        kfree(sub->ring.bio_sqes);
    }

    kfree(olayer->submitter);

    return 0;
}
