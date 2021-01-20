# Lab 6: Driver and LibC

## 习题解答

### 1. I/O 框架

#### 1.1 请求队列

> 请补全 `inc/buf.h` 以便于 SD 卡驱动中请求队列的实现，即每个请求都是一个 `buf`，所有请求排成一队。

这里直接参考了 Xv6 [^1] 和 Xv6 for RISC-V [^2] 的设计，`struct buf` 的代码如下，具体作用参见注释：

```c {.line-numbers}
// inc/buf.h

struct buf {
    int flags;
    uint32_t dev;          // device
    uint32_t blockno;      // block number
    uint8_t data[BSIZE];   // storing data
    uint32_t refcnt;       // the number of waiting devices
    struct spinlock lock;  // when locked, waiting for driver to wake it up
    struct buf* prev;      // less recent buffer
    struct buf* next;      // more recent buffer
};
```

对于一个请求（`buf`）队列，我们在文件 `kern/bio.c` 中定义了 `bcache`，其代码如下：

```c {.line-numbers}
// kern/bio.c

struct {
    struct spinlock lock;
    struct buf buf[NBUF];

    // Linked list of all buffers, through prev / next.
    // Sorted by how recently the buffer was used.
    // head.next is the most recent, head.prev is the least.
    struct buf head;
} bcache;
```

本质上，`bcache` 是一个带哨兵节点的双向循环链表。这里 `NBUF` 的值定义为 `30`，表示请求队列长度的最大值。

在使用前，我们先对 `bcache` 进行初始化，也就是创建一个双向循环链表，并初始化每个 `buf` 的锁。

```c {.line-numbers}
// kern/bio.c

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
```

当我们需要进行读操作时，我们先调用函数 `bget` 在 `bcache` 中获取一个可用的 `buf`。如果这个请求已经在 `bcache` 里了，那么就将相应的 `buf` 加锁并返回；否则，我们回收一个最早使用过的（Least Recently Used, LRU）且当前不在使用中的 `buf`，然后将这个 `buf` 加锁并返回。

```c {.line-numbers}
// kern/bio.c

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
```

随后，我们调用函数 `bread` 对这个 `buf` 进行读操作。由于目前我们还未实现文件系统，因此暂时不做实际的 I/O 操作。

```c {.line-numbers}
// kern/bio.c

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
```

当我们需要进行写操作时，我们调用函数 `bwrite` 对这个 `buf` 进行写操作。同理，我们暂时不做实际的 I/O 操作。

```c {.line-numbers}
// kern/bio.c

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
```

最后，当我们需要释放一个 `buf` 时，我们调用函数 `brelease` 将它的 `refcnt` 减 1。如果此时 `refcnt` 的值为 0，说明已经没有设备在等待这个 `buf` 了，那么我们就将这个 `buf` 移动到 `bcache` 的头部（实际是 `head->next`），表示这是一个最晚使用过的（Most Recently Used, MRU）`buf`。

```c {.line-numbers}
// kern/bio.c

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
```

### 2. 块设备驱动

#### 2.1 Sleep 实现

> 请完成 `kern/proc.c` 中的 `sleep` 和 `wakeup` 函数，并简要描述并分析你的设计。

#### 2.2 SD 卡初始化

> 请完成 `kern/sd.c` 中的 `sd_init`, `sd_intr`, `sdrw`，然后分别在合适的地方调用 `sd_init` 和 `sd_test` 完成 SD 卡初始化并通过测试。

### 3. 制作启动盘

#### 3.1 获取分区信息

> 请在 `sd_init` 中解析 MBR 获得第二分区起始块的 LBA 和分区大小以便后续使用。

## 测试环境

- OS: Ubuntu 18.04.5 LTS (5.4.72-microsoft-standard-WSL2)
- Compiler: gcc version 8.4.0 (Ubuntu/Linaro 8.4.0-1ubuntu1~18.04)
  - Target: aarch64-linux-gnu
- Debugger: GNU gdb 8.2 (Ubuntu 8.2-0ubuntu1~18.04)
  - Target: aarch64-linux-gnu
- Emulator: QEMU emulator version 5.0.50
- Using GNU Make 4.1

[^1]: mit-pdos/xv6-public: xv6 OS - GitHub  
[^2]: mit-pdos/xv6-riscv: Xv6 for RISC-V - GitHub
