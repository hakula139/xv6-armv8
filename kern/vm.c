#include "vm.h"

#include <stdint.h>

#include "console.h"
#include "kalloc.h"
#include "memlayout.h"
#include "mmu.h"
#include "string.h"
#include "types.h"

// If the page is invalid, then alloc a new one. Return NULL if failed.
static uint64_t*
pde_validate(uint64_t* pde, int64_t alloc)
{
    if (!(*pde & PTE_P)) {  // if the page is invalid
        if (!alloc) return NULL;
        char* p = kalloc();
        if (!p) return NULL;  // allocation failed
        memset(p, 0, PGSIZE);
        *pde = V2P(p) | PTE_P | PTE_PAGE | PTE_USER | PTE_RW;
    }
    return pde;
}

/*
 * Given 'pgdir', a pointer to a page directory, pgdir_walk returns
 * a pointer to the page table entry (PTE) for virtual address 'va'.
 * This requires walking the four-level page table structure.
 *
 * The relevant page table page might not exist yet.
 * If this is true, and alloc == false, then pgdir_walk returns NULL.
 * Otherwise, pgdir_walk allocates a new page table page with kalloc.
 *   - If the allocation fails, pgdir_walk returns NULL.
 *   - Otherwise, the new page is cleared, and pgdir_walk returns
 *     a pointer into the new page table page.
 */
static uint64_t*
pgdir_walk(uint64_t* pgdir, const void* va, int64_t alloc)
{
    uint64_t sign = ((uint64_t)va >> 48) & 0xFFFF;
    if (sign != 0 && sign != 0xFFFF) return NULL;

    uint64_t* pde = pgdir;
    for (int level = 0; level < 3; ++level) {
        pde = &pde[PTX(level, va)];  // get pde at the next level
        if (!(pde = pde_validate(pde, alloc))) return NULL;
        pde = (uint64_t*)P2V(PTE_ADDR(*pde));
    }
    return &pde[PTX(3, va)];
}

/*
 * Create PTEs for virtual addresses starting at va that refer to
 * physical addresses starting at pa. va and size might **NOT**
 * be page-aligned.
 * Use permission bits perm|PTE_P|PTE_TABLE|PTE_AF for the entries.
 *
 * Hint: call pgdir_walk to get the corresponding page table entry
 */
static int
map_region(uint64_t* pgdir, void* va, uint64_t size, uint64_t pa, int64_t perm)
{
    for (uint64_t i = 0; i < size; i += PGSIZE) {
        uint64_t* pte = pgdir_walk(pgdir, va + i, 1);
        if (!pte) return 1;
        *pte = V2P(PTE_ADDR(pa + i)) | perm | PTE_P | PTE_TABLE | PTE_AF;
    }
    return 0;
}

/*
 * Free a page table.
 *
 * Hint: You need to free all existing PTEs for this pgdir.
 */
void
vm_free(uint64_t* pgdir, int level)
{
    // cprintf("vm_free: currently at 0x%p at level %d.\n", pgdir, 4 - level);
    if (!pgdir || level < 0) return;
    if (PTE_FLAGS(pgdir)) panic("vm_free: invalid pgdir.\n");
    if (!level) {
        // cprintf("vm_free: free 0x%p at level %d.\n", pgdir, level);
        kfree((char*)pgdir);
        return;
    }
    for (uint64_t i = 0; i < ENTRYSZ; ++i) {
        // cprintf("[%lld]: 0x%llx\n", i, pgdir[i]);
        if (pgdir[i] & PTE_P) {
            uint64_t* v = (uint64_t*)P2V(PTE_ADDR(pgdir[i]));
            // cprintf("vm_free: free 0x%p at level %d.\n", v, 5 - level);
            vm_free(v, level - 1);
        }
    }
    kfree((char*)pgdir);
}

void
check_map_region()
{
    *((uint64_t*)P2V(0)) = 0xac;
    char* p = kalloc();
    memset(p, 0, PGSIZE);
    map_region((uint64_t*)p, (void*)0x1000, PGSIZE, 0, 0);
    asm volatile("msr ttbr0_el1, %[x]" : : [x] "r"(V2P(p)));

    if (*((uint64_t*)0x1000) == 0xac) {
        cprintf("check_map_region: passed!\n");
    } else {
        cprintf("check_map_region: failed!\n");
    }

    vm_free((uint64_t*)p, 4);
    cprintf("check_vm_free: passed!\n");
}
