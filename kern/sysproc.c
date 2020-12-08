#include "console.h"
#include "proc.h"
#include "syscall.h"

int
sys_exit()
{
    cprintf("sys_exit: in exit.\n");
    exit(0);
    panic("\tsys_exit: should never return.\n");
    return -1;
}
