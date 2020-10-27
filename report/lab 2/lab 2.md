# Lab 2: Memory Management

## 习题解答

### 0. 一些宏的定义

```c {.line-numbers}
// inc/mmu.h

/*
 * A virtual address 'va' has a four-part structure as follows:
 * +-----9-----+-----9-----+-----9-----+-----9-----+---------12---------+
 * |  Level 0  |  Level 1  |  Level 2  |  Level 3  | Offset within Page |
 * |   Index   |   Index   |   Index   |   Index   |                    |
 * +-----------+-----------+-----------+-----------+--------------------+
 *  \PTX(va, 0)/\PTX(va, 1)/\PTX(va, 2)/\PTX(va, 3)/
 */

#define PGSIZE 4096
#define PGSHIFT 12
#define L0SHIFT 39
#define L1SHIFT 30
#define L2SHIFT 21
#define L3SHIFT 12
#define ENTRYSZ 512

#define PTX(level, va) (((uint64_t)(va) >> (39 - 9 * level)) & 0x1FF)
#define L0X(va) (((uint64_t)(va) >> L0SHIFT) & 0x1FF)
#define L1X(va) (((uint64_t)(va) >> L1SHIFT) & 0x1FF)
#define L2X(va) (((uint64_t)(va) >> L2SHIFT) & 0x1FF)
#define L3X(va) (((uint64_t)(va) >> L3SHIFT) & 0x1FF)

/* accessibility */
#define PTE_P        (1<<0)      /* valid */
#define PTE_BLOCK    (0<<1)
#define PTE_PAGE     (1<<1)
#define PTE_TABLE    (1<<1)      /* entry gives address of the next level of translation table */
#define PTE_KERNEL   (0<<6)      /* privileged, supervisor EL1 access only */
#define PTE_USER     (1<<6)      /* unprivileged, EL0 access allowed */
#define PTE_RW       (0<<7)      /* read-write */
#define PTE_RO       (1<<7)      /* read-only */
#define PTE_AF       (1<<10)     /* P2066 access flags */
// Address in page table or page directory entry
#define PTE_ADDR(pte)   ((uint64_t)(pte) & ~0xFFF)
#define PTE_FLAGS(pte)  ((uint64_t)(pte) &  0xFFF)
```

```c {.line-numbers}
// inc/memlayout.h

#define EXTMEM 0x80000                /* Start of extended memory */
#define PHYSTOP 0x3F000000            /* Top physical memory */

#define KERNBASE 0xFFFF000000000000   /* First kernel virtual address */
#define KERNLINK (KERNBASE + EXTMEM)  /* Address where kernel is linked */

#define V2P_WO(x) ((x) - KERNBASE)    /* Same as V2P, but without casts */
#define P2V_WO(x) ((x) + KERNBASE)    /* Same as P2V, but without casts */

#ifndef __ASSEMBLER__

#    include <stdint.h>
#    define V2P(a) (((uint64_t) (a)) - KERNBASE)
#    define P2V(a) ((void *)(((char *) (a)) + KERNBASE))

#endif
```

### 1. 物理内存分配器

> 完成物理内存分配器的分配函数 `kalloc` 以及回收函数 `kfree`。

由源代码可知，物理页表位于 `kmem.free_list` 这个链表里。

```c {.line-numbers}
// kern/kalloc.c
struct {
    struct run* free_list; /* Free list of physical pages */
} kmem;
```

对于函数 `kalloc`，我们所需要做的就是从 `free_list` 链表中取出头节点返回。因此不难得到函数 `kalloc` 的代码如下：

```c {.line-numbers}
// kern/kalloc.c

/*
 * Allocate one 4096-byte page of physical memory.
 * Returns a pointer that the kernel can use.
 * Returns 0 if the memory cannot be allocated.
 */
char*
kalloc()
{
    struct run* p;
    p = kmem.free_list;
    if (p) kmem.free_list = p->next;
    return (char*)p;
}
```

对于函数 `kfree`，则是函数 `kalloc` 的逆过程，即将 free 后的物理页插回 `free_list` 链表头部。

```c {.line-numbers}
// kern/kalloc.c

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
    r->next = kmem.free_list;
    kmem.free_list = r;
}
```

这一部分并不复杂。

### 2. 页表管理

> 完成物理地址的映射函数 `map_region` 以及回收页表物理空间函数 `vm_free`。

本过程中，我们需要构建 `ttbr0_el1` 页表，并将其映射到虚拟地址（高地址）。

在完成映射函数 `map_region` 之前，我们需要先按照要求完成函数 `pgdir_walk`。根据注释，函数 `pgdir_walk` 所做的事情是根据提供的虚拟地址 `va` 找到相应的页表，如果途径的页表项（PDE, Page Directory Entry）不存在，则分配（allocate）一个新的页表项。

这里我将分配新页表项的逻辑单独封装成一个函数 `pde_validate`，以提升代码的可读性。

```c {.line-numbers}
// kern/vm.c

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
```

需要注意的是，PDE 中前半段保存的地址应当为物理地址（低地址）。

函数 `pgdir_walk` 中我们进行了 3 次循环。每次循环，我们根据当前所在层级（level）的 PDE 所保存的物理地址，将其转换为虚拟地址后，再以 `va` 相应的片段为索引，找到下一级 PDE 所在的虚拟地址。过程中如果 PDE 不存在，则通过函数 `pde_validate` 分配一个新的。

```c {.line-numbers}
// kern/vm.c

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
```

为什么 4 级页表只进行了 3 次循环，是因为最后一级我们只需要返回 PDE 中地址所指向的页表（PTE, Page Table Entry）地址即可。

对于回收页表物理空间函数 `vm_free`，我们需要遍历 4 级页表，并将其中的节点全部 free 掉。

```c {.line-numbers}
// kern/vm.c

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
```

这里我们使用了递归的写法。需要注意的是，`pgdir` 中 PDE 保存的地址为物理地址，在传给函数 `kfree` 前需要先转换为虚拟地址。代码中，`ENTRYSZ` 的值为 `512`，表示 $4\ \mathrm{KB}$ 页表中的 PDE 项数（每项的大小为 $64\ \mathrm{bit} = 8\ \mathrm{B}$）。
