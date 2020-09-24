#include <stdint.h>
#include "types.h"
#include "mmu.h"
#include "string.h"
#include "memlayout.h"
#include "console.h"

#include "vm.h"
#include "kalloc.h"

/* 
 * Given 'pgdir', a pointer to a page directory, pgdir_walk returns
 * a pointer to the page table entry (PTE) for linear address 'va'.
 * This requires walking the two-level page table structure.
 *
 * The relevant page table page might not exist yet.
 * If this is true, and alloc == false, then pgdir_walk returns NULL.
 * Otherwise, pgdir_walk allocates a new page table page with kalloc.
 *         - If the allocation fails, pgdir_walk returns NULL.
 *         - Otherwise, the new page is cleared, and pgdir_walk returns
 * a pointer into the new page table page.
 *
 * Hint 1: the x86 MMU checks permission bits in both the page directory
 * and the page table, so it's safe to leave permissions in the page
 * directory more permissive than strictly necessary.
 *
 * Hint 2: look at inc/mmu.h for useful macros that manipulate page
 * table and page directory entries.
 */

static uint64_t *
pgdir_walk(uint64_t *pgdir, const void *va, int64_t alloc)
{
    /* TODO: Your code here. */
}

/*
 * Create PTEs for virtual addresses starting at va that refer to
 * physical addresses starting at pa. va and size might **NOT**
 * be page-aligned.
 * Use permission bits perm|PTE_P for the entries.
 *
 * Hint: call pgdir_walk to get the corresponding page table entry
 */

static int
map_region(uint64_t *pgdir, void *va, uint64_t size, uint64_t pa, int64_t perm)
{
    /* TODO: Your code here. */
}

/* 
 * Free a page table.
 *
 * Hint: You need to free all existing PTEs for this pgdir.
 */

void
vm_free(uint64_t *pgdir, int level)
{
    /* TODO: Your code here. */
}