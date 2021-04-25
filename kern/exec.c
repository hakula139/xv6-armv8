#include <elf.h>

#include "console.h"
#include "file.h"
#include "log.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "string.h"
#include "syscall1.h"
#include "trap.h"
#include "vm.h"

int
execve(char* path, char* const argv[], char* const envp[])
{
    cprintf("exec: start '%s'.\n", path);

    // Read program file.

    begin_op();
    struct inode* ip = namei(path);
    if (!ip) {
        end_op();
        cprintf("exec: failed to read program file at '%s'.\n", path);
        return -1;
    }
    ilock(ip);

    // Check ELF header.

    uint64_t* pgdir = NULL;
    Elf64_Ehdr elf;
    if (readi(ip, (char*)&elf, 0, sizeof(elf)) != sizeof(elf)) {
        cprintf("exec: failed to read ELF.\n");
        goto bad;
    }
    if (strncmp((char*)elf.e_ident, ELFMAG, SELFMAG)) {
        cprintf("exec: check ELF header failed.\n");
        goto bad;
    }
    pgdir = pgdir_init();
    if (!pgdir) {
        cprintf("exec: failed to init pgdir.\n");
        goto bad;
    }

    // Load program into memory.

    Elf64_Phdr ph;
    int off = elf.e_phoff;
    uint64_t sz = 0;
    for (int i = 0; i < elf.e_phnum; ++i, off += sizeof(ph)) {
        if (readi(ip, (char*)&ph, off, sizeof(ph)) != sizeof(ph)) {
            cprintf("exec: failed to read program header.\n");
            goto bad;
        }
        if (ph.p_type != PT_LOAD) continue;
        if (ph.p_memsz < ph.p_filesz) {
            cprintf("exec: memory size < file size.\n");
            goto bad;
        }
        if (ph.p_vaddr + ph.p_memsz < ph.p_vaddr) {
            cprintf("exec: addr overflowed.\n");
            goto bad;
        }
        sz = uvm_alloc(pgdir, sz, ph.p_vaddr + ph.p_memsz);
        if (!sz) {
            cprintf("exec: failed to allocate uvm.\n");
            goto bad;
        }
        if (ph.p_vaddr % PGSIZE) {
            cprintf("exec: addr not page aligned.\n");
            goto bad;
        }
        if (uvm_load(pgdir, (char*)ph.p_vaddr, ip, ph.p_offset, ph.p_filesz)
            < 0) {
            cprintf("exec: failed to load uvm.\n");
            goto bad;
        }
    }
    iunlockput(ip);
    end_op();
    ip = NULL;

    // Allocate user stack.

    // Allocate two pages at the next page boundary.
    // Make the first inaccessible. Use the second as the user stack.
    sz = ROUNDUP(sz, PGSIZE);
    sz = uvm_alloc(pgdir, sz, sz + 2 * PGSIZE);
    if (!sz) {
        cprintf("exec: failed to allocate uvm.\n");
        goto bad;
    }
    uvm_clear(pgdir, (char*)(sz - 2 * PGSIZE));
    uint64_t sp = sz;

    // Push auxiliary vectors.

    uint64_t auxv[] = {0, AT_PAGESZ, PGSIZE, AT_NULL};
    sp -= sizeof(auxv);
    sp -= sp % 0x10;  // 16-byte aligned
    if (copyout(pgdir, sp, (char*)auxv, sizeof(auxv)) < 0) {
        cprintf("exec: failed to push auxiliary vectors.\n");
        goto bad;
    }

    // FIXME: Push environment pointers.
    sp -= 8;
    sp -= sp % 0x10;
    if (copyout(pgdir, sp, (char*)envp, 8) < 0) {
        cprintf("exec: failed to push environment pointers.\n");
        goto bad;
    }

    // Push argument strings.

    uint64_t argc = 0;
    uint64_t ustack[MAXARG + 1];
    for (; argv[argc]; ++argc) {
        if (argc >= MAXARG) {
            cprintf("exec: too many arguments.\n");
            goto bad;
        }
        sp -= strlen(argv[argc]) + 1;
        sp -= sp % 0x10;
        if (copyout(pgdir, sp, argv[argc], strlen(argv[argc]) + 1) < 0) {
            cprintf("exec: failed to push argument strings.\n");
            goto bad;
        }
        ustack[argc] = sp;
    }
    ustack[argc] = 0;
    ustack[0] = argc;

    uint64_t stack_size = (argc + 1) * sizeof(uint64_t);
    sp -= stack_size;
    sp -= sp % 0x10;
    if (copyout(pgdir, sp, (char*)ustack, stack_size) < 0) {
        cprintf("exec: failed to push argv[] pointers.\n");
        goto bad;
    }

    struct proc* p = thisproc();
    p->tf->x1 = sp;

    // Save program name for debugging.

    char* last = path;
    for (char* s = path; *s; ++s)
        if (*s == '/') last = s + 1;
    strncpy(p->name, last, sizeof(p->name));

    // Commit to the user image.
    uint64_t* old_pgdir = p->pgdir;
    p->pgdir = pgdir;
    p->sz = sz;
    p->tf->sp_el0 = sp;
    p->tf->elr_el1 = elf.e_entry;
    uvm_switch(p);
    if (old_pgdir) vm_free(old_pgdir, 4);

    cprintf("exec: end '%s'.\n", path);
    return argc;

bad:
    if (pgdir) vm_free(pgdir, 4);
    if (ip) {
        iunlockput(ip);
        end_op();
    }

    cprintf("exec: failed to run '%s'.\n", path);
    return -1;
}
