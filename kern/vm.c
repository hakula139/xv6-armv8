#include "vm.h"

#include <stdint.h>

#include "arm.h"
#include "console.h"
#include "kalloc.h"
#include "memlayout.h"
#include "mmu.h"
#include "string.h"
#include "types.h"

extern uint64_t* kpgdir;

/*
 * If the page is invalid, then allocate a new one. Return NULL if failed.
 */
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
 * Use permission bits perm|PTE_P|PTE_TABLE|(MT_NORMAL << 2)|PTE_AF|PTE_SH for
 * the entries.
 */
static int
map_region(uint64_t* pgdir, void* va, uint64_t size, uint64_t pa, int64_t perm)
{
    for (uint64_t i = 0; i < size; i += PGSIZE) {
        uint64_t* pte = pgdir_walk(pgdir, va + i, 1);
        if (!pte) return 1;
        *pte = V2P(PTE_ADDR(pa + i)) | perm | PTE_P | PTE_TABLE
               | (MT_NORMAL << 2) | PTE_AF | PTE_SH;
    }
    return 0;
}

/*
 * Free a page table.
 */
void
vm_free(uint64_t* pgdir, int level)
{
    if (!pgdir || level < 0) return;
    if (PTE_FLAGS(pgdir)) panic("\tvm_free: invalid pgdir.\n");
    if (!level) {
        kfree((char*)pgdir);
        return;
    }
    for (uint64_t i = 0; i < ENTRYSZ; ++i) {
        if (pgdir[i] & PTE_P) {
            uint64_t* v = (uint64_t*)P2V(PTE_ADDR(pgdir[i]));
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
        cprintf("check_map_region: passed.\n");
    } else {
        panic("\tcheck_map_region: failed.\n");
    }

    vm_free((uint64_t*)p, 4);
    cprintf("check_vm_free: passed.\n");
}

/*
 * Get a new page table.
 */
uint64_t*
pgdir_init()
{
    uint64_t* pgdir;
    if (!(pgdir = (uint64_t*)kalloc())) return NULL;
    memset(pgdir, 0, PGSIZE);
    return pgdir;
}

/*
 * Load binary code into address 0 of pgdir.
 * sz must be less than a page.
 * The page table entry should be set with
 * additional PTE_USER|PTE_RW|PTE_PAGE permission.
 */
void
uvm_init(uint64_t* pgdir, char* binary, uint64_t sz)
{
    char* mem;
    if (sz >= PGSIZE) panic("\tuvm_init: sz must be less than a page.\n");
    if (!(mem = kalloc())) panic("\tuvm_init: not enough memory.\n");
    memset(mem, 0, PGSIZE);
    map_region(
        pgdir, (void*)0, PGSIZE, (uint64_t)mem, PTE_USER | PTE_RW | PTE_PAGE);
    memmove((void*)mem, (const void*)binary, sz);
}

/*
 * Switch to the process's own page table for execution of it.
 */
void
uvm_switch(struct proc* p)
{
    if (!p) panic("\tuvm_switch: no process.\n");
    if (!p->kstack) panic("\tuvm_switch: no kstack.\n");
    if (!p->pgdir) panic("\tuvm_switch: no pgdir.\n");

    lttbr0(V2P(p->pgdir));  // Switch to process's address space
}
