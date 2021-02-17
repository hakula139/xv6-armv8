# Lab 6: Driver and LibC

## 习题解答

### 1. I/O 框架

#### 1.1 请求队列

> 请补全 `inc/buf.h` 以便于 SD 卡驱动中请求队列的实现，即每个请求都是一个 `buf`，所有请求排成一队。

这里直接参考了 Xv6 [^1] 和 Xv6 for RISC-V [^2] 的设计，具体作用参见注释：

```c {.line-numbers}
// inc/buf.h

struct buf {
    int flags;
    uint32_t dev;           // device
    uint32_t blockno;       // block number
    uint8_t data[BSIZE];    // storing data
    uint32_t refcnt;        // the number of waiting devices
    struct sleeplock lock;  // when locked, waiting for driver to release
    struct buf* prev;       // less recent buffer
    struct buf* next;       // more recent buffer
};
```

对于一个请求（`buf`）队列，我们在 `kern/bio.c` 中定义了 `bcache`。

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
        initsleeplock(&b->lock, "buffer");
        bcache.head.next->prev = b;
        bcache.head.next = b;
    }
}
```

当我们需要进行读操作时，我们先调用函数 `bget` 在 `bcache` 中获取一个可用的 `buf`。如果这个请求已经在 `bcache` 里了，那么就将相应的 `buf` 的 `refcnt` 加 `1`，然后加锁并返回；否则，我们回收一个最早使用过的（Least Recently Used, LRU）且当前不在使用中的 `buf`，将其 `refcnt` 设置为 `1`，然后加锁并返回。

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
}
```

随后，我们调用函数 `bread` 对这个 `buf` 进行读操作。这里函数 `sd_rw` 负责 SD 卡的读写操作，详情将在之后提到。

```c {.line-numbers}
// kern/bio.c

/*
 * Return a locked buf with the contents of the indicated block.
 */
struct buf*
bread(uint32_t dev, uint32_t blockno)
{
    struct buf* b = bget(dev, blockno);
    if (!(b->flags & B_VALID)) sd_rw(b);
    return b;
}
```

当我们需要进行写操作时，我们调用函数 `bwrite` 对这个 `buf` 进行写操作。

```c {.line-numbers}
// kern/bio.c

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
```

最后，当我们需要释放一个 `buf` 时，我们调用函数 `brelse` 将它的 `refcnt` 减 `1`。如果此时 `refcnt` 的值为 `0`，说明已经没有设备在等待这个 `buf` 了，那么我们就将这个 `buf` 移动到 `bcache` 的头部（实际是 `head->next`），表示这是一个最晚使用过的（Most Recently Used, MRU）`buf`。

```c {.line-numbers}
// kern/bio.c

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
```

此外，我们还定义了函数 `bpin` 和 `bunpin`，分别用于将一个 `buf` 的 `refcnt` 加 `1` 或减 `1`。

```c {.line-numbers}
// kern/bio.c

void
bpin(struct buf* b)
{
    acquire(&bcache.lock);
    b->refcnt++;
    release(&bcache.lock);
}
```

```c {.line-numbers}
// kern/bio.c

void
bunpin(struct buf* b)
{
    acquire(&bcache.lock);
    b->refcnt--;
    release(&bcache.lock);
}
```

### 2. 块设备驱动

#### 2.1 Sleep 实现

> 请完成 `kern/proc.c` 中的 `sleep` 和 `wakeup` 函数，并简要描述并分析你的设计。

函数 `sleep` 的工作是释放进程所持有的锁，设置进程状态为 SLEEPING，并在 `chan` 上睡眠，然后调用函数 `sched` 回到 `scheduler`，决定下一个运行的程序；当进程被唤醒且再次轮到本进程执行时，重新获取进程本来持有的锁。由于我们在执行 `sleep` 前会先获取进程锁 `p->lock`，而执行 `wakeup` 时同样需要先获取进程锁，因此我们可以确定在执行 `sleep` 的过程中，进程不会被意外 `wakeup`，导致这个 `wakeup` 没有被捕获到，进程永远不再醒来。

```c {.line-numbers}
// kern/proc.c

/*
 * Atomically release lock and sleep on chan.
 * Reacquires lock when awakened.
 */
void
sleep(void* chan, struct spinlock* lk)
{
    struct proc* p = thiscpu->proc;

    // Must acquire p->lock in order to
    // change p->state and then call sched.
    // Once we hold p->lock, we can be
    // guaranteed that we won't miss any wakeup
    // (wakeup locks p->lock),
    // so it's okay to release lk.

    acquire(&p->lock);
    release(lk);

    // Go to sleep.
    p->chan = chan;
    p->state = SLEEPING;
    sched();

    // Tidy up.
    p->chan = 0;

    // Reacquire original lock.
    release(&p->lock);
    acquire(lk);
}
```

函数 `wakeup` 的工作是将指定 `chan` 上睡眠的进程全部唤醒，设置进程状态为 RUNNABLE，从而可以被 `scheduler` 调度。

```c {.line-numbers}
// kern/proc.c

/*
 * Wake up all processes sleeping on chan.
 * Must be called without any p->lock.
 */
void
wakeup(void* chan)
{
    for (struct proc* p = ptable.proc; p < &ptable.proc[NPROC]; ++p) {
        if (p != thiscpu->proc) {
            acquire(&p->lock);
            if (p->state == SLEEPING && p->chan == chan) {
                p->state = RUNNABLE;
            }
            release(&p->lock);
        }
    }
}
```

#### 2.2 SD 卡初始化

> 请完成 `kern/sd.c` 中的 `sd_init`, `sd_intr`, `sd_rw`，然后分别在合适的地方调用 `sd_init` 和 `sd_test` 完成 SD 卡初始化并通过测试。

我研究了半天，最后还是觉得要修改其他源代码才能比较优雅地实现这几个函数，否则结构实在太乱了，复用性也很差。而且引用的这个[源代码](https://github.com/moizumi99/RPiHaribote/blob/master/sdcard.c)的代码风格简直不忍直视，看得恶心，最后还是先全部简单改了一遍。完全改过来还是算了，style fix 约等于 refactor，像日志输出全是乱打的。强烈建议每一位 C / C++ 程序员在写代码前先通览一遍 [Google C++ Style Guide](https://google.github.io/styleguide/cppguide.html) 或者其他随便什么靠谱的 style guide，最起码要保证前后的格式是统一的，不要想怎么写就怎么写。

##### 2.2.1 SD 卡读写磁盘：`sd_rw`

为了优雅地实现 `sd_init`，我们决定先实现 `sd_rw`。

这里我们就直接在函数 `_sd_start` 里修改了，因为它其实已经实现了 SD 卡的写操作，我们只需在它的基础上增加一个读操作即可（**直接读到** `buf->data`）。根据 `buf` 的 `flags` 中 `B_DIRTY` 位的值，我们可以判断此时应当采取读操作（`0`）还是写操作（`1`）。

```c {.line-numbers}
// kern/sd.c

/*
 * Start the request for b. Caller must hold sdlock.
 */
static void
_sd_start(struct buf* b)
{
    // Address is different depending on the card type.
    // HC passes address as block number.
    // SC passes address straight through.
    int blockno = sd_card.type == SD_TYPE_2_HC ? b->blockno : b->blockno << 9;
    int write = b->flags & B_DIRTY;
    int cmd = write ? IX_WRITE_SINGLE : IX_READ_SINGLE;

    cprintf(
        "_sd_start: CPU %d, flag 0x%x, blockno %d, write=%d.\n", cpuid(),
        b->flags, blockno, write);

    // Ensure that any data operation has completed before doing the transfer.
    disb();
    asserts(
        !*EMMC_INTERRUPT,
        "\tEMMC ERROR: Interrupt flag should be empty: 0x%x.\n",
        *EMMC_INTERRUPT);

    *EMMC_BLKSIZECNT = BSIZE;

    int resp = _sd_send_command_a(cmd, blockno);
    asserts(!resp, "\tEMMC ERROR: Send command error.\n");

    uint32_t* intbuf = (uint32_t*)b->data;
    asserts(
        !((uint64_t)b->data & 0x3), "\tOnly support word-aligned buffers.\n");

    if (write) {
        resp = _sd_wait_for_interrupt(INT_WRITE_RDY);
        asserts(!resp, "\tEMMC ERROR: Timeout waiting for ready to write.\n");
        asserts(
            !*EMMC_INTERRUPT,
            "\tEMMC ERROR: Interrupt flag should be empty: 0x%x.\n",
            *EMMC_INTERRUPT);
        for (int done = 0; done < BSIZE / 4; ++done) {
            *EMMC_DATA = intbuf[done];
        }
    } else {
        resp = _sd_wait_for_interrupt(INT_READ_RDY);
        asserts(!resp, "\tEMMC ERROR: Timeout waiting for ready to read.\n");
        asserts(
            !*EMMC_INTERRUPT,
            "\tEMMC ERROR: Interrupt flag should be empty: 0x%x.\n",
            *EMMC_INTERRUPT);
        for (int done = 0; done < BSIZE / 4; ++done) {
            intbuf[done] = *EMMC_DATA;
        }
    }

    resp = _sd_wait_for_interrupt(INT_DATA_DONE);
    asserts(!resp, "\tEMMC ERROR: Timeout waiting for data done.\n");
}
```

在函数 `sd_rw` 里，我们先调用函数 `_sd_start` 对 `buf` 进行 I/O 操作，然后将其 `flags` 的 `B_DIRTY` 位设置为 `0`、`B_VALID` 位设置为 `1`，最后调用函数 `brelse` 释放 `buf`（见 1.1 节）。

```c {.line-numbers}
// kern/sd.c

/*
 * Sync buf with disk.
 * If B_DIRTY is set, write buf to disk, clear B_DIRTY, set B_VALID.
 * Else if B_VALID is not set, read buf from disk, set B_VALID.
 */
void
sd_rw(struct buf* b)
{
    acquiresleep(&b->lock);
    _sd_start(b);
    b->flags &= ~B_DIRTY;
    b->flags |= B_VALID;
    brelse(b);
}
```

##### 2.2.2 SD 卡中断处理：`sd_intr`

函数 `sd_intr` 的工作是处理设备中断。具体来说，检查请求是否已执行完毕，然后清空中断信息。

```c {.line-numbers}
// kern/sd.c

/*
 * The interrupt handler.
 */
void
sd_intr()
{
    int i = *EMMC_INTERRUPT;
    if (!(i & INT_DATA_DONE)) {
        cprintf("\tsd_intr: Unexpected SD interrupt.\n");
        return;
    }
    *EMMC_INTERRUPT = i;  // Clear interrupt
    disb();
}
```

我稍微解释一下为什么只有这么点内容。简单来说是因为大多数事情已经被其他函数干掉了，具体来说：

1. 不需要检查 `bcache` 是否为空，因为我们不需要对 `bcache` 中的 `buf` 进行读写，原因见 2.
2. 不需要检查中断是否可能为 `INT_READ_RDY`，因为我们在函数 `_sd_start` 里已经完成了读取到 `buf->data` 的操作，它只能为 `INT_DATA_DONE`
3. 不需要获取 `bcache` 的队首 `buf`，原因同 1.
4. 不需要判断请求类型，并据此判断中断是否合法，原因同 2.
5. 不需要将数据读取到 `buf->data`，原因同 2.
6. 不需要设置 `buf->flags`，因为我们在函数 `sd_rw` 里完成了这项操作
7. 不需要唤醒 `buf`，因为我们在函数 `brelse` 里完成了这项操作
8. 不知道是否需要继续进行下一个 `buf` 的读写操作，因为测试代码里似乎没有对函数 `sd_intr` 的测试，我不清楚文件系统里会怎么调用这个 `sd_intr`
   - 毕竟文件系统是我自己设计的，而我大概不太会采用样例里的这个中断处理逻辑
   - 目前我的 `bcache` 不需要手动维护其结构，如果需要一个清空 `buf` 队列的操作（我猜 `sd_intr` 可能想干这个事情），到时候我可以直接在 `bcache` 提供一个方法 `bclear`，思路是循环调用 `sd_rw`，并不需要让 `sd_intr` 去处理这个事情
   - 还是要看实际的应用场景，在有函数需要用到 `sd_intr` 前我不打算写这一部分，反正这个到时候加起来很快的

##### 2.2.3 SD 卡初始化：`sd_init`

函数 `sd_init` 的工作分为两部分：

1. 初始化 SD 卡
2. 解析主引导记录（Master Boot Record, MBR）

对于第一部分，我们只需调用函数 `binit` 初始化 `bcache`，以及调用函数 `_sd_init` 初始化 SD 卡即可。

对于第二部分，我们先从磁盘地址 `0x0` 处读取 MBR 到 `buf`；然后利用函数 `_parse_partition_entry` 解析每条磁盘分区表项（partition table entry）并输出其内容，其中 4 条分区表的地址分别为 `0x1BE`, `0x1CE`, `0x1DE`, `0x1EE`；最后输出 2 字节的结束标志（若为 `55`, `AA` 则表示 MBR 有效）。[^3]

```c {.line-numbers}
// kern/sd.c

/*
 * Initialize SD card and parse MBR.
 * 1. The first partition should be FAT and is used for booting.
 * 2. The second partition is used by our file system.
 *
 * See https://en.wikipedia.org/wiki/Master_boot_record
 */
void
sd_init()
{
    /*
     * Initialize the lock and request queue if any.
     * Remember to call sd_init() at somewhere.
     */

    binit();
    _sd_init();
    asserts(sd_card.init, "\tFailed to initialize SD card.\n");

    /*
     * Read and parse 1st block (MBR) and collect whatever
     * information you want.
     */

    struct buf mbr;
    memset(&mbr, 0, sizeof(mbr));
    sd_rw(&mbr);
    asserts((uint32_t)mbr.flags & B_VALID, "\tMBR is not valid.\n");

    uint8_t* partitions = mbr.data + 0x1BE;
    for (int i = 0; i < 4; ++i) {
        _parse_partition_entry(partitions + (i << 4), i + 1);
    }

    uint8_t* ending = mbr.data + 0x1FE;
    cprintf("sd_init: Boot signature: %x %x\n", ending[0], ending[1]);
    asserts(ending[0] == 0x55 && ending[1] == 0xAA, "\tMBR is not valid.\n");

    cprintf("sd_init: success.\n");
}
```

其中，函数 `_parse_partition_entry` 负责解析各分区表项，并输出以下信息 [^3]：

1. 磁盘状态（state）
2. 分区内第一个扇区的柱面-磁头-扇区（cylinder-head-sector, CHS）地址
3. 分区类型（partition type）
4. 分区内最后一个扇区的柱面-磁头-扇区（cylinder-head-sector, CHS）地址
5. 分区内第一个扇区的逻辑区块地址（logical block address, LBA）
6. 分区内的总扇区数（number of sectors）

```c {.line-numbers}
// kern/sd.c

static void
_parse_partition_entry(uint8_t* entry, int id)
{
    cprintf("sd_init: Partition %d: ", id);
    for (int i = 0; i < 16; ++i) {
        uint8_t byte = entry[i];
        cprintf("%x%x ", byte >> 4, byte & 0xf);
    }
    cprintf("\n");

    uint8_t status = entry[0];
    cprintf("- Status: %d\n", status);

    uint8_t head = entry[1];
    uint8_t sector = entry[2] & 0x3f;
    uint16_t cylinder = (((uint16_t)entry[2] & 0xc0) << 8) | entry[3];
    cprintf(
        "- CHS address of first absolute sector: head=%d, sector=%d, cylinder=%d\n",
        head, sector, cylinder);

    uint8_t partition_type = entry[4];
    cprintf("- Partition type: %d\n", partition_type);

    head = entry[5];
    sector = entry[6] & 0x3f;
    cylinder = (((uint16_t)entry[6] & 0xc0) << 8) | entry[7];
    cprintf(
        "- CHS address of last absolute sector: head=%d, sector=%d, cylinder=%d\n",
        head, sector, cylinder);

    uint32_t lba = _parse_uint32_t(&entry[8]);
    cprintf("- LBA of first absolute sector: 0x%x\n", lba);

    uint32_t sectorno = _parse_uint32_t(&entry[12]);
    cprintf("- Number of sectors: %d\n", sectorno);
}
```

这里函数 `_parse_uint32_t` 用于将 4 个字节（`uint8_t`）按照小端法（little-endian）合并为一个 32 位的整数（`uint32_t`）。

```c {.line-numbers}
// kern/sd.c

static uint32_t
_parse_uint32_t(uint8_t* bytes)
{
    return (((uint32_t)bytes[3]) << 24) | (((uint32_t)bytes[2]) << 16)
           | (((uint32_t)bytes[1]) << 8) | (((uint32_t)bytes[0]) << 0);
}
```

##### 2.2.4 SD 卡初始化及测试

在函数 `main` 中，调用函数 `sd_init` 完成 SD 卡初始化，调用函数 `sd_test` 进行测试。

```c {.line-numbers}
// kern/main.c

void
main()
{
    if (cpuid() == 0) {
        // ...
        sd_init();
        sd_test();
        // ...
    }
    // ...
}
```

### 3. 制作启动盘

#### 3.1 获取分区信息

> 请在 `sd_init` 中解析 MBR 获得第二分区起始块的 LBA 和分区大小以便后续使用。

见 2.2.3 节。由于暂时不知道要以何种形式用到这些信息，我们暂时不做函数返回之类的处理，而只是将相关信息直接输出到终端。

## 运行结果

```bash
> make qemu
```

```text
console_init: success.
main: [CPU 0] init started.
alloc_init: success.
proc_init: success.
irq_init: success.
timer_init: success at CPU 0.
proc_alloc: proc 1 success.
user_init: proc 1 (initproc) success.
- mbox write: 0x7fd38
- mbox read: 0x7fd38
- clock rate: 50000000
- SD base clock rate from mailbox: 50000000
- Reset the card.
- Divisor selected = 104, shift count = 6
- EMMC: Set clock, status 0x1ff0000 CONTROL1: 0xe6807
- Send IX_GO_IDLE_STATE command.
- Send command response: 0
- EMMC: Sending ACMD41 SEND_OP_COND status 1ff0000
- Divisor selected = 2, shift count = 0
- EMMC: Set clock, status 0x1ff0000 CONTROL1: 0xe0207
- EMMC: SD Card Type 2 SC 128Mb UHS-I 0 mfr 170 'XY:QEMU!' r0.1 2/2006, #deadbeef RCA 4567
sd_init: Partition 1: 00 20 21 00 0c 49 01 08 00 08 00 00 00 00 02 00 
- Status: 0
- CHS address of first absolute sector: head=32, sector=33, cylinder=0
- Partition type: 12
- CHS address of last absolute sector: head=73, sector=1, cylinder=8
- LBA of first absolute sector: 0x800
- Number of sectors: 131072
sd_init: Partition 2: 00 49 02 08 83 51 01 10 00 08 02 00 00 f8 01 00 
- Status: 0
- CHS address of first absolute sector: head=73, sector=2, cylinder=8
- Partition type: 131
- CHS address of last absolute sector: head=81, sector=1, cylinder=16
- LBA of first absolute sector: 0x20800
- Number of sectors: 129024
sd_init: Partition 3: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 
- Status: 0
- CHS address of first absolute sector: head=0, sector=0, cylinder=0
- Partition type: 0
- CHS address of last absolute sector: head=0, sector=0, cylinder=0
- LBA of first absolute sector: 0x0
- Number of sectors: 0
sd_init: Partition 4: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 
- Status: 0
- CHS address of first absolute sector: head=0, sector=0, cylinder=0
- Partition type: 0
- CHS address of last absolute sector: head=0, sector=0, cylinder=0
- LBA of first absolute sector: 0x0
- Number of sectors: 0
sd_init: Boot signature: 55 aa
sd_init: success.
sd_test: begin nblocks 2048
sd_test: sd check rw...
sd_test: read 1048576 B (1 MB), t: 58641006 cycles, speed: 1.0 MB/s
sd_test: write 1048576 B (1 MB), t: 63637462 cycles, speed: 0.9 MB/s
main: [CPU 0] init success.
main: [CPU 3] init started.
main: [CPU 2] init started.
timer_init: success at CPU 3.
main: [CPU 1] init started.
main: [CPU 3] init success.
timer_init: success at CPU 2.
main: [CPU 2] init success.
timer_init: success at CPU 1.
scheduler: switch to proc 1 at CPU 3.
main: [CPU 1] init success.
syscall: proc 1 calls syscall 0.
sys_exec: executing /init with parameters: /init 
syscall: proc 1 calls syscall 1.
sys_exit: in exit.
```

## 测试环境

- OS: Ubuntu 18.04.5 LTS (5.4.72-microsoft-standard-WSL2)
- Compiler: gcc version 8.4.0 (Ubuntu/Linaro 8.4.0-1ubuntu1~18.04)
  - Target: aarch64-linux-gnu
- Debugger: GNU gdb 8.2 (Ubuntu 8.2-0ubuntu1~18.04)
  - Target: aarch64-linux-gnu
- Emulator: QEMU emulator version 5.0.50
- Using GNU Make 4.1

[^1]: [mit-pdos/xv6-public: xv6 OS - GitHub](https://github.com/mit-pdos/xv6-public)  
[^2]: [mit-pdos/xv6-riscv: Xv6 for RISC-V - GitHub](https://github.com/mit-pdos/xv6-riscv)  
[^3]: [Master boot record - Wikipedia](https://en.wikipedia.org/wiki/Master_boot_record)
