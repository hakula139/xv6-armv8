/*
 * Buffer cache.
 *
 * The buffer cache is a linked list of buf structures holding
 * cached copies of disk block contents.  Caching disk blocks
 * in memory reduces the number of disk reads and also provides
 * a synchronization point for disk blocks used by multiple processes.
 *
 * Interface:
 *   To get a buffer for a particular disk block, call bread.
 *   After changing buffer data, call bwrite to write it to disk.
 *   When done with the buffer, call brelse.
 *   Do not use the buffer after calling brelse.
 *   Only one process at a time can use a buffer, so do not keep them longer
 *     than necessary.
 *
 * The implementation uses two state flags internally:
 *   B_VALID: the buffer data has been read from the disk.
 *   B_DIRTY: the buffer data has been modified
 *     and needs to be written to disk.
 */

#include "buf.h"
#include "console.h"
#include "fs.h"
#include "sd.h"
#include "sleeplock.h"
#include "spinlock.h"

struct {
    struct spinlock lock;
    struct buf buf[NBUF];

    // Linked list of all buffers, through prev / next.
    // Sorted by how recently the buffer was used.
    // head.next is the most recent, head.prev is the least.
    struct buf head;
} bcache;

void
binit()
{
    initlock(&bcache.lock, "bcache");

    // Create linked list of buffers
    bcache.head.prev = &bcache.head;
    bcache.head.next = &bcache.head;
    for (struct buf* b = bcache.buf; b < bcache.buf + NBUF; ++b) {
        b->next = bcache.head.next;
        b->prev = &bcache.head;
        initsleeplock(&b->lock, "buffer");
        bcache.head.next->prev = b;
        bcache.head.next = b;
    }

    cprintf("binit: success.\n");
}

/*
 * Look through buffer cache for block on device dev.
 * If not found, allocate a buffer.
 * In either case, return locked buffer.
 */
static struct buf*
bget(uint32_t dev, uint32_t blockno)
{
    cprintf("bget: dev %d blockno %d\n", dev, blockno);
    acquire(&bcache.lock);

    // Is the block already cached?
    for (struct buf* b = bcache.head.next; b != &bcache.head; b = b->next) {
        if (b->dev == dev && b->blockno == blockno) {
            b->refcnt++;
            release(&bcache.lock);
            acquiresleep(&b->lock);
            return b;
        }
    }

    // Not cached.
    // Recycle the least recently used (LRU) unused buffer.
    for (struct buf* b = bcache.head.prev; b != &bcache.head; b = b->prev) {
        if (!b->refcnt && !(b->flags & B_DIRTY)) {
            b->dev = dev;
            b->blockno = blockno;
            b->flags = 0;
            b->refcnt = 1;
            release(&bcache.lock);
            acquiresleep(&b->lock);
            return b;
        }
    }

    panic("\tbget: no buffers.\n");
    return NULL;
}

/*
 * Return a locked buf with the contents of the indicated block.
 */
struct buf*
bread(uint32_t dev, uint32_t blockno)
{
    // Logical block address of the first absolute sector in partition 2,
    // where our file system locates.
    const uint32_t LBA = 0x20800;
    struct buf* b = bget(dev, blockno + LBA);
    if (!(b->flags & B_VALID)) sd_rw(b);
    return b;
}

/*
 * Write b's contents to disk. Must be locked.
 */
void
bwrite(struct buf* b)
{
    if (!holdingsleep(&b->lock)) panic("\tbwrite: buf not locked.\n");
    b->flags |= B_DIRTY;
    sd_rw(b);
}

/*
 * Release a locked buffer.
 * Move to the head of the most-recently-used list.
 */
void
brelse(struct buf* b)
{
    if (!holdingsleep(&b->lock)) panic("\tbrelse: buffer not locked.\n");
    releasesleep(&b->lock);

    acquire(&bcache.lock);
    b->refcnt--;
    if (!b->refcnt) {
        // No one is waiting for it.
        b->next->prev = b->prev;
        b->prev->next = b->next;
        b->next = bcache.head.next;
        b->prev = &bcache.head;
        bcache.head.next->prev = b;
        bcache.head.next = b;
    }
    release(&bcache.lock);
}

void
bpin(struct buf* b)
{
    acquire(&bcache.lock);
    b->refcnt++;
    release(&bcache.lock);
}

void
bunpin(struct buf* b)
{
    acquire(&bcache.lock);
    b->refcnt--;
    release(&bcache.lock);
}
