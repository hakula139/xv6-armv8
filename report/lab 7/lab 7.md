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
// kern/fs.h

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
    struct buf* b;
    b = bread(dev, 1);
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

#### 1.5 Directory

#### 1.6 Pathname

#### 1.7 File descriptor

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
