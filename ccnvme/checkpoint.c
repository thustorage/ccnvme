#include <linux/blk_types.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/mm_types.h>
#include <linux/moduleparam.h>

#include "horae.h"

static char cp_device[128] = "/dev/null";
module_param_string(cp_device, cp_device, 128, 0);
MODULE_PARM_DESC(cp_device, "The file system journal and PMR checkpoint device. Must be specified!");

static unsigned int journal_size = JOURNAL_REGION;
module_param(journal_size, uint, 0444);
MODULE_PARM_DESC(journal_size, "The size of log region in MB. Default 1024 MB.");

static unsigned int cp_size = PMR_CHECKPOINT_REGION;
module_param(cp_size, uint, 0444);
MODULE_PARM_DESC(cp_size, "The size of PMR checkpoint region in MB. Default 10 MB.");

struct block_device *cp_dev;
fmode_t mode = FMODE_READ | FMODE_WRITE;

extern struct olayer_info *olayer;
extern unsigned int nr_streams;
extern struct kmem_cache *horae_ipu_entry_cache;
extern unsigned int version_consistency;

struct waiting_bh_list {
    struct buffer_head *bh;
    struct list_head list;
};

static int commit_write_entry_per_stream(struct nvme_rw_command_ *o, struct super_block *sb,
                                         u64 txid, struct list_head *bh_list) {
    u32 group_idx, i;
    uint tree_idx, tree_off;
    ulong start_lba = o->lba;
    uint nr_blocks = o->length;
    struct ipu_entry *ipu_entry;
    struct buffer_head *bh;
    struct waiting_bh_list *list;

    if (sb == NULL) return 0;

    for (i = 0; i < nr_blocks; i++, start_lba++) {
        group_idx = GROUP_IDX(start_lba, olayer->blocks_per_group);
        tree_idx = TREE_IDX(group_idx);
        tree_off = TREE_OFF(group_idx, start_lba, olayer->blocks_per_group);

        down_write(&olayer->ipu_tree_locks[tree_idx]);
        ipu_entry = radix_tree_delete(&olayer->ipu_tree[tree_idx], tree_off);
        up_write(&olayer->ipu_tree_locks[tree_idx]);
        if (ipu_entry) {
            olayer_debug(1, "lba %u, %llu vs %llu\n", ipu_entry->lba, ipu_entry->txid, txid);
            if (ipu_entry->txid != txid) {  // skip old data
                continue;
            }
            WARN_ON(ipu_entry->txid != txid && o->plba == 0);
            WARN_ON(start_lba != ipu_entry->lba);
            bh = sb_getblk(sb, start_lba);
            if (unlikely(!bh)) {
                pr_err("no memory for bh\n");
                up_write(&olayer->ipu_tree_locks[tree_idx]);
                return -ENOMEM;
            }
            if (buffer_locked(bh)) {
                olayer_debug(2, "wait bh %lu, %lu\n", start_lba, bh->b_blocknr);
                // wait_on_buffer(bh);
                list = kmalloc(sizeof(struct waiting_bh_list), GFP_KERNEL);
                list->bh = bh;
                list_add_tail(&list->list, bh_list);
            } else {
                lock_buffer(bh);
                get_bh(bh);
                bh->b_end_io = end_buffer_write_sync;
                submit_bh(REQ_OP_WRITE, REQ_SYNC, bh);
                olayer_debug(2, "commit bh %lu, %lu\n", start_lba, bh->b_blocknr);
                // wait_on_buffer(bh);
                list = kmalloc(sizeof(struct waiting_bh_list), GFP_KERNEL);
                list->bh = bh;
                list_add_tail(&list->list, bh_list);
            }
            kmem_cache_free(horae_ipu_entry_cache, ipu_entry);
        }
    }
    return 0;
}

struct super_block *get_sb_by_devid(dev_t id) {
    struct registerd_dev *dev;
    int found = 0;
    list_for_each_entry(dev, &olayer->registered_list, list) {
        if (MINOR(dev->dev_ID) == id) {
            return dev->sb;
        }
    }
    WARN(found != 1, "super block of dev %u not found!\n", id);
    return NULL;
}

int commit_write(struct stream *s) {
    int j, k, m;
    struct nvme_rw_command_ *o, *o_tmp;
    int phase = 0;  // 0: find begin; 1: find end;
    u32 begin_idx;
    u64 begin_txid;
    uint waiting = 0;
    u64 time;

    struct list_head bh_list;
    struct waiting_bh_list *list, *tmp_list;
    INIT_LIST_HEAD(&bh_list);

    time = ktime_get_ns();
    for (m = s->head; m != s->flushed; m = (m + 1) % s->nr_ometa) {
        j = m;
        o = stream_ometa_at(s, j);
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
                    o_tmp = stream_ometa_at(s, k);
                    if (o_tmp->plba == 0) continue;
                    commit_write_entry_per_stream(o_tmp, get_sb_by_devid(o_tmp->device_id), o->txid,
                                                  &bh_list);
                }
                phase = 0;
            }
        }
    }

    list_for_each_entry_safe(list, tmp_list, &bh_list, list) {
        waiting++;
        wait_on_buffer(list->bh);

        if (!version_consistency) {
            put_page(list->bh->b_page);  // unpin this page
        }

        list_del_init(&list->list);
        kfree(list);
    }

    olayer_debug(1, "commit write sumary: stream %u, total %u, time %u ns\n", s->stream_id, waiting,
                 ktime_get_ns() - time);

    return 0;
}

#ifdef DISABLE_MQ_JOURNAL
int commit_write_submit(struct stream *s, struct list_head *bh_list) {
    int j, k, m;
    struct nvme_rw_command_ *o, *o_tmp;
    int phase = 0;  // 0: find begin; 1: find end;
    u32 begin_idx;
    u64 begin_txid;

    for (m = s->head; m != s->flushed; m = (m + 1) % s->nr_ometa) {
        j = m;
        o = stream_ometa_at(s, j);
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
                    o_tmp = stream_ometa_at(s, k);
                    if (o_tmp->plba == 0) continue;
                    commit_write_entry_per_stream(o_tmp, get_sb_by_devid(o_tmp->device_id), o->txid,
                                                  bh_list);
                }
                phase = 0;
            }
        }
    }
    return 0;
}

void commit_write_all(void) {
    int i;
    struct stream *s;
    u64 time;
    uint waiting = 00;
    struct list_head bh_list;
    struct waiting_bh_list *list, *tmp_list;
    INIT_LIST_HEAD(&bh_list);

    olayer_debug(1, "commit write all begin\n");
    time = ktime_get_ns();
    for (i = 0; i < nr_streams; i++) {
        s = &olayer->stream[i];
        commit_write_submit(s, &bh_list);
        cond_resched();
    }

    olayer_debug(1, "commit write starts waiting\n");
    list_for_each_entry_safe(list, tmp_list, &bh_list, list) {
        waiting++;
        get_bh(list->bh);
        wait_on_buffer(list->bh);
        if (!version_consistency) {
            put_page(list->bh->b_page);  // unpin this page
        }
        cond_resched();
        brelse(list->bh);
        list_del_init(&list->list);
        kfree(list);
    }

    for (i = 0; i < nr_streams; i++) {
        s = &olayer->stream[i];
        s->head = s->flushed;
        s->j_write_ptr = s->off_in_journal;
    }

    olayer_debug(1, "commit write sumary: total %u, time %u ns\n", waiting, ktime_get_ns() - time);
}
#endif

u32 alloc_journal_block(uint sid, struct bio *src_bio, uint nr_blocks) {
    struct stream *stream = &olayer->stream[sid];
    uint parea_right_boundary = stream->off_in_journal + stream->nr_journal_block;
    uint parea_start_blk = JOURNAL_REGION_START;
    u64 current_pwrite_ptr = 0;

    if (nr_blocks > stream->nr_journal_block) {
        pr_err("parea size %llu is too small to serve %u blocks\n", stream->nr_journal_block,
               nr_blocks);
        return -1;
    }

#ifdef DISABLE_MQ_JOURNAL
    percpu_down_read(&olayer->fs_cp_rwsem);
#endif
    // allocate space
    mutex_lock(&stream->journal_lock);
    if (nr_blocks + stream->j_write_ptr > parea_right_boundary) {
#ifdef DISABLE_MQ_JOURNAL
        mutex_unlock(&stream->journal_lock);
        percpu_up_read(&olayer->fs_cp_rwsem);

        percpu_down_write(&olayer->fs_cp_rwsem);
        commit_write_all();
        percpu_up_write(&olayer->fs_cp_rwsem);

        percpu_down_read(&olayer->fs_cp_rwsem);
        mutex_lock(&stream->journal_lock);
#else
        commit_write(stream);
        stream->head = stream->flushed;
        stream->j_write_ptr = stream->off_in_journal;
#endif
    }
    current_pwrite_ptr = stream->j_write_ptr;
    stream->j_write_ptr += nr_blocks;
    mutex_unlock(&stream->journal_lock);

#ifdef DISABLE_MQ_JOURNAL
    percpu_up_read(&olayer->fs_cp_rwsem);
#endif

    bio_set_dev(src_bio, cp_dev);
    src_bio->bi_iter.bi_sector = (parea_start_blk + current_pwrite_ptr) * 8;

    return (parea_start_blk + current_pwrite_ptr);
}
EXPORT_SYMBOL(alloc_journal_block);

// return plba
u32 journal_write_alloc(struct stream *stream, struct bio *src_bio) {
    uint parea_right_boundary = stream->off_in_journal + stream->nr_journal_block;
    uint parea_start_blk = JOURNAL_REGION_START;
    u64 current_pwrite_ptr = 0;
    u32 group_idx, i;
    uint tree_idx, tree_off;
    struct ipu_entry *ipu_entry;
    ulong start_lba = horae_get_lba(src_bio);
    uint nr_blocks = horae_get_secs(src_bio);
    struct bio_vec *bvl;

    if (nr_blocks > stream->nr_journal_block) {
        pr_err("parea size %llu is too small to serve %u blocks\n", stream->nr_journal_block,
               nr_blocks);
        return -1;
    }

#ifdef DISABLE_MQ_JOURNAL
    percpu_down_read(&olayer->fs_cp_rwsem);
#endif
    // allocate space
    mutex_lock(&stream->journal_lock);
    if (nr_blocks + stream->j_write_ptr > parea_right_boundary) {
#ifdef DISABLE_MQ_JOURNAL
        mutex_unlock(&stream->journal_lock);
        percpu_up_read(&olayer->fs_cp_rwsem);

        percpu_down_write(&olayer->fs_cp_rwsem);
        commit_write_all();
        percpu_up_write(&olayer->fs_cp_rwsem);

        percpu_down_read(&olayer->fs_cp_rwsem);
        mutex_lock(&stream->journal_lock);
#else
        commit_write(stream);
        stream->head = stream->flushed;
        stream->j_write_ptr = stream->off_in_journal;
#endif
    }
    current_pwrite_ptr = stream->j_write_ptr;
    stream->j_write_ptr += nr_blocks;
    mutex_unlock(&stream->journal_lock);

#ifdef DISABLE_MQ_JOURNAL
    percpu_up_read(&olayer->fs_cp_rwsem);
#endif

    // update indexing
    for (i = 0, bvl = (src_bio)->bi_io_vec; i < nr_blocks; i++, start_lba++, bvl++) {
        group_idx = GROUP_IDX(start_lba, olayer->blocks_per_group);
        tree_idx = TREE_IDX(group_idx);
        tree_off = TREE_OFF(group_idx, start_lba, olayer->blocks_per_group);

        olayer_debug(3, "group %lu, tree %u, off %u, lba %llu", group_idx, tree_idx, tree_off,
                     start_lba);
        down_write(&olayer->ipu_tree_locks[tree_idx]);
        ipu_entry = radix_tree_lookup(&olayer->ipu_tree[tree_idx], tree_off);
        if (ipu_entry == NULL) {
            ipu_entry = kmem_cache_alloc(horae_ipu_entry_cache, GFP_NOFS | __GFP_NOFAIL);
            ipu_entry->lba = start_lba;
            ipu_entry->plba = parea_start_blk + current_pwrite_ptr + i;
            ipu_entry->txid = stream->last_txid;
            radix_tree_insert(&olayer->ipu_tree[tree_idx], tree_off, ipu_entry);
            if (!version_consistency) {
                get_page(bvl->bv_page);
            }
            // bio_for_each_segment(bvec, src_bio, iter) { get_page(bvec.bv_page); }
            olayer_debug(3, "insert lba %lu, plba %u, txid %u", ipu_entry->lba, ipu_entry->plba,
                         ipu_entry->txid);
        } else {
            if (stream->last_txid > ipu_entry->txid) {  // must update the tree
                ipu_entry->lba = start_lba;
                ipu_entry->plba = parea_start_blk + current_pwrite_ptr + i;
                olayer_debug(3, "update lba %lu, plba %u, new txid %u, old txid %u", ipu_entry->lba,
                             ipu_entry->plba, stream->last_txid, ipu_entry->txid);
                ipu_entry->txid = stream->last_txid;

            } else {
                olayer_debug(3, "??? lba %lu, plba %u, new txid %u, old txid %u", ipu_entry->lba,
                             ipu_entry->plba, ipu_entry->txid, stream->last_txid);
            }
        }
        up_write(&olayer->ipu_tree_locks[tree_idx]);
    }

    bio_set_dev(src_bio, cp_dev);
    src_bio->bi_iter.bi_sector = (parea_start_blk + current_pwrite_ptr) * 8;

    return (parea_start_blk + current_pwrite_ptr);
}

int truncate_ordering_log(struct stream *horae) {
    u64 head = horae->head;
    u64 flushed = horae->flushed;

    if (head == flushed) return 0;

    olayer_debug(1, "perform truncate\n");

#ifdef DISABLE_MQ_JOURNAL
    percpu_down_write(&olayer->fs_cp_rwsem);
    commit_write_all();
    percpu_up_write(&olayer->fs_cp_rwsem);
#else
    mutex_lock(&horae->journal_lock);
    commit_write(horae);
    horae->head = horae->flushed;
    horae->j_write_ptr = horae->off_in_journal;
    mutex_unlock(&horae->journal_lock);
#endif

    return 0;
}

int write_checkpoint(struct stream *stream) {
    u64 head = stream->head;
    u64 tail = stream->tail;
    int i;
    uint nr_4k, space_valid_ometa;
    struct bio *bio;

    olayer_debug(2, "valid count %llu, max bio pages %llu\n",
                 horae_count(head, tail, stream->nr_ometa), BIO_MAX_PAGES);
    space_valid_ometa = horae_count(head, tail, stream->nr_ometa) * sizeof(struct nvme_rw_command_);
    nr_4k = space_valid_ometa % HORAE_BLOCK_SIZE == 0 ? space_valid_ometa / HORAE_BLOCK_SIZE
                                                      : space_valid_ometa / HORAE_BLOCK_SIZE + 1;
    nr_4k = min_t(int, nr_4k, BIO_MAX_PAGES);

    bio = bio_alloc(GFP_NOIO | __GFP_NOFAIL, nr_4k);
    bio_set_op_attrs(bio, REQ_OP_WRITE, REQ_SYNC | REQ_FUA);
    bio_set_dev(bio, cp_dev);
    bio->bi_iter.bi_sector = CP_REGION_START(journal_size);

    for (i = 0; i < nr_4k; i++) {
        struct page *page = alloc_page(GFP_KERNEL);
        if (bio_add_page(bio, page, PAGE_SIZE, 0) < PAGE_SIZE) {
            break;
        }
    }

    olayer_debug(2, "swap head %llu, tail %llu, needs %llu pages\n", head, tail, nr_4k);

    if (submit_bio_wait(bio)) {
        pr_warn("submit bio wait err code");
    }
    bio_free_pages(bio);
    bio_put(bio);

    return 0;
}

int stream_do_checkpoint(struct stream *stream, uint nr_entries) {
#ifdef OLAYER_ANALYSIS
    u64 s, e;
#endif

    down_write(&stream->cp_rwsem);
    if (horae_ring_space(stream->head, stream->tail, stream->nr_ometa) >= nr_entries) {
        up_write(&stream->cp_rwsem);
        return 0;
    }
    olayer_debug(2, "need horae cp head: %u, tail: %u, size: %u, entries: %u, ring space %u",
                 stream->head, stream->tail, stream->nr_ometa, nr_entries,
                 horae_ring_space(stream->head, stream->tail, stream->nr_ometa));

#ifdef OLAYER_ANALYSIS
    s = ktime_get_ns();
#endif
    truncate_ordering_log(stream);
#ifdef OLAYER_ANALYSIS
    e = ktime_get_ns();
    olayer->t_truncate = e - s;
#endif

    if (horae_ring_space(stream->head, stream->tail, stream->nr_ometa) >= nr_entries) {
        up_write(&stream->cp_rwsem);
        return 0;
    }
#ifdef OLAYER_ANALYSIS
    s = ktime_get_ns();

#endif
    // Checkpoint procedure
    write_checkpoint(stream);
#ifdef OLAYER_ANALYSIS
    e = ktime_get_ns();
    olayer->t_swap = e - s;
#endif

    stream->head = stream->flushed = stream->tail;
    olayer_debug(2, "after cp, head %llu, tail %llu, flushed %llu\n", stream->head, stream->tail,
                 stream->flushed);

    up_write(&stream->cp_rwsem);

    return 0;
}

void stream_init_cp(struct stream *s) {
    init_rwsem(&s->cp_rwsem);
    s->nr_cp_block = NR_CP_REGION_BLOCK(cp_size) / nr_streams;
    s->cp_size = s->nr_cp_block * HORAE_BLOCK_SIZE;
    s->off_in_cp = s->stream_id * s->nr_cp_block;
}

void stream_init_journal(struct stream *s) {
    mutex_init(&s->journal_lock);
    s->nr_journal_block = NR_JOURNAL_REGION_BLOCK(journal_size) / nr_streams;
    s->j_size = s->nr_journal_block * HORAE_BLOCK_SIZE;
    s->off_in_journal = s->stream_id * s->nr_journal_block;
    s->j_write_ptr = s->off_in_journal;
}

void stream_stop_journal(struct stream *s) {}

void stream_stop_cp(struct stream *s) {}

int __init init_cp_device() {
    int ret;

    cp_dev = blkdev_get_by_path(cp_device, mode, NULL);

    if (IS_ERR(cp_dev)) {
        ret = PTR_ERR(cp_dev);
        if (ret != -ENOTBLK) {
            pr_err("failed to open block device %s: (%ld)\n", cp_device, PTR_ERR(cp_dev));
        }
        pr_err("block device for checkpoint required. cp_device=/dev/sdaX or cp_device=nvme0n1pX\n");
        cp_dev = NULL;
        return ret;
    }

    return 0;
}

int __exit exit_cp_device() {
    blkdev_put(cp_dev, mode);
    return 0;
}