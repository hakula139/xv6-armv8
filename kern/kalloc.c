#include "kalloc.h"

#include <stdint.h>

#include "console.h"
#include "memlayout.h"
#include "mmu.h"
#include "spinlock.h"
#include "string.h"
#include "types.h"

extern char end[];

/*
 * Free page's list element struct.
 * We store each free page's run structure in the free page itself.
 */
struct run {
    struct run* next;
};

struct {
    struct spinlock lock;
    struct run* free_list; /* Free list of physical pages */
} kmem;

void
alloc_init()
{
    kmem.lock.locked = 0; /* Init kmem lock */
    free_range(end, P2V(PHYSTOP));
}

/* Free the page of physical memory pointed at by v. */
void
kfree(char* v)
{
    struct run* r;

    if ((uint64_t)v % PGSIZE || v < end || V2P(v) >= PHYSTOP)
        panic("kfree: invalid address: 0x%p\n", V2P(v));

    /* Fill with junk to catch dangling refs. */
    memset(v, 1, PGSIZE);

    r = (struct run*)v;
    acquire(&kmem.lock);
    r->next = kmem.free_list;
    kmem.free_list = r;
    release(&kmem.lock);
}

void
free_range(void* vstart, void* vend)
{
    char* p;
    p = ROUNDUP((char*)vstart, PGSIZE);
    for (; p + PGSIZE <= (char*)vend; p += PGSIZE) kfree(p);
}

/*
 * Allocate one 4096-byte page of physical memory.
 * Returns a pointer that the kernel can use.
 * Returns 0 if the memory cannot be allocated.
 */
char*
kalloc()
{
    struct run* p;
    acquire(&kmem.lock);
    p = kmem.free_list;
    if (p) kmem.free_list = p->next;
    release(&kmem.lock);
    return (char*)p;
}

void
check_free_list()
{
    struct run* p;
    if (!kmem.free_list)
        panic("check_free_list: free_list is null.\n");

    for (p = kmem.free_list; p; p = p->next) { assert((void*)p > (void*)end); }
    cprintf("check_free_list: passed.\n");
}
