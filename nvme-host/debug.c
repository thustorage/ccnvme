#include <linux/debugfs.h>
#include "nvme.h"

static struct dentry *nvme_host_debugfs_root;

static DEFINE_MUTEX(nvme_host_stat_mutex);
static LIST_HEAD(nvme_host_stat_list);

static int stat_show(struct seq_file *s, void *v)
{
	struct nvme_dev *dev;
	int i;
	mutex_lock(&nvme_host_stat_mutex);
	list_for_each_entry(dev, &nvme_host_stat_list, stat_list)
	{
        seq_printf(s, "%s: ", dev_name(dev->ctrl.device));
        for (i = 0; i < dev->max_qid; i++) {
			// if (dev->queues[i].time_in_queue == dev->queues[i].old_time_in_queue) {
				// seq_printf(s, "#, ");
			// } else {
				// seq_printf(s, "%llu, ", jiffies_to_usecs(dev->queues[i].time_in_queue - dev->queues[i].old_time_in_queue) / dev->iostat_interval);
				seq_printf(s, "%u, ", jiffies_to_usecs(dev->queues[i].time_in_queue));
			// }
        }
        seq_printf(s, "\n");
	}
	mutex_unlock(&nvme_host_stat_mutex);
	return 0;
}

static int stat_open(struct inode *inode, struct file *file)
{
	return single_open(file, stat_show, inode->i_private);
}

static const struct file_operations stat_fops = {
	.owner = THIS_MODULE,
	.open = stat_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

int nvme_host_build_stats(struct nvme_dev *dev)
{
	mutex_lock(&nvme_host_stat_mutex);
	list_add_tail(&dev->stat_list, &nvme_host_stat_list);
	mutex_unlock(&nvme_host_stat_mutex);
	return 0;
}

void nvme_host_destroy_stats(struct nvme_dev *dev)
{
	mutex_lock(&nvme_host_stat_mutex);
	list_del(&dev->stat_list);
	mutex_unlock(&nvme_host_stat_mutex);
}

int __init nvme_create_root_stats(void)
{
	struct dentry *file;

	nvme_host_debugfs_root = debugfs_create_dir("nvme_host", NULL);
	if (!nvme_host_debugfs_root)
		return -ENOMEM;

	file = debugfs_create_file("status", S_IRUGO, nvme_host_debugfs_root,
							   NULL, &stat_fops);
	if (!file)
	{
		debugfs_remove(nvme_host_debugfs_root);
		nvme_host_debugfs_root = NULL;
		return -ENOMEM;
	}

	return 0;
}

void nvme_destroy_root_stats(void)
{
	if (!nvme_host_debugfs_root)
		return;

	debugfs_remove_recursive(nvme_host_debugfs_root);
	nvme_host_debugfs_root = NULL;
}