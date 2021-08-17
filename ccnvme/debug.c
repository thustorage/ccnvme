#include <linux/debugfs.h>
#include "horae.h"

static struct dentry *olayer_debugfs_root;

static DEFINE_MUTEX(olayer_stat_mutex);
static LIST_HEAD(olayer_stat_list);

extern unsigned int nr_submitters;
extern unsigned int nr_streams;

static int stat_show(struct seq_file *s, void *v) {
    struct olayer_info *olayer;
    struct stream *stream;
    int i, j;
    mutex_lock(&olayer_stat_mutex);
    list_for_each_entry(olayer, &olayer_stat_list, stat_list) {
        for (i = 0; i < nr_streams; i++) {
            stream = &olayer->stream[i];
            seq_printf(s, "Stream %u, delegated bio %llu, done by self %llu\n", stream->stream_id,
                       stream->delegated_bio, stream->done_by_self_bio);
        }

        for (j = 0; j < nr_submitters; j++) {
            seq_printf(s, "submitter %d, # of todo bios %llu\n", j,
                       horae_count(olayer->submitter[j].ring.head, olayer->submitter[j].ring.tail,
                                   olayer->submitter[j].ring.len));
        }

#ifdef OLAYER_ANALYSIS
        if (olayer->total_count == 0) continue;
        seq_printf(s,
            "Time olayer core %llu ns, cmb trans %llu ns, cmb flush %llu ns, delegated %llu ns\n",
            olayer->t_olayer_core / olayer->total_count, olayer->t_cmb_trans / olayer->total_count,
            olayer->t_cmb_flush / olayer->total_count,
            olayer->t_delegate_bio / olayer->total_count);
        seq_printf(s, "Time truncate %llu ns, swap %llu ns\n",
                   olayer->t_truncate / olayer->total_count, olayer->t_swap / olayer->total_count);
        seq_printf(s, "Time cas %llu ns\n", olayer->t_cas / olayer->total_count);
#endif
    }
    mutex_unlock(&olayer_stat_mutex);
    return 0;
}

static int stat_open(struct inode *inode, struct file *file) {
    return single_open(file, stat_show, inode->i_private);
}

static const struct file_operations stat_fops = {
    .owner = THIS_MODULE,
    .open = stat_open,
    .read = seq_read,
    .llseek = seq_lseek,
    .release = single_release,
};

int olayer_build_stats(struct olayer_info *olayer) {
    mutex_lock(&olayer_stat_mutex);
    list_add_tail(&olayer->stat_list, &olayer_stat_list);
    mutex_unlock(&olayer_stat_mutex);
    return 0;
}

void olayer_destroy_stats(struct olayer_info *olayer) {
    mutex_lock(&olayer_stat_mutex);
    list_del(&olayer->stat_list);
    mutex_unlock(&olayer_stat_mutex);
}

int __init olayer_create_root_stats(void) {
    struct dentry *file;

    olayer_debugfs_root = debugfs_create_dir("olayer", NULL);
    if (!olayer_debugfs_root) return -ENOMEM;

    file = debugfs_create_file("status", S_IRUGO, olayer_debugfs_root, NULL, &stat_fops);
    if (!file) {
        debugfs_remove(olayer_debugfs_root);
        olayer_debugfs_root = NULL;
        return -ENOMEM;
    }

    return 0;
}

void olayer_destroy_root_stats(void) {
    if (!olayer_debugfs_root) return;

    debugfs_remove_recursive(olayer_debugfs_root);
    olayer_debugfs_root = NULL;
}