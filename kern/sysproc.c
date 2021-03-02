#include <stdint.h>
#include <syscall.h>

#include "console.h"
#include "proc.h"
#include "string.h"
#include "syscall1.h"
#include "trap.h"

int
sys_exec()
{
    char* path;
    char* argv[MAXARG];
    uint64_t uargv, uarg;

    if (argstr(0, &path) < 0 || argint(1, &uargv) < 0) return -1;
    memset(argv, 0, sizeof(argv));
    for (int i = 0;; ++i) {
        if (i >= NELEM(argv)) return -1;
        if (fetchint(uargv + sizeof(uint64_t) * i, &uarg) < 0) return -1;
        if (!uarg) {
            argv[i] = NULL;
            break;
        }
        if (fetchstr(uarg, &argv[i]) < 0) return -1;
    }
    return execve(path, argv, NULL);
}

int
sys_yield()
{
    yield();
    return 0;
}

size_t
sys_brk()
{
    uint64_t n;
    if (argint(0, &n) < 0) return -1;
    uint64_t addr = thisproc()->sz;
    if (growproc(n) < 0) return -1;
    return addr;
}

int
sys_clone()
{
    void* childstk;
    uint64_t flag;
    if (argint(0, &flag) < 0 || argint(1, &childstk) < 0) return -1;
    if (flag != 17) {
        cprintf("sys_clone: flags other than SIGCHLD are not supported.\n");
        return -1;
    }
    return fork();
}

int
sys_wait4()
{
    int64_t pid, opt;
    int* wstatus;
    void* rusage;

    if (argint(0, &pid) < 0 || argint(1, &wstatus) < 0 || argint(2, &opt) < 0
        || argint(3, &rusage) < 0)
        return -1;

    if (pid != -1 || wstatus != 0 || opt != 0 || rusage != 0) {
        cprintf(
            "\tsys_wait4: unimplemented. pid %d, wstatus 0x%p, opt 0x%x, rusage 0x%p\n",
            pid, wstatus, opt, rusage);
        return -1;
    }

    return wait();
}

int
sys_exit()
{
    exit(0);
    return 0;
}
