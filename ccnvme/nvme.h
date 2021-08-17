/*
 * Copyright (c) 2011-2014, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#ifndef _NVME_H
#define _NVME_H

#include <linux/nvme.h>
#include <linux/cdev.h>
#include <linux/pci.h>
#include <linux/kref.h>
#include <linux/blk-mq.h>
#include <linux/lightnvm.h>
#include <linux/sed-opal.h>
#include <linux/fault-inject.h>
#include <linux/rcupdate.h>

enum nvme_ctrl_state {
    NVME_CTRL_NEW,
    NVME_CTRL_LIVE,
    NVME_CTRL_ADMIN_ONLY,    /* Only admin queue live */
    NVME_CTRL_RESETTING,
    NVME_CTRL_CONNECTING,
    NVME_CTRL_DELETING,
    NVME_CTRL_DEAD,
};

struct nvme_ctrl {
    enum nvme_ctrl_state state;
    bool identified;
    spinlock_t lock;
    const struct nvme_ctrl_ops *ops;
    struct request_queue *admin_q;
    struct request_queue *connect_q;
    struct device *dev;
    int instance;
    struct blk_mq_tag_set *tagset;
    struct blk_mq_tag_set *admin_tagset;
    struct list_head namespaces;
    struct rw_semaphore namespaces_rwsem;
    struct device ctrl_device;
    struct device *device;	/* char device */
    struct cdev cdev;
    struct work_struct reset_work;
    struct work_struct delete_work;

    struct nvme_subsystem *subsys;
    struct list_head subsys_entry;

    struct opal_dev *opal_dev;

    char name[12];
    u16 cntlid;

    u32 ctrl_config;
    u16 mtfa;
    u32 queue_count;

    u64 cap;
    u32 page_size;
    u32 max_hw_sectors;
    u32 max_segments;
    u16 oncs;
    u16 oacs;
    u16 nssa;
    u16 nr_streams;
    atomic_t abort_limit;
    u8 vwc;
    u32 vs;
    u32 sgls;
    u16 kas;
    u8 npss;
    u8 apsta;
    u32 oaes;
    u32 aen_result;
    unsigned int shutdown_timeout;
    unsigned int kato;
    bool subsystem;
    unsigned long quirks;
    struct nvme_id_power_state psd[32];
    struct nvme_effects_log *effects;
    struct work_struct scan_work;
    struct work_struct async_event_work;
    struct delayed_work ka_work;
    struct nvme_command ka_cmd;
    struct work_struct fw_act_work;
    unsigned long events;

    /* Power saving configuration */
    u64 ps_max_latency_us;
    bool apst_enabled;

    /* PCIe only: */
    u32 hmpre;
    u32 hmmin;
    u32 hmminds;
    u16 hmmaxd;

    /* Fabrics only */
    u16 sqsize;
    u32 ioccsz;
    u32 iorcsz;
    u16 icdoff;
    u16 maxcmd;
    int nr_reconnects;
    struct nvmf_ctrl_options *opts;
};

struct nvme_queue {
    struct device *q_dmadev;
    struct nvme_dev *dev;
    spinlock_t sq_lock;
    struct nvme_command *sq_cmds;
    struct nvme_command __iomem *sq_cmds_io;
    spinlock_t cq_lock ____cacheline_aligned_in_smp;
    volatile struct nvme_completion *cqes;
    struct blk_mq_tags **tags;
    dma_addr_t sq_dma_addr;
    dma_addr_t cq_dma_addr;
    u32 __iomem *q_db;
    u16 q_depth;
    s16 cq_vector;
    u16 sq_tail;
    u16 cq_head;
    u16 last_cq_head;
    u16 qid;
    u8 cq_phase;
    u32 *dbbuf_sq_db;
    u32 *dbbuf_cq_db;
    u32 *dbbuf_sq_ei;
    u32 *dbbuf_cq_ei;
};

struct nvme_dev {
    struct nvme_queue *queues;
    struct blk_mq_tag_set tagset;
    struct blk_mq_tag_set admin_tagset;
    u32 __iomem *dbs;
    struct device *dev;
    struct dma_pool *prp_page_pool;
    struct dma_pool *prp_small_pool;
    unsigned online_queues;
    unsigned max_qid;
    unsigned int num_vecs;
    int q_depth;
    u32 db_stride;
    void __iomem *bar;
    unsigned long bar_mapped_size;
    struct work_struct remove_work;
    struct mutex shutdown_lock;
    bool subsystem;
    void __iomem *cmb;
    pci_bus_addr_t cmb_bus_addr;
    u64 cmb_size;
    u32 cmbsz;
    u32 cmbloc;
    struct nvme_ctrl ctrl;
    struct completion ioq_wait;

    mempool_t *iod_mempool;

    /* shadow doorbell buffer support: */
    u32 *dbbuf_dbs;
    dma_addr_t dbbuf_dbs_dma_addr;
    u32 *dbbuf_eis;
    dma_addr_t dbbuf_eis_dma_addr;

    /* host memory buffer support: */
    u64 host_mem_size;
    u32 nr_host_mem_descs;
    dma_addr_t host_mem_descs_dma;
    struct nvme_host_mem_buf_desc *host_mem_descs;
    void **host_mem_desc_bufs;
};

static u64 nvme_cmb_size_unit(struct nvme_dev *dev)
{
    u8 szu = (dev->cmbsz >> NVME_CMBSZ_SZU_SHIFT) & NVME_CMBSZ_SZU_MASK;

    return 1ULL << (12 + 4 * szu);
}

static u32 nvme_cmb_size(struct nvme_dev *dev)
{
    return (dev->cmbsz >> NVME_CMBSZ_SZ_SHIFT) & NVME_CMBSZ_SZ_MASK;
}

#endif /* _NVME_H */
