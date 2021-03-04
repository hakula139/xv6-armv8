#ifndef INC_FILE_H_
#define INC_FILE_H_

#include <sys/stat.h>

#include "fs.h"
#include "sleeplock.h"
#include "types.h"

#define NFILE 100  // Open files per system

struct file {
    enum { FD_NONE, FD_PIPE, FD_INODE } type;
    int ref;
    char readable;
    char writable;
    struct pipe* pipe;
    struct inode* ip;
    size_t off;
};

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

/*
 * Table mapping major device number to
 * device functions
 */
struct devsw {
    ssize_t (*read)(struct inode*, char*, ssize_t);
    ssize_t (*write)(struct inode*, char*, ssize_t);
};

extern struct devsw devsw[];

// kern/file.c

void file_init();
struct file* file_alloc();
struct file* file_dup(struct file*);
void file_close(struct file*);
int file_stat(struct file*, struct stat*);
ssize_t file_read(struct file*, char*, ssize_t);
ssize_t file_write(struct file*, char*, ssize_t);

// kern/fs.c

void readsb(int, struct superblock*);

void iinit(int);
struct inode* ialloc(uint32_t, uint16_t);
void iupdate(struct inode*);
struct inode* idup(struct inode*);
void ilock(struct inode*);
void iunlock(struct inode*);
void iput(struct inode*);
void iunlockput(struct inode*);
void stati(struct inode*, struct stat*);
ssize_t readi(struct inode*, char*, size_t, size_t);
ssize_t writei(struct inode*, char*, size_t, size_t);

int namecmp(const char*, const char*);
struct inode* dirlookup(struct inode*, char*, size_t*);
int dirlink(struct inode*, char*, uint32_t);

struct inode* namei(char*);
struct inode* nameiparent(char*, char*);

#endif  // INC_FILE_H_
