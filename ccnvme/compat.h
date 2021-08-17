#ifndef _COMPAT_H
#define _COMPAT_H
#include <linux/blk_types.h>
#include <linux/writeback.h>

#define REQ_ORDERED (1ULL << (__REQ_NR_BITS + 1))
#define REQ_BARRIER (1ULL << (__REQ_NR_BITS + 2))
#define REQ_TX (1ULL << (__REQ_NR_BITS + 3))
#define REQ_TX_COMMIT (1ULL << (__REQ_NR_BITS + 4))

#define WB_ORDERED_ALL (WB_SYNC_ALL + 1)
#define WB_BARRIER_ALL (WB_SYNC_ALL + 2)
#define WB_TX_WRITE (WB_SYNC_ALL + 3)

#endif