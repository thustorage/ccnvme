#ifndef _HORAE_H
#define _HORAE_H

#include <asm-generic/pci_iomap.h>
#include <linux/bio.h>
#include <linux/blk_types.h>
#include <linux/buffer_head.h>
#include <linux/fs.h>
#include <linux/genhd.h>
#include <linux/kfifo.h>
#include <linux/kthread.h>
#include <linux/moduleparam.h>
#include <linux/rwsem.h>
#include <linux/topology.h>
#include <linux/types.h>
#include <linux/wait.h>

#include "compat.h"

// #define USE_AEP
// #define USE_CMB

// #define DISABLE_TX_DOORBELL
// #define DISABLE_MQ_JOURNAL

#define KB 1024
#define MB (1024 * 1024)
#define HORAE_BLOCK_SIZE 4096

#define L2P_SIZE 8  // in Bytes
#define NR_L2PS_PER_BLOCK (HORAE_BLOCK_SIZE / L2P_SIZE)

#define CACHELINE_SIZE 64  // in Bytes

#define TIMER_INTERVAL (5)
// #define ENABLE_BG_SUBMITTER

#define CMB_DEVICE_ID_BITS 8
#define MAX_DEVICES (1ULL << CMB_DEVICE_ID_BITS)

#define horae_ring_space(head, tail, size) (((size) + (head) - (tail)-1) % ((size)))

#define horae_count(head, tail, size) (((size) + (tail) - (head)) % (size))

#ifdef OLAYER_DEBUG
#define olayer_debug(n, fmt, a...) __olayer_debug((n), __FILE__, __func__, __LINE__, (fmt), ##a)
void __olayer_debug(int level, const char *file, const char *func, unsigned int line,
                    const char *fmt, ...);
#else
#define olayer_debug(n, fmt, a...) /**/
#endif

enum {
    HORAE_TX_BEGIN,  // boundary of atomic operations
    HORAE_TX,        //
    HORAE_TX_END,    // boundary of atomic operations
};

/* submitter */
#define MAX_BIO_SQES 1024
#define DELEGATOR_IDEL_TIMEOUT 5000  // in ms

enum {
    TYPE_BIO = 0,
    TYPE_BH = 1,
};

#define HORAE_SUBMITTER_NEED_WAKEUP 1

struct submitter_ring {
    u64 head;
    u64 tail;
    u64 len;
    u64 tail_watermark;
    struct bio **bio_sqes;
    int flags;
};

struct submitter {
    struct task_struct *submitter_thread;
    int should_stop;
    struct submitter_ring ring;
    uint id;
};

/******/
/* cmb on dev */
struct pdev_info {
    unsigned int vendor;
    unsigned int device;
    const char *name;
};

struct init_param {
    int nr_device;
    dev_t *device_ids;  // runtime device ID, MAJOR and MINOR numbers
    struct super_block **sbs;
    u32 cmb_device_id;
    u32 parea_partition_hint;
    u32 registered;
};

struct nvme_rw_command_ {
    /* 24 B*/
    u64 lba;     // slba
    u16 length;  // length
    u16 pad1[3];
    u64 txid;  // transaction ID

    /* 8 B*/
    u32 tag : 3;  // tx tag
    u32 pad2 : 21;
    // just for in-memory indexing, real implementation does not uses these fields
    u32 device_id : CMB_DEVICE_ID_BITS;
    u32 plba;  // checkpoint and recovery

    // other fields of the original NVMe command is omitted
    /* 32 B*/
    u64 padding2[4];  // ccnvme extended to 64 Byte, the same as the size of struct nvme_rw_command
                      // in nvme.h
} __packed;

#define OLAYER_NR_SUBMITTERS 0

struct stream {
    // Ordering metadata
    u64 ometa_size;
    u64 nr_ometa;
    u64 off_in_cmb;  // in bytes

    u64 head;
    u64 tail;
    u64 tail_watermark;
    u64 flushed;

    u64 last_txid;

    int committed;
    u64 last_queue_flushed;

    struct nvme_rw_command_ *ometa_start;

    // Checkpoint region
    u64 cp_size;
    u64 nr_cp_block;
    u64 off_in_cp;  // in block size

    struct rw_semaphore cp_rwsem;

    // Log region
    u64 j_size;
    u64 nr_journal_block;
    u64 off_in_journal;  // in block size

    u64 j_write_ptr;
    struct mutex journal_lock;

    int nr_devices;
    u32 cmb_device_id;
    u32 stream_id;

    // stats
    u64 delegated_bio;
    u64 done_by_self_bio;
};

#define OLAYER_NR_STREAM 1  // default number of streams
//#define OLAYER_ANALYSIS

struct registerd_dev {
    dev_t dev_ID;
    struct super_block *sb;  // holding a reference for commit write
    struct list_head list;
};
#define NR_IPU_TREE num_online_cpus()

#define GROUP_IDX(lba, n) ((lba) / (n))
#define TREE_IDX(group_idx) ((group_idx) % NR_IPU_TREE)
#define TREE_OFF(group_idx, lba, n) (((group_idx) / NR_IPU_TREE * (n)) + ((lba) % (n)))

struct ipu_entry {
    u32 lba;
    u32 plba;
    u64 txid;
};

struct olayer_info {
    struct stream *stream;
    u64 olog_size;

    spinlock_t free_stream_lock;
    DECLARE_KFIFO_PTR(free_stream_q, u32);

    // for recovery
    spinlock_t registered_list_lock;
    struct list_head registered_list;

    struct rw_semaphore *ipu_tree_locks;
    struct radix_tree_root *ipu_tree;
    u32 blocks_per_group;

    // for submitter
    bool percore_submitter;
    u32 nr_submitters;
    struct submitter *submitter;

#ifdef USE_CMB
    void **read_buffer;
    int cpus;
#endif

    struct list_head stat_list;

#ifdef DISABLE_MQ_JOURNAL
    // for fs checkpoint
    struct percpu_rw_semaphore fs_cp_rwsem;
#endif

#ifdef OLAYER_ANALYSIS
    // for analysis
    u64 total_count;
    u64 t_olayer_core;
    u64 t_delegate_bio;
    u64 t_cmb_trans;
    u64 t_cmb_flush;
    u64 t_truncate;
    u64 t_swap;
    u64 t_cas;
#endif
};

// for CMB
struct cmb_info {
    struct pci_dev *pci_dev;
    void __iomem *dev_addr;
    u64 dev_size;
    resource_size_t raddr;
};

// default region size
#define JOURNAL_REGION 1024     // in MB
#define PMR_CHECKPOINT_REGION 10  // in MB

#define NR_CP_REGION_BLOCK(cp_size) ((MB) / (HORAE_BLOCK_SIZE) * (cp_size))
#define NR_JOURNAL_REGION_BLOCK(journal_size) ((MB) / (HORAE_BLOCK_SIZE) * (journal_size))

// on-disk layout of cp_device
// JOURNAL_REGION | PMR_CP_REGION
// #define CP_REGION_START 0
// #define JOURNAL_REGION_START(cp_size) (CP_REGION_START + NR_CP_REGION_BLOCK(cp_size))

#define JOURNAL_REGION_START 0
#define CP_REGION_START(journal_size) (JOURNAL_REGION_START + NR_JOURNAL_REGION_BLOCK(journal_size))


#ifdef USE_AEP
void NVM_FLUSH(u64 start, u64 size);
#endif

static inline bool op_is_barrier_write(unsigned int op) {
    return (op & (REQ_BARRIER)) || (op & (REQ_FUA));
}

static inline bool op_is_ordered_write(unsigned int op) {
    return (op & (REQ_ORDERED)) || op_is_barrier_write(op);
}

static inline bool op_is_tx_write(unsigned int op) { return (op & (REQ_TX)); }

static inline bool op_is_tx_commit(unsigned int op) { return (op & (REQ_TX_COMMIT)); }

static inline sector_t horae_get_lba(struct bio *bio) { return bio->bi_iter.bi_sector / 8; }

static inline unsigned int horae_get_secs(struct bio *bio) { return bio->bi_iter.bi_size / 4096; }

static inline unsigned int horae_get_bh_secs(struct buffer_head *bh) { return bh->b_size / 4096; }

static inline struct nvme_rw_command_ *stream_ometa_at(struct stream *s, uint idx) {
    return &s->ometa_start[idx];
}

static inline uint find_ometa_idx(struct stream *s, uint off) {
    uint res = (off + s->head) % s->nr_ometa;
    return res;
}

extern void stream_submit_bio(struct bio *bio, uint sid);
extern void stream_submit_bio_ipu(struct bio *bio, uint sid, int);
extern void stream_submit_bio_noop(struct bio *bio, uint sid);
extern int stream_submit_bh(struct buffer_head *bh, int op, uint op_flags, uint sid);
extern int stream_submit_bh_force_IPU(struct buffer_head *bh, int op, uint op_flags, uint sid);
extern int write_cmb_test(int, int, uint);
extern int write_cmb_test_multithread(int, int, uint, uint);
extern int read_cmb_test(int, uint);
extern int read_cmb_test_multithread(int, uint, uint);
extern int stream_blkdev_issue_flush(struct block_device **bdevs, uint nr_devices, gfp_t gfp_mask,
                                     sector_t *error_sector, uint sid);
extern int stream_blkdev_issue_flush_noop(uint sid);

extern int stream_commit_tx(uint sid, u64);
#define STREAM_COMMIT_TX(sid, txid) stream_commit_tx(sid, txid);

extern int olayer_register_streams(uint nr, u32 *strs, struct init_param *param);
extern int olayer_unregister_streams(uint nr, u32 *strs, uint nr_devs, struct block_device **);
void olayer_reset_stream(uint sid);

void stream_init_cp(struct stream *s);
void stream_stop_cp(struct stream *s);
int stream_do_checkpoint(struct stream *horae, uint nr_entries);
u32 journal_write_alloc(struct stream *stream, struct bio *src_bio);

void stream_init_journal(struct stream *s);
void stream_stop_journal(struct stream *s);

#ifdef USE_CMB
int init_cmb(struct cmb_info *);
void copy_from_buff_to_dev(struct cmb_info *ci, struct stream *horae, void *data_addr_in_buff,
                           int data_len);
void cmb_write(struct cmb_info *ci, struct stream *horae, void *data_addr_in_buff, int data_len);
void cmb_read(struct cmb_info *ci, struct stream *horae, u32 data_off, int data_len);
void cmb_flush(struct cmb_info *ci, struct stream *s, u32 data_off, int data_len);
int stream_cmb_needs_flush(struct stream *s, int device_id);
#endif

int init_cp_device(void);
int exit_cp_device(void);

int init_submitter(void);
int stop_submitter(void);
int delegate_bio(struct stream *horae, struct bio *sqe, uint submitter_id);
struct bio *bh2bio(int op, int op_flags, struct buffer_head *bh);

void olayer_destroy_stats(struct olayer_info *olayer);
void olayer_destroy_root_stats(void);
int olayer_build_stats(struct olayer_info *olayer);
int olayer_create_root_stats(void);

// recovery.c
extern void debug_stream(uint sid);
void graceful_shutdown(u32 *, u32, struct super_block *sb);
void print_ometa(struct nvme_rw_command_ *o);
int olayer_crash_recovery(struct block_device *bd);

u32 alloc_journal_block(uint sid, struct bio *src_bio, uint nr_blocks);
#endif
