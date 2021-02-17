#ifndef INC_SLEEPLOCK_H_
#define INC_SLEEPLOCK_H_

#include "proc.h"
#include "spinlock.h"

/* Long-term locks for processes */
struct sleeplock {
    int locked;         /* Is the lock held? */
    struct spinlock lk; /* Spinlock protecting this sleep lock */
    int pid;
};

void initsleeplock(struct sleeplock* lk, char* name);
void acquiresleep(struct sleeplock* lk);
void releasesleep(struct sleeplock* lk);
int holdingsleep(struct sleeplock* lk);

#endif  // INC_SLEEPLOCK_H_
