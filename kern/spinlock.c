#include "spinlock.h"
#include "arm.h"
#include "console.h"
#include "proc.h"
#include "string.h"

/*
 * Check whether this cpu is holding the lock.
 */
int
holding(struct spinlock* lk)
{
    int hold;
    hold = lk->locked && lk->cpu == thiscpu;
    return hold;
}

void
initlock(struct spinlock* lk, char* name)
{
    lk->name = name;
    lk->locked = 0;
    lk->cpu = 0;
}

void
acquire(struct spinlock* lk)
{
    if (holding(lk)) {
        panic("acquire: lock %s at CPU %d already held.\n", lk->name, cpuid());
    }
    while (lk->locked || __atomic_test_and_set(&lk->locked, __ATOMIC_ACQUIRE)) {
    }
    lk->cpu = thiscpu;
}

void
release(struct spinlock* lk)
{
    if (!holding(lk)) {
        panic("release: lock %s at CPU %d not held.\n", lk->name, cpuid());
    }
    lk->cpu = NULL;
    __atomic_clear(&lk->locked, __ATOMIC_RELEASE);
}
