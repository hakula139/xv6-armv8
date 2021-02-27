# Lab 7: File System and Shell

## 习题解答

### 1. 文件系统

> 请实现文件系统，本实验中的文件系统遵循 xv6 的设计，你也可以从 0 开始设计属于你的文件系统。如果你的文件系统不同于 xv6 的话，请修改 `user/src/mkfs`。你需要添加测试证明你实现的文件系统可以读取到你打包的文件，在数量、内容上是正确的。

#### 1.0 总览

![Layers of the xv6 file system](./assets/file_system.png)

本图引自 *xv6: a simple, Unix-like teaching operating system* [^1]。

文件系统的总体结构参考 Xv6 [^2] 的设计，其 7 层结构如图所示。以下我们将自底向上依次进行阐述。

#### 1.1 Disk

第 1 层是磁盘驱动，作为物理磁盘的抽象层，为操作系统提供了读写磁盘块的方法。我们已在 Lab 6 时在 `kern/sd.c` 中实现，详见 Lab 6 第 2 节。

在这一层中，我们提供了以下方法：

- `sd_init`：初始化 SD 卡并解析主引导记录
- `sd_intr`：处理 SD 卡设备中断
- `sd_rw`：读写 SD 卡磁盘块

#### 1.2 Buffer cache

第 2 层是磁盘块缓存，用于将磁盘块缓存到内存中，从而加快磁盘读写。我们已在 Lab 6 时在 `kern/bio.c` 中实现，详见 Lab 6 第 1 节。

在这一层中，我们提供了以下方法：

- `binit`：初始化 `buf` 队列 `bcache`
- `bread`：从磁盘读取 `buf` 到内存
- `bwrite`：将 `buf` 从内存写入磁盘
- `brelse`：释放一个不在使用中的 `buf`
- `bpin`：将 `buf` 的引用数（`refcnt`）加 `1`，其中引用数表示当前正在等待此 `buf` 的设备数量
- `bunpin`：将 `buf` 的引用数（`refcnt`）减 `1`

#### 1.3 Logging

第 3 层是磁盘改动日志，用于维护文件系统的崩溃一致性（crash consistency），确保写磁盘的事务是原子（atomic）的。我们将在 `kern/log.c` 中实现。

在这一层中，我们提供了以下方法：

- `initlog`：初始化 `log`
- `log_write`：作为 `bwrite` 的代理，在 `log` 中记录需要被写入磁盘的 block 的标号，并标记为 dirty，之后统一写入
- `begin_op`：开始文件系统调用
- `end_op`：结束文件系统调用

##### 1.3.1 `initlog`

函数 `initlog` 的主要工作是根据 super block 中的信息对 `log` 进行初始化，然后调用函数 `recover_from_log`，根据 log header 恢复崩溃前未写入到磁盘的数据。

```c {.line-numbers}
// kern/log.c

void
initlog(int dev)
{
    if (sizeof(struct logheader) >= BSIZE)
        panic("\tinitlog: logheader is too big.\n");

    struct superblock sb;
    initlock(&log.lock, "log");
    readsb(dev, &sb);
    log.start = sb.logstart;
    log.size = sb.nlog;
    log.dev = dev;
    recover_from_log();
}
```

其中，super block 保存了磁盘的布局信息，详见注释：

```c {.line-numbers}
// inc/fs.h

/*
 * Disk layout:
 * [boot block | super block | log | inode blocks | free bit map | data blocks]
 *
 * mkfs computes the super block and builds an initial file system.
 * The super block describes the disk layout:
 */
struct superblock {
    uint32_t size;        // Size of file system image (blocks)
    uint32_t nblocks;     // Number of data blocks
    uint32_t ninodes;     // Number of inodes
    uint32_t nlog;        // Number of log blocks
    uint32_t logstart;    // Block number of first log block
    uint32_t inodestart;  // Block number of first inode block
    uint32_t bmapstart;   // Block number of first free map block
};
```

我们先调用函数 `readsb` 读取 super block。

```c {.line-numbers}
// kern/fs.c

/*
 * Read the super block.
 */
void
readsb(int dev, struct superblock* sb)
{
    struct buf* b = bread(dev, 1);
    memmove(sb, b->data, sizeof(*sb));
    brelse(b);
}
```

然后我们利用 super block 中的信息初始化 `log`，其中 `log` 的结构如下所示：

```c {.line-numbers}
// kern/log.c

/*
 * Contents of the header block, used for both the on-disk header block
 * and to keep track in memory of logged block # before commit.
 */
struct logheader {
    int n;
    int block[LOGSIZE];
};

struct log {
    struct spinlock lock;
    int start;
    int size;
    int outstanding;  // How many FS sys calls are executing.
    int committing;   // In commit(), please wait.
    int dev;
    struct logheader lh;
} log;
```

这里 log header 保存的是已 commit 的 block 的**标号**。

`log` 初始化完毕后，我们调用函数 `recover_from_log` 进行磁盘的恢复工作，以维护磁盘的崩溃一致性。

函数 `recover_from_log` 首先调用函数 `read_head` 将磁盘中的 log header 读取到内存，然后调用函数 `install_trans` 根据 log header 将已 commit 的 block 写入到磁盘，最后清空内存中的 log header，并调用函数 `write_head` 清空磁盘中的 log header。

```c {.line-numbers}
// kern/log.c

static void
recover_from_log()
{
    read_head();
    install_trans();  // if committed, copy from log to disk
    log.lh.n = 0;
    write_head();  // clear the log
}
```

具体来说，函数 `read_head` 先读取磁盘中的 log header，并将其复制到内存中的 `log` 结构里。

```c {.line-numbers}
// kern/log.c

/*
 * Read the log header from disk into the in-memory log header.
 */
static void
read_head()
{
    struct buf* buf = bread(log.dev, log.start);
    struct logheader* lh = (struct logheader*)(buf->data);
    log.lh.n = lh->n;
    for (int i = 0; i < log.lh.n; ++i) log.lh.block[i] = lh->block[i];
    brelse(buf);
}
```

随后，函数 `install_trans` 根据 log header 中 block 的标号，将磁盘中已 commit 但还未写入到磁盘的 block 的内容复制到一个 `buf` 里，然后将它写入到磁盘。

```c {.line-numbers}
// kern/log.c

/*
 * Copy committed blocks from log to their home location.
 */
static void
install_trans()
{
    for (int i = 0; i < log.lh.n; ++i) {
        struct buf* log_buf = bread(log.dev, log.start + i + 1);
        struct buf* dst_buf = bread(log.dev, log.lh.block[i]);
        memmove(dst_buf->data, log_buf->data, BSIZE);
        brelse(log_buf);
        bwrite(dst_buf);
        brelse(dst_buf);
    }
}
```

最后，将内存中的 log header 清空，并调用函数 `write_head` 将其写入到磁盘，从而将磁盘中的 log header 也清空。

```c {.line-numbers}
// kern/log.c

/*
 * Write in-memory log header to disk.
 * This is the true point at which the
 * current transaction commits.
 */
static void
write_head()
{
    struct buf* buf = bread(log.dev, log.start);
    struct logheader* lh = (struct logheader*)(buf->data);
    lh->n = log.lh.n;
    for (int i = 0; i < log.lh.n; ++i) lh->block[i] = log.lh.block[i];
    bwrite(buf);
    brelse(buf);
}
```

##### 1.3.2 `log_write`

函数 `log_write` 的主要工作是在内存中的 log header 里记录需要被写入磁盘的 block 的标号，并标记这个 block 对应的 `buf` 为 dirty，以固定在 `bcache` 中，不会因 LRU 算法被意外淘汰。这些 block 将在之后被统一连续写入磁盘，从而提高效率。

```c {.line-numbers}
// kern/log.c

/*
 * Caller has modified b->data and is done with the buffer.
 * Record the block number and pin in the cache with B_DIRTY.
 * commit() / write_log() will do the disk write.
 *
 * log_write() replaces bwrite(); a typical use is:
 *   bp = bread(...)
 *   modify bp->data[]
 *   log_write(bp)
 *   brelse(bp)
 */
void
log_write(struct buf* b)
{
    if (log.lh.n >= LOGSIZE || log.lh.n >= log.size - 1)
        panic("\tlog_write: transaction is too big.\n");
    if (log.outstanding < 1) panic("\tlog_write: outside of transaction.\n");

    acquire(&log.lock);
    int i = 0;
    for (; i < log.lh.n; ++i) {
        if (log.lh.block[i] == b->blockno) break;  // log absorption
    }
    if (i == log.lh.n) {
        log.lh.block[i] = b->blockno;
        ++log.lh.n;
    }
    b->flags |= B_DIRTY;  // prevent eviction
    release(&log.lock);
}
```

这里引入了 absorption 的优化机制。当在一个事务中多次写入同一个 block 时，在 log header 中仅记录一次这个 block 的标号，从而节省 log header 的空间，并提高效率。

##### 1.3.3 `begin_op`

函数 `begin_op` 的主要工作是在事务开始前，等待 `log` 空闲（不处于正在 commit 的状态）且可用（log header 有足够的空间保存新的待写 block 的标号），然后才允许开始本次文件系统调用。

```c {.line-numbers}
// kern/log.c

/*
 * Called at the start of each FS system call.
 */
void
begin_op()
{
    acquire(&log.lock);
    while (1) {
        if (log.committing) {
            sleep(&log, &log.lock);
        } else if (log.lh.n + (log.outstanding + 1) * MAXOPBLOCKS > LOGSIZE) {
            // This op might exhaust log space; wait for commit.
            sleep(&log, &log.lock);
        } else {
            ++log.outstanding;
            release(&log.lock);
            break;
        }
    }
}
```

##### 1.3.4 `end_op`

函数 `end_op` 的主要工作是在事务结束后，将 log header 中标记的 block 统一连续写入磁盘，并唤醒函数 `begin_op` 中等待 `log` 空闲且可用的文件系统调用。

```c {.line-numbers}
// kern/log.c

/*
 * Called at the end of each FS system call.
 * Commits if this was the last outstanding operation.
 */
void
end_op()
{
    int do_commit = 0;

    acquire(&log.lock);
    --log.outstanding;
    if (log.committing) panic("\tend_op: log is committing.\n");
    if (!log.outstanding) {
        do_commit = 1;
        log.committing = 1;
    } else {
        // begin_op() may be waiting for log space, and decrementing
        // log.outstanding has decreased the amount of reserved space.
        wakeup(&log);
    }
    release(&log.lock);

    if (do_commit) {
        commit();
        acquire(&log.lock);
        log.committing = 0;
        wakeup(&log);
        release(&log.lock);
    }
}
```

具体来说，先判断当前事务是否已结束（即 outstanding 的系统调用数量为 `0`）。如果是，则调用函数 `commit` 进行写磁盘操作。

函数 `commit` 首先调用函数 `write_log` 将待 commit 的 block 写入到磁盘中 `log` 对应的位置，接着调用函数 `write_head` 将内存中的 log header 写入到磁盘，然后调用函数 `install_trans` 根据 log header 将已 commit 的 block 写入到磁盘，最后清空内存中的 log header，并调用函数 `write_head` 清空磁盘中的 log header。

```c {.line-numbers}
// kern/log.c

static void
commit()
{
    if (log.lh.n > 0) {
        write_log();
        write_head();
        install_trans();
        log.lh.n = 0;
        write_head();  // erase the transaction from the log
    }
}
```

具体来说，函数 `write_log` 根据 log header 中 block 的标号，将待 commit 的 block 写入到磁盘中 `log` 对应的位置。

```c {.line-numbers}
// kern/log.c

/*
 * Copy modified blocks from cache to log.
 */
static void
write_log()
{
    for (int i = 0; i < log.lh.n; ++i) {
        struct buf* log_buf = bread(log.dev, log.start + i + 1);
        struct buf* cache_buf = bread(log.dev, log.lh.block[i]);
        memmove(log_buf->data, cache_buf->data, BSIZE);
        brelse(cache_buf);
        bwrite(log_buf);
        brelse(log_buf);
    }
}
```

随后，函数 `write_head` 将内存中的 log header 写入到磁盘，此时事务被 commit。

```c {.line-numbers}
// kern/log.c

/*
 * Write in-memory log header to disk.
 * This is the true point at which the
 * current transaction commits.
 */
static void
write_head()
{
    struct buf* buf = bread(log.dev, log.start);
    struct logheader* lh = (struct logheader*)(buf->data);
    lh->n = log.lh.n;
    for (int i = 0; i < log.lh.n; ++i) lh->block[i] = log.lh.block[i];
    bwrite(buf);
    brelse(buf);
}
```

之后的写磁盘过程同 1.3.1 节中函数 `recover_from_log` 的后半段。

#### 1.4 Inode

第 4 层是索引节点（inode），包含了文件的元信息，用于描述文件系统对象。我们将在 `kern/fs.c` 中实现。

在这一层中，我们提供了以下方法：

- `iinit`：初始化 `inode` 和 `icache`
- `ialloc`：分配一个 `inode`
- `iupdate`：将内存中的 `inode` 写入到磁盘
- `idup`：将 `inode` 的引用数（`ref`）加 `1`，其中引用数表示当前内存中指向这个 `inode` 的指针数量
- `ilock`：给 `inode` 加锁，需要时从磁盘中读取 `inode`
- `iunlock`：给 `inode` 解锁
- `iput`：当 `inode` 的引用数（`ref`）为 `1` 时，清空并释放该 `inode`，否则将其引用数减 `1`
- `iunlockput`：`iunlock` + `iput` 的别名
- `stati`：复制 `inode` 的元数据到 `stat`
- `readi`：从 `inode` 中读取数据
- `writei`：写入数据到 `inode`

##### 1.4.1 `iinit`

函数 `iinit` 的主要工作是初始化 `icache` 和 `inode` 的锁。

```c {.line-numbers}
// kern/fs.c

void
iinit(int dev)
{
    initlock(&icache.lock, "icache");
    for (int i = 0; i < NINODE; ++i)
        initsleeplock(&icache.inode[i].lock, "inode");

    readsb(dev, &sb);
    cprintf(
        "super block: size %d nblocks %d ninodes %d nlog %d logstart %d inodestart %d bmapstart %d\n",
        sb.size, sb.nblocks, sb.ninodes, sb.nlog, sb.logstart, sb.inodestart,
        sb.bmapstart);
}
```

其中，`icache` 的结构如下所示：

```c {.line-numbers}
// kern/fs.c

struct {
    struct spinlock lock;
    struct inode inode[NINODE];
} icache;
```

`inode` 的结构如下所示：

![The representation of a file on disk](./assets/dinode.png)

本图引自 *xv6: a simple, Unix-like teaching operating system* [^1]。

```c {.line-numbers}
// inc/file.h

/*
 * In-memory copy of an inode.
 */
struct inode {
    uint32_t dev;           // Device number
    uint32_t inum;          // Inode number
    int ref;                // Reference count
    struct sleeplock lock;  // Protects everything below here
    int valid;              // Inode has been read from disk?

    uint16_t type;  // Copy of disk inode
    uint16_t major;
    uint16_t minor;
    uint16_t nlink;
    uint32_t size;
    uint32_t addrs[NDIRECT + 1];
};
```

##### 1.4.2 `ialloc`

函数 `ialloc` 的主要工作是在磁盘中找到一个未分配的 `inode`（`type` 为 `0`），然后将它的 `type` 设置为给定的文件类型，表示已分配，最后调用函数 `iget`，返回这个 `inode` 在内存中的拷贝。

```c {.line-numbers}
// kern/fs.c

/*
 * Allocate an inode on device dev.
 *
 * Mark it as allocated by giving it type type.
 * Returns an unlocked but allocated and referenced inode.
 */
struct inode*
ialloc(uint32_t dev, uint16_t type)
{
    for (int inum = 1; inum < sb.ninodes; ++inum) {
        struct buf* bp = bread(dev, IBLOCK(inum, sb));
        struct dinode* dip = (struct dinode*)bp->data + inum % IPB;
        if (!dip->type) {  // a free inode
            memset(dip, 0, sizeof(*dip));
            dip->type = type;
            log_write(bp);  // mark it allocated on the disk
            brelse(bp);
            return iget(dev, inum);
        }
        brelse(bp);
    }
    panic("\tialloc: no inodes.\n");
}
```

其中，函数 `iget` 先在 `icache` 中根据标号（`inum`）寻找对应的 `inode`。如果找到，则将其引用数（`ref`）加 `1` 并返回，否则在 `icache` 中回收一个空闲的 cache entry 给这个 `inode`，将其引用数（`ref`）设置为 `1` 并返回。

```c {.line-numbers}
// kern/fs.c

/*
 * Find the inode with number inum on device dev
 * and return the in-memory copy. Does not lock
 * the inode and does not read it from disk.
 */
static struct inode*
iget(uint32_t dev, uint32_t inum)
{
    acquire(&icache.lock);

    // Is the inode already cached?
    struct inode* empty = NULL;
    for (int i = 0; i < NINODE; ++i) {
        struct inode* ip = &icache.inode[i];
        if (ip->ref > 0 && ip->dev == dev && ip->inum == inum) {
            ip->ref++;
            release(&icache.lock);
            return ip;
        }
        if (!empty && !ip->ref) empty = ip;  // remember empty slot
    }

    // Recycle an inode cache entry.
    if (!empty) panic("\tiget: no inodes.\n");

    struct inode* ip = empty;
    ip->dev = dev;
    ip->inum = inum;
    ip->ref = 1;
    ip->valid = 0;
    release(&icache.lock);
    return ip;
}
```

##### 1.4.3 `iupdate`

函数 `iupdate` 的主要工作是将内存中的 `inode` 写入到磁盘。由于我们的 `icache` 采用直写（write-through）模式，因此每当 `inode` 有字段被修改，就需要调用一次函数 `iupdate` 进行写回操作。

```c {.line-numbers}
// kern/fs.c

/*
 * Copy a modified in-memory inode to disk.
 *
 * Must be called after every change to an ip->xxx field
 * that lives on disk, since i-node cache is write-through.
 * Caller must hold ip->lock.
 */
void
iupdate(struct inode* ip)
{
    struct buf* bp = bread(ip->dev, IBLOCK(ip->inum, sb));
    struct dinode* dip = (struct dinode*)bp->data + ip->inum % IPB;
    dip->type = ip->type;
    dip->major = ip->major;
    dip->minor = ip->minor;
    dip->nlink = ip->nlink;
    dip->size = ip->size;
    memmove(dip->addrs, ip->addrs, sizeof(ip->addrs));
    log_write(bp);
    brelse(bp);
}
```

##### 1.4.4 `idup`

函数 `idup` 的主要工作是将 `inode` 的引用数（`ref`）加 `1`，其中引用数表示当前内存中指向这个 `inode` 的指针数量。

```c {.line-numbers}
// kern/fs.c

/*
 * Increment reference count for ip.
 * Returns ip to enable ip = idup(ip1) idiom.
 */
struct inode*
idup(struct inode* ip)
{
    acquire(&icache.lock);
    ip->ref++;
    release(&icache.lock);
    return ip;
}
```

##### 1.4.5 `ilock`

函数 `ilock` 的主要工作是给指定的 `inode` 加锁。如果当前 `inode` 不在内存中（即 `valid` 为 `0`），则从磁盘中读取，并将 `valid` 设置为 `1`。

```c {.line-numbers}
// kern/fs.c

/*
 * Lock the given inode.
 * Reads the inode from disk if necessary.
 */
void
ilock(struct inode* ip)
{
    if (!ip || ip->ref < 1) panic("\tilock: invalid inode.\n");

    acquiresleep(&ip->lock);
    if (!ip->valid) {
        struct buf* bp = bread(ip->dev, IBLOCK(ip->inum, sb));
        struct dinode* dip = (struct dinode*)bp->data + ip->inum % IPB;
        ip->type = dip->type;
        if (!ip->type) {
            brelse(bp);
            panic("\tilock: no type.\n");
        }
        ip->major = dip->major;
        ip->minor = dip->minor;
        ip->nlink = dip->nlink;
        ip->size = dip->size;
        memmove(ip->addrs, dip->addrs, sizeof(ip->addrs));
        ip->valid = 1;
        brelse(bp);
    }
}
```

##### 1.4.6 `iunlock`

函数 `iunlock` 的主要工作是给指定的 `inode` 解锁。

```c {.line-numbers}
// kern/fs.c

/*
 * Unlock the given inode.
 */
void
iunlock(struct inode* ip)
{
    if (!ip || !holdingsleep(&ip->lock) || ip->ref < 1)
        panic("\tiunlock: invalid inode.\n");
    releasesleep(&ip->lock);
}
```

##### 1.4.7 `iput`

函数 `iput` 的主要工作是当 `inode` 的引用数（`ref`）为 `1` 时，调用函数 `itrunc` 清空并释放该 `inode` 的内容，然后调用函数 `iupdate` 更新磁盘中的 `inode`；否则将其引用数减 `1`。

```c {.line-numbers}
// kern/fs.c

/*
 * Drop a reference to an in-memory inode.
 *
 * If that was the last reference, the inode cache entry can
 * be recycled.
 * If that was the last reference and the inode has no links
 * to it, free the inode (and its content) on disk.
 * All calls to iput() must be inside a transaction in
 * case it has to free the inode.
 */
void
iput(struct inode* ip)
{
    acquire(&icache.lock);
    if (ip->ref == 1 && ip->valid && !ip->nlink) {
        // ip->ref == 1 means no other process can have ip locked,
        // so this acquiresleep() won't block (or deadlock).
        acquiresleep(&ip->lock);
        release(&icache.lock);

        // inode has no links and no other references: truncate and free.
        itrunc(ip);
        ip->type = 0;
        iupdate(ip);
        ip->valid = 0;

        releasesleep(&ip->lock);
        acquire(&icache.lock);
    }

    ip->ref--;
    release(&icache.lock);
}
```

这里函数 `itrunc` 用于清空 `inode` 的内容，包括 `NDIRECT` 个直接索引磁盘块（direct block）和 `NINDIRECT` 个间接索引磁盘块（indirect block）。

```c {.line-numbers}
// kern/fs.c

/*
 * Truncate inode (discard contents).
 *
 * Only called when the inode has no links
 * to it (no directory entries referring to it)
 * and has no in-memory reference to it (is
 * not an open file or current directory).
 */
static void
itrunc(struct inode* ip)
{
    for (int i = 0; i < NDIRECT; ++i) {
        if (ip->addrs[i]) {
            bfree(ip->dev, ip->addrs[i]);
            ip->addrs[i] = 0;
        }
    }

    if (ip->addrs[NDIRECT]) {
        struct buf* bp = bread(ip->dev, ip->addrs[NDIRECT]);
        uint32_t* a = (uint32_t*)bp->data;
        for (int j = 0; j < NINDIRECT; ++j) {
            if (a[j]) bfree(ip->dev, a[j]);
        }
        brelse(bp);
        bfree(ip->dev, ip->addrs[NDIRECT]);
        ip->addrs[NDIRECT] = 0;
    }

    ip->size = 0;
    iupdate(ip);
}
```

其中，函数 `bfree` 用于释放一个 block，将其在 bitmap 中标记为未使用。

```c {.line-numbers}
// kern/fs.c

/*
 * Free a disk block.
 */
static void
bfree(int dev, uint32_t b)
{
    struct buf* bp = bread(dev, BBLOCK(b, sb));
    int bi = b % BPB;
    int m = 1 << (bi % 8);
    if (!(bp->data[bi / 8] & m)) panic("\tbfree: freeing a free block.\n");
    bp->data[bi / 8] &= ~m;
    log_write(bp);
    brelse(bp);
}
```

##### 1.4.8 `iunlockput`

函数 `iunlockput` 是 `iunlock` + `iput` 的别名。

```c {.line-numbers}
// kern/fs.c

/*
 * Common idiom: unlock, then put.
 */
void
iunlockput(struct inode* ip)
{
    iunlock(ip);
    iput(ip);
}
```

##### 1.4.9 `stati`

函数 `stati` 的主要工作是复制 `inode` 的元数据（metadata）到 `stat` 结构，届时用户程序可以通过 `stat` 系统调用读取。

```c {.line-numbers}
// kern/fs.c

/*
 * Copy stat information from inode.
 * Caller must hold ip->lock.
 */
void
stati(struct inode* ip, struct stat* st)
{
    // FIXME: Support other fields in stat.
    st->st_dev = ip->dev;
    st->st_ino = ip->inum;
    st->st_nlink = ip->nlink;
    st->st_size = ip->size;

    switch (ip->type) {
    case T_FILE: st->st_mode = S_IFREG; break;
    case T_DIR: st->st_mode = S_IFDIR; break;
    case T_DEV: st->st_mode = 0; break;
    default: panic("\tstati: unexpected stat type %d.\n", ip->type);
    }
}
```

##### 1.4.10 `readi`

函数 `readi` 的主要工作是从 `inode` 中读取数据。具体来说，先确保数据的读取范围在文件内，然后利用函数 `bmap` 定位文件中 block 的地址并读取到 `buf`，接着将数据从 `buf` 复制到目标地址 `dst`，最后返回成功读取的 block 数量。

```c {.line-numbers}
// kern/fs.c

/*
 * Read data from inode.
 * Caller must hold ip->lock.
 */
ssize_t
readi(struct inode* ip, char* dst, size_t off, size_t n)
{
    if (ip->type == T_DEV) {
        if (ip->major < 0 || ip->major >= NDEV || !devsw[ip->major].read)
            return -1;
        return devsw[ip->major].read(ip, dst, n);
    }

    if (off > ip->size || off + n < off) return -1;
    if (off + n > ip->size) n = ip->size - off;

    for (size_t tot = 0, m = 0; tot < n; tot += m, off += m, dst += m) {
        struct buf* bp = bread(ip->dev, bmap(ip, off / BSIZE));
        m = min(n - tot, BSIZE - off % BSIZE);
        memmove(dst, bp->data + off % BSIZE, m);
        brelse(bp);
    }
    return n;
}
```

这里函数 `bmap` 根据 block 的标号找到其对应的地址并返回，其中 direct block 的地址位于数组 `ip->addrs` 中，indirect block 的地址位于 `ip->addrs[NDIRECT]` 指向的 block 所保存的数组中。如果发现找不到对应的 block，则调用函数 `balloc` 分配一个新的 block。

```c {.line-numbers}
// kern/fs.c

/*
 * Inode content
 *
 * The content (data) associated with each inode is stored
 * in blocks on the disk. The first NDIRECT block numbers
 * are listed in ip->addrs[].  The next NINDIRECT blocks are
 * listed in block ip->addrs[NDIRECT].
 *
 * Return the disk block address of the nth block in inode ip.
 * If there is no such block, bmap allocates one.
 */
static uint32_t
bmap(struct inode* ip, uint32_t bn)
{
    if (bn < NDIRECT) {
        // Load direct block, allocating if necessary.
        uint32_t addr = ip->addrs[bn];
        if (!addr) ip->addrs[bn] = addr = balloc(ip->dev);
        return addr;
    }
    bn -= NDIRECT;

    if (bn < NINDIRECT) {
        // Load indirect block, allocating if necessary.
        uint32_t addr = ip->addrs[NDIRECT];
        if (!addr) ip->addrs[NDIRECT] = addr = balloc(ip->dev);
        struct buf* bp = bread(ip->dev, addr);
        uint32_t* a = (uint32_t*)bp->data;
        addr = a[bn];
        if (!addr) {
            a[bn] = addr = balloc(ip->dev);
            log_write(bp);
        }
        brelse(bp);
        return addr;
    }
    panic("\tbmap: out of range.\n");
}
```

其中，函数 `balloc` 根据 block 在 bitmap 中所对应的位，遍历所有 block 找到一个可用的 block，将其在 bitmap 中标记为使用中，并调用函数 `bzero` 清空此 block。

```c {.line-numbers}
// kern/fs.c

/*
 * Allocate a zeroed disk block.
 */
static uint32_t
balloc(uint32_t dev)
{
    for (int b = 0; b < sb.size; b += BPB) {
        struct buf* bp = bread(dev, BBLOCK(b, sb));
        for (int bi = 0; bi < BPB && b + bi < sb.size; ++bi) {
            int m = 1 << (bi % 8);
            if (!(bp->data[bi / 8] & m)) {  // Is block free?
                bp->data[bi / 8] |= m;      // Mark block in use.
                log_write(bp);
                brelse(bp);
                bzero(dev, b + bi);
                return b + bi;
            }
        }
        brelse(bp);
    }
    panic("\tballoc: out of blocks.\n");
}
```

函数 `bzero` 用于清空一个 block。

```c {.line-numbers}
// kern/fs.c

/*
 * Zero a block.
 */
static void
bzero(int dev, int bno)
{
    struct buf* b = bread(dev, bno);
    memset(b->data, 0, BSIZE);
    log_write(b);
    brelse(b);
}
```

##### 1.4.11 `writei`

函数 `writei` 的主要工作是写入数据到 `inode`。具体来说，先确保数据的写入起始地址在文件内，且写入结束地址不超过最大文件大小 `MAXFILE * BSIZE`，然后利用函数 `bmap` 定位文件中 block 的地址并读取到 `buf`，接着将数据从源地址 `src` 复制到 `buf`，并调用函数 `log_write` 加入写磁盘队列，最后返回成功写入的 block 数量。其中，如果写入的 block 数量超过文件大小，文件将自动扩容，最后需要更新此文件的大小，并调用函数 `iupdate` 写入到磁盘。

```c {.line-numbers}
// kern/fs.c

/*
 * Write data to inode.
 * Caller must hold ip->lock.
 */
ssize_t
writei(struct inode* ip, char* src, size_t off, size_t n)
{
    if (ip->type == T_DEV) {
        if (ip->major < 0 || ip->major >= NDEV || !devsw[ip->major].write)
            return -1;
        return devsw[ip->major].write(ip, src, n);
    }

    if (off > ip->size || off + n < off) return -1;
    if (off + n > MAXFILE * BSIZE) return -1;

    for (size_t tot = 0, m = 0; tot < n; tot += m, off += m, src += m) {
        struct buf* bp = bread(ip->dev, bmap(ip, off / BSIZE));
        m = min(n - tot, BSIZE - off % BSIZE);
        memmove(bp->data + off % BSIZE, src, m);
        log_write(bp);
        brelse(bp);
    }

    if (n > 0 && off > ip->size) {
        ip->size = off;
        iupdate(ip);
    }
    return n;
}
```

#### 1.5 Directory

第 5 层是目录，用于组织文件系统的层次结构（hierarchy）。助教已在 `kern/fs.c` 中实现，由于时间有限，这里就不详细阐述了。

在这一层中，我们提供了以下方法：

- `namecmp`：按字典序比较两个目录名的大小
- `dirlookup`：在一个目录下查找指定名称的文件夹
- `dirlink`：在一个目录下新建指定名称的文件夹

#### 1.6 Pathname

第 6 层是路径，用于以字符串表示一个文件或文件夹在文件系统中的位置。助教已在 `kern/fs.c` 中实现，由于时间有限，这里就不详细阐述了。

在这一层中，我们提供了以下方法：

- `namei`：查找指定路径的文件或文件夹
- `nameiparent`：查找指定路径的父文件夹

#### 1.7 File descriptor

第 7 层是文件描述符，以非负整数的形式，表示一个已打开文件（或管道、socket 等，一切皆文件！）的引用。内核为每个进程维护了一个进程级文件表（file table），同时在全局维护了一个系统级文件表（global file table，或 `ftable`），包含了所有打开的文件，文件描述符实际就是这个表的索引。我们将在 `kern/file.c` 中实现。由于时间有限，我们目前仅支持普通文件。

在这一层中，我们提供了以下方法：

- `fileinit`：初始化 `ftable`
- `filealloc`：分配一个新文件
- `filedup`：将文件的引用数（`ref`）加 `1`
- `fileclose`：将文件的引用数（`ref`）减 `1`，当引用数降到 `0` 时关闭文件
- `filestat`：读取文件的元数据
- `fileread`：从文件读取数据
- `filewrite`：写入数据到文件

##### 1.7.1 `fileinit`

函数 `fileinit` 的主要工作是初始化 `ftable` 的锁。

```c {.line-numbers}
// kern/file.c

void
fileinit()
{
    initlock(&ftable.lock, "ftable");
}
```

其中，`ftable` 的结构如下所示：

```c {.line-numbers}
// kern/file.c

struct {
    struct spinlock lock;
    struct file file[NFILE];
} ftable;
```

`file` 的结构如下所示：

```c {.line-numbers}
// inc/file.h

struct file {
    enum { FD_NONE, FD_PIPE, FD_INODE } type;
    int ref;
    char readable;
    char writable;
    struct pipe* pipe;
    struct inode* ip;
    size_t off;
};
```

##### 1.7.2 `filealloc`

函数 `filealloc` 的主要工作是在 `ftable` 中找到一个未使用的文件（`ref` 为 `0`），然后将它标记为使用中并返回。

```c {.line-numbers}
// kern/file.c

/*
 * Allocate a file structure.
 */
struct file*
filealloc()
{
    acquire(&ftable.lock);
    for (struct file* f = ftable.file; f < ftable.file + NFILE; ++f) {
        if (!f->ref) {
            f->ref = 1;
            release(&ftable.lock);
            return f;
        }
    }
    release(&ftable.lock);
    return NULL;
}
```

##### 1.7.3 `filedup`

函数 `filedup` 的主要工作是将文件的引用数（`ref`）加 `1`，表示创建一个此文件的引用拷贝。

```c {.line-numbers}
// kern/file.c

/*
 * Increment ref count for file f.
 */
struct file*
filedup(struct file* f)
{
    acquire(&ftable.lock);
    if (f->ref < 1) panic("\tfiledup: invalid file.\n");
    f->ref++;
    release(&ftable.lock);
    return f;
}
```

##### 1.7.4 `fileclose`

函数 `fileclose` 的主要工作是将文件的引用数（`ref`）减 `1`；当引用数降到 `0` 时，对于普通文件，调用函数 `iput` 关闭文件（暂不支持其他文件类型）。

```c {.line-numbers}
// kern/file.c

/*
 * Close file f. (Decrement ref count, close when reaches 0.)
 */
void
fileclose(struct file* f)
{
    acquire(&ftable.lock);
    if (f->ref < 1) panic("\tfileclose: invalid file.\n");
    if (--f->ref > 0) {
        release(&ftable.lock);
        return;
    }

    struct file ff = *f;
    f->ref = 0;
    f->type = FD_NONE;
    release(&ftable.lock);

    if (ff.type == FD_INODE) {
        begin_op();
        iput(ff.ip);
        end_op();
    } else {
        panic("\tfileclose: unsupported type.\n");
    }
}
```

##### 1.7.5 `filestat`

函数 `filestat` 的主要工作是调用函数 `stati` 读取文件的元数据。

```c {.line-numbers}
// kern/file.c

/*
 * Get metadata about file f.
 */
int
filestat(struct file* f, struct stat* st)
{
    if (f->type == FD_INODE) {
        ilock(f->ip);
        stati(f->ip, st);
        iunlock(f->ip);
        return 0;
    }
    return -1;
}
```

##### 1.7.6 `fileread`

函数 `fileread` 的主要工作是对于普通文件，调用函数 `readi` 从文件中读取数据（暂不支持其他文件类型）。

```c {.line-numbers}
// kern/file.c

/*
 * Read from file f.
 */
ssize_t
fileread(struct file* f, char* addr, ssize_t n)
{
    if (!f->readable) return -1;
    if (f->type == FD_INODE) {
        ilock(f->ip);
        int r = readi(f->ip, addr, f->off, n);
        if (r > 0) f->off += r;
        iunlock(f->ip);
        return r;
    }
    panic("\tfileread: unsupported type.\n");
}
```

##### 1.7.7 `filewrite`

函数 `filewrite` 的主要工作是对于普通文件，调用函数 `writei` 写入数据到文件（暂不支持其他文件类型）。

```c {.line-numbers}
// kern/file.c

/*
 * Write to file f.
 */
ssize_t
filewrite(struct file* f, char* addr, ssize_t n)
{
    if (!f->writable) return -1;
    if (f->type == FD_INODE) {
        // Write a few blocks at a time to avoid exceeding the maximum log
        // transaction size, including i-node, indirect block, allocation
        // blocks, and 2 blocks of slop for non-aligned writes. This really
        // belongs lower down, since writei() might be writing a device like the
        // console.
        int max = ((MAXOPBLOCKS - 4) / 2) * 512;
        int i = 0;
        while (i < n) {
            int n1 = n - i;
            if (n1 > max) n1 = max;

            begin_op();
            ilock(f->ip);
            int r = writei(f->ip, addr + i, f->off, n1);
            if (r > 0) f->off += r;
            iunlock(f->ip);
            end_op();

            if (r < 0) break;
            if (r != n1) panic("\tfilewrite: partial data written.\n");
            i += r;
        }
        return i == n ? n : -1;
    }
    panic("\tfilewrite: unsupported type.\n");
}
```

### 2. 系统调用

> 请修改 `syscall.c` 以及 `trapasm.S` 来接上 musl，或者修改 Makefile 并搬运 xv6 的简易 libc，从而允许用户态程序通过调用系统调用来操作文件系统。

### 3. Shell

> 我们已经把 xv6 的 shell 搬运到了 `user/src/sh` 目录下，但需要实现 brk 系统调用来使用 malloc，你也可以自行实现一个简单的 shell。请在 `user/src/cat` 中实现 cat 命令并在你的 shell 中执行。

### 4. 测试（可选）

> 文件系统最重要的能力是在系统崩溃和恢复的时候不会出现数据不一致的情况。请你设计测试来验证文件系统的崩溃一致性。

## 运行结果

```bash
> make qemu
```

```text
```

## 测试环境

- OS: Ubuntu 18.04.5 LTS (5.4.72-microsoft-standard-WSL2)
- Compiler: gcc version 8.4.0 (Ubuntu/Linaro 8.4.0-1ubuntu1~18.04)
  - Target: aarch64-linux-gnu
- Debugger: GNU gdb 8.2 (Ubuntu 8.2-0ubuntu1~18.04)
  - Target: aarch64-linux-gnu
- Emulator: QEMU emulator version 5.0.50
- Using GNU Make 4.1

[^1]: [xv6: a simple, Unix-like teaching operating system - MIT](https://pdos.csail.mit.edu/6.828/2020/xv6/book-riscv-rev1.pdf)  
[^2]: [mit-pdos/xv6-public: xv6 OS - GitHub](https://github.com/mit-pdos/xv6-public)
