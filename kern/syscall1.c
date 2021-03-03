#include "syscall1.h"

#include "console.h"
#include "proc.h"

/*
 * Use pid instead of tid since we don't have threads.
 */
int
sys_gettid()
{
    return thisproc()->pid;
}

/*
 * Hack TIOCGWINSZ (get window size).
 */
int
sys_ioctl()
{
    if (thisproc()->tf->x1 == 0x5413) return 0;
    panic("\tsys_ioctl: unimplemented.\n");
    return 0;
}

/*
 * Always return 0 since we don't have signals.
 */
int
sys_rt_sigprocmask()
{
    return 0;
}
