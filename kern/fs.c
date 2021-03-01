/*
 * File system implementation.  Five layers:
 *   Blocks: allocator for raw disk blocks.
 *   Log: crash recovery for multi-step updates.
 *   Files: inode allocator, reading, writing, metadata.
 *   Directories: inode with special contents (list of other inodes!)
 *   Names: paths like /usr/rtm/xv6/fs.c for convenient naming.
 *
 * This file contains the low-level file system manipulation
 * routines.  The (higher-level) system call implementations
 * are in sysfile.c.
 */

#include "fs.h"
#include "buf.h"
#include "console.h"
#include "file.h"
#include "log.h"
#include "mmu.h"
#include "proc.h"
#include "sleeplock.h"
#include "spinlock.h"
#include "string.h"
#include "types.h"

#define min(a, b) ((a) < (b) ? (a) : (b))

static void itrunc(struct inode*);

// There should be one superblock per disk device,
// but we run with only one device.
struct superblock sb;

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

/* Blocks. */

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
    return 0;
}

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

/*
 * Inodes.
 *
 * An inode describes a single unnamed file.
 * The inode disk structure holds metadata: the file's type,
 * its size, the number of links referring to it, and the
 * list of blocks holding the file's content.
 *
 * The inodes are laid out sequentially on disk at
 * sb.startinode. Each inode has a number, indicating its
 * position on the disk.
 *
 * The kernel keeps a cache of in-use inodes in memory
 * to provide a place for synchronizing access
 * to inodes used by multiple processes. The cached
 * inodes include book-keeping information that is
 * not stored on disk: ip->ref and ip->valid.
 *
 * An inode and its in-memory representation go through a
 * sequence of states before they can be used by the
 * rest of the file system code.
 *
 * * Allocation: an inode is allocated if its type (on disk)
 *   is non-zero. ialloc() allocates, and iput() frees if
 *   the reference and link counts have fallen to zero.
 *
 * * Referencing in cache: an entry in the inode cache
 *   is free if ip->ref is zero. Otherwise ip->ref tracks
 *   the number of in-memory pointers to the entry (open
 *   files and current directories). iget() finds or
 *   creates a cache entry and increments its ref; iput()
 *   decrements ref.
 *
 * * Valid: the information (type, size, &c) in an inode
 *   cache entry is only correct when ip->valid is 1.
 *   ilock() reads the inode from
 *   the disk and sets ip->valid, while iput() clears
 *   ip->valid if ip->ref has fallen to zero.
 *
 * * Locked: file system code may only examine and modify
 *   the information in an inode and its content if it
 *   has first locked the inode.
 *
 * Thus a typical sequence is:
 *   ip = iget(dev, inum)
 *   ilock(ip)
 *   ... examine and modify ip->xxx ...
 *   iunlock(ip)
 *   iput(ip)
 *
 * ilock() is separate from iget() so that system calls can
 * get a long-term reference to an inode (as for an open file)
 * and only lock it for short periods (e.g., in read()).
 * The separation also helps avoid deadlock and races during
 * pathname lookup. iget() increments ip->ref so that the inode
 * stays cached and pointers to it remain valid.
 *
 * Many internal file system functions expect the caller to
 * have locked the inodes involved; this lets callers create
 * multi-step atomic operations.
 *
 * The icache.lock spin-lock protects the allocation of icache
 * entries. Since ip->ref indicates whether an entry is free,
 * and ip->dev and ip->inum indicate which i-node an entry
 * holds, one must hold icache.lock while using any of those fields.
 *
 * An ip->lock sleep-lock protects all ip-> fields other than ref,
 * dev, and inum.  One must hold ip->lock in order to
 * read or write that inode's ip->valid, ip->size, ip->type, &c.
 */

struct {
    struct spinlock lock;
    struct inode inode[NINODE];
} icache;

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

static struct inode* iget(uint32_t, uint32_t);

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
    return 0;
}

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

/*
 * Common idiom: unlock, then put.
 */
void
iunlockput(struct inode* ip)
{
    iunlock(ip);
    iput(ip);
}

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
    return 0;
}

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

/* Directories. */

int
namecmp(const char* s, const char* t)
{
    return strncmp(s, t, DIRSIZ);
}

/*
 * Look for a directory entry in a directory.
 * If found, set *poff to byte offset of entry.
 */
struct inode*
dirlookup(struct inode* dp, char* name, size_t* poff)
{
    if (dp->type != T_DIR) panic("\tdirlookup: not DIR.\n");

    struct dirent de;
    for (size_t off = 0; off < dp->size; off += sizeof(de)) {
        if (readi(dp, (char*)&de, off, sizeof(de)) != sizeof(de))
            panic("\tdirlookup: read error.\n");
        if (!de.inum) continue;
        if (!namecmp(name, de.name)) {
            // entry matches path element
            if (poff) *poff = off;
            return iget(dp->dev, de.inum);
        }
    }
    return 0;
}

/*
 * Write a new directory entry (name, inum) into the directory dp.
 */
int
dirlink(struct inode* dp, char* name, uint32_t inum)
{
    struct dirent de;
    struct inode* ip = dirlookup(dp, name, 0);

    /* Check that name is not present. */
    if (ip) {
        iput(ip);
        return -1;
    }

    /* Look for an empty dirent. */
    ssize_t off = 0;
    for (; off < dp->size; off += sizeof(de)) {
        if (readi(dp, (char*)&de, off, sizeof(de)) != sizeof(de))
            panic("\tdirlink: read error.\n");
        if (de.inum == 0) break;
    }

    strncpy(de.name, name, DIRSIZ);
    de.inum = inum;
    if (writei(dp, (char*)&de, off, sizeof(de)) != sizeof(de))
        panic("\tdirlink: write error.\n");

    return 0;
}

/* Paths. */

/*
 * Copy the next path element from path into name.
 *
 * Return a pointer to the element following the copied one.
 * The returned path has no leading slashes,
 * so the caller can check *path=='\0' to see if the name is the last one.
 * If no name to remove, return 0.
 *
 * Examples:
 *   skipelem("a/bb/c", name) = "bb/c", setting name = "a"
 *   skipelem("///a//bb", name) = "bb", setting name = "a"
 *   skipelem("a", name) = "", setting name = "a"
 *   skipelem("", name) = skipelem("////", name) = 0
 */
static char*
skipelem(char* path, char* name)
{
    while (*path == '/') ++path;
    if (*path == '\0') return 0;
    char* s = path;
    while (*path != '/' && *path != '\0') ++path;
    int len = path - s;
    if (len >= DIRSIZ)
        memmove(name, s, DIRSIZ);
    else {
        memmove(name, s, len);
        name[len] = 0;
    }
    while (*path == '/') ++path;
    return path;
}

/*
 * Look up and return the inode for a path name.
 *
 * If parent != 0, return the inode for the parent and copy the final
 * path element into name, which must have room for DIRSIZ bytes.
 * Must be called inside a transaction since it calls iput().
 */
static struct inode*
namex(char* path, int nameiparent, char* name)
{
    struct inode* ip = NULL;
    ip = (*path == '/') ? iget(ROOTDEV, ROOTINO) : idup(thisproc()->cwd);

    while ((path = skipelem(path, name))) {
        ilock(ip);
        if (ip->type != T_DIR) {
            iunlockput(ip);
            return 0;
        }
        if (nameiparent && *path == '\0') {
            // Stop one level early.
            iunlock(ip);
            return ip;
        }
        struct inode* next = dirlookup(ip, name, 0);
        if (!next) {
            iunlockput(ip);
            return 0;
        }
        iunlockput(ip);
        ip = next;
    }
    if (nameiparent) {
        iput(ip);
        return 0;
    }
    return ip;
}

struct inode*
namei(char* path)
{
    char name[DIRSIZ];
    return namex(path, 0, name);
}

struct inode*
nameiparent(char* path, char* name)
{
    return namex(path, 1, name);
}
