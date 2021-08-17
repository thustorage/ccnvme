#include "horae.h"

extern unsigned int nr_streams;
extern struct olayer_info *olayer;
extern struct cmb_info ci;
extern struct kmem_cache *horae_ipu_entry_cache;
extern struct block_device *cp_dev;

void debug_stream(uint sid) {
    int cpu;
    u64 i;
    struct stream *s;
    struct nvme_rw_command_ *start;
    struct nvme_rw_command_ *o;
    if (sid >= nr_streams) {
        pr_err("exceed max stream %u\n", nr_streams);
        return;
    }
    s = &olayer->stream[sid];
    cmb_read(&ci, s, 0, s->ometa_size);
    cpu = smp_processor_id() % olayer->cpus;
    start = olayer->read_buffer[cpu];

    pr_info("-----------------------------------------------------------------");
    for (i = s->head; i != s->tail; i = (i + 1) % s->nr_ometa) {
        o = &start[i];
        if (o->tag & (1 << HORAE_TX_BEGIN) || o->tag & (1 << HORAE_TX_END)) {  // control block
            pr_info("%llu: %u, %llu", i, o->tag, o->txid);
        } else {
            pr_info("%llu: %u, %llu|%u|%u\n", i, o->tag, o->lba, o->length, o->plba);
        }
    }
    pr_info("-----------------------------------------------------------------");
}
EXPORT_SYMBOL(debug_stream);

int commit_write_entry(struct nvme_rw_command_ *o, struct super_block *sb, u64 txid) {
    u32 group_idx, i;
    uint tree_idx, tree_off;
    ulong start_lba = o->lba;
    uint nr_blocks = o->length;
    struct ipu_entry *ipu_entry;
    struct buffer_head *bh;

    for (i = 0; i < nr_blocks; i++, start_lba++) {
        group_idx = GROUP_IDX(start_lba, olayer->blocks_per_group);
        tree_idx = TREE_IDX(group_idx);
        tree_off = TREE_OFF(group_idx, start_lba, olayer->blocks_per_group);

        down_write(&olayer->ipu_tree_locks[tree_idx]);
        ipu_entry = radix_tree_delete(&olayer->ipu_tree[tree_idx], tree_off);
        up_write(&olayer->ipu_tree_locks[tree_idx]);
        if (ipu_entry) {
            olayer_debug(3, "lba %u, %llu vs %llu\n", ipu_entry->lba, ipu_entry->txid, txid);
            WARN_ON(ipu_entry->txid < txid && o->plba == 0);

            bh = sb_getblk(sb, start_lba);
            if (buffer_locked(bh)) {
                olayer_debug(3, "wait bh %lu, %lu\n", start_lba, bh->b_blocknr);
                wait_on_buffer(bh);
            } else {
                olayer_debug(3, "commit bh %lu, %lu\n", start_lba, bh->b_blocknr);
                lock_buffer(bh);
                get_bh(bh);
                bh->b_end_io = end_buffer_write_sync;
                submit_bh(REQ_OP_WRITE, REQ_SYNC, bh);
                wait_on_buffer(bh);
            }
            put_page(bh->b_page);  // unpin this page
            kmem_cache_free(horae_ipu_entry_cache, ipu_entry);
        }
    }
    return 0;
}

void print_ometa(struct nvme_rw_command_ *o) {
    if (o->tag & (1 << HORAE_TX_BEGIN) || o->tag & (1 << HORAE_TX_END)) {
        olayer_debug(3, "%u, %llu", o->tag, o->txid);
    } else {
        olayer_debug(3, "%u, %u|%u|%u|%u|%u\n", o->tag, o->lba, o->length, o->device_id, o->dr,
                     o->plba);
    }
}

void graceful_shutdown(u32 *sids, u32 nr, struct super_block *sb) {
    uint i, j, k, m, n;
    struct stream *s;
    struct nvme_rw_command_ *o;
    u32 sid;
    u32 begin_idx;
    u64 begin_txid;
    int phase = 0;  // 0: find begin; 1: find end;

    for (i = 0; i < nr; i++) {
        sid = sids[i];
        if (sid >= nr_streams) {
            pr_err("sid %u exceed %u\n", sid, nr_streams);
            continue;
        }
        s = &olayer->stream[sid];
        olayer_debug(3,
                     "stream %u, flushed %u, head %u, length "
                     "%u-------------------------------------------------",
                     sid, s->flushed, s->head, horae_count(s->head, s->flushed, s->nr_ometa));
        for (m = s->head; m != s->flushed; m = (m + 1) % s->nr_ometa) {
            j = m;
            o = stream_ometa_at(s, j);
            olayer_debug(3, "ometa at %u\n", j);
            print_ometa(o);
            if (phase == 0) {
                if ((o->tag & (1 << HORAE_TX_BEGIN))) {
                    begin_idx = j;
                    begin_txid = o->txid;
                    phase = 1;
                    continue;
                }
            }
            if (phase == 1) {
                if (o->tag & (1 << HORAE_TX_END)) {
                    WARN_ON(begin_txid != o->txid);
                    olayer_debug(3, "recovery %u, begin %u, count %u\n", sid, begin_idx,
                                 horae_count((begin_idx + 1) % s->nr_ometa, j, s->nr_ometa));
                    for (k = (begin_idx + 1) % s->nr_ometa; k != j; k = (k + 1) % s->nr_ometa) {
                        n = k;
                        if (stream_ometa_at(s, n)->plba == 0) continue;
                        commit_write_entry(stream_ometa_at(s, n), sb, o->txid);
                    }
                    phase = 0;
                }
            }
        }
    }
}
EXPORT_SYMBOL(graceful_shutdown);

int crash_recovery_commit_write(struct nvme_rw_command_ *o, struct block_device *bd) {
    struct bio *read_bio, *write_bio;
    uint i;

    // read bio
    read_bio = bio_alloc(GFP_NOIO | __GFP_NOFAIL, min_t(int, o->length, BIO_MAX_PAGES));
    bio_set_op_attrs(read_bio, REQ_OP_READ, REQ_SYNC);
    bio_set_dev(read_bio, cp_dev);
    read_bio->bi_iter.bi_sector = o->plba * 8;

    for (i = 0; i < o->length; i++) {
        struct page *page = alloc_page(GFP_KERNEL);
        if (bio_add_page(read_bio, page, PAGE_SIZE, 0) < PAGE_SIZE) {
            break;
        }
    }
    write_bio = bio_clone_fast(read_bio, GFP_KERNEL, NULL);
    submit_bio_wait(read_bio);

    // wirte bio
    bio_set_op_attrs(write_bio, REQ_OP_WRITE, REQ_SYNC);
    bio_set_dev(write_bio, bd);
    write_bio->bi_iter.bi_sector = o->lba * 8;
    submit_bio_wait(write_bio);

    bio_put(write_bio);
    bio_put(read_bio);

    bio_free_pages(write_bio);

    return 0;
}

int olayer_unregister_all_streams(void) {
    u32 unregistered = 0, i;
    struct registerd_dev *dev, *tmp;

    spin_lock(&olayer->registered_list_lock);
    list_for_each_entry_safe(dev, tmp, &olayer->registered_list, list) {
        list_del_init(&dev->list);
        kfree(dev);
        olayer_debug(0, "delete dev %u from list\n", dev->dev_ID);
    }
    spin_unlock(&olayer->registered_list_lock);

    while (kfifo_get(&olayer->free_stream_q, &i)) {
        olayer_reset_stream(i);
        unregistered++;
    };

    olayer_debug(2, "unregister %u\n", unregistered);

    return 0;
}

int olayer_crash_recovery(struct block_device *bd) {
    uint i, j, k, m, n;
    struct stream *s;
    struct nvme_rw_command_ *o;
    int cpu;
    struct nvme_rw_command_ *start;
    u32 begin_idx;
    u64 begin_txid;
    int phase = 0;  // 0: find begin; 1: find end;

    for (i = 0; i < nr_streams; i++) {
        s = &olayer->stream[i];
        cmb_read(&ci, s, 0, s->ometa_size);
        cpu = smp_processor_id() % olayer->cpus;
        start = olayer->read_buffer[cpu];

        olayer_debug(3,
                     "stream %u, flushed %u, head %u, length "
                     "%u-------------------------------------------------",
                     i, s->flushed, s->head, horae_count(s->head, s->flushed, s->nr_ometa));

        for (m = s->head; m != s->flushed; m = (m + 1) % s->nr_ometa) {
            j = m;
            o = &start[j];
            print_ometa(o);
            if (phase == 0) {
                if ((o->tag & (1 << HORAE_TX_BEGIN))) {
                    begin_idx = j;
                    begin_txid = o->txid;
                    phase = 1;
                    continue;
                }
            }
            if (phase == 1) {
                if (o->tag & (1 << HORAE_TX_END)) {
                    WARN_ON(begin_txid != o->txid);
                    for (k = (begin_idx + 1) % s->nr_ometa; k != j; k = (k + 1) % s->nr_ometa) {
                        n = k;
                        if (stream_ometa_at(s, n)->plba == 0) continue;
                        crash_recovery_commit_write(stream_ometa_at(s, n), bd);
                    }
                    phase = 0;
                }
            }
        }
    }

    return 0;
}
EXPORT_SYMBOL(olayer_crash_recovery);