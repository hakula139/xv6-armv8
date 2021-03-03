#include "vm.h"

#include <stdint.h>

#include "arm.h"
#include "console.h"
#include "file.h"
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
 * Look up a virtual address, return the physical address,
 * or 0 if not mapped.
 * Can only be used to look up user pages.
 */
static uint64_t
addr_walk(uint64_t* pgdir, const void* va)
{
    uint64_t* pte = pgdir_walk(pgdir, va, 0);
    if (!pte) return 0;
    if (!(*pte & PTE_P)) return 0;
    if (!(*pte & PTE_USER)) return 0;
    return (uint64_t)P2V(PTE_ADDR(*pte));
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
        uint64_t* pte = pgdir_walk(pgdir, (void*)va + i, 1);
        if (!pte) return 1;
        *pte = V2P(PTE_ADDR(pa + i)) | perm | PTE_P | PTE_TABLE
               | (MT_NORMAL << 2) | PTE_AF | PTE_SH;
    }
    return 0;
}

/*
 * Remove npages of mappings starting from va. va must be
 * page-aligned. The mappings must exist.
 * Optionally free the physical memory.
 */
static void
uvm_unmap(uint64_t* pgdir, uint64_t va, uint64_t npages, int do_free)
{
    if (va % PGSIZE) panic("\tuvm_unmap: not aligned.\n");

    uint64_t size = npages * PGSIZE;
    for (uint64_t i = 0; i < size; i += PGSIZE) {
        uint64_t* pte = pgdir_walk(pgdir, (void*)va + i, 0);
        if (!pte) panic("\tuvmunmap: pgdir_walk error.\n");
        if (!(*pte & PTE_P)) panic("\tuvmunmap: not mapped.\n");
        if (PTE_FLAGS(*pte) == PTE_P) panic("\tuvmunmap: not a leaf.\n");
        if (do_free) kfree((char*)V2P(PTE_ADDR(*pte)));
        *pte = 0;
    }
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
 * Load a program segment into pgdir.  addr must be page-aligned
 * and the pages from addr to addr+sz must already be mapped.
 */
int
uvm_load(
    uint64_t* pgdir, char* addr, struct inode* ip, uint64_t offset, uint64_t sz)
{
    if ((uint64_t)addr % PGSIZE)
        panic("\tuvm_load: addr must be page aligned.\n");
    for (uint64_t i = 0; i < sz; i += PGSIZE) {
        uint64_t* pte = pgdir_walk(pgdir, (void*)addr + i, 0);
        if (!pte) panic("uvm_load: address should exist");
        uint64_t pa = PTE_ADDR(*pte);
        uint64_t n = (sz - i < PGSIZE) ? sz - i : PGSIZE;
        if (readi(ip, P2V(pa), offset + i, n) != n) return -1;
    }
    return 0;
}

/*
 * Allocate PTEs and physical memory to grow process from oldsz to
 * newsz, which need not be page aligned.  Returns new size or 0 on error.
 */
uint64_t
uvm_alloc(uint64_t* pgdir, uint64_t oldsz, uint64_t newsz)
{
    if (newsz < oldsz) return oldsz;

    for (uint64_t va = oldsz; va < newsz; va += PGSIZE) {
        char* mem = kalloc();
        if (!mem) {
            uvm_dealloc(pgdir, va, oldsz);
            return 0;
        }
        memset(mem, 0, PGSIZE);
        if (map_region(
                pgdir, (void*)va, PGSIZE, (uint64_t)mem,
                PTE_USER | PTE_RW | PTE_PAGE)) {
            kfree(mem);
            uvm_dealloc(pgdir, va, oldsz);
            return 0;
        }
    }
    return newsz;
}

/*
 * Deallocate user pages to bring the process size from oldsz to
 * newsz.  oldsz and newsz need not be page-aligned, nor does newsz
 * need to be less than oldsz.  oldsz can be larger than the actual
 * process size.  Returns the new process size.
 */
uint64_t
uvm_dealloc(uint64_t* pgdir, uint64_t oldsz, uint64_t newsz)
{
    if (newsz >= oldsz) return oldsz;

    int npages = (oldsz - newsz) / PGSIZE;
    uvm_unmap(pgdir, newsz, npages, 1);

    return newsz;
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

    lttbr0(V2P(p->pgdir));  // switch to process's address space
}

/*
 * Given a parent process's page table, copy its memory into a child's page
 * table. Copies both the page table and the physical memory. Returns 0 on
 * success, -1 on failure. Frees any allocated pages on failure.
 */
int
uvm_copy(uint64_t* old, uint64_t* new, uint64_t sz)
{
    for (uint64_t i = 0; i < sz; i += PGSIZE) {
        uint64_t* pte = pgdir_walk(old, (void*)i, 0);
        if (!pte) panic("\tuvm_copy: pte should exist.\n");
        if (!(*pte & PTE_P)) panic("\tuvm_copy: page not present.\n");
        uint64_t pa = V2P(PTE_ADDR(*pte));
        uint64_t flags = PTE_FLAGS(*pte);
        char* mem = kalloc();
        if (!mem) {
            uvm_unmap(new, 0, i / PGSIZE, 1);
            return -1;
        }
        memmove(mem, (void*)pa, PGSIZE);
        if (map_region(new, (void*)i, PGSIZE, (uint64_t)mem, flags) != 0) {
            kfree(mem);
            uvm_unmap(new, 0, i / PGSIZE, 1);
            return -1;
        }
    }
    return 0;
}

/*
 * Clear PTE_USER on a page. Used to create an inaccessible
 * page beneath the user stack.
 */
void
uvm_clear(uint64_t* pgdir, char* va)
{
    uint64_t* pte = pgdir_walk(pgdir, va, 0);
    if (!pte) panic("\tuvm_clear: failed to locate PTE.\n");
    *pte &= ~PTE_USER;
}

/*
 * Copy from kernel to user.
 * Copy len bytes from src to virtual address dstva in a given page table.
 * Return 0 on success, -1 on error.
 */
int
copyout(uint64_t* pgdir, uint64_t dstva, char* src, uint64_t len)
{
    for (uint64_t va0 = 0, pa0 = 0, n = 0; len > 0;
         len -= n, src += n, dstva = va0 + PGSIZE) {
        va0 = PTE_ADDR(dstva);
        pa0 = addr_walk(pgdir, (void*)va0);
        if (!pa0) return -1;
        n = PGSIZE - (dstva - va0);
        if (n > len) n = len;
        memmove((void*)(pa0 + (dstva - va0)), src, n);
    }
    return 0;
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
