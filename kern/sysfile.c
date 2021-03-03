/*
 * File-system system calls.
 * Mostly argument checking, since we don't trust
 * user code, and calls into file.c and fs.c.
 */

#include <fcntl.h>

#include "console.h"
#include "file.h"
#include "log.h"
#include "mmu.h"
#include "proc.h"
#include "sleeplock.h"
#include "spinlock.h"
#include "string.h"
#include "syscall1.h"
#include "types.h"

struct iovec {
    void* iov_base; /* Starting address. */
    size_t iov_len; /* Number of bytes to transfer. */
};

/*
 * Fetch the nth word-sized system call argument as a file descriptor
 * and return both the descriptor and the corresponding struct file.
 */
static int
argfd(int n, uint64_t* pfd, struct file** pf)
{
    uint64_t fd;
    struct file* f;

    if (argint(n, &fd) < 0) return -1;
    if (fd < 0 || fd >= NOFILE || (f = thisproc()->ofile[fd]) == 0) return -1;
    if (pfd) *pfd = fd;
    if (pf) *pf = f;
    return 0;
}

/*
 * Allocate a file descriptor for the given file.
 * Takes over file reference from caller on success.
 */
static int
fdalloc(struct file* f)
{
    struct proc* p = thisproc();

    for (int fd = 0; fd < NOFILE; ++fd) {
        if (!p->ofile[fd]) {
            p->ofile[fd] = f;
            return fd;
        }
    }
    return -1;
}

int
sys_dup()
{
    struct file* f;
    if (argfd(0, 0, &f) < 0) return -1;

    int fd = fdalloc(f);
    if (fd < 0) return -1;

    filedup(f);
    return fd;
}

ssize_t
sys_read()
{
    struct file* f;
    uint64_t n;
    char* p;

    if (argfd(0, 0, &f) < 0 || argint(2, &n) < 0 || argptr(1, &p, n) < 0)
        return -1;
    return fileread(f, p, n);
}

ssize_t
sys_write()
{
    struct file* f;
    uint64_t n;
    char* p;

    if (argfd(0, 0, &f) < 0 || argint(2, &n) < 0 || argptr(1, &p, n) < 0)
        return -1;
    return filewrite(f, p, n);
}

ssize_t
sys_writev()
{
    struct file* f;
    uint64_t fd, iovcnt;
    struct iovec* iov;
    if (argfd(0, &fd, &f) < 0 || argint(2, &iovcnt) < 0
        || argptr(1, (char**)&iov, iovcnt * sizeof(struct iovec)) < 0) {
        return -1;
    }

    size_t tot = 0;
    for (struct iovec* p = iov; p < iov + iovcnt; ++p) {
        tot += filewrite(f, p->iov_base, p->iov_len);
    }
    return tot;
}

int
sys_close()
{
    uint64_t fd;
    struct file* f;
    struct proc* p = thisproc();

    if (argfd(0, &fd, &f) < 0) return -1;
    p->ofile[fd] = 0;
    fileclose(f);
    return 0;
}

int
sys_fstat()
{
    struct file* f;
    struct stat* st;  // user pointer to struct stat

    if (argfd(0, 0, &f) < 0 || argptr(1, (char**)&st, sizeof(*st)) < 0)
        return -1;
    return filestat(f, st);
}

int
sys_fstatat()
{
    uint64_t dirfd, flags;
    char* path;
    struct stat* st;

    if (argint(0, &dirfd) < 0 || argstr(1, &path) < 0
        || argptr(2, (char**)&st, sizeof(*st)) < 0 || argint(3, &flags) < 0)
        return -1;

    if (dirfd != AT_FDCWD) {
        cprintf("sys_fstatat: dirfd unimplemented.\n");
        return -1;
    }
    if (flags != 0) {
        cprintf("sys_fstatat: flags unimplemented.\n");
        return -1;
    }

    begin_op();
    struct inode* ip = namei(path);
    if (!ip) {
        end_op();
        return -1;
    }
    ilock(ip);
    stati(ip, st);
    iunlockput(ip);
    end_op();

    return 0;
}

static struct inode*
create(char* path, short type, short major, short minor)
{
    uint64_t off;
    char name[DIRSIZ] = {'\0'};

    struct inode* dp = nameiparent(path, name);
    if (!dp) return 0;
    ilock(dp);

    struct inode* ip = dirlookup(dp, name, &off);
    if (ip) {
        iunlockput(dp);
        ilock(ip);
        if (type == T_FILE && ip->type == T_FILE) return ip;
        iunlockput(ip);
        return 0;
    }

    ip = ialloc(dp->dev, type);
    if (!ip) panic("\tcreate: ialloc failed.\n");
    ilock(ip);

    ip->major = major;
    ip->minor = minor;
    ip->nlink = 1;
    iupdate(ip);

    if (type == T_DIR) {
        dp->nlink++;
        iupdate(dp);
        if (dirlink(ip, ".", ip->inum) < 0 || dirlink(ip, "..", dp->inum) < 0) {
            panic("\tcreate: . .. failed.\n");
        }
    }
    if (dirlink(dp, name, ip->inum) < 0) panic("\tcreate: dirlink failed.\n");

    iunlockput(dp);
    return ip;
}

int
sys_openat()
{
    char* path;
    uint64_t dirfd, omode;

    if (argint(0, &dirfd) < 0 || argstr(1, &path) < 0 || argint(2, &omode) < 0)
        return -1;

    if (dirfd != AT_FDCWD) {
        cprintf("sys_openat: dirfd unimplemented.\n");
        return -1;
    }
    if (!(omode & O_LARGEFILE)) {
        cprintf("sys_openat: expect O_LARGEFILE in open flags.\n");
        return -1;
    }

    begin_op();
    struct inode* ip;
    if (omode & O_CREAT) {
        // FIXME: Support acl mode.
        ip = create(path, T_FILE, 0, 0);
        if (!ip) {
            end_op();
            return -1;
        }
    } else {
        ip = namei(path);
        if (!ip) {
            end_op();
            return -1;
        }
        ilock(ip);
        if (ip->type == T_DIR && omode != (O_RDONLY | O_LARGEFILE)) {
            iunlockput(ip);
            end_op();
            return -1;
        }
    }

    struct file* f = filealloc();
    int fd = fdalloc(f);
    if (!f || fd < 0) {
        if (f) fileclose(f);
        iunlockput(ip);
        end_op();
        return -1;
    }
    iunlock(ip);
    end_op();

    f->type = FD_INODE;
    f->ip = ip;
    f->off = 0;
    f->readable = !(omode & O_WRONLY);
    f->writable = (omode & O_WRONLY) || (omode & O_RDWR);
    return fd;
}

int
sys_mkdirat()
{
    uint64_t dirfd, mode;
    char* path;

    if (argint(0, &dirfd) < 0 || argstr(1, &path) < 0 || argint(2, &mode) < 0)
        return -1;
    if (dirfd != AT_FDCWD) {
        cprintf("sys_mkdirat: dirfd unimplemented.\n");
        return -1;
    }
    if (mode != 0) {
        cprintf("sys_mkdirat: mode unimplemented.\n");
        return -1;
    }

    begin_op();
    struct inode* ip = create(path, T_DIR, 0, 0);
    if (!ip) {
        end_op();
        return -1;
    }
    iunlockput(ip);
    end_op();
    return 0;
}

int
sys_mknodat()
{
    char* path;
    uint64_t dirfd, major, minor;

    if (argint(0, &dirfd) < 0 || argstr(1, &path) < 0 || argint(2, &major) < 0
        || argint(3, &minor))
        return -1;

    if (dirfd != AT_FDCWD) {
        cprintf("sys_mknodat: dirfd unimplemented.\n");
        return -1;
    }
    cprintf("mknodat: path '%s', major:minor %d:%d\n", path, major, minor);

    begin_op();
    struct inode* ip = create(path, T_DEV, major, minor);
    if (!ip) {
        end_op();
        return -1;
    }
    iunlockput(ip);
    end_op();
    return 0;
}

int
sys_chdir()
{
    char* path;
    struct proc* p = thisproc();

    begin_op();
    struct inode* ip;
    if (argstr(0, &path) < 0 || (ip = namei(path)) == 0) {
        end_op();
        return -1;
    }
    ilock(ip);
    if (ip->type != T_DIR) {
        iunlockput(ip);
        end_op();
        return -1;
    }
    iunlock(ip);
    iput(p->cwd);
    end_op();
    p->cwd = ip;
    return 0;
}
