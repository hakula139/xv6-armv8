#ifndef INC_SPINLOCK_H_
#define INC_SPINLOCK_H_

#include "proc.h"

struct spinlock {
    volatile int locked;

    /* For debugging: */
    char* name;      /* Name of lock. */
    struct cpu* cpu; /* The cpu holding the lock. */
};

int holding(struct spinlock*);
void acquire(struct spinlock*);
void release(struct spinlock*);
void initlock(struct spinlock*, char*);

#endif  // INC_SPINLOCK_H_
