#include "sleeplock.h"
#include "proc.h"

void
initsleeplock(struct sleeplock* lk, char* name)
{
    initlock(&lk->lk, name);
    lk->locked = 0;
    lk->pid = 0;
}

void
acquiresleep(struct sleeplock* lk)
{
    struct proc* p = thiscpu->proc;
    acquire(&lk->lk);
    while (lk->locked) { sleep(lk, &lk->lk); }
    lk->locked = 1;
    lk->pid = p->pid;
    release(&lk->lk);
}

void
releasesleep(struct sleeplock* lk)
{
    acquire(&lk->lk);
    lk->locked = 0;
    lk->pid = 0;
    wakeup(lk);
    release(&lk->lk);
}

int
holdingsleep(struct sleeplock* lk)
{
    struct proc* p = thiscpu->proc;
    int r;

    acquire(&lk->lk);
    r = lk->locked && (lk->pid == p->pid);
    release(&lk->lk);
    return r;
}
