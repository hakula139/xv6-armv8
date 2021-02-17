#ifndef INC_BUF_H_
#define INC_BUF_H_

#include <stdint.h>

#include "fs.h"
#include "sleeplock.h"

#define BSIZE 512
#define NBUF  30

#define B_VALID 0x2 /* Buffer has been read from disk. */
#define B_DIRTY 0x4 /* Buffer needs to be written to disk. */

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

void binit();
struct buf* bread(uint32_t, uint32_t);
void bwrite(struct buf*);
void brelse(struct buf*);
void bpin(struct buf*);
void bunpin(struct buf*);

#endif  // INC_BUF_H_
