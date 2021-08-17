#include "ext4.h"

void *mqfs_alloc(size_t size, gfp_t flags) {
    void *ptr;

    BUG_ON(size & (size - 1)); /* Must be a power of 2 */
    WARN_ON(size < PAGE_SIZE);

    ptr = (void *)__get_free_pages(flags, get_order(size));

    /* Check alignment; SLUB has gotten this wrong in the past,
     * and this can lead to user data corruption! */
    BUG_ON(((unsigned long)ptr) & (size - 1));

    return ptr;
}

void mqfs_free(void *ptr, size_t size) {
    WARN_ON(size < PAGE_SIZE);
    free_pages((unsigned long)ptr, get_order(size));
}

int copy_out_bh(struct buffer_head *bh_in, struct buffer_head **bh_out) {
    struct buffer_head *new_bh;
    struct page *new_page;
    char *mapped_data;
    char *tmp;
    unsigned int new_offset;

    new_bh = alloc_buffer_head(GFP_NOFS | __GFP_NOFAIL);
    atomic_set(&new_bh->b_count, 1);

    tmp = mqfs_alloc(bh_in->b_size, GFP_NOFS);
    if (!tmp) {
        brelse(new_bh);
        return -ENOMEM;
    }

    lock_buffer(bh_in);

    new_page = bh_in->b_page;
    new_offset = offset_in_page(bh_in->b_data);
    mapped_data = kmap_atomic(new_page);
    memcpy(tmp, mapped_data + new_offset, bh_in->b_size);
    kunmap_atomic(mapped_data);

    unlock_buffer(bh_in);

    new_page = virt_to_page(tmp);
    new_offset = offset_in_page(tmp);

    set_bh_page(new_bh, new_page, new_offset);
    new_bh->b_size = bh_in->b_size;
    new_bh->b_bdev = bh_in->b_bdev;
    new_bh->b_blocknr = bh_in->b_blocknr;
    new_bh->b_private = tmp; // stores the pointer allocated from mqfs_alloc() for further releasing

    *bh_out = new_bh;
    return 0;
}

int release_copy_out_bh(struct buffer_head *bh, size_t size) {
    mqfs_free(bh->b_private, size);
    free_buffer_head(bh);
    return 0;
}