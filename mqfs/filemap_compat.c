#include <ccnvme/compat.h>
#include <ccnvme/horae.h>
#include <linux/backing-dev.h>
#include <linux/dax.h>

#include "ext4.h"

// for mqfs
struct waiting_inode_list {
    struct inode *inode;
    struct list_head list;
};

int get_free_stream(struct ext4_sb_info *sbi, u32 *stream) {
    int cpu = smp_processor_id();
    int loop = sbi->nr_streams;
    int s = cpu % sbi->nr_streams;
    if (mutex_trylock(&sbi->stream_lock[s])) {
        *stream = sbi->streams[s];
        return 0;
    }

    while (--loop) {
        s = (s + 1) % sbi->nr_streams;
        if (mutex_trylock(&sbi->stream_lock[s])) {
            *stream = sbi->streams[s];
            return 0;
        }
    }

    mutex_lock(&sbi->stream_lock[cpu % sbi->nr_streams]);
    *stream = sbi->streams[cpu % sbi->nr_streams];
    return 0;
}

int put_stream(struct ext4_sb_info *sbi, u32 stream) {
    if (stream >= sbi->nr_streams) stream %= sbi->nr_streams;
    mutex_unlock(&sbi->stream_lock[stream]);
    return 0;
}

static bool mapping_needs_writeback(struct address_space *mapping) {
    return (!dax_mapping(mapping) && mapping->nrpages) ||
           (dax_mapping(mapping) && mapping->nrexceptional);
}

int file_tag_write_range(struct file *file, loff_t lstart, loff_t lend, int tag) {
    int err = 0, err2;
    struct address_space *mapping = file->f_mapping;

    if (mapping_needs_writeback(mapping)) {
        err = filemap_fdatawrite_tag_range(mapping, lstart, lend, tag);
    }
    err2 = file_check_and_advance_wb_err(file);
    if (!err) err = err2;
    return err;
}

int file_tag_streamwrite_range(struct file *file, loff_t lstart, loff_t lend, int tag, uint sid) {
    int err = 0, err2;
    struct address_space *mapping = file->f_mapping;

    if (mapping_needs_writeback(mapping)) {
        err = filemap_fstreamwrite_tag_range(mapping, lstart, lend, tag, sid);
    }
    err2 = file_check_and_advance_wb_err(file);
    if (!err) err = err2;
    return err;
}

int file_write_barrier_range(struct file *file, loff_t lstart, loff_t lend) {
    return file_tag_write_range(file, lstart, lend, WB_BARRIER_ALL);
}

int file_write_ordered_range(struct file *file, loff_t lstart, loff_t lend) {
    return file_tag_write_range(file, lstart, lend, WB_ORDERED_ALL);
}

#ifdef PERF_DECOMPOSE
void tx_write_dirty_buffer(struct buffer_head *bh, u32, struct ext4_sb_info *sbi);
#else
void tx_write_dirty_buffer(struct buffer_head *bh, u32);
#endif

static void __remove_assoc_queue(struct buffer_head *bh) {
    list_del_init(&bh->b_assoc_buffers);
    WARN_ON(!bh->b_assoc_map);
    bh->b_assoc_map = NULL;
}

#define BH_ENTRY(list) list_entry((list), struct buffer_head, b_assoc_buffers)

// sumbit the private IOs, but do not wait.
static int mqfs_submit_buffers_list(spinlock_t *lock, struct list_head *list, struct inode *inode,
                                    u32 sid) {
    struct buffer_head *bh;
    struct address_space *mapping;
    int err = 0;
    struct blk_plug plug;
    struct ext4_inode_info *mqfs_ino = EXT4_I(inode);

    blk_start_plug(&plug);

    spin_lock(lock);
    while (!list_empty(list)) {
        bh = BH_ENTRY(list->next);
        mapping = bh->b_assoc_map;
        __remove_assoc_queue(bh);
        /* Avoid race with mark_buffer_dirty_inode() which does
         * a lockless check and we rely on seeing the dirty bit */
        smp_mb();
        if (buffer_dirty(bh) || buffer_locked(bh)) {
            list_add(&bh->b_assoc_buffers, &mqfs_ino->fsmeta_pending_list);
            bh->b_assoc_map = mapping;
            if (buffer_dirty(bh)) {
                get_bh(bh);
                spin_unlock(lock);
                /*
                 * Ensur\e any pending I/O completes so that
                 * write_dirty_buffer() actually writes the
                 * current contents - it is a noop if I/O is
                 * still in flight on potentially older
                 * contents.
                 */
#ifdef PERF_DECOMPOSE
                tx_write_dirty_buffer(bh, sid, EXT4_SB(inode->i_sb));
#else
                tx_write_dirty_buffer(bh, sid);
#endif
                /*
                 * Kick off IO for the previous mapping. Note
                 * that we will not run the very last mapping,
                 * wait_on_buffer() will do that for us
                 * through sync_buffer().
                 */
                brelse(bh);
                spin_lock(lock);
            }
        }
    }
    spin_unlock(lock);
    blk_finish_plug(&plug);
    return err;
}

int mqfs_wait_mapping_buffers(struct address_space *mapping);

int mqfs_submit_mapping_buffers(struct address_space *mapping, u32 sid) {
    struct address_space *buffer_mapping = mapping->private_data;

    if (buffer_mapping == NULL || list_empty(&mapping->private_list)) return 0;

    return mqfs_submit_buffers_list(&buffer_mapping->private_lock, &mapping->private_list,
                                    mapping->host, sid);
}

static int osync_buffers_list(spinlock_t *lock, struct list_head *list) {
    struct buffer_head *bh;
    struct list_head *p;
    int err = 0;

    spin_lock(lock);
repeat:
    list_for_each_prev(p, list) {
        bh = BH_ENTRY(p);
        if (buffer_locked(bh)) {
            get_bh(bh);
            spin_unlock(lock);
            wait_on_buffer(bh);
            if (!buffer_uptodate(bh)) err = -EIO;
            brelse(bh);
            spin_lock(lock);
            goto repeat;
        }
    }
    spin_unlock(lock);
    return err;
}

static int mqfs_wait_buffers_list(spinlock_t *lock, struct list_head *list, struct inode *inode) {
    struct ext4_inode_info *mqfs_ino = EXT4_I(inode);
    struct buffer_head *bh;
    struct address_space *mapping;
    int err = 0, err2;

    spin_lock(lock);
    while (!list_empty(&mqfs_ino->fsmeta_pending_list)) {
        bh = BH_ENTRY(mqfs_ino->fsmeta_pending_list.prev);
        get_bh(bh);
        mapping = bh->b_assoc_map;
        __remove_assoc_queue(bh);
        /* Avoid race with mark_buffer_dirty_inode() which does
         * a lockless check and we rely on seeing the dirty bit */
        smp_mb();
        if (buffer_dirty(bh)) {
            list_add(&bh->b_assoc_buffers, &mapping->private_list);
            bh->b_assoc_map = mapping;
        }
        spin_unlock(lock);
        wait_on_buffer(bh);
        if (!buffer_uptodate(bh)) err = -EIO;
        brelse(bh);
        spin_lock(lock);
    }
    spin_unlock(lock);

    err2 = osync_buffers_list(lock, list);
    if (err)
        return err;
    else
        return err2;
}

int mqfs_wait_mapping_buffers(struct address_space *mapping) {
    struct address_space *buffer_mapping = mapping->private_data;
    if (buffer_mapping == NULL) return 0;

    return mqfs_wait_buffers_list(&buffer_mapping->private_lock, &mapping->private_list,
                                  mapping->host);
}

static void buffer_io_error(struct buffer_head *bh, char *msg) {
    if (!test_bit(BH_Quiet, &bh->b_state))
        printk_ratelimited(KERN_ERR "Buffer I/O error on dev %pg, logical block %llu%s\n",
                           bh->b_bdev, (unsigned long long)bh->b_blocknr, msg);
}

void end_buffer_write_sync_nolock(struct buffer_head *bh, int uptodate) {
    if (uptodate) {
        set_buffer_uptodate(bh);
    } else {
        buffer_io_error(bh, ", lost sync page write");
        mark_buffer_write_io_error(bh);
        clear_buffer_uptodate(bh);
    }
    put_bh(bh);
}

#ifndef PERF_DECOMPOSE
// metadata always goes to IPU write path
void tx_write_dirty_buffer(struct buffer_head *bh, u32 sid) {
    lock_buffer(bh);
    if (!test_clear_buffer_dirty(bh)) {
        unlock_buffer(bh);
        return;
    }
    bh->b_end_io = end_buffer_write_sync;
    get_bh(bh);
    stream_submit_bh_force_IPU(bh, REQ_OP_WRITE, REQ_TX | REQ_SYNC, sid);
}
#else
void tx_write_dirty_buffer(struct buffer_head *bh, u32 sid, struct ext4_sb_info *sbi) {
    int ret;

    lock_buffer(bh);
    if (!test_clear_buffer_dirty(bh)) {
        unlock_buffer(bh);
        return;
    }
    bh->b_end_io = end_buffer_write_sync;
    get_bh(bh);

    sbi->tx_write_count++;

    record_time(stream_submit_bh_force_IPU(bh, REQ_OP_WRITE, REQ_TX | REQ_SYNC, sid), ret,
                sbi->tx_write_time);
}
#endif

int tx_write_wait_inode_buffer(struct inode *inode) {
    struct ext4_inode_info *mqfs_ino = EXT4_I(inode);
    struct address_space *mapping = inode->i_mapping;
    struct address_space *buffer_mapping = mapping->private_data;
    int err = 0;

    if (list_empty(&mqfs_ino->inodemeta_pending_list)) goto copy_out_handling;

    spin_lock(&buffer_mapping->private_lock);
    while (!list_empty(&mqfs_ino->inodemeta_pending_list)) {
        struct buffer_head *bh = BH_ENTRY(mqfs_ino->inodemeta_pending_list.prev);
        get_bh(bh);
        if (bh->b_assoc_map) {
            __remove_assoc_queue(bh);
        } else {
            list_del_init(&bh->b_assoc_buffers);
        }
        spin_unlock(&buffer_mapping->private_lock);
        wait_on_buffer(bh);
        if (buffer_req(bh) && !buffer_uptodate(bh)) {
            EXT4_ERROR_INODE_BLOCK(inode, bh->b_blocknr, "IO error syncing inode");
            err = -EIO;
        }
        brelse(bh);
        spin_lock(&buffer_mapping->private_lock);
    }
    spin_unlock(&buffer_mapping->private_lock);

copy_out_handling:
    if (list_empty(&mqfs_ino->copyout_inode_list)) {
        return err;
    }

    while (!list_empty(&mqfs_ino->copyout_inode_list)) {
        struct buffer_head *bh = BH_ENTRY(mqfs_ino->copyout_inode_list.prev);
        get_bh(bh);
        if (bh->b_assoc_map) {
            __remove_assoc_queue(bh);
        } else {
            list_del_init(&bh->b_assoc_buffers);
        }
        wait_on_buffer(bh);
        if (buffer_req(bh) && !buffer_uptodate(bh)) {
            EXT4_ERROR_INODE_BLOCK(inode, bh->b_blocknr, "IO error syncing inode");
            err = -EIO;
        }
        brelse(bh);
        release_copy_out_bh(bh, bh->b_size);
    }
    return err;
}

int tx_write_inode_metadata(struct inode *inode, u32 stream) {
    struct writeback_control wbc = {.sync_mode = WB_SYNC_ALL,
                                    .nr_to_write = 0, /* metadata-only */
                                    .stream = stream};

    return sync_inode_nowait_data(inode, &wbc);
}

int mqfs_atomic_write_parent(struct inode *inode, uint stream) {
    struct dentry *dentry = NULL;
    struct inode *next;
    int ret = 0;

    if (!ext4_test_inode_state(inode, EXT4_STATE_NEWENTRY)) return 0;
    inode = igrab(inode);
    while (ext4_test_inode_state(inode, EXT4_STATE_NEWENTRY)) {
        ext4_clear_inode_state(inode, EXT4_STATE_NEWENTRY);
        dentry = d_find_any_alias(inode);
        if (!dentry) break;
        next = igrab(d_inode(dentry->d_parent));
        dput(dentry);
        if (!next) break;
        iput(inode);
        inode = next;
        /*
         * The directory inode may have gone through rmdir by now. But
         * the inode itself and its blocks are still allocated (we hold
         * a reference to the inode so it didn't go through
         * ext4_evict_inode()) and so we are safe to flush metadata
         * blocks and the inode.
         */
        // ret = sync_mapping_buffers(inode->i_mapping);
        ret = mqfs_submit_mapping_buffers(inode->i_mapping, stream);
        if (ret) break;
        ret = tx_write_inode_metadata(inode, stream);

        ret = mqfs_wait_mapping_buffers(inode->i_mapping);
        ret = tx_write_wait_inode_buffer(inode);

        if (ret) break;
    }
    iput(inode);
    return ret;
}

int mqfs_atomic_write_parent_wait(struct list_head *waiting_list) {
    struct inode *inode;
    int ret = 0;
    uint waiting = 0;
    struct waiting_inode_list *inode_list, *tmp_list;

    list_for_each_entry_safe(inode_list, tmp_list, waiting_list, list) {
        inode = inode_list->inode;
        ext4_clear_inode_state(inode, EXT4_STATE_NEWENTRY);

        waiting++;
#ifndef DISABLE_METAPAGING
        ret = mqfs_wait_mapping_buffers(inode->i_mapping);
#endif
        ret = tx_write_wait_inode_buffer(inode);
        iput(inode);

        list_del_init(&inode_list->list);
        kfree(inode_list);
    }

    return ret;
}

int mqfs_atomic_write_parent_submit(struct inode *inode, uint stream,
                                    struct list_head *waiting_list) {
    struct dentry *dentry = NULL;
    struct inode *next;
    int ret = 0;
    int leaf_inode = 1;
    struct waiting_inode_list *inode_list;

    if (!ext4_test_inode_state(inode, EXT4_STATE_NEWENTRY)) return 0;
    inode = igrab(inode);
    while (ext4_test_inode_state(inode, EXT4_STATE_NEWENTRY)) {
        if (leaf_inode) ext4_clear_inode_state(inode, EXT4_STATE_NEWENTRY);

        dentry = d_find_any_alias(inode);
        if (!dentry) break;
        next = igrab(d_inode(dentry->d_parent));
        dput(dentry);
        if (!next) break;
        if (leaf_inode) {
            iput(inode);
            leaf_inode = 0;
        }
        inode = next;
        /*
         * The directory inode may have gone through rmdir by now. But
         * the inode itself and its blocks are still allocated (we hold
         * a reference to the inode so it didn't go through
         * ext4_evict_inode()) and so we are safe to flush metadata
         * blocks and the inode.
         */
#ifdef DISABLE_METAPAGING
        ret = sync_mapping_buffers(inode->i_mapping);
#else

        ret = mqfs_submit_mapping_buffers(inode->i_mapping, stream);
#endif
        if (ret) break;

        ret = tx_write_inode_metadata(inode, stream);
        if (ret) break;
        inode_list = kmalloc(sizeof(struct waiting_inode_list), GFP_KERNEL);
        inode_list->inode = inode;
        list_add_tail(&inode_list->list, waiting_list);
    }
    return ret;
}

int __generic_file_atomic_write_submit(struct file *file, loff_t start, loff_t end, int datasync,
                                       uint stream) {
    struct inode *inode = file->f_mapping->host;
    int err, ret;
#ifdef PERF_DECOMPOSE
    struct ext4_sb_info *sbi = EXT4_SB(inode->i_sb);
#endif
    // file data
    record_time(file_tag_streamwrite_range(file, start, end, WB_TX_WRITE, stream), err,
                sbi->fdata_submit_time);

    if (err) return err;

    inode_lock(inode);

    // FS metadata
#ifdef DISABLE_METAPAGING
    record_time(sync_mapping_buffers(inode->i_mapping), ret, sbi->fmapping_submit_time);
#else
    record_time(mqfs_submit_mapping_buffers(inode->i_mapping, stream), ret,
                sbi->fmapping_submit_time);
#endif

    if (!(inode->i_state & I_DIRTY_ALL)) goto out;
    if (datasync && !(inode->i_state & I_DIRTY_DATASYNC)) goto out;

    // inode metadata
    record_time(tx_write_inode_metadata(inode, stream), err, sbi->finode_submit_time);

    if (ret == 0) ret = err;

out:

    /* check and advance again to catch errors after syncing out buffers */
    err = file_check_and_advance_wb_err(file);
    if (ret == 0) ret = err;

    if (err) ret = err;

    return ret;
}

int __generic_file_atomic_write_wait_nodata(struct file *file, loff_t start, loff_t end,
                                            int datasync) {
    struct inode *inode = file->f_mapping->host;

    inode_unlock(inode);
    return 0;
}

int __generic_file_atomic_write_wait(struct file *file, loff_t start, loff_t end, int datasync) {
    struct inode *inode = file->f_mapping->host;
    int ret, err;
#ifdef PERF_DECOMPOSE
    struct ext4_sb_info *sbi = EXT4_SB(inode->i_sb);
#endif

    record_time(tx_write_wait_inode_buffer(inode), ret, sbi->finode_wait_time);

#ifndef DISABLE_METAPAGING
    record_time(mqfs_wait_mapping_buffers(inode->i_mapping), err, sbi->fmapping_wait_time);
#endif

    if (ret == 0) ret = err;

    inode_unlock(inode);

    record_time(file_fdatawait_range(file, start, end), err, sbi->fdata_wait_time);

    if (ret == 0) ret = err;
    /* check and advance again to catch errors after syncing out buffers */
    err = file_check_and_advance_wb_err(file);
    if (err) return err;

    return ret;
}

int __generic_file_atomic_write(struct file *file, loff_t start, loff_t end, int datasync,
                                uint stream) {
    struct inode *inode = file->f_mapping->host;
    int err, ret;

    // file data
    err = file_tag_streamwrite_range(file, start, end, WB_TX_WRITE, stream);
    if (err) return err;

    inode_lock(inode);

    // FS metadata
    ret = mqfs_submit_mapping_buffers(inode->i_mapping, stream);

    if (!(inode->i_state & I_DIRTY_ALL)) goto out;
    if (datasync && !(inode->i_state & I_DIRTY_DATASYNC)) goto out;

    // inode metadata
    err = tx_write_inode_metadata(inode, stream);

    if (ret == 0) ret = err;

out:
    // wait
    ret = mqfs_wait_mapping_buffers(inode->i_mapping);
    err = tx_write_wait_inode_buffer(inode);

    inode_unlock(inode);
    /* check and advance again to catch errors after syncing out buffers */
    err = file_check_and_advance_wb_err(file);
    if (ret == 0) ret = err;

    if (err) ret = err;

    return ret;
}
