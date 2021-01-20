#include "buf.h"
#include "spinlock.h"
#include "types.h"

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
        initlock(&b->lock, "buffer");
        bcache.head.next->prev = b;
        bcache.head.next = b;
    }
}

/*
 * Look through buffer cache for block on device dev.
 * If not found, allocate a buffer.
 * In either case, return locked buffer.
 */
static struct buf*
bget(uint32_t dev, uint32_t blockno)
{
    acquire(&bcache.lock);

    // Is the block already cached?
    for (struct buf* b = bcache.head.next; b != &bcache.head; b = b->next) {
        if (b->dev == dev && b->blockno == blockno) {
            b->refcnt++;
            release(&bcache.lock);
            acquire(&b->lock);
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
            acquire(&b->lock);
            return b;
        }
    }

    panic("\tbget: no buffers.\n");
}

/*
 * Return a locked buf with the contents of the indicated block.
 */
struct buf*
bread(uint32_t dev, uint32_t blockno)
{
    struct buf* b = bget(dev, blockno);
    if (!(b->flags & B_VALID)) {
        // TODO:Limplement io_disk_rw (read)
        // io_disk_rw(b, 0);
    }
    return b;
}

/*
 * Write b's contents to disk. Must be locked.
 */
void
bwrite(struct buf* b)
{
    if (!holding(&b->lock)) panic("\tbwrite: buf not locked.\n");
    b->flags |= B_DIRTY;
    // TODO:Limplement io_disk_rw (write)
    // io_disk_rw(b, 1);
}

/*
 * Release a locked buffer.
 * Move to the head of the most-recently-used list.
 */
void
brelease(struct buf* b)
{
    if (!holding(&b->lock)) panic("\tbrelease: buffer not locked.\n");
    release(&b->lock);

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
