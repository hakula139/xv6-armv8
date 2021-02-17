#include "syscall.h"
#include "console.h"
#include "proc.h"
#include "string.h"
#include "syscallno.h"

/*
 * User code makes a system call with SVC.
 * System call number in r0.
 * Arguments on the stack, from the user call to the C
 * library system call function.
 */

/* Fetch the int at addr from the current process. */
int
fetchint(uint64_t addr, int64_t* ip)
{
    struct proc* proc = thiscpu->proc;

    if (addr >= proc->sz || addr + 8 > proc->sz) { return -1; }
    *ip = *(int64_t*)(addr);
    return 0;
}

/*
 * Fetch the nul-terminated string at addr from the current process.
 * Doesn't actually copy the string - just sets *pp to point at it.
 * Returns length of string, not including nul.
 */
int
fetchstr(uint64_t addr, char** pp)
{
    char *s, *ep;
    struct proc* proc = thiscpu->proc;

    if (addr >= proc->sz) { return -1; }

    *pp = (char*)addr;
    ep = (char*)proc->sz;

    for (s = *pp; s < ep; s++) {
        if (*s == 0) { return s - *pp; }
    }

    return -1;
}

/*
 * Fetch the nth (starting from 0) 32-bit system call argument.
 * In our ABI, r0 contains system call index, r1-r4 contain parameters.
 * now we support system calls with at most 4 parameters.
 */
int
argint(int n, uint64_t* ip)
{
    if (n > 3) panic("\targint: too many system call parameters.\n");

    struct proc* proc = thiscpu->proc;

    // *ip = *(&proc->tf->x1 + n);

    return 0;
}

/*
 * Fetch the nth word-sized system call argument as a pointer
 * to a block of memory of size n bytes.  Check that the pointer
 * lies within the process address space.
 */
int
argptr(int n, char** pp, int size)
{
    uint64_t i;

    if (argint(n, &i) < 0) { return -1; }

    struct proc* proc = thiscpu->proc;

    if ((uint64_t)i >= proc->sz || (uint64_t)i + size > proc->sz) { return -1; }

    *pp = (char*)i;
    return 0;
}

/*
 * Fetch the nth word-sized system call argument as a string pointer.
 * Check that the pointer is valid and the string is nul-terminated.
 * (There is no shared writable memory, so the string can't change
 * between this check and being used by the kernel.)
 */
int
argstr(int n, char** pp)
{
    uint64_t addr;

    if (argint(n, &addr) < 0) { return -1; }

    return fetchstr(addr, pp);
}

extern int sys_exec();
extern int sys_exit();

static int (*syscalls[])() = {
    [SYS_exec] sys_exec,
    [SYS_exit] sys_exit,
};

int
syscall()
{
    struct proc* p = thiscpu->proc;
    int num = p->tf->x0;
    if (num >= 0 && num < NELEM(syscalls) && syscalls[num]) {
        cprintf("syscall: proc %d calls syscall %d.\n", p->pid, num);
        return syscalls[num]();
    } else {
        cprintf(
            "syscall: unknown syscall %d from proc %d (%s).\n", num, p->pid,
            p->name);
        return -1;
    }
    return 0;
}

/*
 * TODO: If you want to use musl
 *
 * 1. Remove inc/syscall.h and inc/syscallno.h
 * 2. Include <syscall.h> from musl.
 * 3. Rename `syscall()` to `syscall1()`.
 * 4. Hack some syscalls for musl as follows.
 *
 * ```
 * int
 * syscall1(struct trapframe *tf)
 * {
 *     thisproc()->tf = tf;
 *     int sysno = tf->x[8];
 *     switch (sysno) {
 *
 *     // FIXME: Use pid instead of tid since we don't have threads :)
 *     case SYS_set_tid_address:
 *     case SYS_gettid:
 *         return thisproc()->pid;
 *
 *     // FIXME: Hack TIOCGWINSZ(get window size)
 *     case SYS_ioctl:
 *         if (tf->x[1] == 0x5413) return 0;
 *         else panic("ioctl unimplemented. ");
 *
 *     // FIXME: Always return 0 since we don't have signals  :)
 *     case SYS_rt_sigprocmask:
 *         return 0;
 *
 *     case SYS_brk:
 *         return sys_brk();
 *
 *     case SYS_execve:
 *         return sys_exec();
 *
 *     case SYS_sched_yield:
 *         return sys_yield();
 *
 *     case SYS_clone:
 *         return sys_clone();
 *
 *     case SYS_wait4:
 *         return sys_wait4();
 *
 *     // FIXME: exit_group should kill every thread in the current thread
 *     // group.
 *     case SYS_exit_group:
 *
 *     case SYS_exit:
 *         return sys_exit();
 *
 *     case SYS_dup:
 *         return sys_dup();
 *
 *     case SYS_chdir:
 *         return sys_chdir();
 *
 *     case SYS_fstat:
 *         return sys_fstat();
 *
 *     case SYS_newfstatat:
 *         return sys_fstatat();
 *
 *     case SYS_mkdirat:
 *         return sys_mkdirat();
 *
 *     case SYS_mknodat:
 *         return sys_mknodat();
 *
 *     case SYS_openat:
 *         return sys_openat();
 *
 *     case SYS_writev:
 *         return sys_writev();
 *
 *     case SYS_read:
 *         return sys_read();
 *
 *     case SYS_close:
 *         return sys_close();
 *
 *     default:
 *         // FIXME: don't panic.
 *
 *         debug_reg();
 *         panic("Unexpected syscall #%d\n", sysno);
 *
 *         return 0;
 *     }
 * }
 * ```
 *
 */
